#include "nt_wait_win.h"
#include "nt_keyed_api_win.h"
#include <limits.h>
#include <stdlib.h>

/* Windows 7+ provides a process-default keyed event for a NULL handle.
 * Non-alertable waits have exactly two expected results, success and timeout.
 * A registered rendezvous cannot be abandoned after an unexpected NT error. */
static int checked_wait_result(LONG status) {
    int rc = win_keyed_status(status);
    if (rc == SALTS_EIO) abort();
    return rc;
}
static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static win_keyed_api keyed;
static int init_status;

static BOOL CALLBACK resolve_operations(PINIT_ONCE once, PVOID arg, PVOID *ctx) {
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    (void)once;
    (void)arg;
    (void)ctx;
    init_status = win_bind_keyed_api(&keyed, module, GetProcAddress);
    return TRUE;
}
int win_wait_runtime_init(void) {
    if (!InitOnceExecuteOnce(&initialized, resolve_operations, NULL, NULL))
        return SALTS_EIO;
    return init_status;
}

static ULONGLONG monotonic_ticks(void) {
    ULONGLONG ticks;
    if (!QueryUnbiasedInterruptTime(&ticks)) abort();
    return ticks;
}
win_deadline win_timeout_ns(uint64_t ns) {
    win_deadline limit = {(LONGLONG)((ns + 99) / 100), WIN_MONOTONIC, monotonic_ticks()};
    return limit;
}
win_deadline win_timeout_utc(int64_t seconds, uint32_t nanos) {
    const int64_t epoch = 11644473600LL;
    const int64_t scale = 10000000;
    const int64_t fraction = (nanos + 99u) / 100u;
    win_deadline limit = {0, WIN_UTC, 0};
    if (seconds < -epoch) return limit;
    /* A caller may supply a UTC instant beyond the NT system clock's entire
     * range. Keep such a wait finite and interruptible at the native layer. */
    if (seconds > (INT64_MAX - fraction) / scale - epoch)
        limit.clock = WIN_BEYOND_NT_UTC;
    else
        limit.ticks = (seconds + epoch) * scale + fraction;
    return limit;
}
BOOL win_expired(const win_deadline *limit) {
    FILETIME ft;
    ULARGE_INTEGER now;
    if (!limit) return FALSE;
    if (limit->clock == WIN_MONOTONIC)
        return monotonic_ticks() - limit->start >= (ULONGLONG)limit->ticks;
    if (limit->clock == WIN_BEYOND_NT_UTC) return FALSE;
    GetSystemTimeAsFileTime(&ft);
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    return now.QuadPart >= (ULONGLONG)limit->ticks;
}
int win_wait_key(void *key, const win_deadline *limit) {
    LARGE_INTEGER due;
    LONG status;
    if (limit) {
        due.QuadPart = limit->ticks;
        if (limit->clock == WIN_BEYOND_NT_UTC) {
            const LONGLONG ticks_per_day = 86400LL * 10000000;
            due.QuadPart = -ticks_per_day;
        } else if (limit->clock == WIN_MONOTONIC) {
            ULONGLONG elapsed = monotonic_ticks() - limit->start;
            due.QuadPart = elapsed < (ULONGLONG)limit->ticks ? (LONGLONG)elapsed - limit->ticks : 0;
        }
    }
    do {
        status = keyed.wait(NULL, key, FALSE, limit ? &due : NULL);
    } while (status == NT_WAIT_TIMEOUT && limit && limit->clock == WIN_BEYOND_NT_UTC);
    return checked_wait_result(status);
}
void win_release_key(void *key) {
    if (keyed.release(NULL, key, FALSE, NULL) != 0) abort();
}
int win_offer_mutex_wake(void *key, BOOL allow_wait) {
    /* Mutex waiters own their registration through timeout. A finite offer
     * lets the releaser observe the departure of the last timed waiter. */
    LARGE_INTEGER interval;
    const LONG ticks_per_millisecond = 10000;
    interval.QuadPart = allow_wait ? -ticks_per_millisecond : 0;
    LONG status = keyed.release(NULL, key, FALSE, &interval);
    return checked_wait_result(status);
}

/* Both public once layouts have a single aligned 32-bit state. An entrant
 * either observes DONE or reserves one release before entering the kernel. */
enum { ONCE_RUNNING = 1, ONCE_DONE = 2, ONCE_WAITER = 4, ONCE_SPINS = 16 };
int win_call_once(volatile LONG *state, void (*callback)(void)) {
    LONG observed = InterlockedCompareExchange(state, ONCE_DONE, ONCE_DONE);
    if (observed == ONCE_DONE) return SALTS_OK;
    int rc = win_wait_runtime_init();
    if (rc != SALTS_OK) return rc;
    observed = InterlockedCompareExchange(state, ONCE_RUNNING, 0);
    if (observed == ONCE_DONE) return SALTS_OK;
    if (observed == 0) {
        callback();
        observed = InterlockedExchange(state, ONCE_DONE);
        for (; observed > ONCE_RUNNING; observed -= ONCE_WAITER)
            win_release_key((void *)state);
        return SALTS_OK;
    }
    /* Spin briefly only while no sleeper has registered. */
    for (unsigned i = 0; i < ONCE_SPINS && observed == ONCE_RUNNING; ++i) {
        YieldProcessor();
        observed = InterlockedCompareExchange(state, ONCE_DONE, ONCE_DONE);
    }
    while (observed != ONCE_DONE) {
        LONG previous;
        if ((observed & 3) != ONCE_RUNNING || observed > LONG_MAX - ONCE_WAITER)
            return SALTS_EINVAL;
        previous = InterlockedCompareExchange(state, observed + ONCE_WAITER, observed);
        if (previous == observed) {
            win_wait_key((void *)state, NULL);
            (void)InterlockedCompareExchange(state, ONCE_DONE, ONCE_DONE);
            break;
        }
        observed = previous;
    }
    return SALTS_OK;
}
int platform_call_once(platform_once *flag, platform_once_fn callback) {
    if (!flag || !callback) return SALTS_EINVAL;
    return win_call_once((volatile LONG *)&flag->state.value, callback);
}
