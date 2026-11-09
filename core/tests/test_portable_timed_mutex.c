#include <threads/threads.h>
#include "thread_backend.h"
#include <tinytest.h>
#include <stdint.h>
#include <stdio.h>

enum { N = 4, LOOPS = 160 };
static platform_mutex *lock;
static atomic32 admissions = {0};
static atomic32 shared = {0};
static atomic32 gate_ready = {0};
static atomic32 gate_release = {0};

static int blocking_holder(void *ignored) {
    uint32_t seen = 0;
    (void)ignored;
    if (platform_mutex_lock(lock)) return -1;
    if (atomic32_store(&gate_ready, 1u, SALTS_MEMORY_RELEASE)) return -2;
    for (;;) {
        if (atomic32_load(&gate_release, SALTS_MEMORY_ACQUIRE, &seen)) return -3;
        if (seen) break;
        platform_thread_yield();
    }
    return platform_mutex_unlock(lock) ? -4 : 23;
}
static int future_unlock(void *ignored) {
    (void)ignored;
    if (platform_thread_sleep_ns(5000000ull)) return -1;
    if (atomic32_store(&gate_release, 1u, SALTS_MEMORY_RELEASE)) return -2;
    return 24;
}
static int contending_worker(void *ignored) {
    int i;
    uint32_t prev;
    (void)ignored;
    if (atomic32_fetch_add(&admissions, 1u, SALTS_MEMORY_ACQ_REL, &prev))
        return -1;
    for (i = 0; i < LOOPS; ++i) {
        if (platform_mutex_timedlock_ns(lock, 2000000000ull))
            return -2;
        if (atomic32_fetch_add(&shared, 1u, SALTS_MEMORY_RELAXED, &prev))
            return -3;
        if (platform_mutex_unlock(lock)) return -4;
    }
    return 45;
}
static int saltscore_legacy_entry_test_portable_timed_mutex_c(void) {
    platform_thread *holder = NULL, *unlocker = NULL, *threads[N] = {0};
    int value, i, rc;
    uint32_t seen = 0, total = 0;
    if (platform_mutex_timedlock_ns(NULL, 0) != SALTS_EINVAL) return 1;
    if (platform_mutex_create(&lock) || !lock) return 2;
    if (platform_mutex_timedlock_ns(lock, UINT64_MAX) != SALTS_EINVAL)
        return 3;
    if (platform_mutex_timedlock_ns(lock, 0) ||
        platform_mutex_unlock(lock)) return 4;
    if (platform_thread_create(&holder, blocking_holder, NULL)) return 5;
    for (i = 0; i < 500000; ++i) {
        if (atomic32_load(&gate_ready, SALTS_MEMORY_ACQUIRE, &seen))
            return 6;
        if (seen) break;
        platform_thread_yield();
    }
    if (!seen) return 7;
    if (platform_mutex_timedlock_ns(lock, 0) != SALTS_ETIMEDOUT) return 8;
    if (platform_mutex_timedlock_ns(lock, 2000000ull) != SALTS_ETIMEDOUT)
        return 9;
    if (platform_mutex_destroy(&lock) != SALTS_EBUSY || !lock) return 10;
    if (platform_thread_create(&unlocker, future_unlock, NULL)) return 11;
    if (platform_mutex_timedlock_ns(lock, 2000000000ull)) return 12;
    if (platform_mutex_unlock(lock)) return 13;
    if (platform_thread_join(&unlocker, &value) || value != 24 ||
        platform_thread_join(&holder, &value) || value != 23)
        return 14;

    if (platform_mutex_lock(lock)) return 15;
    for (i = 0; i < N; ++i)
        if (platform_thread_create(&threads[i], contending_worker, NULL))
            return 16;
    for (i = 0; i < 500000; ++i) {
        if (atomic32_load(&admissions, SALTS_MEMORY_ACQUIRE, &seen))
            return 17;
        if (seen == N) break;
        platform_thread_yield();
    }
    if (seen != N || platform_mutex_unlock(lock)) return 18;
    for (i = 0; i < N; ++i) {
        rc = platform_thread_join(&threads[i], &value);
        if (rc || value != 45) {
            fprintf(stderr, "timed_mutex worker %d failed rc=%d value=%d\n",
                    i, rc, value);
            return 19;
        }
    }
    if (atomic32_load(&shared, SALTS_MEMORY_ACQUIRE, &total) ||
        total != N * LOOPS) return 20;
    if (platform_mutex_destroy(&lock) || lock) return 21;
    puts("portable monotonic timed mutex: held/zero/overflow/release/contention PASS");
    return 0;
}

/* Keep the native contract's original exit codes and cleanup paths intact;
 * surface its exact nonzero result as one registered TinyTest failure. */
spec("Portable timed mutex contention") {
  it("preserves the original native exit and lifetime contract") {
    const int result = saltscore_legacy_entry_test_portable_timed_mutex_c();
    check(result == 0, "original native contract returned %d", result);
  }
}
