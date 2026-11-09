#ifndef SALTS_CORE_PLATFORM_THREAD_H
#define SALTS_CORE_PLATFORM_THREAD_H

#include <salts/api.h>
#include <salts/platform_atomic.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All native thread/lock/condition objects are opaque and module-owned.
 * No heap buffer or native pthread/HANDLE crosses the C compiler ABI.
 * Never copy owner handles or unload a callback's module while live.
 * Creation requires a NULL *out, and destruction/join/detach consumes it.
 */
typedef struct platform_thread platform_thread;
typedef struct platform_mutex platform_mutex;
typedef struct platform_condition platform_condition;
typedef struct platform_tss platform_tss;
typedef int (*platform_thread_fn)(void *arg);
typedef void (*platform_once_fn)(void);
typedef void (*platform_tss_destructor)(void *value);
typedef struct platform_once { atomic32 state; } platform_once;
#define SALTS_PLATFORM_ONCE_INIT { {0u} }

SALTS_API int platform_thread_create(platform_thread **out, platform_thread_fn fn, void *arg);
SALTS_API int platform_thread_join(platform_thread **thread, int *result);
SALTS_API int platform_thread_detach(platform_thread **thread);
/* Native OS identity never crosses the C ABI as pthread_t/HANDLE storage. */
SALTS_API uint64_t platform_thread_current_id(void);
SALTS_API uint64_t platform_thread_id(const platform_thread *thread);
/* Early exit must retire the Platform worker's reference before OS exit. */
SALTS_API void platform_thread_exit(int result);
SALTS_API void platform_thread_yield(void);
/* Uniform maximum representable relative duration on Windows and POSIX.
 * No truncation to infinite wait or silent clock-source downgrade.
 */
#define SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS 4294967294000000ull
/* Relative nanoseconds. Win32 rounds positive values up to 1 millisecond. */
SALTS_API int platform_thread_sleep_ns(uint64_t duration_ns);
SALTS_API int platform_call_once(platform_once *flag, platform_once_fn callback);
SALTS_API int platform_mutex_create(platform_mutex **out);
SALTS_API int platform_mutex_lock(platform_mutex *mutex);
/* Relative MONOTONIC timeout, not C11's absolute UTC mtx_timedlock.
 * 0 = one nonblocking acquisition attempt; a held lock times out.
 * The existing platform owner must remain alive for every entrant.
 * Recursive acquisition of the same native mutex is unsupported.
 * Valid timeout <= SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS.
 */
SALTS_API int platform_mutex_timedlock_ns(platform_mutex *mutex, uint64_t timeout_ns);
SALTS_API int platform_mutex_unlock(platform_mutex *mutex);
SALTS_API int platform_mutex_destroy(platform_mutex **mutex);
SALTS_API int platform_condition_create(platform_condition **out);
SALTS_API int platform_condition_wait(platform_condition *condition, platform_mutex *mutex);
/* Relative monotonic wait. Caller must own mutex before and after call;
 * always re-check predicate, including on successful/spurious wakeup.
 * SALTS_ETIMEDOUT is distinct from SALTS_EINVAL and SALTS_EBUSY.
 */
SALTS_API int platform_condition_timedwait_ns(platform_condition *condition, platform_mutex *mutex, uint64_t timeout_ns);
SALTS_API int platform_condition_signal(platform_condition *condition);
SALTS_API int platform_condition_broadcast(platform_condition *condition);
SALTS_API int platform_condition_destroy(platform_condition **condition);
/* Opaque provider-owned TSS key. Values are borrowed pointers; set(NULL)
 * clears the current thread's value without invoking its destructor.
 * Thread exit invokes the user callback before releasing its reference.
 * Destroy returns BUSY while non-NULL values/callbacks remain active.
 * Key destroy versus concurrent set/get requires caller-side quiescence.
 * Recursive same-key destructor registration is not yet qualified.
 */
SALTS_API int platform_tss_create(platform_tss **out, platform_tss_destructor destructor);
SALTS_API int platform_tss_set(platform_tss *key, void *value);
SALTS_API int platform_tss_get(platform_tss *key, void **out);
SALTS_API int platform_tss_destroy(platform_tss **key);
#ifdef __cplusplus
}
#endif
#endif
