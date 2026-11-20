#ifdef _WIN32
#include <threads/threads.h>
#include "nt_wait_win.h"
#include <limits.h>
#include <stdlib.h>

struct mtx_state {
    platform_mutex lock;
    volatile LONG holder;
    unsigned depth;
};

static int mutex_result(int rc) {
    if (rc == SALTS_OK) return thrd_success;
    return rc == SALTS_ETIMEDOUT ? thrd_timedout : thrd_error;
}
int mtx_init(mtx_t *m, int type) {
    if (!m || (type & ~(mtx_recursive | mtx_timed))) return thrd_error;
    m->owner = NULL;
    m->type = type;
    if (win_wait_runtime_init() != SALTS_OK) return thrd_error;
    m->owner = calloc(1, sizeof(*m->owner));
    return m->owner ? thrd_success : thrd_nomem;
}
static int acquire(mtx_t *m, BOOL try_only, const win_deadline *limit) {
    struct mtx_state *owner;
    LONG self = (LONG)GetCurrentThreadId();
    int rc;
    if (!m || !(owner = m->owner)) return thrd_error;
    if ((m->type & mtx_recursive) &&
        InterlockedCompareExchange(&owner->holder, 0, 0) == self) {
        if (owner->depth == UINT_MAX) return thrd_error;
        ++owner->depth;
        return thrd_success;
    }
    if (try_only) {
        rc = platform_mutex_timedlock_ns(&owner->lock, 0);
        if (rc == SALTS_ETIMEDOUT) return thrd_busy;
    } else {
        rc = win_mutex_acquire(&owner->lock, limit);
    }
    if (rc != SALTS_OK) return mutex_result(rc);
    owner->depth = 1;
    InterlockedExchange(&owner->holder, self);
    return thrd_success;
}
int mtx_lock(mtx_t *m) { return acquire(m, FALSE, NULL); }
int mtx_trylock(mtx_t *m) { return acquire(m, TRUE, NULL); }
int mtx_timedlock_abi(mtx_t *m, int64_t seconds, uint32_t nanos) {
    win_deadline limit;
    if (!m || !(m->type & mtx_timed) || nanos >= 1000000000u) return thrd_error;
    limit = win_timeout_utc(seconds, nanos);
    return acquire(m, FALSE, &limit);
}
int mtx_unlock(mtx_t *m) {
    struct mtx_state *owner;
    if (!m || !(owner = m->owner) ||
        InterlockedCompareExchange(&owner->holder, 0, 0) != (LONG)GetCurrentThreadId())
        return thrd_error;
    if (--owner->depth) return thrd_success;
    InterlockedExchange(&owner->holder, 0);
    return mutex_result(platform_mutex_unlock(&owner->lock));
}
void mtx_destroy(mtx_t *m) {
    if (!m || !m->owner) return;
    if (m->owner->lock.state) abort();
    free(m->owner);
    m->owner = NULL;
}
static int condition_wait(cnd_t *c, mtx_t *m, const win_deadline *limit) {
    struct mtx_state *owner;
    LONG self = (LONG)GetCurrentThreadId();
    unsigned depth;
    int rc;
    if (!c || !c->owner || !m || !(owner = m->owner) ||
        InterlockedCompareExchange(&owner->holder, 0, 0) != self) return thrd_error;
    depth = owner->depth;
    owner->depth = 0;
    InterlockedExchange(&owner->holder, 0);
    rc = win_condition_wait(c->owner, &owner->lock, limit);
    owner->depth = depth;
    InterlockedExchange(&owner->holder, self);
    return mutex_result(rc);
}
int cnd_wait(cnd_t *c, mtx_t *m) { return condition_wait(c, m, NULL); }
int cnd_timedwait_abi(cnd_t *c, mtx_t *m, int64_t seconds, uint32_t nanos) {
    win_deadline limit;
    if (nanos >= 1000000000u) return thrd_error;
    limit = win_timeout_utc(seconds, nanos);
    return condition_wait(c, m, &limit);
}
#endif
