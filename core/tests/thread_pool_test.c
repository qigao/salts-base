/**
 * @file test_threadpool.c
 * @brief Thread pool unit tests
 */

#include <threads/threads.h>
#include <threads/thread_pool.h>
#include "tinytest.h"
#include <stdio.h>
#include <atomic/atomic.h>
#include <stdlib.h>

#define UNUSED(x) (void)(x)
#define NUM_TASKS 100

static sync_mutex_t counter_mutex;
static volatile int counter = 0;
static atomic_int gate_open;
static atomic_int mpmc_counter;

typedef struct callback_protocol_ctx {
    threadpool_t *pool;
    atomic_int entered;
    int first_post_status;
    int second_post_status;
    int wait_status;
} callback_protocol_ctx;

typedef struct task_lifecycle_probe {
    atomic_int run_count;
    atomic_int cancel_count;
    atomic_int finalize_count;
    atomic_int finalize_entered;
    atomic_int finalize_gate;
    atomic_int sequence;
    atomic_int run_sequence;
    atomic_int cancel_sequence;
    atomic_int finalize_sequence;
} task_lifecycle_probe;

typedef struct wait_probe {
    threadpool_t *pool;
    atomic_int entered;
    atomic_int done;
    int status;
} wait_probe;

typedef struct {
    threadpool_t *pool;
    int tasks;
    atomic_int *submit_failures;
} submitter_ctx_t;

static void increment_task(void *arg) {
    UNUSED(arg);
    sync_mutex_lock(&counter_mutex);
    counter++;
    sync_mutex_unlock(&counter_mutex);
}

static void slow_task(void *arg) {
    int *result = (int *)arg;
    sleep_ms(10);
    *result = 42;
}

static void sum_task(void *arg) {
    int *val = (int *)arg;
    sync_mutex_lock(&counter_mutex);
    counter += *val;
    sync_mutex_unlock(&counter_mutex);
}

static void gated_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) {
        sleep_ms(1);
    }
}

static void mpmc_count_task(void *arg) {
    UNUSED(arg);
    atomic_fetch_add(&mpmc_counter, 1);
}

static void gated_count_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) sleep_ms(1);
    atomic_fetch_add(&mpmc_counter, 1);
}

