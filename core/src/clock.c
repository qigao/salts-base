#include <core/clock.h>
#include <threads/threads.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #include <atomic/atomic.h>
#else
  #include <time.h>
#endif

#ifdef _WIN32
uint64_t hrtime(void) {
  /* QueryPerformanceFrequency is stable, but publishing through a volatile
   * global was still a C data race. Use our ONE Platform Atomic provider.
   * Never fall back to native compiler _Atomic or unsynchronized writes.
   */
  static atomic64 frequency = {0};
  uint64_t f = 0, expected = 0;
  int exchanged = 0;
  LARGE_INTEGER counter;

  if (atomic64_load(&frequency, SALTS_MEMORY_ACQUIRE, &f) != SALTS_OK)
    return 0;
  if (f == 0) {
    LARGE_INTEGER candidate;
    if (!QueryPerformanceFrequency(&candidate) || candidate.QuadPart <= 0)
      return 0;
    if (atomic64_cas(&frequency, &expected,
          (uint64_t)candidate.QuadPart,
          SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &exchanged) != SALTS_OK)
      return 0;
    f = exchanged ? (uint64_t)candidate.QuadPart : expected;
  }

  if (!QueryPerformanceCounter(&counter) || f == 0) return 0;

  uint64_t ticks = (uint64_t)counter.QuadPart;
  uint64_t whole = (ticks / f) * 1000000000ULL;
  uint64_t part = (ticks % f) * 1000000000ULL / f;
  return whole + part;
}

