#include <disruptor.h>
#include <error_codes.h>
#include <thread.h>
#include <thread_pool.h>
#include "thread_pool_lf.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct worker_context_s {
  cmeta_threadpool_t *pool;
  int worker_id;
} worker_context_t;

struct cmeta_threadpool_s {
  cmeta_threadpool_lf *lf;
  cmeta_thread_t *threads;
  worker_context_t *workers;
  int num_threads;
  size_t queue_capacity;
  disruptor_t *queue;

  atomic_int accepting;
  atomic_int shutdown;
  atomic_int shutdown_policy;
  atomic_int cancel_pending;
  _Atomic int64_t queued_depth;
  _Atomic int64_t tasks_submitted;
  _Atomic int64_t tasks_started;
  _Atomic int64_t tasks_completed;
  _Atomic int64_t tasks_cancelled;
  _Atomic int64_t tasks_rejected;
  _Atomic int64_t peak_pending_tasks;

  cmeta_mutex_t park_mutex;
  cmeta_cond_t task_available;
  cmeta_cond_t queue_space;
  cmeta_mutex_t dispatch_mutex;
  cmeta_mutex_t wait_mutex;
  cmeta_cond_t all_done;
};

#define SALTS_THREADPOOL_DEFAULT_QUEUE_CAPACITY 4096U
#define SALTS_THREADPOOL_NO_SHUTDOWN_POLICY (-1)

static _Thread_local cmeta_threadpool_t *cmeta_threadpool_current = NULL;

void cmeta_threadpool_call_internal(cmeta_threadpool_t *owner,
    cmeta_task_fn fn, void *arg) {
  if (fn == NULL) return;
  cmeta_threadpool_t *previous = cmeta_threadpool_current;
  cmeta_threadpool_current = owner;
  fn(arg);
  cmeta_threadpool_current = previous;
}

/* Private cross-library query used by CFlow to reject synchronous joins that
 * cannot make progress from the same pool callback. */
int cmeta_threadpool_is_current_internal(const cmeta_threadpool_t *pool) {
  return pool != NULL && cmeta_threadpool_current == pool;
}

static void cmeta_threadpool_run_descriptor(
    cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task) {
  cmeta_threadpool_t *previous = cmeta_threadpool_current;
  cmeta_threadpool_current = pool;
  task->run(task->arg);
  if (task->finalize != NULL) task->finalize(task->arg);
  cmeta_threadpool_current = previous;
}

static void cmeta_threadpool_cancel_descriptor(
    cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task) {
  cmeta_threadpool_t *previous = cmeta_threadpool_current;
  cmeta_threadpool_current = pool;
  if (task->cancel != NULL) task->cancel(task->arg);
  if (task->finalize != NULL) task->finalize(task->arg);
  cmeta_threadpool_current = previous;
}

static uint64_t cmeta_threadpool_round_up_pow2(size_t value) {
  uint64_t rounded = 1U;

  if (value == 0U) return 0U;
  while (rounded < (uint64_t)value) {
    if (rounded > (UINT64_MAX >> 1U)) return 0U;
    rounded <<= 1U;
  }
  return rounded;
}

static int64_t cmeta_threadpool_pending_tasks(const cmeta_threadpool_t *pool) {
  if (pool == NULL) return 0;
  if (pool->lf != NULL) {
    cmeta_threadpool_stats_t stats;
    cmeta_threadpool_lf_stats(pool->lf, &stats, NULL);
    return stats.pending_tasks;
  }
  return atomic_load(&pool->tasks_submitted) -
         atomic_load(&pool->tasks_completed) -
         atomic_load(&pool->tasks_cancelled);
}

static void cmeta_threadpool_update_peak_pending(cmeta_threadpool_t *pool,
                                                 int64_t candidate) {
  int64_t seen = atomic_load(&pool->peak_pending_tasks);
  while (candidate > seen && !atomic_compare_exchange_weak(
                                 &pool->peak_pending_tasks, &seen, candidate)) {
  }
}

static void cmeta_threadpool_notify_progress(cmeta_threadpool_t *pool) {
  if (pool == NULL) return;
  cmeta_mutex_lock(&pool->wait_mutex);
  if (cmeta_threadpool_pending_tasks(pool) <= 0)
    cmeta_cond_broadcast(&pool->all_done);
  cmeta_mutex_unlock(&pool->wait_mutex);
}

