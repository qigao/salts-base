#ifdef __APPLE__
#include "c11_mutex_darwin.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

/* One state word owns the lock and its waiter count. WAKE_RESERVED covers
 * one Mach notification from reservation until a waiter consumes it. It
 * never grants ownership. A timeout only withdraws its own registration. */
enum { WAKE_RESERVED = 2, WAITER = 4 };

int darwin_mutex_init(darwin_mutex *m) {
    atomic_init(&m->state, 0);
    kern_return_t rc = semaphore_create(mach_task_self(), &m->wake, SYNC_POLICY_FIFO, 0);
    if (rc == KERN_RESOURCE_SHORTAGE) return SALTS_ENOMEM;
    return rc == KERN_SUCCESS ? SALTS_OK : SALTS_EIO;
}

void darwin_mutex_destroy(darwin_mutex *m) {
    /* An unused notification may remain after all timed waiters withdrew. */
    if (atomic_load_explicit(&m->state, memory_order_relaxed) & ~(unsigned)WAKE_RESERVED)
        abort();
    if (semaphore_destroy(mach_task_self(), m->wake) != KERN_SUCCESS) abort();
}

static void notify(darwin_mutex *m) {
#ifdef SALTS_TEST_DARWIN_MUTEX
    darwin_mutex_test_hook(m, DARWIN_MUTEX_BEFORE_WAKE);
#endif
    if (semaphore_signal(m->wake) != KERN_SUCCESS) abort();
}

static int time_left(const struct timespec *due, clockid_t clock, mach_timespec_t *left) {
    struct timespec now;
    if (clock_gettime(clock, &now)) return SALTS_EIO;
    if (due->tv_sec < now.tv_sec ||
        (due->tv_sec == now.tv_sec && due->tv_nsec <= now.tv_nsec))
        return SALTS_ETIMEDOUT;

    uint64_t seconds = (uint64_t)due->tv_sec - (uint64_t)now.tv_sec;
    long ns = due->tv_nsec - now.tv_nsec;
    if (ns < 0) { --seconds; ns += 1000000000L; }
    /* Mach uses a relative duration with unsigned seconds. A farther
     * deadline needs another native wait if the first interval expires. */
    left->tv_sec = seconds > UINT_MAX ? UINT_MAX : (unsigned)seconds;
    left->tv_nsec = seconds > UINT_MAX ? 0 : (int)ns;
    return SALTS_OK;
}

static int park(darwin_mutex *m, const struct timespec *due, clockid_t clock) {
    for (;;) {
        kern_return_t rc;
        if (due) {
            mach_timespec_t left;
            int result = time_left(due, clock, &left);
            if (result != SALTS_OK) return result;
            rc = semaphore_timedwait(m->wake, left);
        } else {
            rc = semaphore_wait(m->wake);
        }
        if (rc == KERN_SUCCESS) return SALTS_OK;
        if (rc != KERN_ABORTED && rc != KERN_OPERATION_TIMED_OUT) return SALTS_EIO;
    }
}

static int consume_wake(darwin_mutex *m, int result) {
    unsigned state = atomic_load_explicit(&m->state, memory_order_relaxed);
    unsigned next;
    do {
        if (!(state & WAKE_RESERVED) || state < WAITER) abort();
        next = (state - WAITER) & ~(unsigned)WAKE_RESERVED;
        if (!(next & DARWIN_MUTEX_HELD)) {
            if (result == SALTS_OK) next |= DARWIN_MUTEX_HELD;
            else if (next >= WAITER) next |= WAKE_RESERVED;
        }
    } while (!atomic_compare_exchange_weak_explicit(&m->state, &state, next,
                memory_order_acq_rel, memory_order_relaxed));
    if (next & WAKE_RESERVED) notify(m);
    if (result != SALTS_OK) return result;
    return state & DARWIN_MUTEX_HELD ? SALTS_EBUSY : SALTS_OK;
}

int darwin_mutex_wait(darwin_mutex *m, const struct timespec *due, clockid_t clock) {
    unsigned state = atomic_load_explicit(&m->state, memory_order_relaxed);
    for (;;) {
        mach_timespec_t left;
        if (due) {
            int rc = time_left(due, clock, &left);
            if (rc != SALTS_OK) return rc;
        }
        if (!(state & DARWIN_MUTEX_HELD)) {
            if (atomic_compare_exchange_weak_explicit(&m->state, &state, state | DARWIN_MUTEX_HELD,
                    memory_order_acquire, memory_order_relaxed))
                return SALTS_OK;
            continue;
        }
        if (state > UINT_MAX - WAITER) return SALTS_EBUSY;
        if (!atomic_compare_exchange_weak_explicit(&m->state, &state, state + WAITER,
                memory_order_relaxed, memory_order_relaxed))
            continue;

#ifdef SALTS_TEST_DARWIN_MUTEX
        darwin_mutex_test_hook(m, DARWIN_MUTEX_ENROLLED);
#endif
        int rc = park(m, due, clock);
        if (rc != SALTS_OK) {
            atomic_fetch_sub_explicit(&m->state, WAITER, memory_order_relaxed);
            return rc;
        }
        rc = consume_wake(m, due ? time_left(due, clock, &left) : SALTS_OK);
        if (rc != SALTS_EBUSY) return rc;
        state = atomic_load_explicit(&m->state, memory_order_relaxed);
    }
}

void darwin_mutex_unlock_slow(darwin_mutex *m) {
    unsigned state = atomic_load_explicit(&m->state, memory_order_relaxed);
    unsigned next;
    int wake;
    do {
        if (!(state & DARWIN_MUTEX_HELD)) abort();
        wake = state >= WAITER && !(state & WAKE_RESERVED);
        next = state & ~(unsigned)DARWIN_MUTEX_HELD;
        if (wake) next |= WAKE_RESERVED;
    } while (!atomic_compare_exchange_weak_explicit(&m->state, &state, next,
                memory_order_release, memory_order_relaxed));
    if (wake) notify(m);
}
#endif
