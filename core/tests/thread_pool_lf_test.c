#include <thread_pool.h>
#include <thread.h>
#include <clock.h>
#include <tinytest.h>
#include <stdatomic.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

enum { JOBS = 1024, PRODUCERS = 4, TIMEOUT_MS = 7000 };
typedef struct probe {
  atomic_int run, cancel, finalize, phase, entered, finalized, admitted;
  unsigned id;
  uint64_t input, output;
  bool gate_run, gate_finalize, gate_second;
} probe;
typedef struct submitter {
  cmeta_thread_t thread;
  size_t first, count, stride;
  bool blocking;
  atomic_int entered, status;
} submitter;

static cmeta_threadpool_t *pool;
static probe jobs[JOBS];
static submitter producers[PRODUCERS];
static cmeta_thread_t waiter;
static atomic_int release_gate, release_second, callback_errors, wait_entered, wait_done;
static int self_post[2], self_wait, self_finalize_wait;

static bool await_int(atomic_int *value, int expected) {
  const uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  while (atomic_load(value) != expected && cmeta_monotonic_ms() < deadline)
    cmeta_sleep_ms(1);
  return atomic_load(value) == expected;
}

static void gate(void) {
  if (!await_int(&release_gate, 1)) atomic_fetch_add(&callback_errors, 1);
}

static void run(void *arg) {
  probe *p = arg;
  if (atomic_exchange(&p->phase, 1) != 0) atomic_fetch_add(&callback_errors, 1);
  atomic_fetch_add(&p->run, 1);
  p->output = p->input ^ UINT64_C(0x9e3779b97f4a7c15);
  atomic_store(&p->entered, 1);
  if (p->gate_run) gate();
  if (p->gate_second && !await_int(&release_second, 1))
    atomic_fetch_add(&callback_errors, 1);
}

static void cancel(void *arg) {
  probe *p = arg;
  if (atomic_exchange(&p->phase, 2) != 0) atomic_fetch_add(&callback_errors, 1);
  atomic_fetch_add(&p->cancel, 1);
}

static void finalize(void *arg) {
  probe *p = arg;
  atomic_store(&p->finalized, 1);
  if (p->gate_finalize) gate();
  const int previous = atomic_exchange(&p->phase, 3);
  if (previous != 1 && previous != 2) atomic_fetch_add(&callback_errors, 1);
  atomic_fetch_add(&p->finalize, 1);
}

static cmeta_threadpool_task_t descriptor(size_t id) {
  return (cmeta_threadpool_task_t){run, cancel, finalize, &jobs[id]};
}

static void create(int workers, size_t capacity, size_t batch) {
  const cmeta_threadpool_lf_config_t config = {
      sizeof(config), SALTS_THREADPOOL_LF_VERSION, workers, capacity, batch};
  check_equal(cmeta_threadpool_create_leader_followers(&config, &pool), SALTS_OK);
}

static cmeta_threadpool_lf_stats_t snapshot(void) {
  cmeta_threadpool_lf_stats_t stats = {0};
  stats.struct_size = sizeof(stats);
  stats.version = SALTS_THREADPOOL_LF_VERSION;
  check_equal(cmeta_threadpool_get_lf_stats(pool, &stats), SALTS_OK);
  const int64_t pending = (int64_t)(stats.queued_tasks + stats.claimed_tasks +
      stats.running_tasks + stats.cancelling_tasks + stats.finalizing_tasks);
  check_equal(stats.accepted_tasks, pending + stats.completed_tasks + stats.cancelled_tasks);
  return stats;
}

static bool await_sleeping(size_t workers) {
  const uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  while (cmeta_monotonic_ms() < deadline) {
    cmeta_threadpool_lf_stats_t stats = snapshot();
    if (stats.sleeping_workers == workers) return true;
    cmeta_sleep_ms(1);
  }
  return false;
}