static void cmeta_threadpool_finish_task(cmeta_threadpool_t *pool) {
  atomic_fetch_add(&pool->tasks_completed, 1);
  cmeta_threadpool_notify_progress(pool);
}

static void cmeta_threadpool_cancel_task(cmeta_threadpool_t *pool) {
  atomic_fetch_add(&pool->tasks_cancelled, 1);
  cmeta_threadpool_notify_progress(pool);
}

static void cmeta_threadpool_signal_task_available(cmeta_threadpool_t *pool) {
  cmeta_mutex_lock(&pool->park_mutex);
  cmeta_cond_signal(&pool->task_available);
  cmeta_mutex_unlock(&pool->park_mutex);
}

static void cmeta_threadpool_signal_queue_space(cmeta_threadpool_t *pool) {
  cmeta_mutex_lock(&pool->park_mutex);
  cmeta_cond_signal(&pool->queue_space);
  cmeta_mutex_unlock(&pool->park_mutex);
}

static int cmeta_threadpool_try_reserve_queue_slot(cmeta_threadpool_t *pool,
                                                    int blocking) {
  int64_t depth;

  while (atomic_load(&pool->accepting) && !atomic_load(&pool->shutdown)) {
    depth = atomic_load(&pool->queued_depth);
    while (depth < (int64_t)pool->queue_capacity) {
      if (atomic_compare_exchange_weak(&pool->queued_depth, &depth, depth + 1))
        return 1;
    }

    if (!blocking) return 0;

    cmeta_mutex_lock(&pool->park_mutex);
    while (atomic_load(&pool->queued_depth) >= (int64_t)pool->queue_capacity &&
           atomic_load(&pool->accepting) && !atomic_load(&pool->shutdown)) {
      cmeta_cond_wait(&pool->queue_space, &pool->park_mutex);
    }
    cmeta_mutex_unlock(&pool->park_mutex);
  }

  return 0;
}

static void cmeta_threadpool_release_queue_slot(cmeta_threadpool_t *pool) {
  atomic_fetch_sub(&pool->queued_depth, 1);
  cmeta_threadpool_signal_queue_space(pool);
}

static void cmeta_threadpool_destroy_sync(cmeta_threadpool_t *pool) {
  cmeta_mutex_destroy(&pool->park_mutex);
  cmeta_cond_destroy(&pool->task_available);
  cmeta_cond_destroy(&pool->queue_space);
  cmeta_mutex_destroy(&pool->dispatch_mutex);
  cmeta_mutex_destroy(&pool->wait_mutex);
  cmeta_cond_destroy(&pool->all_done);
}

typedef enum cmeta_threadpool_take_status {
  SALTS_THREADPOOL_TAKE_NONE = 0,
  SALTS_THREADPOOL_TAKE_VALID,
  SALTS_THREADPOOL_TAKE_INVALID
} cmeta_threadpool_take_status;

static cmeta_threadpool_take_status cmeta_threadpool_try_take_task(
    cmeta_threadpool_t *pool, cmeta_threadpool_task_t *task) {
  disruptor_cursor_t cursor = {0};
  const cmeta_threadpool_task_t *entry;

  /* Worker completion advances a contiguous cursor. Serializing this short
   * transfer prevents an out-of-order release from pinning reusable slots. */
  cmeta_mutex_lock(&pool->dispatch_mutex);
  if (!disruptor_worker_try_claim(pool->queue, &cursor)) {
    cmeta_mutex_unlock(&pool->dispatch_mutex);
    return SALTS_THREADPOOL_TAKE_NONE;
  }

  entry = (const cmeta_threadpool_task_t *)
      disruptor_show_entry(pool->queue, &cursor);
  if (entry == NULL || entry->run == NULL) {
    disruptor_worker_release_entry(pool->queue, &cursor);
    cmeta_mutex_unlock(&pool->dispatch_mutex);
    return SALTS_THREADPOOL_TAKE_INVALID;
  }

  *task = *entry;
  disruptor_worker_release_entry(pool->queue, &cursor);
  cmeta_mutex_unlock(&pool->dispatch_mutex);
  return SALTS_THREADPOOL_TAKE_VALID;
}

