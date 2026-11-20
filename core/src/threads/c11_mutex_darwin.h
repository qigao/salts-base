#ifndef SALTS_C11_MUTEX_DARWIN_H
#define SALTS_C11_MUTEX_DARWIN_H

#include <core/error_codes.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <stdatomic.h>
#include <time.h>

typedef struct {
    atomic_uint state;
    semaphore_t wake;
} darwin_mutex;

enum { DARWIN_MUTEX_HELD = 1 };

static inline int darwin_mutex_trylock(darwin_mutex *m) {
    return atomic_fetch_or_explicit(&m->state, DARWIN_MUTEX_HELD, memory_order_acquire)
        & DARWIN_MUTEX_HELD ? SALTS_ETIMEDOUT : SALTS_OK;
}

__attribute__((visibility("hidden"))) int darwin_mutex_init(darwin_mutex *m);
__attribute__((visibility("hidden"))) void darwin_mutex_destroy(darwin_mutex *m);
__attribute__((visibility("hidden"))) int darwin_mutex_wait(
    darwin_mutex *m, const struct timespec *due, clockid_t clock);
__attribute__((visibility("hidden"))) void darwin_mutex_unlock_slow(darwin_mutex *m);

static inline int darwin_mutex_lock(darwin_mutex *m, const struct timespec *due, clockid_t clock) {
    int rc = darwin_mutex_trylock(m);
    return rc == SALTS_OK ? rc : darwin_mutex_wait(m, due, clock);
}

static inline void darwin_mutex_unlock(darwin_mutex *m) {
    unsigned held = DARWIN_MUTEX_HELD;
    if (!atomic_compare_exchange_strong_explicit(&m->state, &held, 0,
            memory_order_release, memory_order_relaxed))
        darwin_mutex_unlock_slow(m);
}

#ifdef SALTS_TEST_DARWIN_MUTEX
enum darwin_mutex_test_phase { DARWIN_MUTEX_ENROLLED, DARWIN_MUTEX_BEFORE_WAKE };
void darwin_mutex_test_hook(darwin_mutex *m, enum darwin_mutex_test_phase phase);
#endif
#endif
