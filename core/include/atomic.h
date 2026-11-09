#ifndef SALTS_ATOMIC_H
#define SALTS_ATOMIC_H

#if defined(__cplusplus) || defined(__STDC_NO_ATOMICS__)
#error "Salts atomic helpers require C11 atomics; use native C++ atomics in C++"
#endif

#include <error_codes.h>

#include <stdbool.h>
#include <stdatomic.h>

static inline bool cmeta_atomic_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_release ||
           order == memory_order_acq_rel || order == memory_order_seq_cst;
}

static inline bool cmeta_atomic_load_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_seq_cst;
}

static inline bool cmeta_atomic_store_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_release ||
           order == memory_order_seq_cst;
}

static inline bool cmeta_atomic_cas_orders_valid(
    memory_order success, memory_order failure) {
    if (!cmeta_atomic_order_valid(success) ||
        !cmeta_atomic_load_order_valid(failure))
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
 * Finite typed atomic declaration owned by Salts::Concurrency.
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
            !cmeta_atomic_load_order_valid(order)) return SALTS_EINVAL; \
        *out = atomic_load_explicit(&p->value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_store( \
        name_ *p, type_ value, memory_order order) { \
        if (p == NULL || !cmeta_atomic_store_order_valid(order)) \
            return SALTS_EINVAL; \
        atomic_store_explicit(&p->value, value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_exchange( \
        name_ *p, type_ value, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !cmeta_atomic_order_valid(order)) \
            return SALTS_EINVAL; \
        *out = atomic_exchange_explicit(&p->value, value, order); \
        return SALTS_OK; \
    } \
    static inline int name_##_compare_exchange( \
        name_ *p, type_ *expected, type_ desired, \
        memory_order success, memory_order failure, bool *exchanged) { \
        if (p == NULL || expected == NULL || exchanged == NULL || \
            !cmeta_atomic_cas_orders_valid(success, failure)) \
            return SALTS_EINVAL; \
        *exchanged = atomic_compare_exchange_strong_explicit( \
            &p->value, expected, desired, success, failure); \
        return SALTS_OK; \
    } \
    static inline int name_##_is_lock_free(const name_ *p, bool *out) { \
        if (p == NULL || out == NULL) return SALTS_EINVAL; \
        *out = atomic_is_lock_free(&p->value); \
        return SALTS_OK; \
    } \
    typedef type_ name_##_value_type

#endif
