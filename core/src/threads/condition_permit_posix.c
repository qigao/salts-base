#if defined(__APPLE__) || defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "condition_permit_posix.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>

/* The monotonic API accepts at most SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS. */
static struct timespec remaining_time(const struct timespec *due) {
    struct timespec now, left = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &now)) abort();
    if (due->tv_sec > now.tv_sec ||
        (due->tv_sec == now.tv_sec && due->tv_nsec > now.tv_nsec)) {
        left.tv_sec = due->tv_sec - now.tv_sec;
        left.tv_nsec = due->tv_nsec - now.tv_nsec;
        if (left.tv_nsec < 0) { --left.tv_sec; left.tv_nsec += 1000000000; }
    }
    return left;
}

#ifdef __APPLE__
static int lock_walltime(dispatch_semaphore_t permit, const struct timespec *due) {
    /* libdispatch encodes epoch nanoseconds 0 and 1 as FOREVER.
     * Preserve the single immediate acquisition attempt for expired dates. */
    if (due->tv_sec == 0 && due->tv_nsec <= 1)
        return dispatch_semaphore_wait(permit, DISPATCH_TIME_NOW) ? SALTS_ETIMEDOUT : SALTS_OK;
    /* libdispatch reserves the top two bits for clock selection. */
    const int64_t max_wall_ns = (INT64_C(1) << 62) - 1;
    if (due->tv_sec <= (max_wall_ns - due->tv_nsec) / 1000000000) {
        return dispatch_semaphore_wait(permit, dispatch_walltime(due, 0))
            ? SALTS_ETIMEDOUT : SALTS_OK;
    }
    /* Dates beyond the native nanosecond range still admit an unlock.
     * Recheck only on expiration of a finite native wait. */
    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now)) return SALTS_EIO;
        if (now.tv_sec > due->tv_sec ||
            (now.tv_sec == due->tv_sec && now.tv_nsec >= due->tv_nsec))
            return SALTS_ETIMEDOUT;
        dispatch_time_t interval = dispatch_time(DISPATCH_TIME_NOW, 86400LL * 1000000000);
        if (!dispatch_semaphore_wait(permit, interval)) return SALTS_OK;
    }
}

int permit_init(native_permit *p, unsigned value) {
    *p = dispatch_semaphore_create(value);
    return *p ? SALTS_OK : SALTS_ENOMEM;
}
void permit_destroy(native_permit *p) { dispatch_release(*p); }
void permit_post(native_permit *p, unsigned count) {
    while (count--) dispatch_semaphore_signal(*p);
}
int permit_wait(native_permit *p, const struct timespec *due, clockid_t clock) {
    if (due && clock == CLOCK_REALTIME) return lock_walltime(*p, due);
    dispatch_time_t limit = DISPATCH_TIME_FOREVER;
    if (due) {
        struct timespec left = remaining_time(due);
        int64_t ns = (int64_t)left.tv_sec * 1000000000 + left.tv_nsec;
        limit = ns ? dispatch_time(DISPATCH_TIME_NOW, ns) : DISPATCH_TIME_NOW;
    }
    return dispatch_semaphore_wait(*p, limit) ? SALTS_ETIMEDOUT : SALTS_OK;
}
#else
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

