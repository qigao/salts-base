#include <threads/threads.h>
#include "nt_wait_win.h"

#include <errno.h>
#include <process.h>
#include <stdlib.h>
#include <windows.h>

static SALTS_THREAD_LOCAL unsigned char thread_token;
const void *sync_thread_current_token(void) { return &thread_token; }

struct thread_wrapper_ctx {
  sync_thread_cb entry;
  void *arg;
};

void sync_mutex_init(sync_mutex_t *mutex) {
  platform_mutex *owner = NULL;
  if (!mutex) return;
  (void)platform_mutex_create(&owner);
  *mutex = owner;
}

void sync_mutex_destroy(sync_mutex_t *mutex) {
  if (mutex && *mutex) {
    platform_mutex *owner = *mutex;
    if (platform_mutex_destroy(&owner) != SALTS_OK) abort();
    *mutex = NULL;
  }
}

void sync_mutex_lock(sync_mutex_t *mutex) {
  if (mutex && *mutex && platform_mutex_lock(*mutex) != SALTS_OK) abort();
}

void sync_mutex_unlock(sync_mutex_t *mutex) {
  if (mutex && *mutex && platform_mutex_unlock(*mutex) != SALTS_OK) abort();
}

int sync_rwlock_init(sync_rwlock_t *lock) {
  PSRWLOCK native;
  if (lock == NULL) return -EINVAL;
  *lock = NULL;
  native = (PSRWLOCK)malloc(sizeof(SRWLOCK));
  if (native == NULL) return -ENOMEM;
  InitializeSRWLock(native);
  *lock = native;
  return 0;
}

void sync_rwlock_destroy(sync_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  free(*lock);
  *lock = NULL;
}

void sync_rwlock_rdlock(sync_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockShared((PSRWLOCK)*lock);
}

void sync_rwlock_rdunlock(sync_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockShared((PSRWLOCK)*lock);
}

void sync_rwlock_wrlock(sync_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockExclusive((PSRWLOCK)*lock);
}

void sync_rwlock_wrunlock(sync_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockExclusive((PSRWLOCK)*lock);
}

void sync_cond_init(sync_cond_t *cond) {
  platform_condition *owner = NULL;
  if (!cond) return;
  (void)platform_condition_create(&owner);
  *cond = owner;
}

void sync_cond_destroy(sync_cond_t *cond) {
  if (cond && *cond) {
    platform_condition *owner = *cond;
    if (platform_condition_destroy(&owner) != SALTS_OK) abort();
    *cond = NULL;
  }
}

void sync_cond_signal(sync_cond_t *cond) {
  if (cond && *cond) (void)platform_condition_signal(*cond);
}

void sync_cond_broadcast(sync_cond_t *cond) {
  if (cond && *cond) (void)platform_condition_broadcast(*cond);
}

void sync_cond_wait(sync_cond_t *cond, sync_mutex_t *mutex) {
  if (cond && *cond && mutex && *mutex &&
      platform_condition_wait(*cond, *mutex) != SALTS_OK) abort();
}

int sync_cond_timedwait(sync_cond_t *cond, sync_mutex_t *mutex, uint64_t timeout_ns) {
  int rc;
  if (!cond || !*cond || !mutex || !*mutex) return -EINVAL;
  /* This legacy extension caps large timeouts at the largest finite DWORD wait. */
  if (timeout_ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS)
    timeout_ns = SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS;
  rc = platform_condition_timedwait_ns(*cond, *mutex, timeout_ns);
  if (rc == SALTS_OK) return 0;
  return rc == SALTS_ETIMEDOUT ? -ETIMEDOUT : -EIO;
}

static unsigned __stdcall thread_entry_wrapper(void *arg) {
  struct thread_wrapper_ctx *ctx =
      (struct thread_wrapper_ctx *)arg;
  sync_thread_cb entry = ctx->entry;
  void *real_arg = ctx->arg;
  free(ctx);
  entry(real_arg);
  return 0;
}

int sync_thread_create(sync_thread_t *thread, sync_thread_cb entry, void *arg) {
  struct thread_wrapper_ctx *ctx;
  HANDLE native;

  if (thread == NULL || entry == NULL) return -EINVAL;
  *thread = NULL;
  ctx = (struct thread_wrapper_ctx *)malloc(sizeof(*ctx));
  if (ctx == NULL) return -ENOMEM;
  ctx->entry = entry;
  ctx->arg = arg;

  native = (HANDLE)_beginthreadex(NULL, 0, thread_entry_wrapper,
                                  ctx, 0, NULL);
  if (native == NULL) {
    free(ctx);
    return -1;
  }
  *thread = native;
  return 0;
}

int sync_thread_join(sync_thread_t *thread) {
  HANDLE native;
  if (thread == NULL || *thread == NULL) return -EINVAL;
  native = (HANDLE)*thread;
  if (WaitForSingleObject(native, INFINITE) != WAIT_OBJECT_0) return -1;
  CloseHandle(native);
  *thread = NULL;
  return 0;
}

void sync_thread_destroy(sync_thread_t *thread) {
  if (thread == NULL || *thread == NULL) return;
  CloseHandle((HANDLE)*thread);
  *thread = NULL;
}

void sync_once(sync_once_t *guard, void (*callback)(void)) {
  if (guard == NULL || callback == NULL) return;
  if (win_call_once((volatile LONG *)&guard->state, callback) != SALTS_OK) abort();
}

void sleep_ms(uint32_t ms) { Sleep(ms); }

void sync_thread_yield(void) { SwitchToThread(); }

int cpu_count(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 4;
}
