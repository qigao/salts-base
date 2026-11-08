/**
 * @file test_threadpool.c
 * @brief Thread pool unit tests
 */

#include <thread.h>
#include <thread_pool.h>
#include "tinytest.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

#define UNUSED(x) (void)(x)
#define NUM_TASKS 100

static cmeta_mutex_t counter_mutex;
static volatile int counter = 0;
static atomic_int gate_open;
static atomic_int mpmc_counter;

typedef struct callback_protocol_ctx {
    cmeta_threadpool_t *pool;
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
    cmeta_threadpool_t *pool;
    atomic_int entered;
    atomic_int done;
    int status;
} wait_probe;

typedef struct {
    cmeta_threadpool_t *pool;
    int tasks;
    atomic_int *submit_failures;
} submitter_ctx_t;

static void increment_task(void *arg) {
    UNUSED(arg);
    cmeta_mutex_lock(&counter_mutex);
    counter++;
    cmeta_mutex_unlock(&counter_mutex);
}

static void slow_task(void *arg) {
    int *result = (int *)arg;
    cmeta_sleep_ms(10);
    *result = 42;
}

static void sum_task(void *arg) {
    int *val = (int *)arg;
    cmeta_mutex_lock(&counter_mutex);
    counter += *val;
    cmeta_mutex_unlock(&counter_mutex);
}

static void gated_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) {
        cmeta_sleep_ms(1);
    }
}

static void mpmc_count_task(void *arg) {
    UNUSED(arg);
    atomic_fetch_add(&mpmc_counter, 1);
}

static void gated_count_task(void *arg) {
    UNUSED(arg);
    while (!atomic_load(&gate_open)) cmeta_sleep_ms(1);
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
    while (!atomic_load(&probe->finalize_gate)) cmeta_sleep_ms(1);
}

static void wait_for_pool(void *arg) {
    wait_probe *probe = (wait_probe *)arg;
    atomic_store(&probe->entered, 1);
    probe->status = cmeta_threadpool_wait_status(probe->pool);
    atomic_store(&probe->done, 1);
}

static void callback_protocol_probe(void *arg) {
    callback_protocol_ctx *ctx = (callback_protocol_ctx *)arg;
    atomic_store(&ctx->entered, 1);
    ctx->first_post_status = cmeta_threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->second_post_status = cmeta_threadpool_submit(
        ctx->pool, mpmc_count_task, NULL);
    ctx->wait_status = cmeta_threadpool_wait_status(ctx->pool);
}

static void submitter_thread(void *arg) {
    submitter_ctx_t *ctx = (submitter_ctx_t *)arg;
    for (int i = 0; i < ctx->tasks; ++i) {
        if (cmeta_threadpool_submit(ctx->pool, mpmc_count_task, NULL) != 0) {
            atomic_fetch_add(ctx->submit_failures, 1);
        }
    }
}

