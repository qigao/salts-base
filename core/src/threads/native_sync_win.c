#include "nt_wait_win.h"
#include <limits.h>
#include <stdlib.h>

/* Bit zero denotes ownership. Each parked thread owns one registration,
 * removed exactly once after its kernel wait. Timeout never needs to wait
 * for a concurrent unlocker to retire a claimed release. */
enum { MUTEX_HELD = 1, MUTEX_WAITER = 2, MUTEX_SPINS = 32 };

static LONG mutex_state(platform_mutex *m) {
    return InterlockedCompareExchange(&m->state, 0, 0);
}
/* An available mutex can still have registered sleepers. */
static BOOL try_acquire(platform_mutex *m, LONG *observed) {
    LONG initial = InterlockedCompareExchange(&m->state, MUTEX_HELD, 0);
    *observed = initial;
    if (!initial) return TRUE;
    if (initial & MUTEX_HELD) return FALSE;
    *observed = InterlockedCompareExchange(&m->state, initial | MUTEX_HELD, initial);
    return *observed == initial;
}
int win_mutex_acquire(platform_mutex *m, const win_deadline *limit) {
    LONG state;
    if (try_acquire(m, &state)) return SALTS_OK;
    unsigned spins = 0;
    for (;;) {
        if (win_expired(limit)) return SALTS_ETIMEDOUT;
        if (!(state & MUTEX_HELD)) {
            LONG previous = InterlockedCompareExchange(&m->state, state | MUTEX_HELD, state);
            if (previous == state) return SALTS_OK;
            state = previous;
        } else if (state == MUTEX_HELD && spins < MUTEX_SPINS) {
            ++spins;
            YieldProcessor();
            state = mutex_state(m);
        } else {
            LONG previous;
            if (state > LONG_MAX - MUTEX_WAITER) return SALTS_EBUSY;
            previous = InterlockedCompareExchange(&m->state, state + MUTEX_WAITER, state);
            if (previous == state) {
                int rc = win_wait_key(m, limit);
                /* A paired release has already committed this wake. Claim a
                 * free mutex before retiring the registration. Checking the
                 * deadline first could strand the other sleepers. If another
                 * owner won, its unlock is responsible for the next wake. */
                BOOL acquired = rc == SALTS_OK &&
                    !(InterlockedOr(&m->state, MUTEX_HELD) & MUTEX_HELD);
                InterlockedExchangeAdd(&m->state, -MUTEX_WAITER);
                if (acquired) return SALTS_OK;
                if (rc != SALTS_OK) return rc;
                state = mutex_state(m);
                spins = 0;
            } else {
                state = previous;
            }
        }
    }
}

