#include <threads/threads.h>
#include "c11_threads_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

/* Export the standard C11 symbols directly.
 * thrd_t is a scalar opaque provider-owned pointer, never a native
 * pthread_t/HANDLE or a struct returned across different compiler ABIs.
 */
struct thrd_state {
    atomic64 id;
    atomic32 refs; /* creator + worker, even across detach/exit */
    platform_thread *owner;
    thrd_start_t callback;
    void *argument;
};
static SALTS_THREAD_LOCAL struct thrd_state *current_thread;
static SALTS_THREAD_LOCAL struct thrd_state foreign_thread;

static uint64_t token_id(const struct thrd_state *thread) {
    uint64_t id = 0;
    if (!thread) return 0;
    if (atomic64_load(&thread->id, SALTS_MEMORY_ACQUIRE, &id) != SALTS_OK)
        return 0;
    return id;
}
static void set_thread_id(struct thrd_state *thread, uint64_t id) {
    uint64_t expected = 0;
    int changed = 0;
    if (id) (void)atomic64_cas(&thread->id, &expected, id,
                SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &changed);
}
static void release_c11_thread(struct thrd_state *t) {
    uint32_t previous = 0;
    (void)atomic32_fetch_add(&t->refs, UINT32_MAX,
                                   SALTS_MEMORY_ACQ_REL, &previous);
    if (previous == 1u) free(t);
}
static int bridge_thread(void *ctx) {
    struct thrd_state *thread = (struct thrd_state *)ctx;
    int ret;
    current_thread = thread;
    set_thread_id(thread, platform_thread_current_id());
    ret = thread->callback(thread->argument);
    tss_cleanup_current();
    current_thread = NULL;
    release_c11_thread(thread);
    return ret;
}
int thrd_create(thrd_t *out, thrd_start_t fn, void *argument) {
    struct thrd_state *thread;
    int rc;
    if (!out || !fn) return thrd_error;
    *out = NULL;
    thread = (struct thrd_state *)calloc(1u, sizeof(*thread));
    if (!thread) return thrd_nomem;
    thread->callback = fn;
    thread->argument = argument;
    if (atomic32_init(&thread->refs, 2u)) { free(thread); return thrd_error; }
    if (atomic64_init(&thread->id, 0)) {
        free(thread);
        return thrd_error;
    }
    rc = platform_thread_create(&thread->owner, bridge_thread, thread);
    if (rc != SALTS_OK) {
        free(thread);
        return rc == SALTS_ENOMEM ? thrd_nomem : thrd_error;
    }
    set_thread_id(thread, platform_thread_id(thread->owner));
    *out = thread;
    return thrd_success;
}
int thrd_join(thrd_t thread, int *result) {
    int rc;
    if (!thread || !thread->owner) return thrd_error;
    rc = platform_thread_join(&thread->owner, result);
    if (rc != SALTS_OK) return thrd_error;
    release_c11_thread(thread);
    return thrd_success;
}
int thrd_detach(thrd_t thread) {
    int rc;
    if (!thread || !thread->owner) return thrd_error;
    rc = platform_thread_detach(&thread->owner);
    if (rc != SALTS_OK) return thrd_error;
    /* Worker keeps its reference until normal return or thrd_exit. */
    release_c11_thread(thread);
    return thrd_success;
}
thrd_t thrd_current(void) {
    uint64_t id;
    if (current_thread) return current_thread;
    id = platform_thread_current_id();
    set_thread_id(&foreign_thread, id);
    return &foreign_thread;
}
int thrd_equal(thrd_t a, thrd_t b) {
    uint64_t left, right;
    if (!a || !b) return 0;
    if (a == b) return 1;
    left = token_id(a);
    right = token_id(b);
    return left && left == right;
}
void thrd_yield(void) {
    platform_thread_yield();
}
/* Stable cross-compiler transport: duration is NOT a CRT timespec pointer. */
int thrd_sleep_abi(
    uint64_t seconds, uint32_t nanos) {
    uint64_t remaining_seconds;
    uint32_t remaining_nanos;
    return thrd_sleep_remaining_abi(seconds, nanos,
        &remaining_seconds, &remaining_nanos);
}
int thrd_sleep_remaining_abi(
    uint64_t seconds, uint32_t nanos, uint64_t *remaining_seconds,
    uint32_t *remaining_nanos) {
    if (nanos >= 1000000000u || !remaining_seconds || !remaining_nanos)
        return -2;
    *remaining_seconds = 0;
    *remaining_nanos = 0;
    return thrd_sleep_native(seconds, nanos, remaining_seconds, remaining_nanos);
}
void thrd_exit(int result) {
    struct thrd_state *thread = current_thread;
    tss_cleanup_current();
    if (thread) {
        current_thread = NULL;
        release_c11_thread(thread);
    }
    /* Platform early-exit retires its own worker reference before OS exit. */
    platform_thread_exit(result);
    abort();
}

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
    if (nanos >= 1000000000u || timespec_get(&now, TIME_UTC) != TIME_UTC)
        return thrd_error;
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
int cnd_init(cnd_t *c) {
    int rc;
    if (!c) return thrd_error;
    c->owner = NULL;
    rc = platform_condition_create(&c->owner);
    return rc == SALTS_OK ? thrd_success : rc == SALTS_ENOMEM ? thrd_nomem : thrd_error;
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
int cnd_signal(cnd_t *c) {
    return c && c->owner && platform_condition_signal(c->owner) == SALTS_OK
        ? thrd_success : thrd_error;
}
int cnd_broadcast(cnd_t *c) {
    return c && c->owner && platform_condition_broadcast(c->owner) == SALTS_OK
        ? thrd_success : thrd_error;
}
void cnd_destroy(cnd_t *c) {
    if (c && c->owner && platform_condition_destroy(&c->owner) != SALTS_OK) abort();
}
_Static_assert(sizeof(once_flag) == sizeof(platform_once),
               "Unsupported libc once_flag size for the Core C11 ABI");
_Static_assert(_Alignof(once_flag) == _Alignof(platform_once),
               "Unsupported libc once_flag alignment for the Core C11 ABI");
void call_once(once_flag *f,
    void (*callback)(void)) {
    if (platform_call_once((platform_once *)(void *)f, callback) != SALTS_OK) abort();
}
