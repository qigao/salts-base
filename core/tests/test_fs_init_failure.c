#include <fs_async.h>

#include <thread_pool.h>
#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include <thread.h>

static size_t fs_init_failure_executor_calls;

static void fs_init_failure_completion(void *user, uint64_t request_id,
                                       salts_fs_operation_kind operation, int result) {
  (void)user;
  (void)request_id;
  (void)operation;
  (void)result;
}

static void fs_init_failure_mutex_init(cmeta_mutex_t *mutex) {
  if (mutex != NULL) *mutex = NULL;
}

static cmeta_threadpool_t *fs_init_failure_executor_init(const cmeta_threadpool_config_t *config) {
  (void)config;
  ++fs_init_failure_executor_calls;
  return NULL;
}

#define cmeta_mutex_init fs_init_failure_mutex_init
#define cmeta_threadpool_create_with_config fs_init_failure_executor_init
#define salts_fs_service_init salts_fs_service_init_with_init_failure
#define salts_fs_try_stat salts_fs_try_stat_with_init_failure
#define salts_fs_try_lstat salts_fs_try_lstat_with_init_failure
#define salts_fs_try_read_directory salts_fs_try_read_directory_with_init_failure
#define salts_fs_try_mkdir salts_fs_try_mkdir_with_init_failure
#define salts_fs_try_rmdir salts_fs_try_rmdir_with_init_failure
#define salts_fs_try_rename salts_fs_try_rename_with_init_failure
#define salts_fs_try_unlink salts_fs_try_unlink_with_init_failure
#define salts_fs_try_cancel salts_fs_try_cancel_with_init_failure
#define salts_fs_run_ready salts_fs_run_ready_with_init_failure
#define salts_fs_service_close salts_fs_service_close_with_init_failure
#define salts_fs_is_quiescent salts_fs_is_quiescent_with_init_failure
#define salts_fs_get_stats salts_fs_get_stats_with_init_failure
#define salts_fs_destroy salts_fs_destroy_with_init_failure
#include "../src/fs_async.c"
#undef salts_fs_destroy
#undef salts_fs_get_stats
#undef salts_fs_is_quiescent
#undef salts_fs_service_close
#undef salts_fs_run_ready
#undef salts_fs_try_cancel
#undef salts_fs_try_unlink
#undef salts_fs_try_rename
#undef salts_fs_try_rmdir
#undef salts_fs_try_mkdir
#undef salts_fs_try_read_directory
#undef salts_fs_try_lstat
#undef salts_fs_try_stat
#undef salts_fs_service_init
#undef cmeta_threadpool_create_with_config
#undef cmeta_mutex_init

spec("Native filesystem service initialization failures") {
  it("returns ENOMEM before executor initialization when its mutex is unavailable") {
    salts_fs_service service = {0};
    const salts_fs_config config = {.worker_count = 1u,
                                    .request_capacity = 1u,
                                    .path_capacity = 16u,
                                    .completion = fs_init_failure_completion};

    fs_init_failure_executor_calls = 0u;
    check_equal(salts_fs_service_init_with_init_failure(&service, &config), SALTS_ENOMEM);
    check_equal(fs_init_failure_executor_calls, (size_t)0u);
    check_null(service.impl);
  }
}
