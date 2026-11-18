#define _POSIX_C_SOURCE 200809L
#include <threads/threads.h>
#include <time.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>

static once_flag retry_once = ONCE_FLAG_INIT;
static int attempts, published;
static void exit_first(void) {
    if (++attempts == 1) thrd_exit(37);
    published = 41;
}
static int initialize(void *unused) {
    (void)unused;
    call_once(&retry_once, exit_first);
    return published;
}

static struct timespec deadline_ms(long ms) {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t)) abort();
    t.tv_nsec += ms * 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}

struct race {
    mtx_t mutex;
    cnd_t changed;
    int ready;
    long budget;
};
static int timed_contender(void *arg) {
    struct race *r = arg;
    struct timespec due = deadline_ms(r->budget);
    int rc = mtx_timedlock(&r->mutex, &due);
    if (rc == thrd_success && mtx_unlock(&r->mutex)) abort();
    return rc;
}
static int condition_contender(void *arg) {
    struct race *r = arg;
    if (mtx_lock(&r->mutex)) abort();
    struct timespec due = deadline_ms(r->budget);
    r->ready = 1;
    if (cnd_broadcast(&r->changed)) abort();
    int rc = thrd_success;
    while (r->ready == 1 && rc == thrd_success)
        rc = cnd_timedwait(&r->changed, &r->mutex, &due);
    if (mtx_unlock(&r->mutex)) abort();
    return rc;
}

spec("POSIX C11 blocking primitives") {
    it("allows another caller to finish once after the first initializer exits") {
        thrd_t first, rest[4];
        int result;
        check_equal(thrd_create(&first, initialize, NULL), thrd_success);
        check_equal(thrd_join(first, &result), thrd_success);
        check_equal(result, 37);
        for (unsigned i = 0; i < 4; ++i)
            check_equal(thrd_create(&rest[i], initialize, NULL), thrd_success);
        for (unsigned i = 0; i < 4; ++i) {
            check_equal(thrd_join(rest[i], &result), thrd_success);
            check_equal(result, 41);
        }
        check_equal(attempts, 2);
    }
    it("times out behind a held mutex at short, medium and long deadlines") {
        struct race r = {0};
        const long budgets[] = {1, 20, 100};
        check_equal(mtx_init(&r.mutex, mtx_timed), thrd_success);
        check_equal(mtx_lock(&r.mutex), thrd_success);
        for (unsigned i = 0; i < 3; ++i) {
            thrd_t worker;
            int result;
            r.budget = budgets[i];
            check_equal(thrd_create(&worker, timed_contender, &r), thrd_success);
            check_equal(thrd_join(worker, &result), thrd_success);
            check_equal(result, thrd_timedout);
        }
        check_equal(mtx_unlock(&r.mutex), thrd_success);
        mtx_destroy(&r.mutex);
    }
    it("reuses the same condition and mutex across signal and timeout races") {
        struct race r = {0};
        check_equal(mtx_init(&r.mutex, mtx_timed), thrd_success);
        check_equal(cnd_init(&r.changed), thrd_success);
        for (unsigned i = 0; i < 120; ++i) {
            thrd_t worker;
            int result;
            r.ready = 0;
            r.budget = i % 2;
            check_equal(thrd_create(&worker, condition_contender, &r), thrd_success);
            check_equal(mtx_lock(&r.mutex), thrd_success);
            while (!r.ready) check_equal(cnd_wait(&r.changed, &r.mutex), thrd_success);
            r.ready = 2;
            check_equal(cnd_signal(&r.changed), thrd_success);
            check_equal(mtx_unlock(&r.mutex), thrd_success);
            check_equal(thrd_join(worker, &result), thrd_success);
            check(result == thrd_success || result == thrd_timedout);
        }
        cnd_destroy(&r.changed);
        mtx_destroy(&r.mutex);
    }
}