spec("Thread Pool Tests") {
    before_each() {
        cmeta_mutex_init(&counter_mutex);
        counter = 0;
        atomic_store(&gate_open, 0);
        atomic_store(&mpmc_counter, 0);
    }

    after_each() {
        cmeta_mutex_destroy(&counter_mutex);
    }

    it("should create and destroy pool") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(2);
        check(pool != NULL);
        check_equal(cmeta_threadpool_size(pool), 2);
        cmeta_threadpool_destroy(pool);
    }

    it("should auto-detect CPU cores when 0") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(0);
        check(pool != NULL);
        check(cmeta_threadpool_size(pool) >= 1);
        printf("  (detected %d cores)\n", cmeta_threadpool_size(pool));
        cmeta_threadpool_destroy(pool);
    }

    it("should execute single task") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(2);

        int result = 0;
        cmeta_threadpool_submit(pool, slow_task, &result);
        cmeta_threadpool_wait(pool);

        check_equal(result, 42);
        cmeta_threadpool_destroy(pool);
    }

    it("should execute many tasks") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(4);

        for (int i = 0; i < NUM_TASKS; i++) {
            cmeta_threadpool_submit(pool, increment_task, NULL);
        }

        cmeta_threadpool_wait(pool);
        check_equal(counter, NUM_TASKS);

        cmeta_threadpool_destroy(pool);
    }

    it("should accept tasks from multiple producers") {
        enum { PRODUCERS = 4, TASKS_PER_PRODUCER = 250 };
        cmeta_threadpool_t *pool = cmeta_threadpool_create(4);
        cmeta_thread_t producers[PRODUCERS];
        submitter_ctx_t contexts[PRODUCERS];
        atomic_int submit_failures;

        atomic_store(&submit_failures, 0);
        check(pool != NULL);

        for (int i = 0; i < PRODUCERS; ++i) {
            contexts[i].pool = pool;
            contexts[i].tasks = TASKS_PER_PRODUCER;
            contexts[i].submit_failures = &submit_failures;
            check_equal(cmeta_thread_create(&producers[i], submitter_thread, &contexts[i]), 0);
        }

        for (int i = 0; i < PRODUCERS; ++i) {
            cmeta_thread_join(&producers[i]);
        }

        cmeta_threadpool_wait(pool);
        check_equal(atomic_load(&submit_failures), 0);
        check_equal(atomic_load(&mpmc_counter), PRODUCERS * TASKS_PER_PRODUCER);

        cmeta_threadpool_destroy(pool);
    }

    it("should handle more tasks than threads") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(2);

        for (int i = 0; i < 50; i++) {
            cmeta_threadpool_submit(pool, increment_task, NULL);
        }

        cmeta_threadpool_wait(pool);
        check_equal(counter, 50);

        cmeta_threadpool_destroy(pool);
    }

    it("should pass arguments correctly") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(4);

        int values[10];
        for (int i = 0; i < 10; i++) {
            values[i] = i + 1;  // 1..10
            cmeta_threadpool_submit(pool, sum_task, &values[i]);
        }

        cmeta_threadpool_wait(pool);
        // Sum of 1..10 = 55
        check_equal(counter, 55);

        cmeta_threadpool_destroy(pool);
    }

    it("should report pending count") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(1);

        // Submit slow tasks
        int results[5] = {0};
        for (int i = 0; i < 5; i++) {
            cmeta_threadpool_submit(pool, slow_task, &results[i]);
        }

        // Should have pending tasks
        check(cmeta_threadpool_pending(pool) > 0);

        cmeta_threadpool_wait(pool);
        check_equal(cmeta_threadpool_pending(pool), 0);

        cmeta_threadpool_destroy(pool);
    }

    it("should handle empty wait") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(2);

        // Wait with no tasks should return immediately
        cmeta_threadpool_wait(pool);
        check_equal(cmeta_threadpool_pending(pool), 0);

        cmeta_threadpool_destroy(pool);
    }

    it("should reject tasks after shutdown") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(2);
        cmeta_threadpool_shutdown(pool);
        check_equal(cmeta_threadpool_is_accepting(pool), 0);
        check_equal(cmeta_threadpool_submit(pool, increment_task, NULL),
                    SALTS_ESHUTDOWN);
        cmeta_threadpool_destroy(pool);

        check(1);
    }

    it("should reject invalid submissions with an exact status") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(1);

        check(pool != NULL);
        check_equal(cmeta_threadpool_try_submit(NULL, gated_task, NULL),
                    SALTS_EINVAL);
        check_equal(cmeta_threadpool_try_submit(pool, NULL, NULL),
                    SALTS_EINVAL);

        cmeta_threadpool_destroy(pool);
    }

    it("should honor configured queue capacity for try_submit") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        cmeta_threadpool_stats_t stats = {0};
        int accepted = 0;
        int rejected = 0;
        int rejected_status = SALTS_OK;
        int attempts = 0;

        check(pool != NULL);
        check_equal((int)cmeta_threadpool_capacity(pool), 2);

        while (accepted < 1 && attempts < 200) {
            if (cmeta_threadpool_try_submit(pool, gated_task, NULL) == 0) {
                accepted++;
                break;
            }
            cmeta_sleep_ms(1);
            attempts++;
        }
        check_equal(accepted, 1);

        attempts = 0;
        do {
            cmeta_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks >= 1) {
                break;
            }
            cmeta_sleep_ms(1);
            attempts++;
        } while (attempts < 200);
        check_equal((int)stats.active_tasks, 1);

        attempts = 0;
        while ((accepted < 3 || rejected < 1) && attempts < 400) {
            int status = cmeta_threadpool_try_submit(pool, gated_task, NULL);
            if (status == SALTS_OK) {
                accepted++;
            } else {
                rejected++;
                rejected_status = status;
            }
            cmeta_sleep_ms(1);
            attempts++;
        }

        cmeta_threadpool_get_stats(pool, &stats);
        check_equal(accepted, 3);
        check(rejected >= 1);
        check_equal(rejected_status, SALTS_ENOBUFS);
        check_equal((int)stats.rejected_tasks, 1);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.queued_tasks, 2);
        check_equal((int)stats.pending_tasks, 3);
        check_equal((int)stats.peak_pending_tasks, 3);

        atomic_store(&gate_open, 1);
        cmeta_threadpool_wait(pool);
        cmeta_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.pending_tasks, 0);
        cmeta_threadpool_destroy(pool);
    }

    it("keeps running tasks outside configured queue capacity") {
        enum { WORKERS = 4 };
        cmeta_threadpool_config_t config = {
            .num_threads = WORKERS,
            .queue_capacity = 1,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        cmeta_threadpool_stats_t stats = {0};
        int submit_status[WORKERS] = {0};
        int queued_status;
        int full_status;

        check_not_null(pool);
        for (int i = 0; i < WORKERS; ++i) {
            int attempts = 0;
            submit_status[i] =
                cmeta_threadpool_try_submit(pool, gated_count_task, NULL);
            if (submit_status[i] != SALTS_OK) continue;
            do {
                cmeta_threadpool_get_stats(pool, &stats);
                if (stats.active_tasks >= i + 1) break;
                cmeta_sleep_ms(1);
            } while (++attempts < 200);
        }

        queued_status =
            cmeta_threadpool_try_submit(pool, gated_count_task, NULL);
        full_status =
            cmeta_threadpool_try_submit(pool, gated_count_task, NULL);
        cmeta_threadpool_get_stats(pool, &stats);
        atomic_store(&gate_open, 1);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        cmeta_threadpool_destroy(pool);

        for (int i = 0; i < WORKERS; ++i)
            check_equal(submit_status[i], SALTS_OK);
        check_equal((int)stats.active_tasks, WORKERS);
        check_equal((int)stats.queued_tasks, 1);
        check_equal(queued_status, SALTS_OK);
        check_equal(full_status, SALTS_ENOBUFS);
        check_equal(atomic_load(&mpmc_counter), WORKERS + 1);
    }

    it("fails callback self-blocking operations without deadlock") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        callback_protocol_ctx ctx = {
            .pool = pool,
            .first_post_status = SALTS_UNKNOWN,
            .second_post_status = SALTS_UNKNOWN,
            .wait_status = SALTS_UNKNOWN,
        };

        check_not_null(pool);
        atomic_init(&ctx.entered, 0);
        check_equal(cmeta_threadpool_submit(
                        pool, callback_protocol_probe, &ctx), SALTS_OK);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&ctx.entered), 1);
        check_equal(ctx.first_post_status, SALTS_OK);
        check_equal(ctx.second_post_status, SALTS_EBUSY);
        check_equal(ctx.wait_status, SALTS_EBUSY);
        check_equal(atomic_load(&mpmc_counter), 1);
        cmeta_threadpool_destroy(pool);
    }

    it("cancels queued callbacks while allowing running work to finish") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 2,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        cmeta_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit(pool, gated_count_task, NULL),
                    SALTS_OK);
        do {
            cmeta_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            cmeta_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(cmeta_threadpool_submit(pool, mpmc_count_task, NULL),
                    SALTS_OK);
        check_equal(cmeta_threadpool_submit(pool, mpmc_count_task, NULL),
                    SALTS_OK);

        check_equal(cmeta_threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&mpmc_counter), 1);
        check_equal(cmeta_threadpool_cancelled(pool), (int64_t)2);
        check_equal(cmeta_threadpool_pending(pool), 0);
        cmeta_threadpool_destroy(pool);
    }

    it("runs and finalizes an accepted task exactly once") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(1);
        task_lifecycle_probe probe = {0};
        cmeta_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&probe.run_count), 1);
        check_equal(atomic_load(&probe.cancel_count), 0);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.run_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        cmeta_threadpool_destroy(pool);
    }

    it("keeps a running descriptor pending until finalize returns") {
        cmeta_threadpool_t *pool = cmeta_threadpool_create(1);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = SALTS_UNKNOWN,
        };
        cmeta_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        cmeta_threadpool_stats_t stats = {0};
        cmeta_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            cmeta_sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        cmeta_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.active_tasks, 1);
        check_equal((int)stats.pending_tasks, 1);

        check_equal(cmeta_thread_create(&wait_thread, wait_for_pool, &waiter),
                    SALTS_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            cmeta_sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        cmeta_sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        cmeta_thread_join(&wait_thread);
        cmeta_threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, SALTS_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(atomic_load(&lifecycle.run_count), 1);
        check_equal(atomic_load(&lifecycle.finalize_count), 1);
        cmeta_threadpool_destroy(pool);
    }

    it("cancels and finalizes queued task exactly once") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        task_lifecycle_probe probe = {0};
        cmeta_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &probe,
        };
        cmeta_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            cmeta_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            cmeta_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(cmeta_threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&probe.run_count), 0);
        check_equal(atomic_load(&probe.cancel_count), 1);
        check_equal(atomic_load(&probe.finalize_count), 1);
        check_equal(atomic_load(&probe.cancel_sequence), 1);
        check_equal(atomic_load(&probe.finalize_sequence), 2);
        cmeta_threadpool_destroy(pool);
    }

    it("keeps a cancelling descriptor pending until finalize returns") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        task_lifecycle_probe lifecycle = {0};
        wait_probe waiter = {
            .pool = pool,
            .status = SALTS_UNKNOWN,
        };
        cmeta_threadpool_task_t task = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = blocking_lifecycle_finalize,
            .arg = &lifecycle,
        };
        cmeta_threadpool_stats_t stats = {0};
        cmeta_thread_t wait_thread;
        int attempts = 0;

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            cmeta_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            cmeta_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(cmeta_threadpool_submit_task(pool, &task), SALTS_OK);
        check_equal(cmeta_threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);

        atomic_store(&gate_open, 1);
        attempts = 0;
        while (!atomic_load(&lifecycle.finalize_entered) && ++attempts < 200)
            cmeta_sleep_ms(1);
        check_equal(atomic_load(&lifecycle.finalize_entered), 1);

        cmeta_threadpool_get_stats(pool, &stats);
        check_equal((int)stats.queued_tasks, 1);
        check_equal((int)stats.active_tasks, 0);
        check_equal((int)stats.pending_tasks, 1);
        check_equal(cmeta_threadpool_cancelled(pool), (int64_t)0);

        check_equal(cmeta_thread_create(&wait_thread, wait_for_pool, &waiter),
                    SALTS_OK);
        attempts = 0;
        while (!atomic_load(&waiter.entered) && ++attempts < 200)
            cmeta_sleep_ms(1);
        check_equal(atomic_load(&waiter.entered), 1);
        cmeta_sleep_ms(10);
        check_equal(atomic_load(&waiter.done), 0);

        atomic_store(&lifecycle.finalize_gate, 1);
        cmeta_thread_join(&wait_thread);
        cmeta_threadpool_get_stats(pool, &stats);
        check_equal(waiter.status, SALTS_OK);
        check_equal(atomic_load(&waiter.done), 1);
        check_equal((int)stats.queued_tasks, 0);
        check_equal((int)stats.pending_tasks, 0);
        check_equal(cmeta_threadpool_cancelled(pool), (int64_t)1);
        cmeta_threadpool_destroy(pool);
    }

    it("does not invoke descriptor callbacks after full rejection") {
        cmeta_threadpool_config_t config = {
            .num_threads = 1,
            .queue_capacity = 1,
        };
        cmeta_threadpool_t *pool = cmeta_threadpool_create_with_config(&config);
        task_lifecycle_probe accepted_probe = {0};
        task_lifecycle_probe rejected_probe = {0};
        cmeta_threadpool_task_t accepted = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &accepted_probe,
        };
        cmeta_threadpool_task_t rejected = {
            .run = lifecycle_run,
            .cancel = lifecycle_cancel,
            .finalize = lifecycle_finalize,
            .arg = &rejected_probe,
        };
        cmeta_threadpool_stats_t stats = {0};
        int attempts = 0;

        check_not_null(pool);
        check_equal(cmeta_threadpool_submit(pool, gated_task, NULL), SALTS_OK);
        do {
            cmeta_threadpool_get_stats(pool, &stats);
            if (stats.active_tasks == 1) break;
            cmeta_sleep_ms(1);
        } while (++attempts < 200);
        check_equal((int)stats.active_tasks, 1);
        check_equal(cmeta_threadpool_submit_task(pool, &accepted), SALTS_OK);
        check_equal(cmeta_threadpool_try_submit_task(pool, &rejected),
                    SALTS_ENOBUFS);
        check_equal(atomic_load(&rejected_probe.run_count), 0);
        check_equal(atomic_load(&rejected_probe.cancel_count), 0);
        check_equal(atomic_load(&rejected_probe.finalize_count), 0);

        check_equal(cmeta_threadpool_shutdown_with_policy(
                        pool, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING),
                    SALTS_OK);
        atomic_store(&gate_open, 1);
        check_equal(cmeta_threadpool_wait_status(pool), SALTS_OK);
        check_equal(atomic_load(&accepted_probe.cancel_count), 1);
        check_equal(atomic_load(&accepted_probe.finalize_count), 1);
        check_equal(atomic_load(&rejected_probe.sequence), 0);
        cmeta_threadpool_destroy(pool);
    }
}
