#include "thread_pool_lf.h"
#include <queue/disruptor.h>
#include <threads/threads.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct lf_worker {
  threadpool_lf *pool;
  sync_thread_t thread;
  sync_cond_t wake;
  threadpool_task_t *batch;
  size_t id;
  bool eligible, waiting;
} lf_worker;

struct threadpool_lf {
  threadpool_t *owner;
  disruptor_t *queue;
  sync_mutex_t monitor;
  sync_cond_t space, idle;
  lf_worker *workers;
  threadpool_task_t *scratch;
  size_t worker_count, created, capacity, batch_size;
  size_t leader, last_leader, queued, claimed, running, cancelling, finalizing;
  int policy; /* -1 OPEN, otherwise the immutable shutdown policy. */
  int64_t accepted, started, completed, cancelled, rejected, peak_pending;
  uint64_t handoffs, wake_signals, empty_wakes;
};

static void count_signal(uint64_t *value) {
  if (*value != UINT64_MAX) ++*value;
}

static int64_t pending_locked(const threadpool_lf *p) {
  return p->accepted - p->completed - p->cancelled;
}

static void notify_locked(threadpool_lf *p) {
  if (p->leader != SIZE_MAX && p->queued != 0 &&
      p->workers[p->leader].waiting) {
    count_signal(&p->wake_signals);
    sync_cond_signal(&p->workers[p->leader].wake);
  }
}

/* O(W), round-robin among eligible workers; only the monitor elects. */
static void elect_locked(threadpool_lf *p) {
  if (p->leader != SIZE_MAX || (p->policy != -1 && pending_locked(p) == 0)) return;
  size_t next = p->last_leader;
  for (size_t n = 0; n < p->worker_count; ++n) {
    next = next + 1 == p->worker_count ? 0 : next + 1;
    if (!p->workers[next].eligible) continue;
    p->leader = p->last_leader = next;
    count_signal(&p->handoffs);
    notify_locked(p);
    return;
  }
}

static void wake_all_locked(threadpool_lf *p) {
  for (size_t n = 0; n < p->worker_count; ++n) {
    if (p->workers[n].wake != NULL) {
      count_signal(&p->wake_signals);
      sync_cond_broadcast(&p->workers[n].wake);
    }
  }
  sync_cond_broadcast(&p->space);
  sync_cond_broadcast(&p->idle);
}

static void worker_entry(void *arg) {
  lf_worker *w = arg;
  threadpool_lf *p = w->pool;
  sync_mutex_lock(&p->monitor);
  w->eligible = true;
  elect_locked(p);
  for (;;) {
    if (p->policy != -1 && pending_locked(p) == 0) break;
    if (p->leader != w->id || p->queued == 0) {
      /* Predicate publication and atomic unlock-and-wait share the monitor. */
      w->waiting = true;
      sync_cond_wait(&w->wake, &p->monitor);
      w->waiting = false;
      if (p->queued == 0) count_signal(&p->empty_wakes);
      continue;
    }
    size_t count = 0;
    while (count < p->batch_size && p->queued != 0) {
      disruptor_cursor_t cursor = {0};
      if (!disruptor_worker_try_claim(p->queue, &cursor)) abort();
      const threadpool_task_t *task = disruptor_show_entry(p->queue, &cursor);
      if (task == NULL || task->run == NULL) abort();
      w->batch[count++] = *task;
      disruptor_worker_release_entry(p->queue, &cursor);
      --p->queued;
      ++p->claimed;
    }
    sync_cond_broadcast(&p->space);
    w->eligible = false;
    p->leader = SIZE_MAX;
    elect_locked(p); /* LF hands off before executing independent callbacks. */
    for (size_t n = 0; n < count; ++n) {
      const threadpool_task_t task = w->batch[n];
      /* Start/cancel linearizes with shutdown, including private batch items. */
      const bool cancel = p->policy == SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING;
      --p->claimed;
      if (cancel) ++p->cancelling;
      else { ++p->running; ++p->started; }
      sync_mutex_unlock(&p->monitor);
      threadpool_call_internal(p->owner, cancel ? task.cancel : task.run, task.arg);
      sync_mutex_lock(&p->monitor);
      if (cancel) --p->cancelling;
      else --p->running;
      ++p->finalizing;
      sync_mutex_unlock(&p->monitor);
      threadpool_call_internal(p->owner, task.finalize, task.arg);
      sync_mutex_lock(&p->monitor);
      --p->finalizing;
      if (cancel) ++p->cancelled;
      else ++p->completed;
      if (pending_locked(p) == 0) {
        sync_cond_broadcast(&p->idle);
        if (p->policy != -1) wake_all_locked(p);
      }
    }
    w->eligible = true;
    elect_locked(p);
  }
  w->eligible = false;
  if (p->leader == w->id) p->leader = SIZE_MAX;
  sync_mutex_unlock(&p->monitor);
}

