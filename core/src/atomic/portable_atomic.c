#include <atomic/atomic.h>
#include <stddef.h>
#include <stdint.h>

/* MSVC uses Win32 Interlocked. GCC and Clang use their memory-model-aware
 * builtins on both Windows and POSIX; MinGW-GCC is a provider, not just a client.
 */
#if defined(_MSC_VER) && !defined(__clang__)
# if !defined(_WIN32)
#  error "The MSVC atomic provider requires Windows"
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
# endif
# include <windows.h>
# include <intrin.h>
# define SALTS_ATOMIC_INTERLOCKED 1
#elif defined(__GNUC__) || defined(__clang__)
# define SALTS_ATOMIC_INTERLOCKED 0
#else
# error "Build the atomic provider with a C11-capable MSVC, GCC or Clang compiler"
#endif

int platform_portable_abi_verify(
    uint32_t version, uint32_t atomic64_bytes, uint32_t atomic64_alignment,
    uint32_t atomic_pointer_bytes) {
    struct probe64 { char pad; atomic64 value; };
    const uint32_t align64 = (uint32_t)offsetof(struct probe64, value);
    if (version != SALTS_PLATFORM_PORTABLE_ABI_VERSION ||
        atomic64_bytes != sizeof(atomic64) ||
        atomic64_alignment != align64 ||
        atomic_pointer_bytes != sizeof(atomic_ptr))
        return SALTS_EINVAL;
    return SALTS_OK;
}

static int valid_order(atomic_order order) {
    return order <= SALTS_MEMORY_SEQ_CST;
}
static int valid_load(atomic_order order) {
    return order == SALTS_MEMORY_RELAXED || order == SALTS_MEMORY_CONSUME ||
           order == SALTS_MEMORY_ACQUIRE || order == SALTS_MEMORY_SEQ_CST;
}
static int valid_store(atomic_order order) {
    return order == SALTS_MEMORY_RELAXED || order == SALTS_MEMORY_RELEASE ||
           order == SALTS_MEMORY_SEQ_CST;
}
static int valid_cas(atomic_order succ, atomic_order fail) {
    if (!valid_order(succ) || !valid_load(fail)) return 0;
    if (fail == SALTS_MEMORY_RELAXED) return 1;
    if (fail == SALTS_MEMORY_CONSUME)
        return succ == SALTS_MEMORY_CONSUME || succ == SALTS_MEMORY_ACQUIRE ||
               succ == SALTS_MEMORY_ACQ_REL || succ == SALTS_MEMORY_SEQ_CST;
    if (fail == SALTS_MEMORY_ACQUIRE)
        return succ == SALTS_MEMORY_ACQUIRE || succ == SALTS_MEMORY_ACQ_REL ||
               succ == SALTS_MEMORY_SEQ_CST;
    return succ == SALTS_MEMORY_SEQ_CST;
}
static int aligned(const void *p, size_t align) {
    return p && (((uintptr_t)p & (align - 1u)) == 0u);
}

/* Native target backends: every operation is implemented at least seq_cst.
 * Never read or modify shared storage with plain volatile arithmetic.
 */
