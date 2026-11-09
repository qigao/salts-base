#ifndef SALTS_ATOMIC_H
#define SALTS_ATOMIC_H

#include <salts/api.h>

/* Salts::Core compiled C ABI. These slots are NOT C11 _Atomic or
 * C++ std::atomic objects and must not be copied while being used.
 * The provider owns the operations; weaker requested orders currently
 * execute with sequential consistency. No volatile-only fallback.
 */
#include <stdint.h>
#include <salts/error_codes.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
# define SALTS_CORE_ALIGN8 __declspec(align(8))
#elif defined(__GNUC__) || defined(__clang__) || defined(__TINYC__)
# define SALTS_CORE_ALIGN8 __attribute__((aligned(8)))
#else
# error "An explicit eight-byte alignment implementation is required"
#endif

/* Fixed 32-bit ABI even if clients use -fshort-enums. */
typedef uint32_t atomic_order;
enum {
    SALTS_MEMORY_RELAXED = 0,
    SALTS_MEMORY_CONSUME = 1,
    SALTS_MEMORY_ACQUIRE = 2,
    SALTS_MEMORY_RELEASE = 3,
    SALTS_MEMORY_ACQ_REL = 4,
    SALTS_MEMORY_SEQ_CST = 5
};

typedef struct atomic8 { uint8_t value; } atomic8;
typedef struct atomic16 { uint16_t value; } atomic16;
typedef struct atomic32 { uint32_t value; } atomic32;
typedef struct atomic64 { SALTS_CORE_ALIGN8 uint64_t value; } atomic64;
typedef struct atomic_ptr { void *value; } atomic_ptr;

/* Fail closed on cross-compiler ABI mismatch. Never cast native _Atomic
 * storage to these slot types, even if sizes happen to match.
 */
#define SALTS_PLATFORM_PORTABLE_ABI_VERSION 1u
SALTS_API int platform_portable_abi_verify(
    uint32_t version, uint32_t atomic64_bytes, uint32_t atomic64_alignment,
    uint32_t atomic_pointer_bytes);

/* CAS returns SALTS_OK and sets *exchanged to 0/1; on comparison failure it
 * updates *expected with the observed value. Weak CAS may use strong CAS.
 * Fetch operations commit the previous value to *old only on success.
 * Invalid orders, alignment or outputs return SALTS_EINVAL without changing
 * storage or outputs. is_lock_free writes 0/1 for a valid aligned slot.
 */
#define SALTS_CORE_DECLARE_ATOMIC(N, T) \
  SALTS_API int atomic##N##_init(atomic##N *slot, T value); \
  SALTS_API int atomic##N##_load(const atomic##N *slot, atomic_order order, T *out); \
  SALTS_API int atomic##N##_store(atomic##N *slot, T value, atomic_order order); \
  SALTS_API int atomic##N##_exchange(atomic##N *slot, T value, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_add(atomic##N *slot, T delta, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_sub(atomic##N *slot, T delta, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_and(atomic##N *slot, T bits, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_or(atomic##N *slot, T bits, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_xor(atomic##N *slot, T bits, atomic_order order, T *old); \
  SALTS_API int atomic##N##_cas(atomic##N *slot, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged); \
  SALTS_API int atomic##N##_cas_weak(atomic##N *slot, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged); \
  SALTS_API int atomic##N##_is_lock_free(const atomic##N *slot, int *out)
SALTS_CORE_DECLARE_ATOMIC(8, uint8_t);
SALTS_CORE_DECLARE_ATOMIC(16, uint16_t);
SALTS_CORE_DECLARE_ATOMIC(32, uint32_t);
SALTS_CORE_DECLARE_ATOMIC(64, uint64_t);
#undef SALTS_CORE_DECLARE_ATOMIC

SALTS_API int atomic_ptr_init(atomic_ptr *slot, void *value);
SALTS_API int atomic_ptr_load(const atomic_ptr *slot, atomic_order order, void **out);
SALTS_API int atomic_ptr_store(atomic_ptr *slot, void *value, atomic_order order);
SALTS_API int atomic_ptr_exchange(atomic_ptr *slot, void *value, atomic_order order, void **old);
SALTS_API int atomic_ptr_cas(atomic_ptr *slot, void **expected, void *desired, atomic_order success, atomic_order failure, int *exchanged);
SALTS_API int atomic_ptr_cas_weak(atomic_ptr *slot, void **expected, void *desired, atomic_order success, atomic_order failure, int *exchanged);
SALTS_API int atomic_ptr_is_lock_free(const atomic_ptr *slot, int *out);
SALTS_API int platform_atomic_thread_fence(atomic_order order);
SALTS_API int platform_atomic_signal_fence(atomic_order order);

#ifdef __cplusplus
}
#endif

/* Native C11 storage and checked typed helpers. C++ clients use <atomic> for
 * native objects. TinyCC and C implementations declaring no atomics can use
 * the compiled ABI above without requiring compiler-native _Atomic support.
 */
#if !defined(__cplusplus) && !defined(__STDC_NO_ATOMICS__) && !defined(__TINYC__)
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

static inline bool atomic_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_release ||
           order == memory_order_acq_rel || order == memory_order_seq_cst;
}

static inline bool atomic_load_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_seq_cst;
}

