#include <atomic/atomic.h>
#include <threads/threads.h>
#include <tinytest.h>
#include <limits.h>
#include <stdint.h>

SALTS_ATOMIC_INTEGER_TYPE(AtomicInteger, int);
typedef int *IntegerPointer;
SALTS_ATOMIC_POINTER_TYPE(AtomicPointer, IntegerPointer);

typedef struct publication {
    atomic_int ready;
    int payload[3];
} publication;

static int publish(void *arg) {
    publication *state = arg;
    state->payload[0] = 17;
    state->payload[1] = 31;
    state->payload[2] = 53;
    atomic_store_explicit(&state->ready, 1, memory_order_release);
    return 29;
}

spec("C11 atomic operations") {
    it("provides integer arithmetic, bitwise operations and failed CAS outputs") {
        AtomicInteger value;
        int old = -1, expected = 99, actual = 0;
        bool changed = true;
        check_equal(AtomicInteger_init(&value, 7), SALTS_OK);
        check_equal(AtomicInteger_fetch_add(&value, 3, memory_order_relaxed, &old), SALTS_OK);
        check_equal(old, 7);
        check_equal(AtomicInteger_fetch_sub(&value, 2, memory_order_acq_rel, &old), SALTS_OK);
        check_equal(old, 10);
        check_equal(AtomicInteger_fetch_or(&value, 3, memory_order_release, &old), SALTS_OK);
        check_equal(old, 8);
        check_equal(AtomicInteger_fetch_and(&value, 7, memory_order_acquire, &old), SALTS_OK);
        check_equal(old, 11);
        check_equal(AtomicInteger_fetch_xor(&value, 6, memory_order_seq_cst, &old), SALTS_OK);
        check_equal(old, 3);
        check_equal(AtomicInteger_compare_exchange_weak(&value, &expected, 19,
            memory_order_acq_rel, memory_order_acquire, &changed), SALTS_OK);
        check_false(changed);
        check_equal(expected, 5);
        old = 123;
        check_equal(AtomicInteger_fetch_add(&value, 1, (memory_order)99, &old), SALTS_EINVAL);
        check_equal(old, 123);
        check_equal(AtomicInteger_load(&value, memory_order_relaxed, &actual), SALTS_OK);
        check_equal(actual, 5);
    }
    it("uses the defined signed atomic wraparound and element-scaled pointer arithmetic") {
        AtomicInteger integer;
        AtomicPointer pointer;
        int array[5] = {0};
        int old, actual;
        IntegerPointer previous = NULL, current = NULL;
        check_equal(AtomicInteger_init(&integer, INT_MAX), SALTS_OK);
        check_equal(AtomicInteger_fetch_add(&integer, 1, memory_order_seq_cst, &old), SALTS_OK);
        check_equal(old, INT_MAX);
        check_equal(AtomicInteger_load(&integer, memory_order_relaxed, &actual), SALTS_OK);
        check_equal(actual, INT_MIN);
        check_equal(AtomicPointer_init(&pointer, array), SALTS_OK);
        check_equal(AtomicPointer_fetch_add(&pointer, 3, memory_order_acq_rel, &previous), SALTS_OK);
        check(previous == array);
        check_equal(AtomicPointer_load(&pointer, memory_order_relaxed, &current), SALTS_OK);
        check(current == array + 3);
        check_equal(AtomicPointer_fetch_sub(&pointer, 2, memory_order_release, &previous), SALTS_OK);
        check(previous == array + 3);
        check_equal(AtomicPointer_exchange(&pointer, array + 4, memory_order_seq_cst, &previous), SALTS_OK);
        check(previous == array + 1);
    }
    it("provides standard atomic flags and all six fence orders") {
        atomic_flag flag = ATOMIC_FLAG_INIT;
        const memory_order orders[] = {memory_order_relaxed, memory_order_consume,
            memory_order_acquire, memory_order_release, memory_order_acq_rel,
            memory_order_seq_cst};
        check_false(atomic_flag_test_and_set_explicit(&flag, memory_order_acquire));
        check_true(atomic_flag_test_and_set_explicit(&flag, memory_order_relaxed));
        atomic_flag_clear_explicit(&flag, memory_order_release);
        check_false(atomic_flag_test_and_set(&flag));
        atomic_flag_clear(&flag);
        for (size_t i = 0; i < sizeof(orders) / sizeof(orders[0]); ++i) {
            atomic_thread_fence(orders[i]);
            atomic_signal_fence(orders[i]);
        }
    }
    it("accepts exactly the C11 CAS order pairs and preserves outputs on rejection") {
        const memory_order orders[] = {memory_order_relaxed, memory_order_consume,
            memory_order_acquire, memory_order_release, memory_order_acq_rel,
            memory_order_seq_cst};
        const atomic_order abi_orders[] = {SALTS_MEMORY_RELAXED, SALTS_MEMORY_CONSUME,
            SALTS_MEMORY_ACQUIRE, SALTS_MEMORY_RELEASE, SALTS_MEMORY_ACQ_REL,
            SALTS_MEMORY_SEQ_CST};
        /* Bit j admits failure order j for success order i (C11 7.17.7.4). */
        const unsigned admitted[] = {1u, 3u, 7u, 1u, 7u, 39u};
        for (size_t i = 0; i < 6u; ++i) {
            for (size_t j = 0; j < 6u; ++j) {
                AtomicInteger value;
                atomic32 slot;
                int expected = 9;
                uint32_t expected_slot = 9;
                bool changed = true;
                int changed_slot = 1;
                const int valid = (admitted[i] & (1u << j)) != 0u;
                check_equal(AtomicInteger_init(&value, 5), SALTS_OK);
                check_equal(atomic32_init(&slot, 5), SALTS_OK);
                check_equal(AtomicInteger_compare_exchange(&value, &expected, 7,
                    orders[i], orders[j], &changed), valid ? SALTS_OK : SALTS_EINVAL);
                check_equal(atomic32_cas(&slot, &expected_slot, 7,
                    abi_orders[i], abi_orders[j], &changed_slot), valid ? SALTS_OK : SALTS_EINVAL);
                check_equal(expected, valid ? 5 : 9);
                check_equal(expected_slot, valid ? 5u : 9u);
                check_equal(changed, valid ? false : true);
                check_equal(changed_slot, valid ? 0 : 1);
            }
        }
    }
    it("publishes ordinary payload through a C11 release/acquire pair") {
        publication state;
        thrd_t thread;
        int result = -1;
        atomic_init(&state.ready, 0);
        check_equal(thrd_create(&thread, publish, &state), thrd_success);
        while (!atomic_load_explicit(&state.ready, memory_order_acquire)) thrd_yield();
        check_equal(state.payload[0], 17);
        check_equal(state.payload[1], 31);
        check_equal(state.payload[2], 53);
        check_equal(thrd_join(thread, &result), thrd_success);
        check_equal(result, 29);
    }
    it("implements every fixed-width ABI read-modify-write operation") {
#define CHECK_WIDTH(N, T) do { \
        atomic##N value; T old = 0, actual = 0, expected = 9; int changed = -1, lock_free = -1; \
        check_equal(atomic##N##_init(&value, 1), SALTS_OK); \
        check_equal(atomic##N##_fetch_sub(&value, 2, SALTS_MEMORY_RELAXED, &old), SALTS_OK); \
        check_equal(old, (T)1); \
        check_equal(atomic##N##_fetch_and(&value, 15, SALTS_MEMORY_ACQUIRE, &old), SALTS_OK); \
        check_equal(old, (T)-1); \
        check_equal(atomic##N##_fetch_xor(&value, 3, SALTS_MEMORY_RELEASE, &old), SALTS_OK); \
        check_equal(old, (T)15); \
        check_equal(atomic##N##_fetch_or(&value, 1, SALTS_MEMORY_ACQ_REL, &old), SALTS_OK); \
        check_equal(old, (T)12); \
        check_equal(atomic##N##_cas_weak(&value, &expected, 2, SALTS_MEMORY_SEQ_CST, SALTS_MEMORY_ACQUIRE, &changed), SALTS_OK); \
        check_equal(changed, 0); check_equal(expected, (T)13); \
        check_equal(atomic##N##_load(&value, SALTS_MEMORY_RELAXED, &actual), SALTS_OK); \
        check_equal(actual, (T)13); \
        check_equal(atomic##N##_is_lock_free(&value, &lock_free), SALTS_OK); \
        check(lock_free == 0 || lock_free == 1); \
        old = 42; \
        check_equal(atomic##N##_fetch_sub(&value, 1, 99, &old), SALTS_EINVAL); \
        check_equal(old, (T)42); \
    } while (0)
        CHECK_WIDTH(8, uint8_t);
        CHECK_WIDTH(16, uint16_t);
        CHECK_WIDTH(32, uint32_t);
        CHECK_WIDTH(64, uint64_t);
#undef CHECK_WIDTH
    }
}
