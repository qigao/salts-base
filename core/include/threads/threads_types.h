/* Public thread ABI types and initializers. Installed with the Core SDK.
 * Object layouts and callback signatures are shared by C and C++ clients.
 * Thread operations are declared in <threads/threads.h>.
 */
#ifndef SALTS_CORE_THREADS_TYPES_H
#define SALTS_CORE_THREADS_TYPES_H

#include <salts/api.h>
#include <atomic/atomic.h>
#include <salts/error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* C23 libc stdlib.h may declare C once APIs; include it before C aliases. */
#include <stdlib.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Salts extensions: opaque native handles, affinity, synchronization policy. */
typedef void *sync_mutex_t;
typedef void *sync_cond_t;
typedef void *sync_thread_t;
typedef void *sync_rwlock_t;

/* Keep the native POSIX once event visible to sanitizer-instrumented callers
 * even when Core itself is not instrumented. Its ABI is unchanged by merging
 * the public declarations into this header. */
typedef struct sync_once_s {
#if defined(_WIN32)
  volatile int state;
#else
  pthread_once_t native;
#endif
} sync_once_t;
#if defined(_WIN32)
#define SALTS_ONCE_INIT {0}
#else
#define SALTS_ONCE_INIT {PTHREAD_ONCE_INIT}
#endif

typedef void (*sync_thread_cb)(void *arg);

/*
 * Address-stable thread-affinity state owned by Salts::Core.
 * This carries no value lifecycle callbacks and owns no payload.
 */
typedef struct sync_thread_affine_state {
  const void *owner;
  const void *thread;
  bool busy;
} sync_thread_affine_state;

/* C11 API: opaque provider-owned objects across compiler/DLL boundaries. */
typedef struct thrd_state *thrd_t;
typedef int (*thrd_start_t)(void *);
typedef struct { struct mtx_state *owner; int type; } mtx_t;
typedef struct { struct condition_state *owner; } cnd_t;
typedef struct tss_key *tss_t;
typedef void (*tss_dtor_t)(void *);
#define TSS_DTOR_ITERATIONS 4
#if !defined(__cplusplus) && !defined(thread_local)
#define thread_local SALTS_THREAD_LOCAL
#endif
/* C23 libc may already expose once_flag through stdlib.h. Otherwise Core
 * provides its portable once object directly, without a type-name macro.
 */
struct thread_once_state { atomic32 state; };
#ifndef ONCE_FLAG_INIT
typedef struct thread_once_state once_flag;
#define ONCE_FLAG_INIT { {0u} }
#endif
enum { thrd_success=0, thrd_nomem=1, thrd_timedout=2, thrd_busy=3, thrd_error=4 };
enum { mtx_plain=0, mtx_recursive=1, mtx_timed=2 };

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CORE_THREADS_TYPES_H */