int permit_init(native_permit *p, unsigned value) {
    *p = eventfd(value, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    return *p < 0 ? SALTS_EIO : SALTS_OK;
}
void permit_destroy(native_permit *p) { if (close(*p)) abort(); }
void permit_post(native_permit *p, unsigned count) {
    uint64_t value = count;
    ssize_t n;
    do { n = write(*p, &value, sizeof(value)); } while (n < 0 && errno == EINTR);
    if (n != sizeof(value)) abort();
}
int permit_wait(native_permit *p, const struct timespec *due, clockid_t clock) {
    if (due && clock != CLOCK_MONOTONIC) return SALTS_EINVAL;
    struct pollfd fd = {*p, POLLIN, 0};
    for (;;) {
        uint64_t token;
        ssize_t n = read(*p, &token, sizeof(token));
        if (n == sizeof(token)) return SALTS_OK;
        if (n >= 0 || (errno != EAGAIN && errno != EINTR)) return SALTS_EIO;
        struct timespec left;
        if (due) {
            left = remaining_time(due);
            if (!left.tv_sec && !left.tv_nsec) return SALTS_ETIMEDOUT;
        }
        int rc = ppoll(&fd, 1, due ? &left : NULL, NULL);
        if (!rc) return SALTS_ETIMEDOUT;
        if (rc < 0 && errno != EINTR) return SALTS_EIO;
        if (rc > 0 && !(fd.revents & POLLIN)) return SALTS_EIO;
    }
}
#endif

/* The open group accepts registrations until its first notification.
 * Sealing it prevents later arrivals from stealing posted permits. At most
 * two groups are attached to a condition. Each parked caller retains its
 * own group until it has received a grant or withdrawn from eligibility. */
struct wait_group {
    native_permit permits;
    atomic_uint refs, eligible;
};

static void release_group(struct wait_group *g) {
    if (atomic_fetch_sub(&g->refs, 1) == 1) {
        permit_destroy(&g->permits);
        free(g);
    }
}

static int enroll(struct wait_group **slot) {
    struct wait_group *g = *slot;
    if (!g) {
        g = malloc(sizeof(*g));
        if (!g) return SALTS_ENOMEM;
        int rc = permit_init(&g->permits, 0);
        if (rc) { free(g); return rc; }
        atomic_init(&g->refs, 1);
        atomic_init(&g->eligible, 0);
        *slot = g;
    }
    /* Enrollment is the only increment and is serialized by the guard. */
    if (atomic_load(&g->refs) == UINT_MAX) return SALTS_EBUSY;
    atomic_fetch_add(&g->refs, 1);
    atomic_fetch_add(&g->eligible, 1);
    return SALTS_OK;
}

/* Timeout and signal compete for the same remaining eligibility count.
 * Once every caller has a grant, a timed caller may return success even
 * if the native post is still in flight. The sealed group owns that post. */
static unsigned claim(struct wait_group *g, int all) {
    if (all) return atomic_exchange(&g->eligible, 0);
    unsigned count = atomic_load(&g->eligible);
    while (count && !atomic_compare_exchange_weak(&g->eligible, &count, count - 1)) {}
    return count != 0;
}

int permit_condition_wait(struct permit_condition *c, platform_mutex *m,
                          const struct timespec *due, clockid_t clock) {
    struct wait_group *g = NULL;
    int rc = permit_wait(&c->guard, due, clock);
    if (!rc) {
        rc = enroll(&c->open);
        if (!rc) g = c->open;
        permit_post(&c->guard, 1);
    }
    if (platform_mutex_unlock(m)) abort();
    if (g) {
        rc = permit_wait(&g->permits, due, clock);
        if (rc && !claim(g, 0) && rc == SALTS_ETIMEDOUT) rc = SALTS_OK;
        release_group(g);
    }
    if (platform_mutex_lock(m)) abort();
    return rc;
}

int permit_condition_notify(struct permit_condition *c, int all) {
    if (permit_wait(&c->guard, NULL, CLOCK_MONOTONIC)) abort();
    int delivered = 0;
    for (;;) {
        if (!c->sealed) {
            c->sealed = c->open;
            c->open = NULL;
        }
        struct wait_group *g = c->sealed;
        if (!g) break;
        unsigned count = claim(g, all);
        if (count) {
            permit_post(&g->permits, count);
            delivered = 1;
        }
        if (!atomic_load(&g->eligible)) {
            c->sealed = NULL;
            release_group(g);
        }
        if (count && !all) break;
    }
    permit_post(&c->guard, 1);
    return delivered;
}

void permit_condition_destroy(struct permit_condition *c) {
    if (c->open) release_group(c->open);
    if (c->sealed) release_group(c->sealed);
    permit_destroy(&c->guard);
}
#endif
