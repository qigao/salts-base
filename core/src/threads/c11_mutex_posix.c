#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <threads/threads.h>
#include "thread_backend.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#elif defined(__ANDROID__)
#if __ANDROID_API__ < 28
#define NEED_MONOTONIC_CONDITION 1
#endif
#elif defined(__GLIBC__)
#if !__GLIBC_PREREQ(2, 30)
#define NEED_MONOTONIC_CONDITION 1
#endif
#else
#define NEED_MONOTONIC_CONDITION 1
#endif

struct platform_mutex {
#ifdef __APPLE__
    dispatch_semaphore_t permit;
#else
    pthread_mutex_t native;
#ifdef NEED_MONOTONIC_CONDITION
    pthread_cond_t available;
    pthread_mutex_t admission;
#endif
#endif
};

struct condition_state {
    pthread_cond_t native;
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    pthread_mutex_t registration;
#endif
};

struct mtx_state {
    platform_mutex *lock;
    atomic64 holder;
    unsigned depth;
};

static int mutex_result(int rc) {
    if (!rc) return SALTS_OK;
    if (rc == ETIMEDOUT || rc == EBUSY) return SALTS_ETIMEDOUT;
    return SALTS_EINVAL;
}

#ifndef __APPLE__
static int relative_deadline(uint64_t ns, struct timespec *out) {
    if (clock_gettime(CLOCK_MONOTONIC, out) || out->tv_sec < 0) return SALTS_EIO;
    uint64_t seconds = (uint64_t)out->tv_sec + ns / 1000000000;
    long fraction = out->tv_nsec + (long)(ns % 1000000000);
    seconds += fraction / 1000000000;
    out->tv_sec = (time_t)seconds;
    out->tv_nsec = fraction % 1000000000;
    return out->tv_sec >= 0 && (uint64_t)out->tv_sec == seconds ? SALTS_OK : SALTS_EINVAL;
}
#endif

static int init_condition(pthread_cond_t *cond, clockid_t clock) {
    if (clock == CLOCK_REALTIME) return pthread_cond_init(cond, NULL);
#ifdef __APPLE__
    /* Relative waits select their clock in pthread_cond_timedwait_relative_np. */
    return pthread_cond_init(cond, NULL);
#else
    pthread_condattr_t attr;
    int rc = pthread_condattr_init(&attr);
    if (rc) return rc;
    rc = pthread_condattr_setclock(&attr, clock);
    if (!rc) rc = pthread_cond_init(cond, &attr);
    if (pthread_condattr_destroy(&attr)) abort();
    return rc;
#endif
}

int platform_mutex_create(platform_mutex **out) {
    if (!out || *out) return SALTS_EINVAL;
    platform_mutex *m = calloc(1, sizeof(*m));
    if (!m) return SALTS_ENOMEM;
#ifdef __APPLE__
    m->permit = dispatch_semaphore_create(1);
    if (!m->permit) { free(m); return SALTS_ENOMEM; }
#else
    int rc = pthread_mutex_init(&m->native, NULL);
    if (rc) { free(m); return SALTS_EBUSY; }
#ifdef NEED_MONOTONIC_CONDITION
    rc = pthread_mutex_init(&m->admission, NULL);
    if (!rc) {
        rc = init_condition(&m->available, CLOCK_MONOTONIC);
        if (rc) pthread_mutex_destroy(&m->admission);
    }
    if (rc) { pthread_mutex_destroy(&m->native); free(m); return SALTS_EBUSY; }
#endif
#endif
    *out = m;
    return SALTS_OK;
}

int platform_mutex_lock(platform_mutex *m) {
    if (!m) return SALTS_EINVAL;
#ifdef __APPLE__
    return dispatch_semaphore_wait(m->permit, DISPATCH_TIME_FOREVER) ? SALTS_EIO : SALTS_OK;
#else
    return mutex_result(pthread_mutex_lock(&m->native));
#endif
}

