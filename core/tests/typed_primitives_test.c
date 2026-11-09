#include <atomic.h>
#include <rcu.h>
#include <thread.h>
#include "tinytest.h"

SALTS_ATOMIC_TYPE(IntAtomic, int);
typedef int *IntPointer;
SALTS_ATOMIC_TYPE(PointerAtomic, IntPointer);
SALTS_RCU_TYPE(IntRcu, int);

typedef struct publication { IntAtomic ready; int payload; int seen; } publication;
static void acquire_payload(void *arg) {
    publication *ctx = arg;
    int ready = 0;
    while (ready == 0) {
        if (IntAtomic_load(&ctx->ready, memory_order_acquire, &ready) != SALTS_OK) return;
        cmeta_thread_yield();
    }
    ctx->seen = ctx->payload;
}

spec("Concurrency typed atomic and RCU ownership") {
    it("rejects invalid atomic arguments before mutating storage or outputs") {
        IntAtomic value;
        int out = 7, expected = 1;
        bool exchanged = true, lock_free = false;
        check_equal(IntAtomic_init(&value, 1), SALTS_OK);
        check_equal(IntAtomic_init(NULL, 2), SALTS_EINVAL);
        check_equal(IntAtomic_load(NULL, memory_order_acquire, &out), SALTS_EINVAL);
        check_equal(out, 7);
        check_equal(IntAtomic_load(&value, memory_order_acquire, NULL), SALTS_EINVAL);
        check_equal(IntAtomic_store(NULL, 2, memory_order_release), SALTS_EINVAL);
        check_equal(IntAtomic_exchange(&value, 2, memory_order_relaxed, NULL), SALTS_EINVAL);
        check_equal(IntAtomic_compare_exchange(&value, NULL, 2,
            memory_order_acq_rel, memory_order_acquire, &exchanged), SALTS_EINVAL);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 2,
            memory_order_acq_rel, memory_order_acquire, NULL), SALTS_EINVAL);
        check_equal(expected, 1);
        check_true(exchanged);
        check_equal(IntAtomic_is_lock_free(NULL, &lock_free), SALTS_EINVAL);
        check_false(lock_free);
        check_equal(IntAtomic_is_lock_free(&value, NULL), SALTS_EINVAL);
        check_equal(IntAtomic_load(&value, memory_order_relaxed, &out), SALTS_OK);
        check_equal(out, 1);
    }
    it("checks atomic orders and native compare-exchange expected-value semantics") {
        enum { INVALID_ORDER = 99 };
        IntAtomic value; PointerAtomic pointer;
        int out = 77, expected = 3, first = 1, second = 2;
        IntPointer pointer_expected = &first, pointer_out = NULL;
        bool exchanged = false, lock_free;
        check_equal(IntAtomic_init(&value, 4), SALTS_OK);
        check_equal(IntAtomic_load(&value, memory_order_release, &out), SALTS_EINVAL);
        check_equal(out, 77);
        check_equal(IntAtomic_store(&value, 9, memory_order_acquire), SALTS_EINVAL);
        check_equal(IntAtomic_exchange(&value, 9, (memory_order)INVALID_ORDER, &out), SALTS_EINVAL);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_release, memory_order_acquire, &exchanged), SALTS_EINVAL);
        check_equal(expected, 3);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_acq_rel, memory_order_acquire, &exchanged), SALTS_OK);
        check_false(exchanged); check_equal(expected, 4);
        check_equal(IntAtomic_compare_exchange(&value, &expected, 5,
            memory_order_acq_rel, memory_order_acquire, &exchanged), SALTS_OK);
        check_true(exchanged);
        check_equal(IntAtomic_exchange(&value, 6, memory_order_relaxed, &out), SALTS_OK);
        check_equal(out, 5);
        check_equal(IntAtomic_is_lock_free(&value, &lock_free), SALTS_OK);
        check_equal(PointerAtomic_init(&pointer, &first), SALTS_OK);
        check_equal(PointerAtomic_compare_exchange(&pointer, &pointer_expected, &second,
            memory_order_release, memory_order_relaxed, &exchanged), SALTS_OK);
        check_true(exchanged);
        check_equal(PointerAtomic_load(&pointer, memory_order_acquire, &pointer_out), SALTS_OK);
        check_true(pointer_out == &second);
        check_equal(PointerAtomic_store(&pointer, &first, memory_order_release), SALTS_OK);
        check_equal(PointerAtomic_exchange(&pointer, &second, memory_order_acq_rel, &pointer_out), SALTS_OK);
        check_true(pointer_out == &first);
        check_equal(PointerAtomic_is_lock_free(&pointer, &lock_free), SALTS_OK);
    }
    it("publishes payload through explicit release/acquire and exposes typed RCU ownership") {
        publication ctx = {0}; cmeta_thread_t thread = NULL;
        IntRcu domain = {0}; IntRcu_guard guard = {0};
        int first = 1, second = 2; int *out = NULL;
        check_equal(IntAtomic_init(&ctx.ready, 0), SALTS_OK);
        check_equal(cmeta_thread_create(&thread, acquire_payload, &ctx), 0);
        ctx.payload = 42;
        check_equal(IntAtomic_store(&ctx.ready, 1, memory_order_release), SALTS_OK);
        if (thread != NULL) check_equal(cmeta_thread_join(&thread), 0);
        check_equal(ctx.seen, 42);
        check_equal(IntRcu_init(&domain, &first, 1), SALTS_OK);
        check_equal(IntRcu_read_lock(&domain, &guard), SALTS_OK);
        check_equal(IntRcu_replace(&domain, &second), SALTS_OK);
        check_equal(*IntRcu_load(&guard), 1);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_EBUSY);
        check_null(out);
        check_equal(IntRcu_read_unlock(&guard), SALTS_OK);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_OK);
        check_true(out == &first);
        check_equal(IntRcu_close(&domain), SALTS_OK);
        check_equal(IntRcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == &second);
    }
    it("admits exactly the C11 compare-exchange success/failure order pairs") {
        enum { ORDER_COUNT = 6 };
        const memory_order orders[ORDER_COUNT] = {
            memory_order_relaxed, memory_order_consume, memory_order_acquire,
            memory_order_release, memory_order_acq_rel, memory_order_seq_cst
        };
        const bool admitted[ORDER_COUNT][ORDER_COUNT] = {
            {true, false, false, false, false, false},
            {true, true, false, false, false, false},
            {true, true, true, false, false, false},
            {true, false, false, false, false, false},
            {true, true, true, false, false, false},
            {true, true, true, false, false, true}
        };
        for (size_t success = 0; success < ORDER_COUNT; ++success) {
            for (size_t failure = 0; failure < ORDER_COUNT; ++failure) {
                IntAtomic value;
                int expected = 1;
                bool exchanged = false;
                check_equal(IntAtomic_init(&value, 1), SALTS_OK);
                check_equal(IntAtomic_compare_exchange(&value, &expected, 2,
                    orders[success], orders[failure], &exchanged),
                    admitted[success][failure] ? SALTS_OK : SALTS_EINVAL);
                check_equal(exchanged, admitted[success][failure]);
            }
        }
    }
}