#if SALTS_ATOMIC_INTERLOCKED
# define SALTS_NATIVE_CAS8(p, e, d) _InterlockedCompareExchange8((volatile char *)(p), (char)(d), (char)(e))
# define SALTS_NATIVE_CAS16(p, e, d) _InterlockedCompareExchange16((volatile short *)(p), (short)(d), (short)(e))
# define SALTS_NATIVE_CAS32(p, e, d) _InterlockedCompareExchange((volatile long *)(p), (long)(d), (long)(e))
# define SALTS_NATIVE_CAS64(p, e, d) _InterlockedCompareExchange64((volatile __int64 *)(p), (__int64)(d), (__int64)(e))
# define SALTS_DEFINE_ATOMIC(N, T) \
int atomic##N##_init(atomic##N *p, T v) { \
    if (!aligned(p, sizeof(T))) return SALTS_EINVAL; \
    p->value = v; return SALTS_OK; \
} \
int atomic##N##_load(const atomic##N *p, atomic_order order, T *out) { \
    if (!aligned(p, sizeof(T)) || !out || !valid_load(order)) return SALTS_EINVAL; \
    *out = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); return SALTS_OK; \
} \
int atomic##N##_cas(atomic##N *p, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged) { \
    T observed; \
    if (!aligned(p, sizeof(T)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL; \
    observed = (T)SALTS_NATIVE_CAS##N(&p->value, *expected, desired); \
    *exchanged = (observed == *expected); \
    if (!*exchanged) *expected = observed; return SALTS_OK; \
} \
int atomic##N##_store(atomic##N *p, T v, atomic_order order) { \
    T current, expected; int yes; \
    if (!aligned(p, sizeof(T)) || !valid_store(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; \
         current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, v); \
         yes = (current == expected); \
    } while (!yes); return SALTS_OK; \
} \
int atomic##N##_exchange(atomic##N *p, T v, atomic_order order, T *old) { \
    T current, expected; \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, v); \
    } while (current != expected); *old = current; return SALTS_OK; \
} \
int atomic##N##_fetch_add(atomic##N *p, T delta, atomic_order order, T *old) { \
    T current, expected; \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, (T)(expected + delta)); \
    } while (current != expected); *old = current; return SALTS_OK; \
}
#else
# define SALTS_DEFINE_ATOMIC(N, T) \
int atomic##N##_init(atomic##N *p, T v) { \
    if (!aligned(p, sizeof(T))) return SALTS_EINVAL; \
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int atomic##N##_load(const atomic##N *p, atomic_order order, T *out) { \
    if (!aligned(p, sizeof(T)) || !out || !valid_load(order)) return SALTS_EINVAL; \
    *out = __atomic_load_n(&p->value, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int atomic##N##_cas(atomic##N *p, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged) { \
    if (!aligned(p, sizeof(T)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL; \
    *exchanged = __atomic_compare_exchange_n(&p->value, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    return SALTS_OK; \
} \
int atomic##N##_store(atomic##N *p, T v, atomic_order order) { \
    if (!aligned(p, sizeof(T)) || !valid_store(order)) return SALTS_EINVAL; \
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int atomic##N##_exchange(atomic##N *p, T v, atomic_order order, T *old) { \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    *old = __atomic_exchange_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int atomic##N##_fetch_add(atomic##N *p, T delta, atomic_order order, T *old) { \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    *old = __atomic_fetch_add(&p->value, delta, __ATOMIC_SEQ_CST); return SALTS_OK; \
}
#endif
SALTS_DEFINE_ATOMIC(8, uint8_t)
SALTS_DEFINE_ATOMIC(16, uint16_t)
SALTS_DEFINE_ATOMIC(32, uint32_t)
SALTS_DEFINE_ATOMIC(64, uint64_t)
#undef SALTS_DEFINE_ATOMIC


/* A strong CAS is a valid weak CAS implementation. The shared loop keeps
 * modular arithmetic and output-commit behavior identical across providers.
 */
#define SALTS_ATOMIC_RMW(N, T, NAME, EXPR) \
int atomic##N##_fetch_##NAME(atomic##N *p, T operand, atomic_order order, T *old) { \
    T expected = 0; int exchanged = 0; \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    if (atomic##N##_load(p, SALTS_MEMORY_RELAXED, &expected) != SALTS_OK) return SALTS_EINVAL; \
    do { \
        T before = expected; \
        int rc = atomic##N##_cas(p, &expected, (T)(EXPR), order, SALTS_MEMORY_RELAXED, &exchanged); \
        if (rc != SALTS_OK) return rc; \
        if (exchanged) { *old = before; return SALTS_OK; } \
    } while (!exchanged); \
    return SALTS_EINVAL; \
}
#if SALTS_ATOMIC_INTERLOCKED
# define SALTS_ATOMIC_LOCK_FREE(p) 1
#else
# define SALTS_ATOMIC_LOCK_FREE(p) __atomic_is_lock_free(sizeof((p)->value), &(p)->value)
#endif
#define SALTS_ATOMIC_EXTENSIONS(N, T) \
    SALTS_ATOMIC_RMW(N, T, sub, expected - operand) \
    SALTS_ATOMIC_RMW(N, T, and, expected & operand) \
    SALTS_ATOMIC_RMW(N, T, or, expected | operand) \
    SALTS_ATOMIC_RMW(N, T, xor, expected ^ operand) \
    int atomic##N##_cas_weak(atomic##N *p, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged) { \
        return atomic##N##_cas(p, expected, desired, success, failure, exchanged); \
    } \
    int atomic##N##_is_lock_free(const atomic##N *p, int *out) { \
        if (!aligned(p, sizeof(T)) || !out) return SALTS_EINVAL; \
        *out = SALTS_ATOMIC_LOCK_FREE(p); return SALTS_OK; \
    }