int platform_mutex_timedlock_ns(platform_mutex *m, uint64_t ns) {
    if (!m || ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return SALTS_EINVAL;
#ifdef __APPLE__
    dispatch_time_t due = ns ? dispatch_time(DISPATCH_TIME_NOW, (int64_t)ns) : DISPATCH_TIME_NOW;
    return dispatch_semaphore_wait(m->permit, due) ? SALTS_ETIMEDOUT : SALTS_OK;
#else
    if (!ns) return mutex_result(pthread_mutex_trylock(&m->native));
    struct timespec due;
    int rc = relative_deadline(ns, &due);
    if (rc != SALTS_OK) return rc;
#ifdef NEED_MONOTONIC_CONDITION
    /* Bionic before API 28 and libcs without mutex clock selection.
     * admission serializes registration with unlock, never user code. */
    rc = pthread_mutex_trylock(&m->native);
    if (rc != EBUSY) return mutex_result(rc);
    if (pthread_mutex_lock(&m->admission)) return SALTS_EINVAL;
    while ((rc = pthread_mutex_trylock(&m->native)) == EBUSY) {
        rc = pthread_cond_timedwait(&m->available, &m->admission, &due);
        if (rc) break;
    }
    if (pthread_mutex_unlock(&m->admission)) abort();
#elif defined(__ANDROID__) && __ANDROID_API__ < 30
    rc = pthread_mutex_timedlock_monotonic_np(&m->native, &due);
#else
    rc = pthread_mutex_clocklock(&m->native, CLOCK_MONOTONIC, &due);
#endif
    return mutex_result(rc);
#endif
}

int platform_mutex_unlock(platform_mutex *m) {
    if (!m) return SALTS_EINVAL;
#ifdef __APPLE__
    dispatch_semaphore_signal(m->permit);
#else
#ifdef NEED_MONOTONIC_CONDITION
    if (pthread_mutex_lock(&m->admission)) return SALTS_EINVAL;
#endif
    int rc = pthread_mutex_unlock(&m->native);
#ifdef NEED_MONOTONIC_CONDITION
    if (!rc && pthread_cond_signal(&m->available)) abort();
    if (pthread_mutex_unlock(&m->admission)) abort();
#endif
    if (rc) return SALTS_EINVAL;
#endif
    return SALTS_OK;
}

int platform_mutex_destroy(platform_mutex **out) {
    if (!out || !*out) return SALTS_EINVAL;
    platform_mutex *m = *out;
    int rc = platform_mutex_timedlock_ns(m, 0);
    if (rc != SALTS_OK) return rc == SALTS_ETIMEDOUT ? SALTS_EBUSY : rc;
    if (platform_mutex_unlock(m) != SALTS_OK) abort();
#ifdef __APPLE__
    dispatch_release(m->permit);
#else
    if (pthread_mutex_destroy(&m->native)) return SALTS_EBUSY;
#ifdef NEED_MONOTONIC_CONDITION
    if (pthread_cond_destroy(&m->available) || pthread_mutex_destroy(&m->admission)) abort();
#endif
#endif
    free(m);
    *out = NULL;
    return SALTS_OK;
}

static int create_condition(platform_condition **out, clockid_t clock) {
    if (!out || *out) return SALTS_EINVAL;
    platform_condition *c = malloc(sizeof(*c));
    if (!c) return SALTS_ENOMEM;
    if (init_condition(&c->native, clock)) { free(c); return SALTS_EBUSY; }
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    if (pthread_mutex_init(&c->registration, NULL)) {
        pthread_cond_destroy(&c->native);
        free(c);
        return SALTS_EBUSY;
    }
#endif
    *out = c;
    return SALTS_OK;
}

int platform_condition_create(platform_condition **out) {
    return create_condition(out, CLOCK_MONOTONIC);
}

int cnd_init(cnd_t *c) {
    if (!c) return thrd_error;
    c->owner = NULL;
    int rc = create_condition(&c->owner, CLOCK_REALTIME);
    return rc == SALTS_OK ? thrd_success : rc == SALTS_ENOMEM ? thrd_nomem : thrd_error;
}

static int wait_condition(platform_condition *c, platform_mutex *m,
                          const struct timespec *due, int relative) {
    if (!c || !m) return SALTS_EINVAL;
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    /* These mutexes need an explicit unlock to publish availability. Take
     * registration first so a notifier cannot pass the pending pthread wait. */
    pthread_mutex_t *lock = &c->registration;
    if (pthread_mutex_lock(lock)) return SALTS_EINVAL;
    if (platform_mutex_unlock(m) != SALTS_OK) abort();
#else
    pthread_mutex_t *lock = &m->native;
#endif
    int rc;
    (void)relative;
    if (!due) rc = pthread_cond_wait(&c->native, lock);
#ifdef __APPLE__
    else if (relative) rc = pthread_cond_timedwait_relative_np(&c->native, lock, due);
#endif
    else rc = pthread_cond_timedwait(&c->native, lock, due);
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    if (pthread_mutex_unlock(lock) || platform_mutex_lock(m) != SALTS_OK) abort();
#endif
    return mutex_result(rc);
}

int platform_condition_wait(platform_condition *c, platform_mutex *m) {
    return wait_condition(c, m, NULL, 0);
}
int platform_condition_timedwait_ns(platform_condition *c, platform_mutex *m, uint64_t ns) {
    if (ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return SALTS_EINVAL;
#ifdef __APPLE__
    struct timespec due = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
#else
    struct timespec due;
    int rc = relative_deadline(ns, &due);
    if (rc != SALTS_OK) return rc;
#endif
    return wait_condition(c, m, &due, 1);
}

static int notify(platform_condition *c, int all) {
    if (!c) return SALTS_EINVAL;
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    if (pthread_mutex_lock(&c->registration)) return SALTS_EINVAL;
#endif
    int rc = all ? pthread_cond_broadcast(&c->native) : pthread_cond_signal(&c->native);
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    if (pthread_mutex_unlock(&c->registration)) abort();
#endif
    return rc ? SALTS_EINVAL : SALTS_OK;
}
int platform_condition_signal(platform_condition *c) { return notify(c, 0); }
int platform_condition_broadcast(platform_condition *c) { return notify(c, 1); }
int platform_condition_destroy(platform_condition **out) {
    if (!out || !*out) return SALTS_EINVAL;
    if (pthread_cond_destroy(&(*out)->native)) return SALTS_EBUSY;
#if defined(__APPLE__) || defined(NEED_MONOTONIC_CONDITION)
    if (pthread_mutex_destroy(&(*out)->registration)) abort();
#endif
    free(*out);
    *out = NULL;
    return SALTS_OK;
}

static uint64_t holder(struct mtx_state *m) {
    uint64_t id;
    if (atomic64_load(&m->holder, SALTS_MEMORY_ACQUIRE, &id)) abort();
    return id;
}
static void set_holder(struct mtx_state *m, uint64_t id) {
    if (atomic64_store(&m->holder, id, SALTS_MEMORY_RELEASE)) abort();
}
static int c11_result(int rc) {
    if (rc == SALTS_OK) return thrd_success;
    return rc == SALTS_ETIMEDOUT ? thrd_timedout : thrd_error;
}
int mtx_init(mtx_t *m, int type) {
    if (!m || (type & ~(mtx_recursive | mtx_timed))) return thrd_error;
    m->type = type;
    m->owner = calloc(1, sizeof(*m->owner));
    if (!m->owner) return thrd_nomem;
    int rc = platform_mutex_create(&m->owner->lock);
    if (rc != SALTS_OK) {
        free(m->owner);
        m->owner = NULL;
    }
    return c11_result(rc);
}
static int acquire(mtx_t *m, int try_only, const struct timespec *due) {
    if (!m || !m->owner) return thrd_error;
    struct mtx_state *s = m->owner;
    uint64_t self = platform_thread_current_id();
    if ((m->type & mtx_recursive) && holder(s) == self) {
        if (s->depth == UINT_MAX) return thrd_error;
        ++s->depth;
        return thrd_success;
    }
    int rc;
    if (try_only) {
        rc = platform_mutex_timedlock_ns(s->lock, 0);
        if (rc == SALTS_ETIMEDOUT) return thrd_busy;
    } else if (!due) rc = platform_mutex_lock(s->lock);
#ifdef __APPLE__
    else rc = dispatch_semaphore_wait(s->lock->permit, dispatch_walltime(due, 0))
                  ? SALTS_ETIMEDOUT : SALTS_OK;
#else
    else rc = mutex_result(pthread_mutex_timedlock(&s->lock->native, due));
#endif
    if (rc != SALTS_OK) return c11_result(rc);
    s->depth = 1;
    set_holder(s, self);
    return thrd_success;
}
int mtx_lock(mtx_t *m) { return acquire(m, 0, NULL); }
int mtx_trylock(mtx_t *m) { return acquire(m, 1, NULL); }
static int utc_deadline(int64_t sec, uint32_t ns, struct timespec *out) {
    if (ns >= 1000000000u) return thrd_error;
    out->tv_sec = sec < 0 ? 0 : (time_t)sec;
    out->tv_nsec = sec < 0 ? 0 : (long)ns;
    return sec >= 0 && (int64_t)out->tv_sec != sec ? thrd_error : thrd_success;
}
int mtx_timedlock_abi(mtx_t *m, int64_t sec, uint32_t ns) {
    struct timespec due;
    if (!m || !(m->type & mtx_timed) || utc_deadline(sec, ns, &due)) return thrd_error;
    return acquire(m, 0, &due);
}
int mtx_unlock(mtx_t *m) {
    if (!m || !m->owner || holder(m->owner) != platform_thread_current_id()) return thrd_error;
    if (--m->owner->depth) return thrd_success;
    set_holder(m->owner, 0);
    return c11_result(platform_mutex_unlock(m->owner->lock));
}
void mtx_destroy(mtx_t *m) {
    if (!m || !m->owner) return;
    if (platform_mutex_destroy(&m->owner->lock) != SALTS_OK) abort();
    free(m->owner);
    m->owner = NULL;
}
static int c11_wait(cnd_t *c, mtx_t *m, const struct timespec *due) {
    if (!c || !c->owner || !m || !m->owner) return thrd_error;
    struct mtx_state *s = m->owner;
    uint64_t self = platform_thread_current_id();
    if (holder(s) != self) return thrd_error;
    unsigned depth = s->depth;
    s->depth = 0;
    set_holder(s, 0);
    int rc = wait_condition(c->owner, s->lock, due, 0);
    s->depth = depth;
    set_holder(s, self);
    return c11_result(rc);
}
int cnd_wait(cnd_t *c, mtx_t *m) { return c11_wait(c, m, NULL); }
int cnd_timedwait_abi(cnd_t *c, mtx_t *m, int64_t sec, uint32_t ns) {
    struct timespec due;
    if (utc_deadline(sec, ns, &due)) return thrd_error;
    return c11_wait(c, m, &due);
}
#endif
