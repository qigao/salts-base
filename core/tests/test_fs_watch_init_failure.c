#include "../src/fs_watch_internal.h"
#include <thread.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>

static size_t watch_init_backend_calls;
static void watch_init_mutex_fail(cmeta_mutex_t *mutex) { *mutex = NULL; }
static int watch_init_backend_open(salts_fs_watch_impl *impl, const char *path,
                                   const salts_fs_watch_config *config) {
  (void)impl; (void)path; (void)config;
  ++watch_init_backend_calls;
  return SALTS_EIO;
}
static void watch_init_event(void *user, const salts_fs_watch_event *event) {
  (void)user; (void)event;
}
#define cmeta_mutex_init watch_init_mutex_fail
#define salts_fs_watch_backend_open watch_init_backend_open
#define salts_fs_watch_backend_set salts_fs_watch_backend_set_with_init_failure
#define salts_fs_watch_backend_get salts_fs_watch_backend_get_with_init_failure
#define salts_fs_watch_close_requested salts_fs_watch_close_requested_with_init_failure
#define salts_fs_watch_backend_mark_done salts_fs_watch_backend_mark_done_with_init_failure
#define salts_fs_watch_publish_loss salts_fs_watch_publish_loss_with_init_failure
#define salts_fs_watch_publish salts_fs_watch_publish_with_init_failure
#define salts_fs_watch_open_notified salts_fs_watch_open_notified_with_init_failure
#define salts_fs_watch_open salts_fs_watch_open_with_init_failure
#define salts_fs_watch_has_ready_or_done salts_fs_watch_has_ready_or_done_with_init_failure
#define salts_fs_watch_backend_done_and_empty salts_fs_watch_backend_done_and_empty_with_init_failure
#define salts_fs_watch_run_ready salts_fs_watch_run_ready_with_init_failure
#define salts_fs_watch_acknowledge_rescan salts_fs_watch_acknowledge_rescan_with_init_failure
#define salts_fs_watch_close salts_fs_watch_close_with_init_failure
#define salts_fs_watch_is_quiescent salts_fs_watch_is_quiescent_with_init_failure
#define salts_fs_watch_get_stats salts_fs_watch_get_stats_with_init_failure
#define salts_fs_watch_destroy salts_fs_watch_destroy_with_init_failure
#include "../src/fs_watch.c"
#undef salts_fs_watch_backend_set
#undef salts_fs_watch_backend_get
#undef salts_fs_watch_close_requested
#undef salts_fs_watch_backend_mark_done
#undef salts_fs_watch_publish_loss
#undef salts_fs_watch_publish
#undef salts_fs_watch_open_notified
#undef salts_fs_watch_open
#undef salts_fs_watch_has_ready_or_done
#undef salts_fs_watch_backend_done_and_empty
#undef salts_fs_watch_run_ready
#undef salts_fs_watch_acknowledge_rescan
#undef salts_fs_watch_close
#undef salts_fs_watch_is_quiescent
#undef salts_fs_watch_get_stats
#undef salts_fs_watch_destroy
#undef salts_fs_watch_backend_open
#undef cmeta_mutex_init

spec("Native filesystem watch initialization failures") {
  it("fails mutex allocation before opening any native watcher") {
    salts_fs_watch watch = {0};
    salts_fs_watch_config config = {.event_capacity = 1u, .watch_capacity = 1u,
        .path_capacity = 32u, .native_buffer_capacity = 1024u, .event = watch_init_event};
    watch_init_backend_calls = 0u;
    check_equal(salts_fs_watch_open_with_init_failure(&watch, ".", &config), SALTS_ENOMEM);
    check_equal(watch_init_backend_calls, (size_t)0u);
    check_null(watch.impl);
  }
}
