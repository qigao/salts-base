#ifndef _WIN32
#include <threads/threads.h>
#include "thread_backend.h"
#include <limits.h>
#include <stdlib.h>

/* The guard owns the logical mutex state. No native recursive-lock or CRT
 * layout crosses the DLL boundary. Condition wait releases logical ownership
 * under the same guard used by lock acquisition, so no wakeup is lost.
 */
struct mtx_state {
    platform_mutex *guard;
    platform_condition *available;
    uint64_t holder;
    unsigned depth;
};

enum { C11_UTC_RECHECK_NS = 100000000 };

/* C11 deadlines use TIME_UTC, including clock corrections during a wait.
 * Bound individual monotonic waits, then recompute from the original UTC
 * deadline. Never reject a distant valid deadline due to a DWORD duration.
 */
static int deadline_slice(int64_t seconds, uint32_t nanos, uint64_t *wait_ns) {
    struct timespec now;
    int64_t now_seconds;
    uint64_t delta, fraction;
    if (nanos >= 1000000000u) return thrd_error;
#if defined(__ANDROID__) && __ANDROID_API__ < 29
    /* Bionic exposes timespec_get only from API 29. CLOCK_REALTIME has the
     * same UTC timebase and preserves the original deadline/recheck rules. */
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return thrd_error;
#else
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return thrd_error;
#endif
    now_seconds = (int64_t)now.tv_sec;
    if (seconds < now_seconds ||
        (seconds == now_seconds && nanos <= (uint32_t)now.tv_nsec)) {
        *wait_ns = 0;
        return thrd_success;
    }
    delta = (uint64_t)seconds - (uint64_t)now_seconds;
    if (delta > 1u) {
        *wait_ns = C11_UTC_RECHECK_NS;
        return thrd_success;
    }
    fraction = delta * 1000000000u + nanos - (uint32_t)now.tv_nsec;
    *wait_ns = fraction > C11_UTC_RECHECK_NS ? C11_UTC_RECHECK_NS : fraction;
    return thrd_success;
}

int mtx_init(mtx_t *m, int type) {
    struct mtx_state *owner;
    int rc;
    if (!m || (type & ~(mtx_recursive | mtx_timed)) != 0) return thrd_error;
    m->owner = NULL;
    m->type = type;
    owner = (struct mtx_state *)calloc(1u, sizeof(*owner));
    if (!owner) return thrd_error;
    rc = platform_mutex_create(&owner->guard);
    if (rc == SALTS_OK)
        rc = platform_condition_create(&owner->available);
    if (rc != SALTS_OK) {
        if (owner->guard) (void)platform_mutex_destroy(&owner->guard);
        free(owner);
        return thrd_error;
    }
    m->owner = owner;
    return thrd_success;
}

/* guard held on entry and return; the logical lock is acquired only on success. */
static int mutex_acquire(mtx_t *m, int try_only, int timed,
                         int64_t seconds, uint32_t nanos) {
    struct mtx_state *owner = m->owner;
    const uint64_t self = platform_thread_current_id();
    int rc;
    while (owner->depth != 0u &&
           (owner->holder != self || (m->type & mtx_recursive) == 0)) {
        uint64_t wait_ns;
        if (try_only) return thrd_busy;
        if (timed) {
            if (deadline_slice(seconds, nanos, &wait_ns) != thrd_success)
                return thrd_error;
            if (wait_ns == 0u) return thrd_timedout;
            rc = platform_condition_timedwait_ns(owner->available,
                                                       owner->guard, wait_ns);
            if (rc == SALTS_ETIMEDOUT) continue;
        } else {
            rc = platform_condition_wait(owner->available, owner->guard);
        }
        if (rc != SALTS_OK) return thrd_error;
    }
    if (owner->depth == UINT_MAX) return thrd_error;
    owner->holder = self;
    ++owner->depth;
    return thrd_success;
}

