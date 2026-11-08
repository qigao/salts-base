#include <platform_atomic.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN 1
# endif
# include <windows.h>
# if defined(_MSC_VER) || defined(__clang__)
#  include <intrin.h> /* Native Interlocked intrinsics; no linker API guess. */
# else
#  error "Windows provider requires MSVC or Clang; TinyCC remains a C ABI consumer"
# endif
#else
# if !defined(__GNUC__) && !defined(__clang__)
#  error "Platform atomic backend requires Win32 Interlocked or GCC/Clang __atomic; no volatile fallback"
# endif
#endif

int SALTS_CORE_PLATFORM_CALL salts_platform_portable_abi_verify(
    uint32_t version, uint32_t atomic64_bytes, uint32_t atomic64_alignment,
    uint32_t atomic_pointer_bytes) {
    struct probe64 { char pad; salts_atomic64 value; };
    const uint32_t align64 = (uint32_t)offsetof(struct probe64, value);
    if (version != SALTS_PLATFORM_PORTABLE_ABI_VERSION ||
        atomic64_bytes != sizeof(salts_atomic64) ||
        atomic64_alignment != align64 ||
        atomic_pointer_bytes != sizeof(salts_atomic_ptr))
        return SALTS_EINVAL;
    return SALTS_OK;
}