static void lifecycle_run(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->run_count, 1);
    atomic_store(&probe->run_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void lifecycle_cancel(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->cancel_count, 1);
    atomic_store(&probe->cancel_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void lifecycle_finalize(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    atomic_fetch_add(&probe->finalize_count, 1);
    atomic_store(&probe->finalize_sequence,
                 atomic_fetch_add(&probe->sequence, 1) + 1);
}

static void blocking_lifecycle_finalize(void *arg) {
    task_lifecycle_probe *probe = (task_lifecycle_probe *)arg;
    lifecycle_finalize(arg);
    atomic_store(&probe->finalize_entered, 1);
    while (!atomic_load(&probe->finalize_gate)) sleep_ms(1);
}

static void wait_for_pool(void *arg) {
    wait_probe *probe = (wait_probe *)arg;
    atomic_store(&probe->entered, 1);
    probe->status = threadpool_wait_status(probe->pool);
    atomic_store(&probe->done, 1);
}

static void callback_protocol_probe(void *arg) {
    callback_protocol_ctx *ctx = (callback_protocol_ctx *)arg;
    atomic_store(&ctx->entered, 1);
    ctx->first_post_status = threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->second_post_status = threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->wait_status = threadpool_wait_status(ctx->pool);
}

static void submitter_thread(void *arg) {
    submitter_ctx_t *ctx = (submitter_ctx_t *)arg;
    for (int i = 0; i < ctx->tasks; ++i) {
        if (threadpool_submit(ctx->pool, mpmc_count_task, NULL) != 0) {
            atomic_fetch_add(ctx->submit_failures, 1);
        }
    }
}

spec("Thread Pool Tests") {
    before_each() {
        sync_mutex_init(&counter_mutex);
        counter = 0;
        atomic_store(&gate_open, 0);
        atomic_store(&mpmc_counter, 0);
    }

    after_each() {
        sync_mutex_destroy(&counter_mutex);
    }

    it("should create and destroy pool") {
        threadpool_t *pool = threadpool_create(2);
        check(pool != NULL);
        check_equal(threadpool_size(pool), 2);
        threadpool_destroy(pool);
    }

    it("should auto-detect CPU cores when 0") {
        threadpool_t *pool = threadpool_create(0);
        check(pool != NULL);
        check(threadpool_size(pool) >= 1);
        printf("  (detected %d cores)\n", threadpool_size(pool));
        threadpool_destroy(pool);
    }

    it("should execute single task") {
        threadpool_t *pool = threadpool_create(2);

        int result = 0;
        threadpool_submit(pool, slow_task, &result);
        threadpool_wait(pool);

        check_equal(result, 42);
        threadpool_destroy(pool);
    }

    it("should execute many tasks") {
        threadpool_t *pool = threadpool_create(4);

        for (int i = 0; i < NUM_TASKS; i++) {
            threadpool_submit(pool, increment_task, NULL);
        }

        threadpool_wait(pool);
        check_equal(counter, NUM_TASKS);

        threadpool_destroy(pool);
    }

    it("should accept tasks from multiple producers") {
        enum { PRODUCERS = 4, TASKS_PER_PRODUCER = 250 };
        threadpool_t *pool = threadpool_create(4);
        sync_thread_t producers[PRODUCERS];
        submitter_ctx_t contexts[PRODUCERS];
        atomic_int submit_failures;

        atomic_store(&submit_failures, 0);
        check(pool != NULL);

        for (int i = 0; i < PRODUCERS; ++i) {
            contexts[i].pool = pool;
            contexts[i].tasks = TASKS_PER_PRODUCER;
            contexts[i].submit_failures = &submit_failures;
            check_equal(sync_thread_create(&producers[i], submitter_thread, &contexts[i]), 0);
        }

        for (int i = 0; i < PRODUCERS; ++i) {
            sync_thread_join(&producers[i]);
        }

        threadpool_wait(pool);
        check_equal(atomic_load(&submit_failures), 0);
        check_equal(atomic_load(&mpmc_counter), PRODUCERS * TASKS_PER_PRODUCER);

        threadpool_destroy(pool);
    }

    it("should handle more tasks than threads") {
        threadpool_t *pool = threadpool_create(2);

        for (int i = 0; i < 50; i++) {
            threadpool_submit(pool, increment_task, NULL);
        }

        threadpool_wait(pool);
        check_equal(counter, 50);

        threadpool_destroy(pool);
    }

    it("should pass arguments correctly") {
        threadpool_t *pool = threadpool_create(4);

        int values[10];
        for (int i = 0; i < 10; i++) {
            values[i] = i + 1;  // 1..10
            threadpool_submit(pool, sum_task, &values[i]);
        }

        threadpool_wait(pool);
        // Sum of 1..10 = 55
        check_equal(counter, 55);

        threadpool_destroy(pool);
    }

    it("should report pending count") {
        threadpool_t *pool = threadpool_create(1);

        // Submit slow tasks
        int results[5] = {0};
        for (int i = 0; i < 5; i++) {
            threadpool_submit(pool, slow_task, &results[i]);
        }

        // Should have pending tasks
        check(threadpool_pending(pool) > 0);

        threadpool_wait(pool);
        check_equal(threadpool_pending(pool), 0);

        threadpool_destroy(pool);
    }

    it("should handle empty wait") {
        threadpool_t *pool = threadpool_create(2);

        // Wait with no tasks should return immediately
        threadpool_wait(pool);
        check_equal(threadpool_pending(pool), 0);

        threadpool_destroy(pool);
    }

    it("should reject tasks after shutdown") {
        threadpool_t *pool = threadpool_create(2);
        threadpool_shutdown(pool);
        check_equal(threadpool_is_accepting(pool), 0);
        check_equal(threadpool_submit(pool, increment_task, NULL),
                    SALTS_ESHUTDOWN);
        threadpool_destroy(pool);

        check(1);
    }

    it("should reject invalid submissions with an exact status") {
        threadpool_t *pool = threadpool_create(1);

        check(pool != NULL);
        check_equal(threadpool_try_submit(NULL, gated_task, NULL),
                    SALTS_EINVAL);
        check_equal(threadpool_try_submit(pool, NULL, NULL),
                    SALTS_EINVAL);

        threadpool_destroy(pool);
    }

    it("should honor configured queue capacity for try_submit") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        threadpool_stats_t stats = {0};
        int accepted = 0;
        int rejected = 0;
        int rejected_status = SALTS_OK;
        int attempts = 0;

        check(pool != NULL);
        check_equal((int)threadpool_capacity(pool), 2);

        while (accepted < 1 && attempts < 200) {
            if (threadpool_try_submit(pool, gated_task, NULL) == 0) {
                accepted++;
                break;
            }
            sleep_ms(1);
            attempts++;
        }
        check_equal(accepted, 1);

        attempts = 0;
        do {
            threadpool_get_stats(pool, &stats);
            if (stats.active_tasks >= 1) {
                break;
            }
            sleep_ms(1);
            attempts++;
        } while (attempts < 200);
        check_equal((int)stats.active_tasks, 1);

        attempts = 0;
        while ((accepted < 3 || rejected < 1) && attempts < 400) {
            int status = threadpool_try_submit(pool, gated_task, NULL);
            if (status == SALTS_OK) {
                accepted++;
            } else {
                rejected++;
                rejected_status = status;
            }
            sleep_ms(1);
            attempts++;
        }

        threadpool_get_stats(pool, &stats);
        check_equal(accepted, 3);
        check(rejected >= 1);
        check_equal(rejected_status, SALTS_ENOBUFS);
        check_equal((int)stats.rejected_tasks, 1);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.queued_tasks, 2);
        check_equal((int)stats.pending_tasks, 3);
        check_equal((int)stats.peak_pending_tasks, 3);

        atomic_store(&gate_open, 1);
        threadpool_wait(pool);
        threadpool_get_stats(pool, &stats);
        check_equal((int)stats.pending_tasks, 0);
        threadpool_destroy(pool);
    }

    it("keeps running tasks outside configured queue capacity") {
        enum { WORKERS = 4 };
        threadpool_config_t config = {
            .num_threads = WORKERS,
            .queue_capacity = 1,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        threadpool_stats_t stats = {0};
        int submit_status[WORKERS] = {0};
        int queued_status;
        int full_status;

        check_not_null(pool);
        for (int i = 0; i < WORKERS; ++i) {
            int attempts = 0;
            submit_status[i] =
                threadpool_try_submit(pool, gated_count_task, NULL);
            if (submit_status[i] != SALTS_OK) continue;
            do {
                threadpool_get_stats(pool, &stats);
                if (stats.active_tasks >= i + 1) break;
                sleep_ms(1);
            } while (++attempts < 200);
        }

        queued_status =
            threadpool_try_submit(pool, gated_count_task, NULL);
        full_status =
            threadpool_try_submit(pool, gated_count_task, NULL);
        threadpool_get_stats(pool, &stats);
        atomic_store(&gate_open, 1);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        threadpool_destroy(pool);

        for (int i = 0; i < WORKERS; ++i)
            check_equal(submit_status[i], SALTS_OK);
        check_equal((int)stats.active_tasks, WORKERS);
        check_equal((int)stats.queued_tasks, 1);
        check_equal(queued_status, SALTS_OK);
        check_equal(full_status, SALTS_ENOBUFS);
        check_equal(atomic_load(&mpmc_counter), WORKERS + 1);
    }

    it("fails callback self-blocking operations without deadlock") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        callback_protocol_ctx ctx = {
            .pool = pool,
            .first_post_status = SALTS_UNKNOWN,
            .second_post_status = SALTS_UNKNOWN,
            .wait_status = SALTS_UNKNOWN,
        };

        check_not_null(pool);
        atomic_init(&ctx.entered, 0);
        check_equal(threadpool_submit(
                        pool, callback_protocol_probe, &ctx), SALTS_OK);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&ctx.entered), 1);
        check_equal(ctx.first_post_status, SALTS_OK);
        check_equal(ctx.second_post_status, SALTS_EBUSY);
        check_equal(ctx.wait_status, SALTS_EBUSY);
        check_equal(atomic_load(&mpmc_counter), 1);
        threadpool_destroy(pool);
    }

    it("cancels queued callbacks while allowing running work to finish") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(threadpool_submit(pool, gated_count_task, NULL),
                    SALTS_OK);
        do {
            threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(threadpool_submit(pool, mpmc_count_task, NULL),
                    SALTS_OK);
        check_equal(threadpool_submit(pool, mpmc_count_task, NULL),
                    SALTS_OK);

        check_equal(threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&mpmc_counter), 1);
        check_equal(threadpool_cancelled(pool), (int64_t)2);
        check_equal(threadpool_pending(pool), 0);
        threadpool_destroy(pool);
    }

    it("runs and finalizes an accepted task exactly once") {
        threadpool_t *pool = threadpool_create(1);
        task_lifecycle_probe probe = {0};
        threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };

        check_not_null(pool);
        check_equal(threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&probe.run_count), 1);
        check_equal(atomic_load(&probe.cancel_count), 0);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.run_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        threadpool_destroy(pool);
    }

    it("keeps a running descriptor pending until finalize returns") {
        threadpool_t *pool = threadpool_create(1);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = SALTS_UNKNOWN,
        };
        threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        threadpool_stats_t stats = {0};
        sync_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(threadpool_submit_task(pool, &task), SALTS_OK);
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.pending_tasks, 1);

        check_equal(sync_thread_create(&wait_thread, wait_for_pool, &waiter),
                    SALTS_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        sync_thread_join(&wait_thread);
        threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, SALTS_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(atomic_load(&lifecycle.run_count), 1);
        check_equal(atomic_load(&lifecycle.finalize_count), 1);
        threadpool_destroy(pool);
    }

    it("cancels and finalizes queued task exactly once") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        task_lifecycle_probe probe = {0};
        threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };
        threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&probe.run_count), 0);
        check_equal(atomic_load(&probe.cancel_count), 1);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.cancel_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        threadpool_destroy(pool);
    }

    it("keeps a cancelling descriptor pending until finalize returns") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = SALTS_UNKNOWN,
        };
        threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        threadpool_stats_t stats = {0};
        sync_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);

        atomic_store(&gate_open, 1);
        attempts = 0;
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 1);
        check_equal(threadpool_cancelled(pool), (int64_t)0);

        check_equal(sync_thread_create(&wait_thread, wait_for_pool, &waiter),
                    SALTS_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        sync_thread_join(&wait_thread);
        threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, SALTS_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(threadpool_cancelled(pool), (int64_t)1);
        threadpool_destroy(pool);
    }

    it("does not invoke descriptor callbacks after full rejection") {
        threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        threadpool_t *pool = threadpool_create_with_config(&config);
        task_lifecycle_probe accepted_probe = {0};
        task_lifecycle_probe rejected_probe = {0};
        threadpool_task_t accepted = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &accepted_probe,
        };
        threadpool_task_t rejected = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &rejected_probe,
        };
        threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(threadpool_submit_task(pool, &accepted), SALTS_OK);
        check_equal(threadpool_try_submit_task(pool, &rejected),
                    SALTS_ENOBUFS);
        check_equal(atomic_load(&rejected_probe.run_count), 0);
        check_equal(atomic_load(&rejected_probe.cancel_count), 0);
        check_equal(atomic_load(&rejected_probe.finalize_count), 0);

        check_equal(threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&accepted_probe.cancel_count), 1);
        check_equal(atomic_load(&accepted_probe.finalize_count), 1);
        check_equal(atomic_load(&rejected_probe.sequence), 0);
        threadpool_destroy(pool);
    }
}

