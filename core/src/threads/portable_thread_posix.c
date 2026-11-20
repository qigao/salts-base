#include "thread_backend.h"
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
struct platform_thread {
    pthread_t native;
    platform_thread_fn callback;
    void *argument;
    int result;
    atomic32 refs;
};
static __thread platform_thread *platform_current;

static uint64_t os_thread_id(pthread_t value) {
    uint64_t id = 0;
    typedef char pthread_id_fits[(sizeof(pthread_t) <= sizeof(uint64_t)) ? 1 : -1];
    (void)sizeof(pthread_id_fits);
    memcpy(&id, &value, sizeof(value));
    return id;
}
uint64_t platform_thread_current_id(void) {
    return os_thread_id(pthread_self());
}
uint64_t platform_thread_id(const platform_thread *thread) {
    return thread ? os_thread_id(thread->native) : 0u;
}

static void release_thread(platform_thread *thread) {
    uint32_t before = 0;
    (void)atomic32_fetch_add(&thread->refs, UINT32_MAX,
                                    SALTS_MEMORY_SEQ_CST, &before);
    if (before == 1u) free(thread);
}
static void *thread_entry(void *context) {
    platform_thread *thread = (platform_thread *)context;
    platform_current = thread;
    thread->result = thread->callback(thread->argument);
    platform_current = NULL;
    release_thread(thread);
    return NULL;
}
int platform_thread_create(platform_thread **out,
                              platform_thread_fn fn, void *arg) {
    platform_thread *thread;
    if (!out || !fn || *out) return SALTS_EINVAL;
    thread = (platform_thread *)calloc(1u, sizeof(*thread));
    if (!thread) return SALTS_ENOMEM;
    thread->callback = fn;
    thread->argument = arg;
    (void)atomic32_init(&thread->refs, 2u);
    if (pthread_create(&thread->native, NULL, thread_entry, thread) != 0) {
        free(thread);
        return SALTS_EBUSY;
    }
    *out = thread;
    return SALTS_OK;
}
int platform_thread_join(platform_thread **owner, int *result) {
    platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (pthread_equal(pthread_self(), thread->native)) return SALTS_EINVAL;
    if (pthread_join(thread->native, NULL) != 0) return SALTS_EBUSY;
    if (result) *result = thread->result;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
int platform_thread_detach(platform_thread **owner) {
    platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (pthread_detach(thread->native) != 0) return SALTS_EBUSY;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
void platform_thread_exit(int result) {
    platform_thread *thread = platform_current;
    if (thread) {
        thread->result = result;
        platform_current = NULL;
        release_thread(thread);
    }
    pthread_exit(NULL);
}
void platform_thread_yield(void) { (void)sched_yield(); }
int platform_thread_sleep_ns(uint64_t duration_ns) {
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
#endif