static inline bool atomic_store_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_release ||
           order == memory_order_seq_cst;
}

static inline bool atomic_cas_orders_valid(
    memory_order success, memory_order failure) {
    if (!atomic_order_valid(success) ||
        !atomic_load_order_valid(failure))
        return false;
    if (failure == memory_order_relaxed)
        return true;
    if (failure == memory_order_consume)
        return success == memory_order_consume ||
               success == memory_order_acquire ||
               success == memory_order_acq_rel ||
               success == memory_order_seq_cst;
    if (failure == memory_order_acquire)
        return success == memory_order_acquire ||
               success == memory_order_acq_rel ||
               success == memory_order_seq_cst;
    return success == memory_order_seq_cst;
}

/*
 * Finite typed atomic declaration owned by Salts::Core.
 *
 * This is a direct C11 atomic wrapper, not a Reflection/type-metadata facility.
 * Storage must never be copied while live. Callers supply every memory order.
 */
#define SALTS_ATOMIC_TYPE(name_, type_) \
    typedef struct name_ { _Atomic(type_) value; } name_; \
    static inline int name_##_init(name_ *p, type_ value) { \
        if (p == NULL) return SALTS_EINVAL; \
        atomic_init(&p->value, value); \
        return SALTS_OK; \
    } \
    static inline int name_##_load( \
        const name_ *p, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || \
            !atomic_load_order_valid(order)) return SALTS_EINVAL; \
        *out = atomic_load_explicit(&p->value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_store( \
        name_ *p, type_ value, memory_order order) { \
        if (p == NULL || !atomic_store_order_valid(order)) \
            return SALTS_EINVAL; \
        atomic_store_explicit(&p->value, value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_exchange( \
        name_ *p, type_ value, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !atomic_order_valid(order)) \
            return SALTS_EINVAL; \
        *out = atomic_exchange_explicit(&p->value, value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_compare_exchange( \
        name_ *p, type_ *expected, type_ desired, \
        memory_order success, memory_order failure, bool *exchanged) { \
        if (p == NULL || expected == NULL || exchanged == NULL || \
            !atomic_cas_orders_valid(success, failure)) \
            return SALTS_EINVAL; \
        *exchanged = atomic_compare_exchange_strong_explicit( \
            &p->value, expected, desired, success, failure); \
        return SALTS_OK; \
    } \
    static inline int name_##_compare_exchange_weak( \
        name_ *p, type_ *expected, type_ desired, \
        memory_order success, memory_order failure, bool *exchanged) { \
        if (p == NULL || expected == NULL || exchanged == NULL || \
            !atomic_cas_orders_valid(success, failure)) \
            return SALTS_EINVAL; \
        *exchanged = atomic_compare_exchange_weak_explicit( \
            &p->value, expected, desired, success, failure); \
        return SALTS_OK; \
    } \
    static inline int name_##_is_lock_free(const name_ *p, bool *out) { \
        if (p == NULL || out == NULL) return SALTS_EINVAL; \
        *out = atomic_is_lock_free(&p->value); \
        return SALTS_OK; \
    } \
    typedef type_ name_##_value_type

/* Integer RMW operations are separate so the general declaration also works
 * for pointers and aggregate _Atomic types. Signed arithmetic follows C11's
 * defined atomic fetch arithmetic rather than a plain signed C expression.
 */
#define SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, operation_) \
    static inline int name_##_fetch_##operation_( \
        name_ *p, type_ operand, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !atomic_order_valid(order)) \
            return SALTS_EINVAL; \
        *out = atomic_fetch_##operation_##_explicit(&p->value, operand, order); \
        return SALTS_OK; \
    }

#define SALTS_ATOMIC_INTEGER_TYPE(name_, type_) \
    SALTS_ATOMIC_TYPE(name_, type_); \
    SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, add) \
    SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, sub) \
    SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, and) \
    SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, or) \
    SALTS_ATOMIC_INTEGER_OPERATION(name_, type_, xor) \
    typedef type_ name_##_integer_value_type

/* Pointer increments count elements, independently of a compiler builtin's
 * byte-oriented fetch-add convention. As in C11, the caller must keep the
 * resulting pointer within its array object (or one past its end).
 */
#define SALTS_ATOMIC_POINTER_OPERATION(name_, type_, operation_, operator_) \
    static inline int name_##_fetch_##operation_( \
        name_ *p, ptrdiff_t count, memory_order order, type_ *out) { \
        type_ expected, desired; \
        if (p == NULL || out == NULL || !atomic_order_valid(order)) \
            return SALTS_EINVAL; \
        expected = atomic_load_explicit(&p->value, memory_order_relaxed); \
        do { desired = expected operator_ count; } \
        while (!atomic_compare_exchange_weak_explicit( \
            &p->value, &expected, desired, order, memory_order_relaxed)); \
        *out = expected; \
        return SALTS_OK; \
    }

#define SALTS_ATOMIC_POINTER_TYPE(name_, type_) \
    SALTS_ATOMIC_TYPE(name_, type_); \
    SALTS_ATOMIC_POINTER_OPERATION(name_, type_, add, +) \
    SALTS_ATOMIC_POINTER_OPERATION(name_, type_, sub, -) \
    typedef type_ name_##_pointer_value_type
#endif /* native C11 atomics */

#endif /* SALTS_ATOMIC_H */