/* Query through Core's public ABI in both native pool implementations. */
typedef struct pool_identity_probe {
    threadpool_t *pool;
    threadpool_t *other;
    atomic_uint observed;
} pool_identity_probe;
static threadpool_t *identity_pools[2];
static pool_identity_probe identity_probe;
enum { IDENTITY_RUN = 1, IDENTITY_CANCEL = 2, IDENTITY_FINALIZE = 4,
       IDENTITY_WRONG = 8, IDENTITY_WAIT_ATTEMPTS = 2000 };
static void identity_record(unsigned phase) {
    if (threadpool_is_current(identity_probe.pool) != 1 ||
        threadpool_is_current(identity_probe.other) != 0 ||
        threadpool_is_current(NULL) != 0)
        phase |= IDENTITY_WRONG;
    atomic_fetch_or(&identity_probe.observed, phase);
}
static void identity_run(void *arg) { (void)arg; identity_record(IDENTITY_RUN); }
static void identity_cancel(void *arg) { (void)arg; identity_record(IDENTITY_CANCEL); }
static void identity_finalize(void *arg) { (void)arg; identity_record(IDENTITY_FINALIZE); }
static void identity_cleanup(void) {
    atomic_store(&gate_open, 1);
    for (unsigned i = 0; i < 2; ++i) {
        if (identity_pools[i]) threadpool_destroy(identity_pools[i]);
        identity_pools[i] = NULL;
    }
}
static void identity_case(int leader_followers, int cancel) {
    const threadpool_config_t ordinary = {1, 2};
    const threadpool_lf_config_t lf = {sizeof(lf), SALTS_THREADPOOL_LF_VERSION, 1, 2, 1};
    const threadpool_task_t task = {identity_run, identity_cancel, identity_finalize, NULL};
    if (leader_followers)
        check_equal(threadpool_create_leader_followers(&lf, &identity_pools[0]), SALTS_OK);
    else
        identity_pools[0] = threadpool_create_with_config(&ordinary);
    identity_pools[1] = threadpool_create_with_config(&ordinary);
    check_not_null(identity_pools[0]);
    check_not_null(identity_pools[1]);
    identity_probe.pool = identity_pools[0];
    identity_probe.other = identity_pools[1];
    atomic_store(&identity_probe.observed, 0);
    atomic_store(&gate_open, 0);
    check_equal(threadpool_is_current(NULL), 0);
    check_equal(threadpool_is_current(identity_pools[0]), 0);
    check_equal(threadpool_is_current(identity_pools[1]), 0);
    if (cancel) {
        threadpool_stats_t stats = {0};
        unsigned attempts = 0;
        check_equal(threadpool_submit(identity_pools[0], gated_task, NULL), SALTS_OK);
        do {
            threadpool_get_stats(identity_pools[0], &stats);
            if (stats.active_tasks == 1) break;
            sleep_ms(1);
        } while (++attempts < IDENTITY_WAIT_ATTEMPTS);
        check_equal(stats.active_tasks, (int64_t)1);
    }
    check_equal(threadpool_submit_task(identity_pools[0], &task), SALTS_OK);
    if (cancel) {
        check_equal(threadpool_shutdown_with_policy(identity_pools[0],
            SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING), SALTS_OK);
        check_equal(threadpool_is_current(identity_pools[0]), 0);
        atomic_store(&gate_open, 1);
    }
    check_equal(threadpool_wait_status(identity_pools[0]), SALTS_OK);
    check_equal(atomic_load(&identity_probe.observed),
        (unsigned)((cancel ? IDENTITY_CANCEL : IDENTITY_RUN) | IDENTITY_FINALIZE));
    check_equal(threadpool_is_current(identity_pools[0]), 0);
    check_equal(threadpool_is_current(identity_pools[1]), 0);
    identity_cleanup();
}
spec("Thread pool callback identity") {
    after_each() { identity_cleanup(); }
    it("reports ordinary run and finalize without retaining callback identity") {
        identity_case(0, 0);
    }
    it("reports ordinary cancellation and finalize without retaining callback identity") {
        identity_case(0, 1);
    }
    it("reports leader/followers run and finalize through the same owner") {
        identity_case(1, 0);
    }
    it("reports leader/followers cancellation and finalize through the same owner") {
        identity_case(1, 1);
    }
}
