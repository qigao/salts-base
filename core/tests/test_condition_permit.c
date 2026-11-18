#define _POSIX_C_SOURCE 200809L
#include <threads/threads.h>
#include "condition_permit_posix.h"
#include <stdatomic.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>

struct waiter {
    struct permit_condition *condition;
    platform_mutex *mutex;
    atomic_int entering;
    long ms;
};
static int wait_for_permit(void *arg) {
    struct waiter *w = arg;
    if (platform_mutex_lock(w->mutex)) abort();
    struct timespec due;
    if (clock_gettime(CLOCK_MONOTONIC, &due)) abort();
    due.tv_nsec += (w->ms % 1000) * 1000000L;
    due.tv_sec += w->ms / 1000 + due.tv_nsec / 1000000000L;
    due.tv_nsec %= 1000000000L;
    atomic_store(&w->entering, 1);
    int rc = permit_condition_wait(w->condition, w->mutex, &due, CLOCK_MONOTONIC);
    if (platform_mutex_unlock(w->mutex)) abort();
    return rc;
}
static thrd_t start_waiter(struct waiter *w) {
    thrd_t thread;
    atomic_store(&w->entering, 0);
    if (thrd_create(&thread, wait_for_permit, w)) abort();
    while (!atomic_load(&w->entering)) thrd_yield();
    return thread;
}
static void await_registration(platform_mutex *mutex) {
    /* The worker holds the application mutex through enrollment. */
    if (platform_mutex_lock(mutex) || platform_mutex_unlock(mutex)) abort();
}
spec("Native condition permits") {
    it("expires both before and after enrollment while the registration gate is held") {
        struct permit_condition c = {0};
        platform_mutex *m = NULL;
        check_equal(permit_init(&c.guard, 1), SALTS_OK);
        check_equal(platform_mutex_create(&m), SALTS_OK);
        struct waiter w = {&c, m, 0, 100};
        for (int enrolled = 0; enrolled <= 1; ++enrolled) {
            if (!enrolled) check_equal(permit_wait(&c.guard, NULL, CLOCK_MONOTONIC), SALTS_OK);
            thrd_t worker = start_waiter(&w);
            if (enrolled) {
                await_registration(m);
                check_equal(permit_wait(&c.guard, NULL, CLOCK_MONOTONIC), SALTS_OK);
            }
            int rc;
            check_equal(thrd_join(worker, &rc), thrd_success);
            check_equal(rc, SALTS_ETIMEDOUT);
            permit_post(&c.guard, 1);
        }
        check_equal(permit_condition_notify(&c, 0), 0);
        permit_condition_destroy(&c);
        check_equal(platform_mutex_destroy(&m), SALTS_OK);
    }
    it("delivers one signal then broadcasts across sealed and open registrations") {
        struct permit_condition c = {0};
        platform_mutex *m = NULL;
        check_equal(permit_init(&c.guard, 1), SALTS_OK);
        check_equal(platform_mutex_create(&m), SALTS_OK);
        struct waiter w = {&c, m, 0, 5000};
        thrd_t workers[4];
        for (int i = 0; i < 3; ++i) {
            workers[i] = start_waiter(&w);
            await_registration(m);
        }
        check_equal(permit_condition_notify(&c, 0), 1);
        workers[3] = start_waiter(&w);
        await_registration(m);
        check_equal(permit_condition_notify(&c, 1), 1);
        for (int i = 0; i < 4; ++i) {
            int rc;
            check_equal(thrd_join(workers[i], &rc), thrd_success);
            check_equal(rc, SALTS_OK);
        }
        w.ms = 0;
        int rc;
        thrd_t late = start_waiter(&w);
        check_equal(thrd_join(late, &rc), thrd_success);
        check_equal(rc, SALTS_ETIMEDOUT);
        permit_condition_destroy(&c);
        check_equal(platform_mutex_destroy(&m), SALTS_OK);
    }
}