static void worker_entry(void *arg) {
  worker_context_t *ctx = (worker_context_t *)arg;
  cmeta_threadpool_t *pool = ctx->pool;

  while (1) {
    cmeta_threadpool_task_t task;
    cmeta_threadpool_take_status take_status =
        cmeta_threadpool_try_take_task(pool, &task);

    if (take_status == SALTS_THREADPOOL_TAKE_NONE) {
      if (atomic_load(&pool->shutdown) &&
          cmeta_threadpool_pending_tasks(pool) <= 0 &&
          atomic_load(&pool->queued_depth) <= 0) {
        break;
      }

      if (atomic_load(&pool->queued_depth) > 0) {
        cmeta_thread_yield();
        continue;
      }

      cmeta_mutex_lock(&pool->park_mutex);
      while (atomic_load(&pool->queued_depth) <= 0 &&
             !atomic_load(&pool->shutdown)) {
        cmeta_cond_wait(&pool->task_available, &pool->park_mutex);
      }
      cmeta_mutex_unlock(&pool->park_mutex);
      continue;
    }

    if (take_status == SALTS_THREADPOOL_TAKE_INVALID) {
      cmeta_threadpool_release_queue_slot(pool);
      cmeta_threadpool_cancel_task(pool);
      continue;
    }

    if (atomic_load(&pool->cancel_pending)) {
      cmeta_threadpool_cancel_descriptor(pool, &task);
      cmeta_threadpool_release_queue_slot(pool);
      cmeta_threadpool_cancel_task(pool);
      continue;
    }
    atomic_fetch_add(&pool->tasks_started, 1);
    cmeta_threadpool_release_queue_slot(pool);
    cmeta_threadpool_run_descriptor(pool, &task);
    cmeta_threadpool_finish_task(pool);
  }

  cmeta_threadpool_notify_progress(pool);
}

cmeta_threadpool_t *
cmeta_threadpool_create_with_config(const cmeta_threadpool_config_t *config) {
  cmeta_threadpool_t *pool;
  disruptor_config_t queue_config;
  int num_threads;
  size_t queue_capacity;
  uint64_t ring_capacity;

  if (config == NULL) return NULL;

  num_threads = config->num_threads;
  if (num_threads <= 0) num_threads = cmeta_cpu_count();
  queue_capacity = config->queue_capacity > 0U
                       ? config->queue_capacity
                       : SALTS_THREADPOOL_DEFAULT_QUEUE_CAPACITY;
  if (queue_capacity == SIZE_MAX) return NULL;

  ring_capacity = cmeta_threadpool_round_up_pow2(queue_capacity);
  if (ring_capacity == 0U || ring_capacity > (uint64_t)SIZE_MAX ||
      ring_capacity > (uint64_t)INT64_MAX)
    return NULL;

  pool = (cmeta_threadpool_t *)calloc(1, sizeof(*pool));
  if (pool == NULL) return NULL;

  pool->num_threads = num_threads;
  pool->queue_capacity = queue_capacity;
  atomic_store(&pool->accepting, 1);
  atomic_store(&pool->shutdown, 0);
  atomic_store(&pool->shutdown_policy,
               SALTS_THREADPOOL_NO_SHUTDOWN_POLICY);
  atomic_store(&pool->cancel_pending, 0);
  atomic_store(&pool->queued_depth, 0);
  atomic_store(&pool->tasks_submitted, 0);
  atomic_store(&pool->tasks_started, 0);
  atomic_store(&pool->tasks_completed, 0);
  atomic_store(&pool->tasks_cancelled, 0);
  atomic_store(&pool->tasks_rejected, 0);
  atomic_store(&pool->peak_pending_tasks, 0);

  queue_config.entry_size = sizeof(cmeta_threadpool_task_t);
  queue_config.capacity = ring_capacity;
  queue_config.consumer_capacity = 1U;
  queue_config.mode = DISRUPTOR_MODE_WORKER_POOL;
  pool->queue = disruptor_create(&queue_config);
  if (pool->queue == NULL) {
    free(pool);
    return NULL;
  }

  cmeta_mutex_init(&pool->park_mutex);
  cmeta_cond_init(&pool->task_available);
  cmeta_cond_init(&pool->queue_space);
  cmeta_mutex_init(&pool->dispatch_mutex);
  cmeta_mutex_init(&pool->wait_mutex);
  cmeta_cond_init(&pool->all_done);
  if (pool->park_mutex == NULL || pool->task_available == NULL ||
      pool->queue_space == NULL || pool->dispatch_mutex == NULL ||
      pool->wait_mutex == NULL || pool->all_done == NULL) {
    cmeta_threadpool_destroy_sync(pool);
    disruptor_destroy(pool->queue);
    free(pool);
    return NULL;
  }

  pool->threads = (cmeta_thread_t *)calloc((size_t)num_threads,
                                           sizeof(*pool->threads));
  pool->workers = (worker_context_t *)calloc((size_t)num_threads,
                                             sizeof(*pool->workers));
  if (pool->threads == NULL || pool->workers == NULL) {
    free(pool->threads);
    free(pool->workers);
    cmeta_threadpool_destroy_sync(pool);
    disruptor_destroy(pool->queue);
    free(pool);
    return NULL;
  }

  for (int i = 0; i < num_threads; ++i) {
    pool->workers[i].pool = pool;
    pool->workers[i].worker_id = i;
    if (cmeta_thread_create(&pool->threads[i], worker_entry,
                            &pool->workers[i]) != 0) {
      (void)cmeta_threadpool_shutdown_with_policy(
          pool, SALTS_THREADPOOL_SHUTDOWN_DRAIN);
      for (int j = 0; j < i; ++j)
        (void)cmeta_thread_join(&pool->threads[j]);
      free(pool->threads);
      free(pool->workers);
      cmeta_threadpool_destroy_sync(pool);
      disruptor_destroy(pool->queue);
      free(pool);
      return NULL;
    }
  }

  return pool;
}