int platform_mutex_create(platform_mutex **out) {
    int rc;
    if (!out || *out) return SALTS_EINVAL;
    rc = win_wait_runtime_init();
    if (rc != SALTS_OK) return rc;
    *out = calloc(1, sizeof(**out));
    return *out ? SALTS_OK : SALTS_ENOMEM;
}
int platform_mutex_lock(platform_mutex *m) {
    return m ? win_mutex_acquire(m, NULL) : SALTS_EINVAL;
}
int platform_mutex_timedlock_ns(platform_mutex *m, uint64_t ns) {
    win_deadline limit;
    LONG state;
    if (!m || ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return SALTS_EINVAL;
    if (!ns) return try_acquire(m, &state) ? SALTS_OK : SALTS_ETIMEDOUT;
    limit = win_timeout_ns(ns);
    return win_mutex_acquire(m, &limit);
}
int platform_mutex_unlock(platform_mutex *m) {
    if (!m) return SALTS_EINVAL;
    LONG state = InterlockedAnd(&m->state, ~MUTEX_HELD);
    BOOL allow_wait = FALSE;
    if (!(state & MUTEX_HELD)) abort();
    while (state >= MUTEX_WAITER) {
        if (win_offer_mutex_wake(m, allow_wait) == SALTS_OK) break;
        /* A release may precede kernel registration. Repeat only while the
         * mutex is available and a registered sleeper still needs a wake.
         * If ownership changed, the next unlock covers remaining sleepers. */
        if (!allow_wait) SwitchToThread();
        allow_wait = TRUE;
        state = mutex_state(m);
        if (state & MUTEX_HELD) break;
    }
    return SALTS_OK;
}
int platform_mutex_destroy(platform_mutex **out) {
    if (!out || !*out) return SALTS_EINVAL;
    if (InterlockedCompareExchange(&(*out)->state, 0, 0)) return SALTS_EBUSY;
    free(*out);
    *out = NULL;
    return SALTS_OK;
}

struct win_waiter {
    struct win_waiter *next, *prev;
    BOOL selected;
    volatile LONG park_state;
};
enum { WAITER_REGISTERED, WAITER_PARKING, WAITER_NOTIFIED };
enum { CONDITION_SPINS = 128 };
static BOOL waiter_notified(const struct win_waiter *w) {
#ifdef _MSC_VER
    return ReadAcquire(&w->park_state) == WAITER_NOTIFIED;
#else
    return __atomic_load_n(&w->park_state, __ATOMIC_ACQUIRE) == WAITER_NOTIFIED;
#endif
}
/* A kernel wait crossing a clock tick suggests that this thread's next
 * untimed wait should park directly. A short handoff clears the hint. */
static SALTS_THREAD_LOCAL BOOL last_condition_wait_slow;

static int wait_for_notification(struct win_waiter *w, const win_deadline *limit) {
    if (!limit && !last_condition_wait_slow) {
        if (waiter_notified(w)) return SALTS_OK;
        /* Let a runnable peer on this processor signal before we park.
         * A peer on another processor can publish during the bounded poll. */
        SwitchToThread();
        for (unsigned i = 0; i < CONDITION_SPINS; ++i) {
            if (waiter_notified(w)) return SALTS_OK;
            YieldProcessor();
        }
    }
    /* Signal can still win between the last load and the parking decision. */
    if (InterlockedCompareExchange(&w->park_state, WAITER_PARKING,
                                   WAITER_REGISTERED) != WAITER_REGISTERED) {
        if (!limit) last_condition_wait_slow = FALSE;
        return SALTS_OK;
    }
    if (limit) return win_wait_key(w, limit);
    DWORD before = GetTickCount();
    int rc = win_wait_key(w, NULL);
    last_condition_wait_slow = GetTickCount() != before;
    return rc;
}
static void notify_waiter(struct win_waiter *w) {
    /* Publishing NOTIFIED completes an early signal entirely in userspace.
     * After publication, only a PARKING waiter still owes a rendezvous. */
    if (InterlockedExchange(&w->park_state, WAITER_NOTIFIED) == WAITER_PARKING)
        win_release_key(w);
}
static void unlink_waiter(platform_condition *c, struct win_waiter *w) {
    if (w->prev) w->prev->next = w->next;
    else c->first = w->next;
    if (w->next) w->next->prev = w->prev;
    else c->last = w->prev;
}
int platform_condition_create(platform_condition **out) {
    int rc;
    if (!out || *out) return SALTS_EINVAL;
    rc = win_wait_runtime_init();
    if (rc != SALTS_OK) return rc;
    *out = calloc(1, sizeof(**out));
    if (!*out) return SALTS_ENOMEM;
    return SALTS_OK;
}
int win_condition_wait(platform_condition *c, platform_mutex *m,
                       const win_deadline *limit) {
    struct win_waiter waiter = {0};
    int rc;
    rc = win_mutex_acquire(&c->guard, limit);
    if (rc != SALTS_OK) {
        /* Even an admission timeout releases and reacquires the caller mutex. */
        if (platform_mutex_unlock(m) || platform_mutex_lock(m)) abort();
        return rc;
    }
    waiter.prev = c->last;
    if (c->last) c->last->next = &waiter;
    else c->first = &waiter;
    c->last = &waiter;
    if (platform_mutex_unlock(&c->guard) != SALTS_OK) abort();
    platform_mutex_unlock(m);
    rc = wait_for_notification(&waiter, limit);
    if (rc == SALTS_ETIMEDOUT) {
        BOOL selected;
        if (platform_mutex_lock(&c->guard) != SALTS_OK) abort();
        selected = waiter.selected;
        if (!selected) unlink_waiter(c, &waiter);
        if (platform_mutex_unlock(&c->guard) != SALTS_OK) abort();
        /* The signaler owns exactly one release for this stack address.
         * Complete the handshake before the address can be reused. */
        if (selected) rc = win_wait_key(&waiter, NULL);
    }
    if (platform_mutex_lock(m) != SALTS_OK) abort();
    return rc;
}
int platform_condition_wait(platform_condition *c, platform_mutex *m) {
    return c && m ? win_condition_wait(c, m, NULL) : SALTS_EINVAL;
}
int platform_condition_timedwait_ns(platform_condition *c, platform_mutex *m, uint64_t ns) {
    win_deadline limit;
    if (!c || !m || ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return SALTS_EINVAL;
    limit = win_timeout_ns(ns);
    return win_condition_wait(c, m, &limit);
}
int platform_condition_signal(platform_condition *c) {
    struct win_waiter *w;
    if (!c) return SALTS_EINVAL;
    if (platform_mutex_lock(&c->guard) != SALTS_OK) abort();
    w = c->first;
    if (w) {
        unlink_waiter(c, w);
        w->selected = TRUE;
    }
    if (platform_mutex_unlock(&c->guard) != SALTS_OK) abort();
    if (w) notify_waiter(w);
    return SALTS_OK;
}
int platform_condition_broadcast(platform_condition *c) {
    struct win_waiter *w, *first;
    if (!c) return SALTS_EINVAL;
    if (platform_mutex_lock(&c->guard) != SALTS_OK) abort();
    first = c->first;
    c->first = c->last = NULL;
    for (w = first; w; w = w->next) w->selected = TRUE;
    if (platform_mutex_unlock(&c->guard) != SALTS_OK) abort();
    while (first) {
        w = first;
        first = first->next;
        notify_waiter(w);
    }
    return SALTS_OK;
}
int platform_condition_destroy(platform_condition **out) {
    if (!out || !*out) return SALTS_EINVAL;
    if ((*out)->first) return SALTS_EBUSY;
    free(*out);
    *out = NULL;
    return SALTS_OK;
}
