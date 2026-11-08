#include <platform_thread.h>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

/* The worker and caller each hold one reference. Detach drops only the
 * caller's reference; the live worker must never access freed storage.
 */
struct salts_platform_thread {
    pthread_t native;
    salts_platform_thread_fn callback;
    void *argument;
    int result;
    salts_atomic32 refs;
};
struct salts_platform_mutex { pthread_mutex_t native; };
struct salts_platform_condition { pthread_cond_t native; };
static __thread salts_platform_thread *salts_platform_current;

static uint64_t os_thread_id(pthread_t value) {
    uint64_t id = 0;
    typedef char pthread_id_fits[(sizeof(pthread_t) <= sizeof(uint64_t)) ? 1 : -1];
    (void)sizeof(pthread_id_fits);
    memcpy(&id, &value, sizeof(value));
    return id;
}
uint64_t SALTS_CORE_PLATFORM_CALL salts_platform_thread_current_id(void) {
    return os_thread_id(pthread_self());
}
uint64_t SALTS_CORE_PLATFORM_CALL salts_platform_thread_id(const salts_platform_thread *thread) {
    return thread ? os_thread_id(thread->native) : 0u;
}

static void release_thread(salts_platform_thread *thread) {
    uint32_t before = 0;
    (void)salts_atomic32_fetch_add(&thread->refs, UINT32_MAX,
                                    SALTS_MEMORY_SEQ_CST, &before);
    if (before == 1u) free(thread);
}
static void *thread_entry(void *context) {
    salts_platform_thread *thread = (salts_platform_thread *)context;
    salts_platform_current = thread;
    thread->result = thread->callback(thread->argument);
    salts_platform_current = NULL;
    release_thread(thread);
    return NULL;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_thread_create(salts_platform_thread **out,
                              salts_platform_thread_fn fn, void *arg) {
    salts_platform_thread *thread;
    if (!out || !fn || *out) return SALTS_EINVAL;
    thread = (salts_platform_thread *)calloc(1u, sizeof(*thread));
    if (!thread) return SALTS_ENOMEM;
    thread->callback = fn;
    thread->argument = arg;
    (void)salts_atomic32_init(&thread->refs, 2u);
    if (pthread_create(&thread->native, NULL, thread_entry, thread) != 0) {
        free(thread);
        return SALTS_EBUSY;
    }
    *out = thread;
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_thread_join(salts_platform_thread **owner, int *result) {
    salts_platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (pthread_equal(pthread_self(), thread->native)) return SALTS_EINVAL;
    if (pthread_join(thread->native, NULL) != 0) return SALTS_EBUSY;
    if (result) *result = thread->result;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_thread_detach(salts_platform_thread **owner) {
    salts_platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (pthread_detach(thread->native) != 0) return SALTS_EBUSY;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
void SALTS_CORE_PLATFORM_CALL salts_platform_thread_exit(int result) {
    salts_platform_thread *thread = salts_platform_current;
    if (thread) {
        thread->result = result;
        salts_platform_current = NULL;
        release_thread(thread);
    }
    pthread_exit(NULL);
}
void SALTS_CORE_PLATFORM_CALL salts_platform_thread_yield(void) { (void)sched_yield(); }
int SALTS_CORE_PLATFORM_CALL salts_platform_thread_sleep_ns(uint64_t duration_ns) {
    const uint64_t seconds = duration_ns / 1000000000ull;
    struct timespec req, remaining;
    if (duration_ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS ||
        seconds > (uint64_t)INT64_MAX) return SALTS_EINVAL;
    req.tv_sec = (time_t)seconds;
    if (req.tv_sec < 0 || (uint64_t)req.tv_sec != seconds) return SALTS_EINVAL;
    req.tv_nsec = (long)(duration_ns % 1000000000ull);
    while (nanosleep(&req, &remaining) != 0) {
        if (errno != EINTR) return SALTS_EINVAL;
        req = remaining;
    }
    return SALTS_OK;
}

int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_create(salts_platform_mutex **out) {
    salts_platform_mutex *m;
    if (!out || *out) return SALTS_EINVAL;
    m = (salts_platform_mutex *)malloc(sizeof(*m));
    if (!m) return SALTS_ENOMEM;
    if (pthread_mutex_init(&m->native, NULL) != 0) { free(m); return SALTS_EBUSY; }
    *out = m; return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_lock(salts_platform_mutex *m) {
    return m && pthread_mutex_lock(&m->native) == 0 ? SALTS_OK : SALTS_EINVAL;
}
/* Portable timed mutex: pthread_mutex_timedlock uses CLOCK_REALTIME on many
 * systems; retry pthread_mutex_trylock under CLOCK_MONOTONIC instead.
 * A bounded 1 ms nanosleep avoids a hot spin and a realtime-clock fallback.
 */
static int portable_monotonic_now_ns(uint64_t *now) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (!now || clock_gettime(CLOCK_MONOTONIC, &ts) != 0 ||
        ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
        return SALTS_ENOTSUP;
    if ((uint64_t)ts.tv_sec > (UINT64_MAX - 999999999ull) / 1000000000ull)
        return SALTS_EINVAL;
    *now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return SALTS_OK;
#else
    (void)now;
    return SALTS_ENOTSUP;
#endif
}
int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_timedlock_ns(
    salts_platform_mutex *m, uint64_t duration_ns) {
    uint64_t start = 0, now = 0, elapsed, remaining;
    int rc;
    if (!m || duration_ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS)
        return SALTS_EINVAL;
    rc = pthread_mutex_trylock(&m->native);
    if (rc == 0) return SALTS_OK;
    if (rc != EBUSY) return SALTS_EBUSY;
    if (duration_ns == 0) return SALTS_ETIMEDOUT;
    rc = portable_monotonic_now_ns(&start);
    if (rc != SALTS_OK) return rc;
    for (;;) {
        struct timespec pause;
        rc = portable_monotonic_now_ns(&now);
        if (rc != SALTS_OK) return rc;
        elapsed = now >= start ? now - start : 0;
        if (elapsed >= duration_ns) return SALTS_ETIMEDOUT;
        remaining = duration_ns - elapsed;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)(remaining > 1000000ull ? 1000000ull : remaining);
        (void)nanosleep(&pause, NULL);
        rc = portable_monotonic_now_ns(&now);
        if (rc != SALTS_OK) return rc;
        if (now >= start && now - start >= duration_ns)
            return SALTS_ETIMEDOUT;
        rc = pthread_mutex_trylock(&m->native);
        if (rc == 0) return SALTS_OK;
        if (rc != EBUSY) return SALTS_EBUSY;
    }
}
int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_unlock(salts_platform_mutex *m) {
    return m && pthread_mutex_unlock(&m->native) == 0 ? SALTS_OK : SALTS_EINVAL;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_destroy(salts_platform_mutex **owner) {
    salts_platform_mutex *m;
    int rc;
    if (!owner || !(m = *owner)) return SALTS_EINVAL;
    /* pthread_mutex_destroy on a locked mutex is undefined by POSIX.
     * Reject an already held lock BEFORE attempting destruction. This
     * trylock is not a concurrent-close barrier: the caller still must
     * guarantee that no new entrants or waiters can arrive.
     */
    rc = pthread_mutex_trylock(&m->native);
    if (rc != 0) return rc == EBUSY ? SALTS_EBUSY : SALTS_EINVAL;
    if (pthread_mutex_unlock(&m->native) != 0) return SALTS_EBUSY;
    if (pthread_mutex_destroy(&m->native) != 0) return SALTS_EBUSY;
    *owner = NULL;
    free(m);
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_create(salts_platform_condition **out) {
    salts_platform_condition *c;
    if (!out || *out) return SALTS_EINVAL;
    c = (salts_platform_condition *)malloc(sizeof(*c));
    if (!c) return SALTS_ENOMEM;
#if defined(__APPLE__)
    if (pthread_cond_init(&c->native, NULL) != 0) { free(c); return SALTS_EBUSY; }
#elif defined(CLOCK_MONOTONIC)
    {
        pthread_condattr_t attr;
        int rc = pthread_condattr_init(&attr);
        if (rc != 0) { free(c); return SALTS_EBUSY; }
        rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (rc == 0) rc = pthread_cond_init(&c->native, &attr);
        (void)pthread_condattr_destroy(&attr);
        if (rc != 0) { free(c); return SALTS_ENOTSUP; }
    }
#else
    free(c);
    return SALTS_ENOTSUP;
#endif
    *out = c; return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_wait(salts_platform_condition *c, salts_platform_mutex *m) {
    if (!c || !m) return SALTS_EINVAL;
    return pthread_cond_wait(&c->native, &m->native) == 0 ? SALTS_OK : SALTS_EINVAL;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_timedwait_ns(
    salts_platform_condition *c, salts_platform_mutex *m, uint64_t timeout_ns) {
    const uint64_t seconds = timeout_ns / 1000000000ull;
    struct timespec deadline;
    int rc;
    if (!c || !m || timeout_ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS ||
        seconds > (uint64_t)INT64_MAX) return SALTS_EINVAL;
#if defined(__APPLE__)
    deadline.tv_sec = (time_t)seconds;
    if (deadline.tv_sec < 0 || (uint64_t)deadline.tv_sec != seconds) return SALTS_EINVAL;
    deadline.tv_nsec = (long)(timeout_ns % 1000000000ull);
    rc = pthread_cond_timedwait_relative_np(&c->native, &m->native, &deadline);
#elif defined(CLOCK_MONOTONIC)
    {
        uint64_t absolute_seconds;
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0 || deadline.tv_sec < 0)
            return SALTS_EINVAL;
        absolute_seconds = (uint64_t)deadline.tv_sec + seconds;
        if (absolute_seconds < seconds || absolute_seconds > (uint64_t)INT64_MAX)
            return SALTS_EINVAL;
        if (deadline.tv_nsec + (long)(timeout_ns % 1000000000ull) >= 1000000000l) {
            ++absolute_seconds;
            deadline.tv_nsec -= 1000000000l;
        }
        deadline.tv_nsec += (long)(timeout_ns % 1000000000ull);
        deadline.tv_sec = (time_t)absolute_seconds;
        if (deadline.tv_sec < 0 || (uint64_t)deadline.tv_sec != absolute_seconds)
            return SALTS_EINVAL;
        rc = pthread_cond_timedwait(&c->native, &m->native, &deadline);
    }
#else
    return SALTS_ENOTSUP;
#endif
    if (rc == 0) return SALTS_OK;
    return rc == ETIMEDOUT ? SALTS_ETIMEDOUT : SALTS_EBUSY;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_signal(salts_platform_condition *c) {
    return c && pthread_cond_signal(&c->native) == 0 ? SALTS_OK : SALTS_EINVAL;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_broadcast(salts_platform_condition *c) {
    return c && pthread_cond_broadcast(&c->native) == 0 ? SALTS_OK : SALTS_EINVAL;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_condition_destroy(salts_platform_condition **owner) {
    salts_platform_condition *c;
    if (!owner || !(c = *owner)) return SALTS_EINVAL;
    if (pthread_cond_destroy(&c->native) != 0) return SALTS_EBUSY;
    *owner = NULL;
    free(c); return SALTS_OK;
}
#endif
