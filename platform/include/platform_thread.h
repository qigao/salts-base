#ifndef SALTS_CORE_PLATFORM_THREAD_H
#define SALTS_CORE_PLATFORM_THREAD_H
#include <platform_atomic.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All native thread/lock/condition objects are opaque and module-owned.
 * No heap buffer or native pthread/HANDLE crosses the C compiler ABI.
 * Never copy owner handles or unload a callback's module while live.
 * Creation requires a NULL *out, and destruction/join/detach consumes it.
 */
typedef struct salts_platform_thread salts_platform_thread;
typedef struct salts_platform_mutex salts_platform_mutex;
typedef struct salts_platform_condition salts_platform_condition;
typedef struct salts_platform_tss salts_platform_tss;
typedef int (SALTS_CORE_PLATFORM_CALL *salts_platform_thread_fn)(void *arg);
typedef void (SALTS_CORE_PLATFORM_CALL *salts_platform_once_fn)(void);
typedef void (SALTS_CORE_PLATFORM_CALL *salts_platform_tss_destructor)(void *value);
typedef struct salts_platform_once { salts_atomic32 state; } salts_platform_once;
#define SALTS_PLATFORM_ONCE_INIT { {0u} }

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_thread_create(salts_platform_thread **out, salts_platform_thread_fn fn, void *arg);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_thread_join(salts_platform_thread **thread, int *result);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_thread_detach(salts_platform_thread **thread);
/* Native OS identity never crosses the C ABI as pthread_t/HANDLE storage. */
SALTS_CORE_PLATFORM_API uint64_t SALTS_CORE_PLATFORM_CALL salts_platform_thread_current_id(void);
SALTS_CORE_PLATFORM_API uint64_t SALTS_CORE_PLATFORM_CALL salts_platform_thread_id(const salts_platform_thread *thread);
/* Early exit must retire the Platform worker's reference before OS exit. */
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL salts_platform_thread_exit(int result);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL salts_platform_thread_yield(void);
/* Uniform maximum representable relative duration on Windows and POSIX.
 * No truncation to infinite wait or silent clock-source downgrade.
 */
#define SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS 4294967294000000ull
/* Relative nanoseconds. Win32 rounds positive values up to 1 millisecond. */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_thread_sleep_ns(uint64_t duration_ns);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_call_once(salts_platform_once *flag, salts_platform_once_fn callback);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_create(salts_platform_mutex **out);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_lock(salts_platform_mutex *mutex);
/* Relative MONOTONIC timeout, not C11's absolute UTC mtx_timedlock.
 * 0 = one nonblocking acquisition attempt; a held lock times out.
 * The existing platform owner must remain alive for every entrant.
 * Recursive acquisition of the same native mutex is unsupported.
 * Valid timeout <= SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_timedlock_ns(salts_platform_mutex *mutex, uint64_t timeout_ns);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_unlock(salts_platform_mutex *mutex);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_mutex_destroy(salts_platform_mutex **mutex);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_create(salts_platform_condition **out);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_wait(salts_platform_condition *condition, salts_platform_mutex *mutex);
/* Relative monotonic wait. Caller must own mutex before and after call;
 * always re-check predicate, including on successful/spurious wakeup.
 * SALTS_ETIMEDOUT is distinct from SALTS_EINVAL and SALTS_EBUSY.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_timedwait_ns(salts_platform_condition *condition, salts_platform_mutex *mutex, uint64_t timeout_ns);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_signal(salts_platform_condition *condition);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_broadcast(salts_platform_condition *condition);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_condition_destroy(salts_platform_condition **condition);
/* Opaque provider-owned TSS key. Values are borrowed pointers; set(NULL)
 * clears the current thread's value without invoking its destructor.
 * Thread exit invokes the C-cdecl callback before releasing its reference.
 * Destroy returns BUSY while non-NULL values/callbacks remain active.
 * Key destroy versus concurrent set/get requires caller-side quiescence.
 * Recursive same-key destructor registration is not yet qualified.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_tss_create(salts_platform_tss **out, salts_platform_tss_destructor destructor);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_tss_set(salts_platform_tss *key, void *value);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_tss_get(salts_platform_tss *key, void **out);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_platform_tss_destroy(salts_platform_tss **key);
#ifdef __cplusplus
}
#endif
#endif