static void release_storage(threadpool_lf *p) {
  if (p->workers != NULL)
    for (size_t n = 0; n < p->worker_count; ++n)
      sync_cond_destroy(&p->workers[n].wake);
  sync_cond_destroy(&p->space);
  sync_cond_destroy(&p->idle);
  sync_mutex_destroy(&p->monitor);
  disruptor_destroy(p->queue);
  free(p->scratch);
  free(p->workers);
  free(p);
}

int threadpool_lf_create(const threadpool_lf_config_t *config,
    threadpool_t *owner, threadpool_lf **out) {
  if (config == NULL || owner == NULL || out == NULL || *out != NULL ||
      config->struct_size != sizeof(*config) || config->version != SALTS_THREADPOOL_LF_VERSION ||
      config->num_threads <= 0 || config->queue_capacity == 0 ||
      config->max_batch == 0 || config->max_batch > SALTS_THREADPOOL_LF_MAX_BATCH)
    return SALTS_EINVAL;
  const size_t workers = (size_t)config->num_threads;
  if (workers > SIZE_MAX / config->max_batch || workers > SIZE_MAX / sizeof(lf_worker))
    return SALTS_EINVAL;
  const size_t scratch_count = workers * config->max_batch;
  if (scratch_count > (size_t)INT_MAX || config->queue_capacity > (size_t)INT_MAX - scratch_count ||
      scratch_count > SIZE_MAX / sizeof(threadpool_task_t)) return SALTS_EINVAL;
  size_t ring_capacity = 1;
  while (ring_capacity < config->queue_capacity) {
    if (ring_capacity > SIZE_MAX / 2) return SALTS_EINVAL;
    ring_capacity *= 2;
  }
  if (ring_capacity > SIZE_MAX / sizeof(threadpool_task_t)) return SALTS_EINVAL;
  threadpool_lf *p = calloc(1, sizeof(*p));
  if (p == NULL) return SALTS_ENOMEM;
  p->owner = owner;
  p->worker_count = workers;
  p->capacity = config->queue_capacity;
  p->batch_size = config->max_batch;
  p->leader = SIZE_MAX;
  p->last_leader = workers - 1;
  p->policy = -1;
  const disruptor_config_t queue_config = {
      sizeof(threadpool_task_t), (uint64_t)ring_capacity, 1, DISRUPTOR_MODE_WORKER_POOL};
  p->queue = disruptor_create(&queue_config);
  p->workers = calloc(workers, sizeof(*p->workers));
  p->scratch = calloc(scratch_count, sizeof(*p->scratch));
  sync_mutex_init(&p->monitor);
  sync_cond_init(&p->space);
  sync_cond_init(&p->idle);
  if (p->queue == NULL || p->workers == NULL || p->scratch == NULL ||
      p->monitor == NULL || p->space == NULL || p->idle == NULL) {
    release_storage(p);
    return SALTS_ENOMEM;
  }
  /* Initialize all condition storage before any worker can inspect it. */
  for (size_t n = 0; n < workers; ++n) {
    lf_worker *w = &p->workers[n];
    w->pool = p;
    w->id = n;
    w->batch = p->scratch + n * p->batch_size;
    sync_cond_init(&w->wake);
    if (w->wake == NULL) { release_storage(p); return SALTS_ENOMEM; }
  }
  for (size_t n = 0; n < workers; ++n) {
    lf_worker *w = &p->workers[n];
    if (sync_thread_create(&w->thread, worker_entry, w) != 0) {
      threadpool_lf_destroy(p);
      return SALTS_ENOMEM;
    }
    ++p->created;
  }
  *out = p;
  return SALTS_OK;
}

int threadpool_lf_shutdown(threadpool_lf *p,
    threadpool_shutdown_policy_t policy) {
  sync_mutex_lock(&p->monitor);
  if (p->policy != -1 && p->policy != (int)policy) {
    sync_mutex_unlock(&p->monitor);
    return SALTS_EBUSY;
  }
  p->policy = (int)policy;
  wake_all_locked(p);
  sync_mutex_unlock(&p->monitor);
  return SALTS_OK;
}

