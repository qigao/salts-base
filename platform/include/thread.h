#ifndef SALTS_THREAD_PRIMITIVES_H
#define SALTS_THREAD_PRIMITIVES_H

#include <platform_api.h>
#include <error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The POSIX once primitive is an OS synchronization event even when Salts
 * itself was built without TSan. This is an intentional current-ABI change;
 * every dependent SDK must be rebuilt against this same header. */
#if !defined(_WIN32)
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SALTS_THREAD_LOCAL
  #if defined(__cplusplus)
    #define SALTS_THREAD_LOCAL thread_local
  #elif defined(_MSC_VER)
    #define SALTS_THREAD_LOCAL __declspec(thread)
  #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
    #define SALTS_THREAD_LOCAL _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define SALTS_THREAD_LOCAL __thread
  #else
    #error "SALTS_THREAD_LOCAL is not supported by this compiler"
  #endif
#endif

typedef void *cmeta_mutex_t;
typedef void *cmeta_cond_t;
typedef void *cmeta_thread_t;
typedef void *cmeta_rwlock_t;

typedef struct cmeta_once_s {
#if defined(_WIN32)
  volatile int state;
#else
  pthread_once_t native;
#endif
} cmeta_once_t;
#if defined(_WIN32)
#define SALTS_ONCE_INIT {0}
#else
#define SALTS_ONCE_INIT {PTHREAD_ONCE_INIT}
#endif

typedef void (*cmeta_thread_cb)(void *arg);

/* Compare tokens only while both OS threads are alive. Their addresses may
 * be reused after thread exit. */
SALTS_CORE_PLATFORM_API const void *SALTS_CORE_PLATFORM_CALL cmeta_thread_current_token(void);

/*
 * Address-stable thread-affinity state owned by Salts::Platform.
 * This carries no value lifecycle callbacks and owns no payload.
 */
typedef struct cmeta_thread_affine_state {
  const void *owner;
  const void *thread;
  bool busy;
} cmeta_thread_affine_state;

static inline int cmeta_thread_affine_init(cmeta_thread_affine_state *state, const void *owner) {
  if (state == NULL || owner == NULL || state->owner != NULL) return SALTS_EINVAL;
  state->owner = owner;
  state->thread = cmeta_thread_current_token();
  state->busy = false;
  return SALTS_OK;
}

static inline int cmeta_thread_affine_check(const cmeta_thread_affine_state *state,
                                            const void *owner) {
  if (state == NULL || owner == NULL || state->owner != owner ||
      state->thread != cmeta_thread_current_token())
    return SALTS_EINVAL;
  return state->busy ? SALTS_EBUSY : SALTS_OK;
}

static inline int cmeta_thread_affine_set_busy(cmeta_thread_affine_state *state, const void *owner,
                                               bool busy) {
  if (state == NULL || owner == NULL || state->owner != owner ||
      state->thread != cmeta_thread_current_token())
    return SALTS_EINVAL;
  state->busy = busy;
  return SALTS_OK;
}

static inline int cmeta_thread_affine_reset(cmeta_thread_affine_state *state, const void *owner) {
  int status = cmeta_thread_affine_check(state, owner);
  if (status != SALTS_OK) return status;
  state->owner = NULL;
  state->thread = NULL;
  state->busy = false;
  return SALTS_OK;
}

SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_mutex_init(cmeta_mutex_t *mutex);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_mutex_destroy(cmeta_mutex_t *mutex);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_mutex_lock(cmeta_mutex_t *mutex);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_mutex_unlock(cmeta_mutex_t *mutex);

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_rwlock_init(cmeta_rwlock_t *lock);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_rwlock_destroy(cmeta_rwlock_t *lock);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_rwlock_rdlock(cmeta_rwlock_t *lock);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_rwlock_rdunlock(cmeta_rwlock_t *lock);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_rwlock_wrlock(cmeta_rwlock_t *lock);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_rwlock_wrunlock(cmeta_rwlock_t *lock);

SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_cond_init(cmeta_cond_t *cond);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_cond_destroy(cmeta_cond_t *cond);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_cond_signal(cmeta_cond_t *cond);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_cond_broadcast(cmeta_cond_t *cond);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_cond_wait(cmeta_cond_t *cond, cmeta_mutex_t *mutex);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_cond_timedwait(cmeta_cond_t *cond, cmeta_mutex_t *mutex, uint64_t timeout_ns);

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_thread_create(cmeta_thread_t *thread, cmeta_thread_cb entry, void *arg);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_thread_join(cmeta_thread_t *thread);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_thread_destroy(cmeta_thread_t *thread);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_once(cmeta_once_t *guard, void (*callback)(void));
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_sleep_ms(uint32_t ms);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_thread_yield(void);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_cpu_count(void);

/* Original Core synchronization policy and process identity surface. */
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_sync_set_single_threaded(int enabled);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_sync_is_single_threaded(void);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_getpid(void);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_THREAD_PRIMITIVES_H */
