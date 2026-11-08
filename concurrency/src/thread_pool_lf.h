#ifndef SALTS_THREAD_POOL_LF_INTERNAL_H
#define SALTS_THREAD_POOL_LF_INTERNAL_H

#include <thread_pool.h>

typedef struct cmeta_threadpool_lf cmeta_threadpool_lf;
int cmeta_threadpool_lf_create(const cmeta_threadpool_lf_config_t *config,
    cmeta_threadpool_t *owner, cmeta_threadpool_lf **out);
void cmeta_threadpool_lf_destroy(cmeta_threadpool_lf *state);
int cmeta_threadpool_lf_submit(cmeta_threadpool_lf *state,
    const cmeta_threadpool_task_t *task, int blocking, int is_current);
int cmeta_threadpool_lf_shutdown(cmeta_threadpool_lf *state,
    cmeta_threadpool_shutdown_policy_t policy);
int cmeta_threadpool_lf_wait(cmeta_threadpool_lf *state);
void cmeta_threadpool_lf_stats(cmeta_threadpool_lf *state,
    cmeta_threadpool_stats_t *stats, cmeta_threadpool_lf_stats_t *lf_stats);
void cmeta_threadpool_call_internal(cmeta_threadpool_t *owner,
    cmeta_task_fn fn, void *arg);

#endif
