#ifndef SALTS_THREAD_POOL_LF_INTERNAL_H
#define SALTS_THREAD_POOL_LF_INTERNAL_H

#include <threads/thread_pool.h>

typedef struct threadpool_lf threadpool_lf;
int threadpool_lf_create(const threadpool_lf_config_t *config,
    threadpool_t *owner, threadpool_lf **out);
void threadpool_lf_destroy(threadpool_lf *state);
int threadpool_lf_submit(threadpool_lf *state,
    const threadpool_task_t *task, int blocking, int is_current);
int threadpool_lf_shutdown(threadpool_lf *state,
    threadpool_shutdown_policy_t policy);
int threadpool_lf_wait(threadpool_lf *state);
void threadpool_lf_stats(threadpool_lf *state,
    threadpool_stats_t *stats, threadpool_lf_stats_t *lf_stats);
void threadpool_call_internal(threadpool_t *owner,
    task_fn fn, void *arg);

#endif