SALTS_ATOMIC_EXTENSIONS(8, uint8_t)
SALTS_ATOMIC_EXTENSIONS(16, uint16_t)
SALTS_ATOMIC_EXTENSIONS(32, uint32_t)
SALTS_ATOMIC_EXTENSIONS(64, uint64_t)
#undef SALTS_ATOMIC_EXTENSIONS
#undef SALTS_ATOMIC_RMW

int atomic_ptr_init(atomic_ptr *p, void *v) {
    if (!aligned(p, sizeof(void *))) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    p->value = v;  /* Initialization must complete before publication. */
#else
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int atomic_ptr_load(const atomic_ptr *p, atomic_order order, void **out) {
    if (!aligned(p, sizeof(void *)) || !out || !valid_load(order)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    *out = InterlockedCompareExchangePointer((void *volatile *)&p->value, NULL, NULL);
#else
    *out = __atomic_load_n(&p->value, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int atomic_ptr_cas(atomic_ptr *p, void **expected, void *desired, atomic_order success, atomic_order failure, int *exchanged) {
    if (!aligned(p, sizeof(void *)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    void *observed = InterlockedCompareExchangePointer((void *volatile *)&p->value, desired, *expected);
    *exchanged = observed == *expected;
    if (!*exchanged) *expected = observed;
#else
    *exchanged = __atomic_compare_exchange_n(&p->value, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int atomic_ptr_store(atomic_ptr *p, void *value, atomic_order order) {
    if (!aligned(p, sizeof(void *)) || !valid_store(order)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    void *current = InterlockedCompareExchangePointer((void *volatile *)&p->value, NULL, NULL), *expected;
    do {
        expected = current;
        current = InterlockedCompareExchangePointer((void *volatile *)&p->value, value, expected);
    } while (current != expected);
#else
    __atomic_store_n(&p->value, value, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int atomic_ptr_exchange(atomic_ptr *p, void *value, atomic_order order, void **old) {
    if (!aligned(p, sizeof(void *)) || !old || !valid_order(order)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    void *current = InterlockedCompareExchangePointer((void *volatile *)&p->value, NULL, NULL), *expected;
    do {
        expected = current;
        current = InterlockedCompareExchangePointer((void *volatile *)&p->value, value, expected);
    } while (current != expected);
    *old = current;
#else
    *old = __atomic_exchange_n(&p->value, value, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int platform_atomic_thread_fence(atomic_order order) {
    if (!valid_order(order)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
    MemoryBarrier();
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int platform_atomic_signal_fence(atomic_order order) {
    if (!valid_order(order)) return SALTS_EINVAL;
#if SALTS_ATOMIC_INTERLOCKED
# if defined(_MSC_VER)
    _ReadWriteBarrier();
# else
    __asm__ __volatile__("" ::: "memory");
# endif
#else
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}

int atomic_ptr_cas_weak(atomic_ptr *p, void **expected, void *desired,
    atomic_order success, atomic_order failure, int *exchanged) {
    return atomic_ptr_cas(p, expected, desired, success, failure, exchanged);
}
int atomic_ptr_is_lock_free(const atomic_ptr *p, int *out) {
    if (!aligned(p, sizeof(void *)) || !out) return SALTS_EINVAL;
    *out = SALTS_ATOMIC_LOCK_FREE(p);
    return SALTS_OK;
}
#undef SALTS_ATOMIC_LOCK_FREE