uint64_t realtime_ms(void) {
  FILETIME ft;
  ULARGE_INTEGER value;

  GetSystemTimeAsFileTime(&ft);
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}
#else
uint64_t hrtime(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t realtime_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
#endif

uint64_t monotonic_ms(void) { return ns_to_ms(hrtime()); }

uint64_t uptime_ms(void) {
  /* Benign race: worst case two threads choose nearly identical start times. */
  static volatile uint64_t start_time = 0;
  uint64_t st = start_time;
  if (st == 0) {
    st = hrtime();
    start_time = st;
  }
  return ns_to_ms(hrtime() - st);
}

// =============================================================================
// Native timer backends
// =============================================================================

#ifdef _WIN32
// Windows implementation using CreateTimerQueueTimer

struct native_timer_s {
  HANDLE timer_handle;
  native_timer_cb callback;
  void *data;
  uint64_t timeout;
  uint64_t repeat;
  int active;
  volatile DWORD callback_thread_id;
  int pending_free;
};

static VOID CALLBACK native_timer_callback_win32(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
  (void)TimerOrWaitFired;
  native_timer_t *timer = (native_timer_t *)lpParameter;
  if (!timer) {
    return;
  }

  timer->callback_thread_id = GetCurrentThreadId();
  if (timer->callback) {
    timer->callback(timer);
  }

  if (timer->pending_free) {
    free(timer);
  } else {
    timer->callback_thread_id = 0;
  }
}

native_timer_t *native_timer_create(void *loop) {
  (void)loop;
  native_timer_t *timer = malloc(sizeof(native_timer_t));
  if (!timer) {
    return NULL;
  }

  memset(timer, 0, sizeof(*timer));
  return timer;
}

void native_timer_destroy(native_timer_t *timer) {
  if (!timer) {
    return;
  }

  native_timer_stop(timer);

  if (GetCurrentThreadId() == timer->callback_thread_id) {
    timer->pending_free = 1;
  } else {
    free(timer);
  }
}

int native_timer_start(native_timer_t *timer, native_timer_cb cb, uint64_t timeout, uint64_t repeat) {
  if (!timer || !cb) {
    return -1;
  }

  native_timer_stop(timer);

  timer->callback = cb;
  timer->timeout = timeout;
  timer->repeat = repeat;

  BOOL result = CreateTimerQueueTimer(&timer->timer_handle, NULL, native_timer_callback_win32,
                                      timer, (DWORD)timeout, (DWORD)repeat, WT_EXECUTEDEFAULT);

  if (!result) {
    (void)GetLastError();
    return -1;
  }

  timer->active = 1;
  return 0;
}

int native_timer_stop(native_timer_t *timer) {
  if (!timer || !timer->active) {
    return 0;
  }

  timer->active = 0;

  HANDLE h = InterlockedExchangePointer(&timer->timer_handle, NULL);
  if (h) {
    HANDLE completion = INVALID_HANDLE_VALUE;
    if (GetCurrentThreadId() == timer->callback_thread_id) {
      completion = NULL;
    }

    if (!DeleteTimerQueueTimer(NULL, h, completion)) {
      DWORD err = GetLastError();
      if (err != ERROR_IO_PENDING) {
        (void)err;
      }
    }
  }
  return 0;
}

#else
// POSIX implementation using a process-local timer manager thread.
//
// Rationale:
// timer_delete(2) specifies that treatment of any pending notification is
// unspecified. The old SIGEV_THREAD backend could therefore free the timer
// object while glibc still had a callback thread pending, which produced
// intermittent use-after-free reports during socket shutdown under ASan/LSan.
//
// The manager below owns all armed POSIX timers, tracks due times with
// CLOCK_MONOTONIC, and spawns a detached worker thread for each firing. This
// preserves the "callback runs off-thread" contract without relying on the
// unspecified lifetime semantics of timer_delete().

  #include <time.h>

struct native_timer_s {
  native_timer_cb callback;
  void *data;
  uint64_t timeout;
  uint64_t repeat;
  uint64_t due_ms;
  int active;
  int destroying;
  int queued;
  int callbacks_inflight;
  sync_mutex_t lock;
  sync_cond_t cond;
  struct native_timer_s *next;
};

typedef struct {
  sync_mutex_t lock;
  sync_cond_t cond;
  native_timer_t *head;
  int initialized;
  int init_failed;
} posix_timer_manager_t;

typedef struct {
  native_timer_t *timer;
  native_timer_cb callback;
} posix_timer_task_t;

static posix_timer_manager_t g_posix_timer_manager;
static sync_once_t g_posix_timer_manager_once = SALTS_ONCE_INIT;

static void posix_timer_queue_remove_locked(native_timer_t *timer) {
  native_timer_t **cursor;

  if (timer == NULL || !timer->queued) {
    return;
  }

  cursor = &g_posix_timer_manager.head;
  while (*cursor != NULL) {
    if (*cursor == timer) {
      *cursor = timer->next;
      timer->next = NULL;
      timer->queued = 0;
      return;
    }
    cursor = &(*cursor)->next;
  }
}

static void posix_timer_queue_insert_locked(native_timer_t *timer) {
  native_timer_t **cursor;

  if (timer == NULL) {
    return;
  }

  posix_timer_queue_remove_locked(timer);
  cursor = &g_posix_timer_manager.head;
  while (*cursor != NULL && (*cursor)->due_ms <= timer->due_ms) {
    cursor = &(*cursor)->next;
  }
  timer->next = *cursor;
  *cursor = timer;
  timer->queued = 1;
}

static void posix_timer_finish_callback(native_timer_t *timer) {
  if (timer == NULL) {
    return;
  }

  sync_mutex_lock(&timer->lock);
  timer->callbacks_inflight--;
  if (timer->destroying && timer->callbacks_inflight == 0) {
    sync_cond_signal(&timer->cond);
  }
  sync_mutex_unlock(&timer->lock);
}

static void posix_timer_callback_thread(void *arg) {
  posix_timer_task_t *task = (posix_timer_task_t *)arg;
  native_timer_t *timer;
  native_timer_cb callback;

  if (task == NULL) {
    return;
  }

  timer = task->timer;
  callback = task->callback;
  free(task);

  if (callback != NULL) {
    callback(timer);
  }
  posix_timer_finish_callback(timer);
}

static void posix_timer_dispatch(native_timer_t *timer, native_timer_cb callback) {
  posix_timer_task_t *task;
  sync_thread_t worker = NULL;

  if (timer == NULL || callback == NULL) {
    posix_timer_finish_callback(timer);
    return;
  }

  task = (posix_timer_task_t *)malloc(sizeof(*task));
  if (task == NULL) {
    callback(timer);
    posix_timer_finish_callback(timer);
    return;
  }

  task->timer = timer;
  task->callback = callback;
  if (sync_thread_create(&worker, posix_timer_callback_thread, task) != 0) {
    free(task);
    callback(timer);
    posix_timer_finish_callback(timer);
    return;
  }
  sync_thread_destroy(&worker);
}

static void posix_timer_manager_thread(void *arg) {
  (void)arg;

  for (;;) {
    native_timer_t *timer = NULL;
    native_timer_cb callback = NULL;
    uint64_t now_ms;

    sync_mutex_lock(&g_posix_timer_manager.lock);
    for (;;) {
      uint64_t wait_ms;

      timer = g_posix_timer_manager.head;
      if (timer == NULL) {
        sync_cond_wait(&g_posix_timer_manager.cond, &g_posix_timer_manager.lock);
        continue;
      }

      now_ms = monotonic_ms();
      if (timer->due_ms <= now_ms) {
        break;
      }

      wait_ms = timer->due_ms - now_ms;
      if (sync_cond_timedwait(&g_posix_timer_manager.cond, &g_posix_timer_manager.lock,
                               wait_ms * 1000000ULL) != 0) {
        continue;
      }
    }

    timer = g_posix_timer_manager.head;
    g_posix_timer_manager.head = timer->next;
    timer->next = NULL;
    timer->queued = 0;

    sync_mutex_lock(&timer->lock);
    if (!timer->destroying && timer->active && timer->callback != NULL) {
      callback = timer->callback;
      timer->callbacks_inflight++;
      if (timer->repeat > 0U) {
        uint64_t next_due_ms = timer->due_ms + timer->repeat;

        if (next_due_ms <= now_ms) {
          uint64_t skipped = ((now_ms - timer->due_ms) / timer->repeat) + 1U;
          next_due_ms = timer->due_ms + skipped * timer->repeat;
        }
        timer->due_ms = next_due_ms;
        posix_timer_queue_insert_locked(timer);
        sync_cond_signal(&g_posix_timer_manager.cond);
      } else {
        timer->active = 0;
        timer->due_ms = 0U;
      }
    }
    sync_mutex_unlock(&timer->lock);
    sync_mutex_unlock(&g_posix_timer_manager.lock);

    if (callback != NULL) {
      posix_timer_dispatch(timer, callback);
    }
  }
}

static void posix_timer_manager_init_once(void) {
  sync_thread_t worker = NULL;

  memset(&g_posix_timer_manager, 0, sizeof(g_posix_timer_manager));
  sync_mutex_init(&g_posix_timer_manager.lock);
  sync_cond_init(&g_posix_timer_manager.cond);
  if (sync_thread_create(&worker, posix_timer_manager_thread, NULL) != 0) {
    g_posix_timer_manager.init_failed = 1;
    sync_cond_destroy(&g_posix_timer_manager.cond);
    sync_mutex_destroy(&g_posix_timer_manager.lock);
    return;
  }

  sync_thread_destroy(&worker);
  g_posix_timer_manager.initialized = 1;
}

static int posix_timer_manager_ensure_started(void) {
  sync_once(&g_posix_timer_manager_once, posix_timer_manager_init_once);
  return g_posix_timer_manager.initialized && !g_posix_timer_manager.init_failed ? 0
                                                                                             : -1;
}

native_timer_t *native_timer_create(void *loop) {
  (void)loop;
  native_timer_t *timer = malloc(sizeof(native_timer_t));

  if (posix_timer_manager_ensure_started() != 0) {
    return NULL;
  }

  if (!timer) {
    return NULL;
  }

  memset(timer, 0, sizeof(*timer));
  sync_mutex_init(&timer->lock);
  sync_cond_init(&timer->cond);
  return timer;
}

void native_timer_destroy(native_timer_t *timer) {
  if (!timer) {
    return;
  }

  sync_mutex_lock(&g_posix_timer_manager.lock);
  sync_mutex_lock(&timer->lock);
  timer->destroying = 1;
  timer->callback = NULL;
  timer->active = 0;
  timer->due_ms = 0U;
  posix_timer_queue_remove_locked(timer);
  sync_cond_signal(&g_posix_timer_manager.cond);
  sync_mutex_unlock(&g_posix_timer_manager.lock);
  while (timer->callbacks_inflight > 0) {
    sync_cond_wait(&timer->cond, &timer->lock);
  }
  sync_mutex_unlock(&timer->lock);

  sync_cond_destroy(&timer->cond);
  sync_mutex_destroy(&timer->lock);
  free(timer);
}

int native_timer_start(native_timer_t *timer, native_timer_cb cb, uint64_t timeout, uint64_t repeat) {
  uint64_t due_ms;

  if (!timer || !cb) {
    return -1;
  }

  due_ms = monotonic_ms() + timeout;
  sync_mutex_lock(&g_posix_timer_manager.lock);
  sync_mutex_lock(&timer->lock);
  if (timer->destroying) {
    sync_mutex_unlock(&timer->lock);
    sync_mutex_unlock(&g_posix_timer_manager.lock);
    return -1;
  }
  posix_timer_queue_remove_locked(timer);
  timer->callback = cb;
  timer->timeout = timeout;
  timer->repeat = repeat;
  timer->due_ms = due_ms;
  timer->active = 1;
  posix_timer_queue_insert_locked(timer);
  sync_mutex_unlock(&timer->lock);
  sync_cond_signal(&g_posix_timer_manager.cond);
  sync_mutex_unlock(&g_posix_timer_manager.lock);
  return 0;
}

int native_timer_stop(native_timer_t *timer) {
  if (!timer) {
    return 0;
  }

  sync_mutex_lock(&g_posix_timer_manager.lock);
  sync_mutex_lock(&timer->lock);
  posix_timer_queue_remove_locked(timer);
  timer->active = 0;
  timer->due_ms = 0U;
  sync_mutex_unlock(&timer->lock);
  sync_cond_signal(&g_posix_timer_manager.cond);
  sync_mutex_unlock(&g_posix_timer_manager.lock);
  return 0;
}

#endif

void native_timer_set_data(native_timer_t *timer, void *data) {
  if (timer) {
    timer->data = data;
  }
}

void *native_timer_get_data(native_timer_t *timer) { return timer ? timer->data : NULL; }

uint64_t native_timer_get_repeat(native_timer_t *timer) { return timer ? timer->repeat : 0; }