static void verify(size_t first, size_t count, bool cancelled) {
  for (size_t i = first; i < first + count; ++i) {
    check_equal(atomic_load(&jobs[i].run), cancelled ? 0 : 1);
    check_equal(atomic_load(&jobs[i].cancel), cancelled ? 1 : 0);
    check_equal(atomic_load(&jobs[i].finalize), 1);
    if (!cancelled) check_equal(jobs[i].output, jobs[i].input ^ UINT64_C(0x9e3779b97f4a7c15));
  }
  check_equal(atomic_load(&callback_errors), 0);
}

static void submit(void *arg) {
  submitter *p = arg;
  const uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  atomic_store(&p->entered, 1);
  for (size_t n = 0; n < p->count; ++n) {
    const cmeta_threadpool_task_t task = descriptor(p->first + n * p->stride);
    int status;
    do {
      status = p->blocking ? cmeta_threadpool_submit_task(pool, &task)
                          : cmeta_threadpool_try_submit_task(pool, &task);
      if (status == SALTS_ENOBUFS) cmeta_thread_yield();
    } while (status == SALTS_ENOBUFS && cmeta_monotonic_ms() < deadline);
    atomic_store(&p->status, status);
    atomic_store(&jobs[p->first + n * p->stride].admitted, status == SALTS_OK);
    if (status != SALTS_OK) break;
  }
}

static void wait_task(void *unused) {
  (void)unused;
  atomic_store(&wait_entered, 1);
  if (cmeta_threadpool_wait_status(pool) != SALTS_OK) atomic_fetch_add(&callback_errors, 1);
  atomic_store(&wait_done, 1);
}

static void join(cmeta_thread_t *thread) {
  if (*thread != NULL && cmeta_thread_join(thread) != 0) abort();
}

static void self_block(void *unused) {
  (void)unused;
  const cmeta_threadpool_task_t task = descriptor(1);
  self_post[0] = cmeta_threadpool_submit_task(pool, &task);
  self_post[1] = cmeta_threadpool_submit_task(pool, &task);
  self_wait = cmeta_threadpool_wait_status(pool);
}

static void self_finalize(void *unused) {
  (void)unused;
  self_finalize_wait = cmeta_threadpool_wait_status(pool);
}