static int valid_order(salts_memory_order order) {
    return order <= SALTS_MEMORY_SEQ_CST;
}
static int valid_load(salts_memory_order order) {
    return order == SALTS_MEMORY_RELAXED || order == SALTS_MEMORY_CONSUME ||
           order == SALTS_MEMORY_ACQUIRE || order == SALTS_MEMORY_SEQ_CST;
}
static int valid_store(salts_memory_order order) {
    return order == SALTS_MEMORY_RELAXED || order == SALTS_MEMORY_RELEASE ||
           order == SALTS_MEMORY_SEQ_CST;
}
static int valid_cas(salts_memory_order succ, salts_memory_order fail) {
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
#if defined(_WIN32)
# define SALTS_NATIVE_CAS8(p, e, d) _InterlockedCompareExchange8((volatile char *)(p), (char)(d), (char)(e))
# define SALTS_NATIVE_CAS16(p, e, d) _InterlockedCompareExchange16((volatile short *)(p), (short)(d), (short)(e))
# define SALTS_NATIVE_CAS32(p, e, d) _InterlockedCompareExchange((volatile long *)(p), (long)(d), (long)(e))
# define SALTS_NATIVE_CAS64(p, e, d) _InterlockedCompareExchange64((volatile __int64 *)(p), (__int64)(d), (__int64)(e))
# define SALTS_DEFINE_ATOMIC(N, T) \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_init(salts_atomic##N *p, T v) { \
    if (!aligned(p, sizeof(T))) return SALTS_EINVAL; \
    p->value = v; return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_load(const salts_atomic##N *p, salts_memory_order order, T *out) { \
    if (!aligned(p, sizeof(T)) || !out || !valid_load(order)) return SALTS_EINVAL; \
    *out = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_cas(salts_atomic##N *p, T *expected, T desired, salts_memory_order success, salts_memory_order failure, int *exchanged) { \
    T observed; \
    if (!aligned(p, sizeof(T)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL; \
    observed = (T)SALTS_NATIVE_CAS##N(&p->value, *expected, desired); \
    *exchanged = (observed == *expected); \
    if (!*exchanged) *expected = observed; return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_store(salts_atomic##N *p, T v, salts_memory_order order) { \
    T current, expected; int yes; \
    if (!aligned(p, sizeof(T)) || !valid_store(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; \
         current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, v); \
         yes = (current == expected); \
    } while (!yes); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_exchange(salts_atomic##N *p, T v, salts_memory_order order, T *old) { \
    T current, expected; \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, v); \
    } while (current != expected); *old = current; return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_fetch_add(salts_atomic##N *p, T delta, salts_memory_order order, T *old) { \
    T current, expected; \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    current = (T)SALTS_NATIVE_CAS##N(&p->value, 0, 0); \
    do { expected = current; current = (T)SALTS_NATIVE_CAS##N(&p->value, expected, (T)(expected + delta)); \
    } while (current != expected); *old = current; return SALTS_OK; \
}
#else
# define SALTS_DEFINE_ATOMIC(N, T) \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_init(salts_atomic##N *p, T v) { \
    if (!aligned(p, sizeof(T))) return SALTS_EINVAL; \
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_load(const salts_atomic##N *p, salts_memory_order order, T *out) { \
    if (!aligned(p, sizeof(T)) || !out || !valid_load(order)) return SALTS_EINVAL; \
    *out = __atomic_load_n(&p->value, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_cas(salts_atomic##N *p, T *expected, T desired, salts_memory_order success, salts_memory_order failure, int *exchanged) { \
    if (!aligned(p, sizeof(T)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL; \
    *exchanged = __atomic_compare_exchange_n(&p->value, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_store(salts_atomic##N *p, T v, salts_memory_order order) { \
    if (!aligned(p, sizeof(T)) || !valid_store(order)) return SALTS_EINVAL; \
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_exchange(salts_atomic##N *p, T v, salts_memory_order order, T *old) { \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    *old = __atomic_exchange_n(&p->value, v, __ATOMIC_SEQ_CST); return SALTS_OK; \
} \
int SALTS_CORE_PLATFORM_CALL salts_atomic##N##_fetch_add(salts_atomic##N *p, T delta, salts_memory_order order, T *old) { \
    if (!aligned(p, sizeof(T)) || !old || !valid_order(order)) return SALTS_EINVAL; \
    *old = __atomic_fetch_add(&p->value, delta, __ATOMIC_SEQ_CST); return SALTS_OK; \
}
#endif
SALTS_DEFINE_ATOMIC(8, uint8_t)
SALTS_DEFINE_ATOMIC(16, uint16_t)
SALTS_DEFINE_ATOMIC(32, uint32_t)
SALTS_DEFINE_ATOMIC(64, uint64_t)
#undef SALTS_DEFINE_ATOMIC

int SALTS_CORE_PLATFORM_CALL salts_atomic_ptr_init(salts_atomic_ptr *p, void *v) {
    if (!aligned(p, sizeof(void *))) return SALTS_EINVAL;
#if defined(_WIN32)
    p->value = v;  /* Initialization must complete before publication. */
#else
    __atomic_store_n(&p->value, v, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_atomic_ptr_load(const salts_atomic_ptr *p, salts_memory_order order, void **out) {
    if (!aligned(p, sizeof(void *)) || !out || !valid_load(order)) return SALTS_EINVAL;
#if defined(_WIN32)
    *out = InterlockedCompareExchangePointer((void *volatile *)&p->value, NULL, NULL);
#else
    *out = __atomic_load_n(&p->value, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_atomic_ptr_cas(salts_atomic_ptr *p, void **expected, void *desired, salts_memory_order success, salts_memory_order failure, int *exchanged) {
    if (!aligned(p, sizeof(void *)) || !expected || !exchanged || !valid_cas(success, failure)) return SALTS_EINVAL;
#if defined(_WIN32)
    void *observed = InterlockedCompareExchangePointer((void *volatile *)&p->value, desired, *expected);
    *exchanged = observed == *expected;
    if (!*exchanged) *expected = observed;
#else
    *exchanged = __atomic_compare_exchange_n(&p->value, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_atomic_ptr_store(salts_atomic_ptr *p, void *value, salts_memory_order order) {
    if (!aligned(p, sizeof(void *)) || !valid_store(order)) return SALTS_EINVAL;
#if defined(_WIN32)
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
int SALTS_CORE_PLATFORM_CALL salts_atomic_ptr_exchange(salts_atomic_ptr *p, void *value, salts_memory_order order, void **old) {
    if (!aligned(p, sizeof(void *)) || !old || !valid_order(order)) return SALTS_EINVAL;
#if defined(_WIN32)
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
int SALTS_CORE_PLATFORM_CALL salts_atomic_thread_fence(salts_memory_order order) {
    if (!valid_order(order)) return SALTS_EINVAL;
#if defined(_WIN32)
    MemoryBarrier();
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_atomic_signal_fence(salts_memory_order order) {
    if (!valid_order(order)) return SALTS_EINVAL;
#if defined(_WIN32)
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
