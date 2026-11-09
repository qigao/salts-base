/* C11 threads and Salts synchronization extensions. Include <salts/threads.h> and link Core.
 * Core supplies the standard C11 symbols. Use its matching types and runtime;
 * do not mix them with a second libc thread implementation.
 */
#ifndef SALTS_CORE_THREADS_H
#define SALTS_CORE_THREADS_H

#include <salts/threads_types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compare tokens only while both OS threads are alive. Their addresses may
 * be reused after thread exit. */
SALTS_API const void *sync_thread_current_token(void);

static inline int sync_thread_affine_init(sync_thread_affine_state *state, const void *owner) {
  if (state == NULL || owner == NULL || state->owner != NULL) return SALTS_EINVAL;
  state->owner = owner;
  state->thread = sync_thread_current_token();
  state->busy = false;
  return SALTS_OK;
}

static inline int sync_thread_affine_check(const sync_thread_affine_state *state,
                                            const void *owner) {
  if (state == NULL || owner == NULL || state->owner != owner ||
      state->thread != sync_thread_current_token())
    return SALTS_EINVAL;
  return state->busy ? SALTS_EBUSY : SALTS_OK;
}

static inline int sync_thread_affine_set_busy(sync_thread_affine_state *state, const void *owner,
                                               bool busy) {
  if (state == NULL || owner == NULL || state->owner != owner ||
      state->thread != sync_thread_current_token())
    return SALTS_EINVAL;
  state->busy = busy;
  return SALTS_OK;
}

static inline int sync_thread_affine_reset(sync_thread_affine_state *state, const void *owner) {
  int status = sync_thread_affine_check(state, owner);
  if (status != SALTS_OK) return status;
  state->owner = NULL;
  state->thread = NULL;
  state->busy = false;
  return SALTS_OK;
}

SALTS_API void sync_mutex_init(sync_mutex_t *mutex);
SALTS_API void sync_mutex_destroy(sync_mutex_t *mutex);
SALTS_API void sync_mutex_lock(sync_mutex_t *mutex);
SALTS_API void sync_mutex_unlock(sync_mutex_t *mutex);

SALTS_API int sync_rwlock_init(sync_rwlock_t *lock);
SALTS_API void sync_rwlock_destroy(sync_rwlock_t *lock);
SALTS_API void sync_rwlock_rdlock(sync_rwlock_t *lock);
SALTS_API void sync_rwlock_rdunlock(sync_rwlock_t *lock);
SALTS_API void sync_rwlock_wrlock(sync_rwlock_t *lock);
SALTS_API void sync_rwlock_wrunlock(sync_rwlock_t *lock);

SALTS_API void sync_cond_init(sync_cond_t *cond);
SALTS_API void sync_cond_destroy(sync_cond_t *cond);
SALTS_API void sync_cond_signal(sync_cond_t *cond);
SALTS_API void sync_cond_broadcast(sync_cond_t *cond);
SALTS_API void sync_cond_wait(sync_cond_t *cond, sync_mutex_t *mutex);
SALTS_API int sync_cond_timedwait(sync_cond_t *cond, sync_mutex_t *mutex, uint64_t timeout_ns);

SALTS_API int sync_thread_create(sync_thread_t *thread, sync_thread_cb entry, void *arg);
SALTS_API int sync_thread_join(sync_thread_t *thread);
SALTS_API void sync_thread_destroy(sync_thread_t *thread);
SALTS_API void sync_once(sync_once_t *guard, void (*callback)(void));
SALTS_API void sleep_ms(uint32_t ms);
SALTS_API void sync_thread_yield(void);
SALTS_API int cpu_count(void);

/* Original Core synchronization policy and process identity surface. */
SALTS_API void sync_set_single_threaded(int enabled);
SALTS_API int sync_is_single_threaded(void);
SALTS_API int platform_getpid(void);

 SALTS_API int thrd_create(thrd_t *thr, thrd_start_t fn, void *arg);
 SALTS_API int thrd_join(thrd_t thr, int *result);
 SALTS_API int thrd_detach(thrd_t thr);
 SALTS_API thrd_t thrd_current(void);
 SALTS_API int thrd_equal(thrd_t a, thrd_t b);
 SALTS_API void thrd_yield(void);
