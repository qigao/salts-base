#include <threads/threads.h>
#include "thread_backend.h"
#include <tinytest.h>
#include <stdint.h>
#include <stdio.h>

static platform_mutex *gate;
static platform_condition *cv;
static int state;
static int worker(void *unused) {
    (void)unused;
    if (platform_mutex_lock(gate)) return -1;
    while (!state) {
        int rc = platform_condition_timedwait_ns(cv, gate, 10000000000ull);
        if (rc != SALTS_OK) return -2;
    }
    if (platform_mutex_unlock(gate)) return -3;
    return 77;
}
static int saltscore_legacy_entry_test_portable_timing_c(void) {
    platform_thread *thread = NULL;
    int rc, out = 0;
    if (platform_mutex_create(&gate) || platform_condition_create(&cv)) return 1;
    if (platform_mutex_lock(gate)) return 2;
    rc = platform_condition_timedwait_ns(cv, gate, 0);
    if (rc != SALTS_ETIMEDOUT) { fprintf(stderr,"timeout=%d\n",rc); return 3; }
    rc = platform_condition_timedwait_ns(cv, gate, 2000000ull);
    if (rc != SALTS_ETIMEDOUT) return 4;
    if (platform_condition_timedwait_ns(cv, gate, UINT64_MAX) != SALTS_EINVAL) return 5;
    if (platform_mutex_unlock(gate)) return 6;
    if (platform_thread_sleep_ns(2000000ull)) return 7;
    if (platform_thread_sleep_ns(UINT64_MAX) != SALTS_EINVAL) return 8;
    if (platform_thread_create(&thread,worker,NULL)) return 9;
    if (platform_mutex_lock(gate)) return 10;
    state=1;
    if (platform_condition_signal(cv) || platform_mutex_unlock(gate)) return 11;
    if (platform_thread_join(&thread,&out) || out!=77) return 12;
    if (platform_condition_destroy(&cv) || platform_mutex_destroy(&gate)) return 13;
    puts("portable relative timedwait/sleep PASS");
    return 0;
}

/* Keep the native contract's original exit codes and cleanup paths intact;
 * surface its exact nonzero result as one registered TinyTest failure. */
spec("Portable timed wait and sleep") {
  it("preserves the original native exit and lifetime contract") {
    const int result = saltscore_legacy_entry_test_portable_timing_c();
    check(result == 0, "original native contract returned %d", result);
  }
}