cmeta_threadpool_t *cmeta_threadpool_create(int num_threads) {
  cmeta_threadpool_config_t config;
  config.num_threads = num_threads;
  config.queue_capacity = SALTS_THREADPOOL_DEFAULT_QUEUE_CAPACITY;
  return cmeta_threadpool_create_with_config(&config);
}

int cmeta_threadpool_create_leader_followers(
    const cmeta_threadpool_lf_config_t *config, cmeta_threadpool_t **out_pool) {
  if (config == NULL || out_pool == NULL || *out_pool != NULL ||
      config->struct_size != sizeof(*config) || config->version != SALTS_THREADPOOL_LF_VERSION ||
      config->num_threads <= 0 || config->queue_capacity == 0 ||
      config->max_batch == 0 || config->max_batch > SALTS_THREADPOOL_LF_MAX_BATCH)
    return SALTS_EINVAL;
  cmeta_threadpool_t *pool = calloc(1, sizeof(*pool));
  if (pool == NULL) return SALTS_ENOMEM;
  const int status = cmeta_threadpool_lf_create(config, pool, &pool->lf);
  if (status != SALTS_OK) { free(pool); return status; }
  pool->num_threads = config->num_threads;
  pool->queue_capacity = config->queue_capacity;
  *out_pool = pool;
  return SALTS_OK;
}

int cmeta_threadpool_get_lf_stats(cmeta_threadpool_t *pool,
    cmeta_threadpool_lf_stats_t *out_stats) {
  if (pool == NULL || out_stats == NULL || out_stats->struct_size != sizeof(*out_stats) ||
      out_stats->version != SALTS_THREADPOOL_LF_VERSION) return SALTS_EINVAL;
  if (pool->lf == NULL) return SALTS_ENOTSUP;
  cmeta_threadpool_lf_stats(pool->lf, NULL, out_stats);
  return SALTS_OK;
}