static int mtx_lock_impl(mtx_t *m, int try_only, int timed,
                       int64_t seconds, uint32_t nanos) {
    int result, rc;
    if (!m || !m->owner) return thrd_error;
    rc = try_only ? platform_mutex_timedlock_ns(m->owner->guard, 0)
                  : platform_mutex_lock(m->owner->guard);
    if (rc != SALTS_OK)
        return try_only && rc == SALTS_ETIMEDOUT ? thrd_busy : thrd_error;
    result = mutex_acquire(m, try_only, timed, seconds, nanos);
    if (platform_mutex_unlock(m->owner->guard) != SALTS_OK)
        return thrd_error;
    return result;
}
int mtx_lock(mtx_t *m) {
    return mtx_lock_impl(m, 0, 0, 0, 0);
}
int mtx_trylock(mtx_t *m) {
    return mtx_lock_impl(m, 1, 0, 0, 0);
}
int mtx_timedlock_abi(
    mtx_t *m, int64_t seconds, uint32_t nanos) {
    if (!m || (m->type & mtx_timed) == 0 || nanos >= 1000000000u)
        return thrd_error;
    return mtx_lock_impl(m, 0, 1, seconds, nanos);
}
int mtx_unlock(mtx_t *m) {
    struct mtx_state *owner;
    int result = thrd_success;
    if (!m || !(owner = m->owner) ||
        platform_mutex_lock(owner->guard) != SALTS_OK) return thrd_error;
    if (owner->depth == 0u || owner->holder != platform_thread_current_id()) {
        result = thrd_error;
    } else if (--owner->depth == 0u) {
        owner->holder = 0;
        if (platform_condition_broadcast(owner->available) != SALTS_OK)
            result = thrd_error;
    }
    if (platform_mutex_unlock(owner->guard) != SALTS_OK) return thrd_error;
    return result;
}
void mtx_destroy(mtx_t *m) {
    if (!m || !m->owner) return;
    /* As required by C11, no thread may be waiting on a destroyed mutex. */
    if (platform_condition_destroy(&m->owner->available) != SALTS_OK ||
        platform_mutex_destroy(&m->owner->guard) != SALTS_OK) abort();
    free(m->owner);
    m->owner = NULL;
}
static int condition_wait(cnd_t *c, mtx_t *m, int timed,
                            int64_t seconds, uint32_t nanos) {
    struct mtx_state *owner;
    unsigned depth;
    int result = thrd_success, rc;
    uint64_t wait_ns;
    if (!c || !c->owner || !m || !(owner = m->owner) ||
        (timed && nanos >= 1000000000u) ||
        platform_mutex_lock(owner->guard) != SALTS_OK) return thrd_error;
    if (owner->depth == 0u || owner->holder != platform_thread_current_id()) {
        (void)platform_mutex_unlock(owner->guard);
        return thrd_error;
    }
    depth = owner->depth;
    owner->depth = 0;
    owner->holder = 0;
    rc = platform_condition_broadcast(owner->available);
    while (rc == SALTS_OK) {
        if (timed) {
            result = deadline_slice(seconds, nanos, &wait_ns);
            if (result != thrd_success) break;
            if (wait_ns == 0u) { result = thrd_timedout; break; }
            rc = platform_condition_timedwait_ns(c->owner, owner->guard, wait_ns);
            if (rc == SALTS_ETIMEDOUT) { rc = SALTS_OK; continue; }
        } else {
            rc = platform_condition_wait(c->owner, owner->guard);
        }
        break;
    }
    if (rc != SALTS_OK) result = thrd_error;
    /* Timeout does not waive the obligation to reacquire the supplied mutex. */
    if (mutex_acquire(m, 0, 0, 0, 0) != thrd_success) abort();
    owner->depth = depth;
    if (platform_mutex_unlock(owner->guard) != SALTS_OK) return thrd_error;
    return result;
}
int cnd_wait(cnd_t *c, mtx_t *m) {
    return condition_wait(c, m, 0, 0, 0);
}
int cnd_timedwait_abi(
    cnd_t *c, mtx_t *m, int64_t seconds, uint32_t nanos) {
    return condition_wait(c, m, 1, seconds, nanos);
}
#endif
