#include "thread_backend.h"
#ifndef _WIN32
#include <pthread.h>
#include <stdlib.h>

#if defined(__linux__)
/* Linux and Bionic use the same zero-initialized int as the published
 * four-byte once flag. Darwin's pthread_once_t has a different ABI. */
_Static_assert(sizeof(pthread_once_t) == sizeof(platform_once), "pthread_once ABI");
_Static_assert(_Alignof(pthread_once_t) == _Alignof(platform_once), "pthread_once alignment");
int platform_call_once(platform_once *flag, platform_once_fn callback) {
    if (!flag || !callback) return SALTS_EINVAL;
    return pthread_once((pthread_once_t *)(void *)&flag->state.value, callback)
        ? SALTS_EINVAL : SALTS_OK;
}
#else
static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
enum { UNSTARTED, RUNNING, COMPLETE };

static void unlock_guard(void *unused) {
    (void)unused;
    if (pthread_mutex_unlock(&guard)) abort();
}
static void publish(platform_once *flag, unsigned state) {
    if (pthread_mutex_lock(&guard)) abort();
    flag->state.value = state;
    if (pthread_cond_broadcast(&changed)) abort();
    unlock_guard(NULL);
}
static void abandoned(void *opaque) { publish(opaque, UNSTARTED); }

int platform_call_once(platform_once *flag, platform_once_fn callback) {
    if (!flag || !callback) return SALTS_EINVAL;
    if (pthread_mutex_lock(&guard)) return SALTS_EINVAL;
    int run;
    pthread_cleanup_push(unlock_guard, NULL);
    while (flag->state.value == RUNNING) {
        if (pthread_cond_wait(&changed, &guard)) abort();
    }
    run = flag->state.value == UNSTARTED;
    if (run) flag->state.value = RUNNING;
    pthread_cleanup_pop(1);
    if (run) {
        pthread_cleanup_push(abandoned, flag);
        callback();
        pthread_cleanup_pop(0);
        publish(flag, COMPLETE);
    }
    return SALTS_OK;
}
#endif
#endif