int cmeta_threadpool_shutdown_with_policy(
    cmeta_threadpool_t *pool, cmeta_threadpool_shutdown_policy_t policy) {
  int expected = SALTS_THREADPOOL_NO_SHUTDOWN_POLICY;
  if (pool == NULL ||
      (policy != SALTS_THREADPOOL_SHUTDOWN_DRAIN &&
       policy != SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING))
    return SALTS_EINVAL;
  if (pool->lf != NULL) return cmeta_threadpool_lf_shutdown(pool->lf, policy);
  if (!atomic_compare_exchange_strong(&pool->shutdown_policy, &expected,
                                      (int)policy) &&
      expected != (int)policy)
    return SALTS_EBUSY;
  atomic_store(&pool->accepting, 0);
  if (policy == SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING)
    atomic_store(&pool->cancel_pending, 1);
  atomic_store(&pool->shutdown, 1);
  cmeta_mutex_lock(&pool->park_mutex);
  cmeta_cond_broadcast(&pool->task_available);
  cmeta_cond_broadcast(&pool->queue_space);
  cmeta_mutex_unlock(&pool->park_mutex);
  cmeta_threadpool_notify_progress(pool);
  return SALTS_OK;
}

void cmeta_threadpool_shutdown(cmeta_threadpool_t *pool) {
  (void)cmeta_threadpool_shutdown_with_policy(
      pool, SALTS_THREADPOOL_SHUTDOWN_DRAIN);
}

void cmeta_threadpool_destroy(cmeta_threadpool_t *pool) {
  if (pool == NULL) return;
  if (pool->lf != NULL) {
    /* Same-pool destroy violates the callback contract; never self-join/free. */
    if (cmeta_threadpool_current == pool) return;
    cmeta_threadpool_lf_destroy(pool->lf);
    free(pool);
    return;
  }
  cmeta_threadpool_shutdown(pool);
  for (int i = 0; i < pool->num_threads; ++i)
    (void)cmeta_thread_join(&pool->threads[i]);

  cmeta_threadpool_destroy_sync(pool);
  disruptor_destroy(pool->queue);
  free(pool->workers);
  free(pool->threads);
  free(pool);
}

static int cmeta_threadpool_submit_internal(cmeta_threadpool_t *pool,
                                            const cmeta_threadpool_task_t *task,
                                            int blocking) {
  disruptor_cursor_t cursor = {0};
  cmeta_threadpool_task_t *entry;
  unsigned int wait_rounds = 0U;

  if (pool == NULL || task == NULL || task->run == NULL) return SALTS_EINVAL;
  if (pool->lf != NULL)
    return cmeta_threadpool_lf_submit(pool->lf, task, blocking,
                                    cmeta_threadpool_current == pool);
  if (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown)) {
    atomic_fetch_add(&pool->tasks_rejected, 1);
    return SALTS_ESHUTDOWN;
  }

  if (!cmeta_threadpool_try_reserve_queue_slot(
          pool, blocking && cmeta_threadpool_current != pool)) {
    atomic_fetch_add(&pool->tasks_rejected, 1);
    if (blocking && cmeta_threadpool_current == pool &&
        atomic_load(&pool->accepting) && !atomic_load(&pool->shutdown))
      return SALTS_EBUSY;
    return (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown))
               ? SALTS_ESHUTDOWN
               : SALTS_ENOBUFS;
  }

  while (!disruptor_publisher_try_claim(pool->queue, &cursor)) {
    if (!blocking || cmeta_threadpool_current == pool ||
        !atomic_load(&pool->accepting) ||
        atomic_load(&pool->shutdown)) {
      cmeta_threadpool_release_queue_slot(pool);
      atomic_fetch_add(&pool->tasks_rejected, 1);
      if (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown))
        return SALTS_ESHUTDOWN;
      return blocking && cmeta_threadpool_current == pool
                 ? SALTS_EBUSY : SALTS_ENOBUFS;
    }

    if ((++wait_rounds & 0xFFU) == 0U)
      cmeta_sleep_ms(1);
    else
      cmeta_thread_yield();
  }

  entry = (cmeta_threadpool_task_t *)
      disruptor_acquire_entry(pool->queue, &cursor);
  *entry = *task;
  {
    int64_t submitted = atomic_fetch_add(&pool->tasks_submitted, 1) + 1;
    cmeta_threadpool_update_peak_pending(
        pool, submitted - atomic_load(&pool->tasks_completed));
  }
  (void)disruptor_publisher_publish(pool->queue, &cursor);
  cmeta_threadpool_signal_task_available(pool);
  return SALTS_OK;
}

int cmeta_threadpool_submit_task(cmeta_threadpool_t *pool,
                                 const cmeta_threadpool_task_t *task) {
  return cmeta_threadpool_submit_internal(pool, task, 1);
}

