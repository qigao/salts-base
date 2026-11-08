#include <thread_pool.h>

typedef struct input {
  uint64_t value, result;
  int finalized;
} input;
static void compute(void *arg) {
  input *job = (input *)arg;
  job->result = job->value ^ UINT64_C(0x9e3779b97f4a7c15);
}
static void release_input(void *arg) { ++((input *)arg)->finalized; }

int main(void) {
  const cmeta_threadpool_lf_config_t config = {
      sizeof(config), SALTS_THREADPOOL_LF_VERSION, 4, 3, 32};
  cmeta_threadpool_t *pool = NULL;
  int result = cmeta_threadpool_create_leader_followers(&config, &pool);
  if (result != SALTS_OK) return 1;
  input jobs[24] = {0};
  for (size_t n = 0; n < 24; ++n) {
    jobs[n].value = (uint64_t)n + 1;
    const cmeta_threadpool_task_t task = {compute, NULL, release_input, &jobs[n]};
    if (cmeta_threadpool_submit_task(pool, &task) != SALTS_OK) result = 2;
  }
  if (cmeta_threadpool_shutdown_with_policy(pool, SALTS_THREADPOOL_SHUTDOWN_DRAIN) != SALTS_OK ||
      cmeta_threadpool_wait_status(pool) != SALTS_OK) result = 3;
  cmeta_threadpool_lf_stats_t stats = {0};
  stats.struct_size = sizeof(stats);
  stats.version = SALTS_THREADPOOL_LF_VERSION;
  if (cmeta_threadpool_get_lf_stats(pool, &stats) != SALTS_OK ||
      stats.accepted_tasks != 24 || stats.completed_tasks != 24) result = 4;
  for (size_t n = 0; n < 24; ++n)
    if (jobs[n].finalized != 1 ||
        jobs[n].result != (jobs[n].value ^ UINT64_C(0x9e3779b97f4a7c15))) result = 5;
  cmeta_threadpool_destroy(pool);
  return result;
}