/* CRT struct timespec must NEVER cross a TinyCC/MSVC/LLVM DLL boundary.
 * Convert it here in the consumer, then use fixed-width values only.
 */
 SALTS_API int thrd_sleep_abi(uint64_t seconds, uint32_t nanos);
 SALTS_API int thrd_sleep_remaining_abi(
    uint64_t seconds, uint32_t nanos, uint64_t *remaining_seconds,
    uint32_t *remaining_nanos);
/* 0: elapsed; -1: interrupted (remaining is written if non-NULL);
 * -2: invalid duration or another underlying sleep error.
 */
static inline int thrd_sleep(
    const struct timespec *duration, struct timespec *remaining) {
    int rc;
    uint64_t seconds = 0;
    uint32_t nanos = 0;
    if (!duration || duration->tv_sec < 0 ||
        duration->tv_nsec < 0 || duration->tv_nsec >= 1000000000L)
        return -2;
    rc = thrd_sleep_remaining_abi((uint64_t)duration->tv_sec,
        (uint32_t)duration->tv_nsec, &seconds, &nanos);
    if ((rc == 0 || rc == -1) && remaining) {
        remaining->tv_sec = (time_t)seconds;
        remaining->tv_nsec = (long)nanos;
    }
    return rc;
}
#if defined(_MSC_VER)
__declspec(noreturn)
#elif defined(__cplusplus)
[[noreturn]]
#else
_Noreturn
#endif
 SALTS_API void thrd_exit(int result);

 SALTS_API int mtx_init(mtx_t *m, int type);
/* All four standard plain/timed/recursive combinations are supported.
 * Timed waits use absolute TIME_UTC. Destroy only with no active users.
 */
 SALTS_API int mtx_lock(mtx_t *m);
 SALTS_API int mtx_trylock(mtx_t *m);
 SALTS_API int mtx_timedlock_abi(mtx_t *m, int64_t seconds, uint32_t nanos);
static inline int mtx_timedlock(
    mtx_t *m, const struct timespec *utc_deadline) {
    if (!utc_deadline || utc_deadline->tv_nsec < 0 ||
        utc_deadline->tv_nsec >= 1000000000L) return thrd_error;
    return mtx_timedlock_abi(m, (int64_t)utc_deadline->tv_sec,
                                        (uint32_t)utc_deadline->tv_nsec);
}
 SALTS_API int mtx_unlock(mtx_t *m);
 SALTS_API void mtx_destroy(mtx_t *m);
 SALTS_API int cnd_init(cnd_t *c);
 SALTS_API int cnd_wait(cnd_t *c, mtx_t *m);
 SALTS_API int cnd_timedwait_abi(cnd_t *c, mtx_t *m, int64_t seconds, uint32_t nanos);
static inline int cnd_timedwait(
    cnd_t *c, mtx_t *m, const struct timespec *utc_deadline) {
    if (!utc_deadline || utc_deadline->tv_nsec < 0 ||
        utc_deadline->tv_nsec >= 1000000000L) return thrd_error;
    return cnd_timedwait_abi(c, m,
                (int64_t)utc_deadline->tv_sec, (uint32_t)utc_deadline->tv_nsec);
}
 SALTS_API int cnd_signal(cnd_t *c);
 SALTS_API int cnd_broadcast(cnd_t *c);
 SALTS_API void cnd_destroy(cnd_t *c);
SALTS_API void call_once(once_flag *flag, void (*callback)(void));

/* tss_create: thrd_success or thrd_error; initial value is NULL per thread.
 * tss_set replaces a borrowed value without invoking its destructor.
 * tss_delete never invokes destructors, including values on live threads;
 * synchronize deletion with uses of that key. Deleted handles are invalid.
 * Non-NULL values with destructors are cleared before each exit callback.
 * Reinstalled values receive at most TSS_DTOR_ITERATIONS destructor passes.
 * At most 1024 live/retired key records are retained; resource exhaustion
 * returns thrd_error. Retired records are reclaimed as thread slots retire.
 * Core and callback modules must outlive all threads using these objects.
 */
 SALTS_API int tss_create(tss_t *key, tss_dtor_t destructor);
 SALTS_API void tss_delete(tss_t key);
 SALTS_API void *tss_get(tss_t key);
 SALTS_API int tss_set(tss_t key, void *value);

#ifdef __cplusplus
}
#endif
#endif