int cmeta_threadpool_try_submit_task(cmeta_threadpool_t *pool,
                                     const cmeta_threadpool_task_t *task) {
  return cmeta_threadpool_submit_internal(pool, task, 0);
}

int cmeta_threadpool_submit(cmeta_threadpool_t *pool,
                            cmeta_task_fn task,
                            void *arg) {
  const cmeta_threadpool_task_t descriptor = {
      .run = task,
      .cancel = NULL,
      .finalize = NULL,
      .arg = arg,
  };
  return cmeta_threadpool_submit_task(pool, &descriptor);
}

int cmeta_threadpool_try_submit(cmeta_threadpool_t *pool,
                                cmeta_task_fn task,
                                void *arg) {
  const cmeta_threadpool_task_t descriptor = {
      .run = task,
      .cancel = NULL,
      .finalize = NULL,
      .arg = arg,
  };
  return cmeta_threadpool_try_submit_task(pool, &descriptor);
}

int cmeta_threadpool_wait_status(cmeta_threadpool_t *pool) {
  if (pool == NULL) return SALTS_EINVAL;
  if (cmeta_threadpool_current == pool) return SALTS_EBUSY;
  if (pool->lf != NULL) return cmeta_threadpool_lf_wait(pool->lf);
  cmeta_mutex_lock(&pool->wait_mutex);
  while (cmeta_threadpool_pending_tasks(pool) > 0)
    cmeta_cond_wait(&pool->all_done, &pool->wait_mutex);
  cmeta_mutex_unlock(&pool->wait_mutex);
  return SALTS_OK;
}

void cmeta_threadpool_wait(cmeta_threadpool_t *pool) {
  (void)cmeta_threadpool_wait_status(pool);
}

int cmeta_threadpool_pending(cmeta_threadpool_t *pool) {
  return (int)cmeta_threadpool_pending_tasks(pool);
}

int64_t cmeta_threadpool_cancelled(cmeta_threadpool_t *pool) {
  if (pool != NULL && pool->lf != NULL) {
    cmeta_threadpool_lf_stats_t stats;
    cmeta_threadpool_lf_stats(pool->lf, NULL, &stats);
    return stats.cancelled_tasks;
  }
  return pool != NULL ? atomic_load(&pool->tasks_cancelled) : 0;
}

int cmeta_threadpool_size(cmeta_threadpool_t *pool) {
  return pool != NULL ? pool->num_threads : 0;
}

size_t cmeta_threadpool_capacity(cmeta_threadpool_t *pool) {
  return pool != NULL ? pool->queue_capacity : 0U;
}

int cmeta_threadpool_is_accepting(cmeta_threadpool_t *pool) {
  if (pool != NULL && pool->lf != NULL) {
    cmeta_threadpool_stats_t stats;
    cmeta_threadpool_lf_stats(pool->lf, &stats, NULL);
    return stats.accepting;
  }
  return pool != NULL ? atomic_load(&pool->accepting) : 0;
}

void cmeta_threadpool_get_stats(cmeta_threadpool_t *pool,
                                cmeta_threadpool_stats_t *stats) {
  int64_t submitted;
  int64_t started;
  int64_t completed;

  if (pool == NULL || stats == NULL) return;
  if (pool->lf != NULL) {
    cmeta_threadpool_lf_stats(pool->lf, stats, NULL);
    return;
  }
  memset(stats, 0, sizeof(*stats));

  submitted = atomic_load(&pool->tasks_submitted);
  started = atomic_load(&pool->tasks_started);
  completed = atomic_load(&pool->tasks_completed);

  stats->num_threads = pool->num_threads;
  stats->queue_capacity = pool->queue_capacity;
  stats->accepting = atomic_load(&pool->accepting);
  stats->submitted_tasks = submitted;
  stats->started_tasks = started;
  stats->completed_tasks = completed;
  stats->rejected_tasks = atomic_load(&pool->tasks_rejected);
  stats->queued_tasks = atomic_load(&pool->queued_depth);
  stats->active_tasks = started - completed;
  stats->pending_tasks = submitted - completed -
                         atomic_load(&pool->tasks_cancelled);
  stats->peak_pending_tasks = atomic_load(&pool->peak_pending_tasks);
}