void threadpool_lf_destroy(threadpool_lf *p) {
  sync_mutex_lock(&p->monitor);
  if (p->policy == -1) p->policy = SALTS_THREADPOOL_SHUTDOWN_DRAIN;
  wake_all_locked(p);
  sync_mutex_unlock(&p->monitor);
  for (size_t n = 0; n < p->created; ++n)
    if (sync_thread_join(&p->workers[n].thread) != 0) abort();
  release_storage(p);
}

int threadpool_lf_submit(threadpool_lf *p,
    const threadpool_task_t *task, int blocking, int is_current) {
  sync_mutex_lock(&p->monitor);
  while (p->policy == -1 && p->queued == p->capacity && blocking && !is_current)
    sync_cond_wait(&p->space, &p->monitor);
  int status = SALTS_OK;
  if (p->policy != -1) status = SALTS_ESHUTDOWN;
  else if (p->accepted == INT64_MAX) {
    p->policy = SALTS_THREADPOOL_SHUTDOWN_DRAIN;
    wake_all_locked(p);
    status = SALTS_ESHUTDOWN;
  } else if (p->queued == p->capacity)
    status = blocking && is_current ? SALTS_EBUSY : SALTS_ENOBUFS;
  if (status != SALTS_OK) {
    if (p->rejected != INT64_MAX) ++p->rejected;
    sync_mutex_unlock(&p->monitor);
    return status;
  }
  disruptor_cursor_t cursor = {0};
  if (!disruptor_publisher_try_claim(p->queue, &cursor)) abort();
  threadpool_task_t *entry = disruptor_acquire_entry(p->queue, &cursor);
  if (entry == NULL) abort();
  *entry = *task;
  /* All publisher claims/publishes are serialized: no publication gaps. */
  if (!disruptor_publisher_publish(p->queue, &cursor)) abort();
  const bool was_empty = p->queued == 0;
  ++p->queued;
  ++p->accepted;
  const int64_t pending = pending_locked(p);
  if (pending > p->peak_pending) p->peak_pending = pending;
  const bool had_leader = p->leader != SIZE_MAX;
  elect_locked(p);
  if (was_empty && had_leader) notify_locked(p);
  sync_mutex_unlock(&p->monitor);
  return SALTS_OK;
}

int threadpool_lf_wait(threadpool_lf *p) {
  sync_mutex_lock(&p->monitor);
  while (pending_locked(p) != 0) sync_cond_wait(&p->idle, &p->monitor);
  sync_mutex_unlock(&p->monitor);
  return SALTS_OK;
}

void threadpool_lf_stats(threadpool_lf *p,
    threadpool_stats_t *stats, threadpool_lf_stats_t *lf_stats) {
  sync_mutex_lock(&p->monitor);
  if (stats != NULL) {
    *stats = (threadpool_stats_t){0};
    stats->num_threads = (int)p->worker_count;
    stats->queue_capacity = p->capacity;
    stats->accepting = p->policy == -1;
    stats->submitted_tasks = p->accepted;
    stats->started_tasks = p->started;
    stats->completed_tasks = p->completed;
    stats->rejected_tasks = p->rejected;
    stats->queued_tasks = (int64_t)p->queued;
    stats->active_tasks = p->started - p->completed;
    stats->pending_tasks = pending_locked(p);
    stats->peak_pending_tasks = p->peak_pending;
  }
  if (lf_stats != NULL) {
    *lf_stats = (threadpool_lf_stats_t){0};
    lf_stats->struct_size = sizeof(*lf_stats);
    lf_stats->version = SALTS_THREADPOOL_LF_VERSION;
    lf_stats->max_batch = p->batch_size;
    lf_stats->leader_id = p->leader;
    lf_stats->queued_tasks = p->queued;
    lf_stats->claimed_tasks = p->claimed;
    lf_stats->running_tasks = p->running;
    lf_stats->cancelling_tasks = p->cancelling;
    lf_stats->finalizing_tasks = p->finalizing;
    for (size_t n = 0; n < p->worker_count; ++n)
      lf_stats->sleeping_workers += p->workers[n].waiting ? 1 : 0;
    lf_stats->accepted_tasks = p->accepted;
    lf_stats->completed_tasks = p->completed;
    lf_stats->cancelled_tasks = p->cancelled;
    lf_stats->handoffs = p->handoffs;
    lf_stats->wake_signals = p->wake_signals;
    lf_stats->empty_wakes = p->empty_wakes;
  }
  sync_mutex_unlock(&p->monitor);
}