suite("Concurrency Leader/Followers owned task lifecycle") {
  before_each() {
    pool = NULL;
    waiter = NULL;
    atomic_store(&release_gate, 0);
    atomic_store(&release_second, 0);
    atomic_store(&callback_errors, 0);
    atomic_store(&wait_entered, 0);
    atomic_store(&wait_done, 0);
    for (size_t n = 0; n < JOBS; ++n) {
      jobs[n].id = (unsigned)n;
      jobs[n].input = UINT64_C(0xd6e8feb86659fd93) * (n + 1);
      jobs[n].output = 0;
      jobs[n].gate_run = jobs[n].gate_finalize = jobs[n].gate_second = false;
      atomic_store(&jobs[n].run, 0);
      atomic_store(&jobs[n].cancel, 0);
      atomic_store(&jobs[n].finalize, 0);
      atomic_store(&jobs[n].phase, 0);
      atomic_store(&jobs[n].entered, 0);
      atomic_store(&jobs[n].finalized, 0);
      atomic_store(&jobs[n].admitted, 0);
    }
    for (size_t n = 0; n < PRODUCERS; ++n) {
      producers[n].thread = NULL;
      producers[n].blocking = false;
      atomic_store(&producers[n].entered, 0);
      atomic_store(&producers[n].status, SALTS_EINVAL);
    }
  }
  after_each() {
    atomic_store(&release_gate, 1);
    atomic_store(&release_second, 1);
    if (pool != NULL)
      (void)cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING);
    for (size_t n = 0; n < PRODUCERS; ++n) join(&producers[n].thread);
    join(&waiter);
    cmeta_threadpool_destroy(pool);
    pool = NULL;
  }
  it("rejects invalid configurations and preserves output and legacy ABI") {
    cmeta_threadpool_lf_config_t config = {sizeof(config), SALTS_THREADPOOL_LF_VERSION, 1, 1, 1};
    check_equal(cmeta_threadpool_create_leader_followers(NULL, &pool), SALTS_EINVAL);
    check_equal(cmeta_threadpool_create_leader_followers(&config, NULL), SALTS_EINVAL);
    for (size_t n = 0; n < 8; ++n) {
      cmeta_threadpool_lf_config_t bad = config;
      switch (n) {
        case 0: bad.struct_size = 0; break;
        case 1: bad.version = 2; break;
        case 2: bad.num_threads = 0; break;
        case 3: bad.queue_capacity = 0; break;
        case 4: bad.max_batch = 0; break;
        case 5: bad.max_batch = 33; break;
        case 6: bad.queue_capacity = SIZE_MAX; break;
        default: bad.num_threads = INT_MAX; bad.max_batch = 32; break;
      }
      check_equal(cmeta_threadpool_create_leader_followers(&bad, &pool), SALTS_EINVAL);
      check_null(pool);
    }
    pool = cmeta_threadpool_create(1);
    check_not_null(pool);
    cmeta_threadpool_t *alias = pool;
    check_equal(cmeta_threadpool_create_leader_followers(&config, &alias), SALTS_EINVAL);
    check_true(alias == pool);
    cmeta_threadpool_lf_stats_t stats = {0};
    stats.struct_size = sizeof(stats);
    stats.version = SALTS_THREADPOOL_LF_VERSION;
    stats.handoffs = 99;
    check_equal(cmeta_threadpool_get_lf_stats(pool, &stats), SALTS_ENOTSUP);
    check_equal(stats.handoffs, (uint64_t)99);
    stats.version = 0;
    check_equal(cmeta_threadpool_get_lf_stats(pool, &stats), SALTS_EINVAL);
    check_equal(stats.handoffs, (uint64_t)99);
  }
  it("promotes a successor while the preceding handler is still blocked") {
    create(2, 3, 1);
    jobs[0].gate_run = true;
    cmeta_threadpool_task_t task = descriptor(0);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[0].entered, 1));
    task = descriptor(1);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[1].finalize, 1));
    check_equal(atomic_load(&jobs[0].finalize), 0);
    check_equal(snapshot().running_tasks, (size_t)1);
    atomic_store(&release_gate, 1);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    verify(0, 2, false);
  }
  it("wakes sleeping leaders and drains sparse and partial batches") {
    for (int workers = 1; workers <= 4; workers *= 2) {
      for (size_t batch = 1; batch <= 32; batch *= 32) {
        create(workers, 5, batch);
        for (size_t n = 0; n < 9; ++n) {
          check_true(await_sleeping((size_t)workers));
          const cmeta_threadpool_task_t task = descriptor(n);
          check_equal(cmeta_threadpool_try_submit_task(pool, &task), SALTS_OK);
          check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        }
        check_true(await_sleeping((size_t)workers));
        for (size_t n = 9; n < 12; ++n) {
          const cmeta_threadpool_task_t task = descriptor(n);
          check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
        }
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        verify(0, 12, false);
        check_equal(snapshot().accepted_tasks, (int64_t)12);
        cmeta_threadpool_destroy(pool);
        pool = NULL;
        for (size_t n = 0; n < 12; ++n) {
          atomic_store(&jobs[n].run, 0); atomic_store(&jobs[n].cancel, 0);
          atomic_store(&jobs[n].finalize, 0); atomic_store(&jobs[n].phase, 0);
        }
      }
    }
  }
  it("bounds FULL, invokes no rejected callback and wakes closed submitters") {
    create(1, 1, 1);
    jobs[0].gate_run = true;
    cmeta_threadpool_task_t task = descriptor(0);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[0].entered, 1));
    task = descriptor(1);
    check_equal(cmeta_threadpool_try_submit_task(pool, &task), SALTS_OK);
    task = descriptor(2);
    check_equal(cmeta_threadpool_try_submit_task(pool, &task), SALTS_ENOBUFS);
    producers[0].first = 2; producers[0].count = 1; producers[0].stride = 1;
    producers[0].blocking = true;
    check_equal(cmeta_thread_create(&producers[0].thread, submit, &producers[0]), 0);
    check_true(await_int(&producers[0].entered, 1));
    check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_OK);
    join(&producers[0].thread);
    check_equal(atomic_load(&producers[0].status), SALTS_ESHUTDOWN);
    check_equal(cmeta_threadpool_try_submit_task(pool, &task), SALTS_ESHUTDOWN);
    atomic_store(&release_gate, 1);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    verify(0, 1, false); verify(1, 1, true);
    check_equal(atomic_load(&jobs[2].phase), 0);
    check_equal(cmeta_threadpool_cancelled(pool), (int64_t)1);
    check_equal(cmeta_threadpool_capacity(pool), (size_t)1);
  }
  it("cancels private batch entries that have not started") {
    create(1, 4, 4);
    jobs[0].gate_run = true;
    cmeta_threadpool_task_t task = descriptor(0);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[0].entered, 1));
    /* Hold the first handler, then hold the first member of the next batch. */
    jobs[1].gate_second = true;
    for (size_t n = 1; n <= 4; ++n) {
      task = descriptor(n);
      check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    }
    atomic_store(&release_gate, 1);
    check_true(await_int(&jobs[1].entered, 1));
    check_equal(snapshot().claimed_tasks, (size_t)3);
    check_equal(snapshot().queued_tasks, (size_t)0);
    check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_OK);
    atomic_store(&release_second, 1);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    const cmeta_threadpool_lf_stats_t stats = snapshot();
    check_equal(stats.accepted_tasks, (int64_t)5);
    check_equal(stats.completed_tasks, (int64_t)2);
    check_equal(stats.cancelled_tasks, (int64_t)3);
    verify(0, 2, false);
    verify(2, 3, true);
  }
  it("resumes blocked admission on space and finalizes cancellation without a cancel callback") {
    create(1, 1, 1);
    jobs[0].gate_run = true;
    cmeta_threadpool_task_t task = descriptor(0);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[0].entered, 1));
    task = descriptor(1);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    producers[0].first = 2; producers[0].count = 1; producers[0].stride = 1;
    producers[0].blocking = true;
    check_equal(cmeta_thread_create(&producers[0].thread, submit, &producers[0]), 0);
    check_true(await_int(&producers[0].entered, 1));
    atomic_store(&release_gate, 1);
    join(&producers[0].thread);
    check_equal(atomic_load(&producers[0].status), SALTS_OK);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    verify(0, 3, false);
    cmeta_threadpool_destroy(pool); pool = NULL;
    create(1, 1, 1);
    atomic_store(&release_second, 0);
    jobs[3].gate_second = true;
    task = descriptor(3);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[3].entered, 1));
    task = descriptor(4);
    task.cancel = NULL;
    task.finalize = self_finalize;
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_OK);
    atomic_store(&release_second, 1);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    check_equal(snapshot().cancelled_tasks, (int64_t)1);
    check_equal(self_finalize_wait, SALTS_EBUSY);
    check_equal(atomic_load(&jobs[4].phase), 0);
  }
  it("linearizes shutdown with competing submitters and settles only admitted descriptors") {
    create(4, 3, 32);
    for (size_t n = 0; n < PRODUCERS; ++n) {
      producers[n].first = n; producers[n].count = JOBS / PRODUCERS;
      producers[n].stride = PRODUCERS;
      check_equal(cmeta_thread_create(&producers[n].thread, submit, &producers[n]), 0);
    }
    for (size_t n = 0; n < PRODUCERS; ++n)
      check_true(await_int(&producers[n].entered, 1));
    check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_OK);
    size_t admitted = 0;
    for (size_t n = 0; n < PRODUCERS; ++n) {
      join(&producers[n].thread);
      const int status = atomic_load(&producers[n].status);
      check_true(status == SALTS_OK || status == SALTS_ESHUTDOWN);
    }
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    for (size_t n = 0; n < JOBS; ++n) {
      const int accepted = atomic_load(&jobs[n].admitted);
      admitted += (size_t)accepted;
      check_equal(atomic_load(&jobs[n].run) + atomic_load(&jobs[n].cancel), accepted);
      check_equal(atomic_load(&jobs[n].finalize), accepted);
    }
    check_equal(snapshot().accepted_tasks, (int64_t)admitted);
    check_equal(atomic_load(&callback_errors), 0);
  }
  it("waits for finalize and rejects self waits in run and finalize") {
    create(1, 3, 1);
    jobs[0].gate_finalize = true;
    const cmeta_threadpool_task_t task = descriptor(0);
    check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
    check_true(await_int(&jobs[0].finalized, 1));
    check_equal(snapshot().finalizing_tasks, (size_t)1);
    check_equal(snapshot().completed_tasks, (int64_t)0);
    check_equal(cmeta_thread_create(&waiter, wait_task, NULL), 0);
    check_true(await_int(&wait_entered, 1));
    check_equal(atomic_load(&wait_done), 0);
    atomic_store(&release_gate, 1);
    join(&waiter);
    check_equal(atomic_load(&wait_done), 1);
    verify(0, 1, false);
    cmeta_threadpool_destroy(pool); pool = NULL;
    create(1, 1, 1);
    const cmeta_threadpool_task_t recursive = {self_block, NULL, self_finalize, NULL};
    check_equal(cmeta_threadpool_submit_task(pool, &recursive), SALTS_OK);
    check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
    check_equal(self_post[0], SALTS_OK);
    check_equal(self_post[1], SALTS_EBUSY);
    check_equal(self_wait, SALTS_EBUSY);
    check_equal(self_finalize_wait, SALTS_EBUSY);
    verify(1, 1, false);
  }
  it("copies descriptors and settles MPSC work exactly once across wraps") {
    for (size_t batch = 1; batch <= 32; batch *= 32) {
      create(4, 3, batch);
      for (size_t n = 0; n < PRODUCERS; ++n) {
        producers[n].first = n; producers[n].count = JOBS / PRODUCERS;
        producers[n].stride = PRODUCERS;
        check_equal(cmeta_thread_create(&producers[n].thread, submit, &producers[n]), 0);
      }
      for (size_t n = 0; n < PRODUCERS; ++n) {
        join(&producers[n].thread);
        check_equal(atomic_load(&producers[n].status), SALTS_OK);
      }
      check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_DRAIN), SALTS_OK);
      check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_DRAIN), SALTS_OK);
      check_equal(cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_EBUSY);
      check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
      verify(0, JOBS, false);
      cmeta_threadpool_stats_t stats;
      cmeta_threadpool_get_stats(pool, &stats);
      check_equal(stats.submitted_tasks, (int64_t)JOBS);
      check_equal(stats.completed_tasks, (int64_t)JOBS);
      check_equal(stats.pending_tasks, (int64_t)0);
      check_less_equal(stats.peak_pending_tasks, (int64_t)(3 + 4 * batch));
      check_equal(cmeta_threadpool_is_accepting(pool), 0);
      cmeta_threadpool_destroy(pool); pool = NULL;
      for (size_t n = 0; n < JOBS; ++n) {
        atomic_store(&jobs[n].run, 0); atomic_store(&jobs[n].cancel, 0);
        atomic_store(&jobs[n].finalize, 0); atomic_store(&jobs[n].phase, 0);
      }
    }
  }
}
