#include <thread.h>

#include <errno.h>
#include <process.h>
#include <stdlib.h>
#include <windows.h>

static SALTS_THREAD_LOCAL unsigned char cmeta_thread_token;
const void *cmeta_thread_current_token(void) { return &cmeta_thread_token; }

struct cmeta_thread_wrapper_ctx {
  cmeta_thread_cb entry;
  void *arg;
};

void cmeta_mutex_init(cmeta_mutex_t *mutex) {
  PSRWLOCK native;
  if (mutex == NULL) return;
  *mutex = NULL;
  native = (PSRWLOCK)malloc(sizeof(SRWLOCK));
  if (native == NULL) return;
  InitializeSRWLock(native);
  *mutex = native;
}

void cmeta_mutex_destroy(cmeta_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  free(*mutex);
  *mutex = NULL;
}

void cmeta_mutex_lock(cmeta_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  AcquireSRWLockExclusive((PSRWLOCK)*mutex);
}

void cmeta_mutex_unlock(cmeta_mutex_t *mutex) {
  if (mutex == NULL || *mutex == NULL) return;
  ReleaseSRWLockExclusive((PSRWLOCK)*mutex);
}

int cmeta_rwlock_init(cmeta_rwlock_t *lock) {
  PSRWLOCK native;
  if (lock == NULL) return -EINVAL;
  *lock = NULL;
  native = (PSRWLOCK)malloc(sizeof(SRWLOCK));
  if (native == NULL) return -ENOMEM;
  InitializeSRWLock(native);
  *lock = native;
  return 0;
}

void cmeta_rwlock_destroy(cmeta_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  free(*lock);
  *lock = NULL;
}

void cmeta_rwlock_rdlock(cmeta_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockShared((PSRWLOCK)*lock);
}

void cmeta_rwlock_rdunlock(cmeta_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockShared((PSRWLOCK)*lock);
}

void cmeta_rwlock_wrlock(cmeta_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  AcquireSRWLockExclusive((PSRWLOCK)*lock);
}

void cmeta_rwlock_wrunlock(cmeta_rwlock_t *lock) {
  if (lock == NULL || *lock == NULL) return;
  ReleaseSRWLockExclusive((PSRWLOCK)*lock);
}

void cmeta_cond_init(cmeta_cond_t *cond) {
  PCONDITION_VARIABLE native;
  if (cond == NULL) return;
  *cond = NULL;
  native = (PCONDITION_VARIABLE)malloc(sizeof(CONDITION_VARIABLE));
  if (native == NULL) return;
  InitializeConditionVariable(native);
  *cond = native;
}

void cmeta_cond_destroy(cmeta_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  free(*cond);
  *cond = NULL;
}

void cmeta_cond_signal(cmeta_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  WakeConditionVariable((PCONDITION_VARIABLE)*cond);
}

void cmeta_cond_broadcast(cmeta_cond_t *cond) {
  if (cond == NULL || *cond == NULL) return;
  WakeAllConditionVariable((PCONDITION_VARIABLE)*cond);
}

void cmeta_cond_wait(cmeta_cond_t *cond, cmeta_mutex_t *mutex) {
  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL) return;
  (void)SleepConditionVariableSRW((PCONDITION_VARIABLE)*cond,
                                  (PSRWLOCK)*mutex, INFINITE, 0);
}

int cmeta_cond_timedwait(cmeta_cond_t *cond, cmeta_mutex_t *mutex,
                         uint64_t timeout_ns) {
  DWORD timeout_ms;
  BOOL result;

  if (cond == NULL || *cond == NULL || mutex == NULL || *mutex == NULL)
    return -EINVAL;

  if (timeout_ns >= (uint64_t)INFINITE * 1000000ULL)
    timeout_ms = INFINITE - 1U;
  else
    timeout_ms = (DWORD)((timeout_ns + 999999ULL) / 1000000ULL);

  result = SleepConditionVariableSRW((PCONDITION_VARIABLE)*cond,
                                     (PSRWLOCK)*mutex, timeout_ms, 0);
  if (result) return 0;
  return GetLastError() == ERROR_TIMEOUT ? -ETIMEDOUT : -EIO;
}

static unsigned __stdcall cmeta_thread_entry_wrapper(void *arg) {
  struct cmeta_thread_wrapper_ctx *ctx =
      (struct cmeta_thread_wrapper_ctx *)arg;
  cmeta_thread_cb entry = ctx->entry;
  void *real_arg = ctx->arg;
  free(ctx);
  entry(real_arg);
  return 0;
}

int cmeta_thread_create(cmeta_thread_t *thread, cmeta_thread_cb entry, void *arg) {
  struct cmeta_thread_wrapper_ctx *ctx;
  HANDLE native;

  if (thread == NULL || entry == NULL) return -EINVAL;
  *thread = NULL;
  ctx = (struct cmeta_thread_wrapper_ctx *)malloc(sizeof(*ctx));
  if (ctx == NULL) return -ENOMEM;
  ctx->entry = entry;
  ctx->arg = arg;

  native = (HANDLE)_beginthreadex(NULL, 0, cmeta_thread_entry_wrapper,
                                  ctx, 0, NULL);
  if (native == NULL) {
    free(ctx);
    return -1;
  }
  *thread = native;
  return 0;
}

int cmeta_thread_join(cmeta_thread_t *thread) {
  HANDLE native;
  if (thread == NULL || *thread == NULL) return -EINVAL;
  native = (HANDLE)*thread;
  if (WaitForSingleObject(native, INFINITE) != WAIT_OBJECT_0) return -1;
  CloseHandle(native);
  *thread = NULL;
  return 0;
}

void cmeta_thread_destroy(cmeta_thread_t *thread) {
  if (thread == NULL || *thread == NULL) return;
  CloseHandle((HANDLE)*thread);
  *thread = NULL;
}

void cmeta_once(cmeta_once_t *guard, void (*callback)(void)) {
  LONG previous;
  if (guard == NULL || callback == NULL) return;
  previous = InterlockedCompareExchange((volatile LONG *)&guard->state, 1, 0);
  if (previous == 0) {
    callback();
    (void)InterlockedExchange((volatile LONG *)&guard->state, 2);
    return;
  }
  while (InterlockedCompareExchange((volatile LONG *)&guard->state, 2, 2) != 2)
    SwitchToThread();
}

void cmeta_sleep_ms(uint32_t ms) { Sleep(ms); }

void cmeta_thread_yield(void) { SwitchToThread(); }

int cmeta_cpu_count(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 4;
}
