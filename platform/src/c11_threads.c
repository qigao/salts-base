#define SALTS_C11_THREADS_EXPERIMENTAL 1
#include <c11/threads.h>
#include <clock.h>
#include <stdint.h>
#include <stdlib.h>

/* Everything exported here is source-compatible C11 but prefixed at link.
 * thrd_t is a scalar opaque provider-owned pointer, never a native
 * pthread_t/HANDLE or a struct returned across different compiler ABIs.
 */
struct salts_c11_thread {
    salts_atomic64 id;
    salts_atomic32 refs; /* creator + worker, even across detach/exit */
    salts_platform_thread *owner;
    thrd_start_t callback;
    void *argument;
};
#if defined(_WIN32) && defined(_MSC_VER)
static __declspec(thread) struct salts_c11_thread *current_thread;
static __declspec(thread) struct salts_c11_thread foreign_thread;
#elif defined(__clang__) || defined(__GNUC__)
static _Thread_local struct salts_c11_thread *current_thread;
static _Thread_local struct salts_c11_thread foreign_thread;
#else
#error "C11 thread facade requires real compiler TLS; no shared global fallback"
#endif

static int status_to_c11(int rc) {
    if (rc == SALTS_OK) return thrd_success;
    if (rc == SALTS_ENOMEM) return thrd_nomem;
    if (rc == SALTS_ETIMEDOUT) return thrd_timedout;
    if (rc == SALTS_EBUSY) return thrd_busy;
    return thrd_error;
}
static uint64_t token_id(const struct salts_c11_thread *thread) {
    uint64_t id = 0;
    if (!thread) return 0;
    if (salts_atomic64_load(&thread->id, SALTS_MEMORY_ACQUIRE, &id) != SALTS_OK)
        return 0;
    return id;
}
static void set_thread_id(struct salts_c11_thread *thread, uint64_t id) {
    uint64_t expected = 0;
    int changed = 0;
    if (id) (void)salts_atomic64_cas(&thread->id, &expected, id,
                SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &changed);
}
static void release_c11_thread(struct salts_c11_thread *t) {
    uint32_t previous = 0;
    (void)salts_atomic32_fetch_add(&t->refs, UINT32_MAX,
                                   SALTS_MEMORY_ACQ_REL, &previous);
    if (previous == 1u) free(t);
}
static int SALTS_CORE_PLATFORM_CALL bridge_thread(void *ctx) {
    struct salts_c11_thread *thread = (struct salts_c11_thread *)ctx;
    int ret;
    current_thread = thread;
    set_thread_id(thread, salts_platform_thread_current_id());
    ret = thread->callback(thread->argument);
    current_thread = NULL;
    release_c11_thread(thread);
    return ret;
}
int SALTS_CORE_PLATFORM_CALL thrd_create(thrd_t *out, thrd_start_t fn, void *argument) {
    struct salts_c11_thread *thread;
    int rc;
    if (!out || !fn) return thrd_error;
    *out = NULL;
    thread = (struct salts_c11_thread *)calloc(1u, sizeof(*thread));
    if (!thread) return thrd_nomem;
    thread->callback = fn;
    thread->argument = argument;
    if (salts_atomic32_init(&thread->refs, 2u)) { free(thread); return thrd_error; }
    if (salts_atomic64_init(&thread->id, 0)) {
        free(thread);
        return thrd_error;
    }
    rc = salts_platform_thread_create(&thread->owner, bridge_thread, thread);
    if (rc != SALTS_OK) { free(thread); return status_to_c11(rc); }
    set_thread_id(thread, salts_platform_thread_id(thread->owner));
    *out = thread;
    return thrd_success;
}
int SALTS_CORE_PLATFORM_CALL thrd_join(thrd_t thread, int *result) {
    int rc;
    if (!thread || !thread->owner) return thrd_error;
    rc = salts_platform_thread_join(&thread->owner, result);
    if (rc != SALTS_OK) return status_to_c11(rc);
    release_c11_thread(thread);
    return thrd_success;
}
int SALTS_CORE_PLATFORM_CALL thrd_detach(thrd_t thread) {
    int rc;
    if (!thread || !thread->owner) return thrd_error;
    rc = salts_platform_thread_detach(&thread->owner);
    if (rc != SALTS_OK) return status_to_c11(rc);
    /* Worker keeps its reference until normal return or thrd_exit. */
    release_c11_thread(thread);
    return thrd_success;
}
thrd_t SALTS_CORE_PLATFORM_CALL thrd_current(void) {
    uint64_t id;
    if (current_thread) return current_thread;
    id = salts_platform_thread_current_id();
    set_thread_id(&foreign_thread, id);
    return &foreign_thread;
}
int SALTS_CORE_PLATFORM_CALL thrd_equal(thrd_t a, thrd_t b) {
    uint64_t left, right;
    if (!a || !b) return 0;
    if (a == b) return 1;
    left = token_id(a);
    right = token_id(b);
    return left && left == right;
}
void SALTS_CORE_PLATFORM_CALL thrd_yield(void) {
    salts_platform_thread_yield();
}
/* Stable cross-compiler transport: duration is NOT a CRT timespec pointer. */
int SALTS_CORE_PLATFORM_CALL salts_c11_thrd_sleep_abi(
    uint64_t seconds, uint32_t nanos) {
    uint64_t ns;
    if (nanos >= 1000000000u ||
        seconds > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS / 1000000000ull)
        return -1;
    ns = seconds * 1000000000ull + (uint64_t)nanos;
    if (ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return -1;
    return salts_platform_thread_sleep_ns(ns) == SALTS_OK ? 0 : -1;
}
void SALTS_CORE_PLATFORM_CALL thrd_exit(int result) {
    struct salts_c11_thread *thread = current_thread;
    if (thread) {
        current_thread = NULL;
        release_c11_thread(thread);
    }
    /* Platform early-exit retires its own worker reference before OS exit. */
    salts_platform_thread_exit(result);
    abort();
}

/* Calendar TIME_UTC conversion happens after the client has marshalled its
 * own struct timespec layout into fixed-width sec/nsec. No native time_t
 * or structure padding crosses a TinyCC/Clang/MSVC DLL boundary.
 */
static int deadline_to_relative_ns(int64_t utc_seconds, uint32_t nanos,
                                    uint64_t *out) {
    uint64_t now_ms, now_secs, now_ns, sec, delta, extra;
    if (!out || nanos >= 1000000000u) return thrd_error;
    if (utc_seconds < 0) { *out = 0; return thrd_success; }
    sec = (uint64_t)utc_seconds;
    now_ms = cmeta_realtime_ms();
    now_secs = now_ms / 1000ull;
    now_ns = (now_ms % 1000ull) * 1000000ull;
    if (sec < now_secs || (sec == now_secs && (uint64_t)nanos <= now_ns)) {
        *out = 0;
        return thrd_success;
    }
    if (sec == now_secs) {
        *out = (uint64_t)nanos - now_ns;
        return thrd_success;
    }
    delta = sec - now_secs - 1ull;
    extra = 1000000000ull - now_ns + (uint64_t)nanos;
    if (extra > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS ||
        delta > (SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS - extra) / 1000000000ull)
        return thrd_error;
    *out = delta * 1000000000ull + extra;
    return thrd_success;
}
int SALTS_CORE_PLATFORM_CALL mtx_init(mtx_t *m, int type) {
    int rc;
    if (!m || (type != mtx_plain && type != mtx_timed)) return thrd_error;
    m->owner = NULL;
    m->type = type;
    rc = salts_platform_mutex_create(&m->owner);
    return status_to_c11(rc);
}
int SALTS_CORE_PLATFORM_CALL mtx_lock(mtx_t *m) {
    return status_to_c11(m && m->owner ?
        salts_platform_mutex_lock(m->owner) : SALTS_EINVAL);
}
int SALTS_CORE_PLATFORM_CALL mtx_trylock(mtx_t *m) {
    int rc = m && m->owner ? salts_platform_mutex_timedlock_ns(m->owner, 0) : SALTS_EINVAL;
    if (rc == SALTS_ETIMEDOUT) return thrd_busy;
    return status_to_c11(rc);
}
int SALTS_CORE_PLATFORM_CALL salts_c11_mtx_timedlock_abi(
    mtx_t *m, int64_t utc_seconds, uint32_t nanos) {
    uint64_t ns;
    if (!m || !m->owner || m->type != mtx_timed ||
        deadline_to_relative_ns(utc_seconds, nanos, &ns) != thrd_success)
        return thrd_error;
    return status_to_c11(salts_platform_mutex_timedlock_ns(m->owner, ns));
}
int SALTS_CORE_PLATFORM_CALL mtx_unlock(mtx_t *m) {
    return status_to_c11(m && m->owner ?
        salts_platform_mutex_unlock(m->owner) : SALTS_EINVAL);
}
void SALTS_CORE_PLATFORM_CALL mtx_destroy(mtx_t *m) {
    if (m && m->owner) (void)salts_platform_mutex_destroy(&m->owner);
}

int SALTS_CORE_PLATFORM_CALL cnd_init(cnd_t *c) {
    if (!c) return thrd_error;
    c->owner = NULL;
    return status_to_c11(salts_platform_condition_create(&c->owner));
}
int SALTS_CORE_PLATFORM_CALL cnd_wait(cnd_t *c, mtx_t *m) {
    return status_to_c11(c && m && c->owner && m->owner ?
        salts_platform_condition_wait(c->owner,m->owner) : SALTS_EINVAL);
}
int SALTS_CORE_PLATFORM_CALL salts_c11_cnd_timedwait_abi(
    cnd_t *c, mtx_t *m, int64_t utc_seconds, uint32_t nanos) {
    uint64_t ns;
    if (!c || !m || !c->owner || !m->owner ||
        deadline_to_relative_ns(utc_seconds, nanos, &ns) != thrd_success)
        return thrd_error;
    return status_to_c11(salts_platform_condition_timedwait_ns(
                             c->owner, m->owner, ns));
}
int SALTS_CORE_PLATFORM_CALL cnd_signal(cnd_t *c) {
    return status_to_c11(c && c->owner ? salts_platform_condition_signal(c->owner) : SALTS_EINVAL);
}
int SALTS_CORE_PLATFORM_CALL cnd_broadcast(cnd_t *c) {
    return status_to_c11(c && c->owner ? salts_platform_condition_broadcast(c->owner) : SALTS_EINVAL);
}
void SALTS_CORE_PLATFORM_CALL cnd_destroy(cnd_t *c) {
    if (c && c->owner) (void)salts_platform_condition_destroy(&c->owner);
}
void SALTS_CORE_PLATFORM_CALL call_once(once_flag *f,
    void (SALTS_CORE_PLATFORM_CALL *callback)(void)) {
    if (salts_platform_call_once(f, callback) != SALTS_OK) abort();
}
