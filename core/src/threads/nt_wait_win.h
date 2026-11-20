#ifndef SALTS_NT_WAIT_WIN_H
#define SALTS_NT_WAIT_WIN_H

#include "thread_backend.h"
#include <windows.h>

/* NULL deadline means indefinite. Relative budgets keep their original
 * monotonic expiration across userspace contention. UTC is NT system time. */
enum win_clock { WIN_UTC, WIN_MONOTONIC, WIN_BEYOND_NT_UTC };
typedef struct win_deadline {
    LONGLONG ticks;
    enum win_clock clock;
    ULONGLONG start;
} win_deadline;

win_deadline win_timeout_ns(uint64_t ns);
win_deadline win_timeout_utc(int64_t seconds, uint32_t nanos);
int win_wait_runtime_init(void);
int win_wait_key(void *key, const win_deadline *deadline);
int win_wait_object(HANDLE object, const win_deadline *deadline);
void win_release_key(void *key);
int win_offer_mutex_wake(void *key, BOOL allow_wait);
BOOL win_expired(const win_deadline *deadline);
int win_call_once(volatile LONG *state, void (*callback)(void));
void win_abandon_once(void);

struct platform_mutex {
    volatile LONG state;
    HANDLE available; /* Condition queue guards retain an auto-reset wake. */
};
struct win_waiter;
struct condition_state {
    platform_mutex guard;
    struct win_waiter *first, *last;
    volatile LONG expired;
};
int win_mutex_acquire(platform_mutex *mutex, const win_deadline *deadline);
int win_condition_wait(platform_condition *condition, platform_mutex *mutex,
                       const win_deadline *deadline);

#endif
