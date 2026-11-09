#include "fs_watch_internal.h"
#include <threads/threads.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>

static size_t watch_init_backend_calls;
static void watch_init_mutex_fail(sync_mutex_t *mutex) { *mutex = NULL; }
static int watch_init_backend_open(fs_watch_impl *impl, const char *path,
                                   const fs_watch_config *config) {
  (void)impl; (void)path; (void)config;
  ++watch_init_backend_calls;
  return SALTS_EIO;
}
static void watch_init_backend_close(fs_watch_impl *impl) {
  (void)impl;
  ++watch_init_backend_calls;
}
static int watch_init_backend_destroy(fs_watch_impl *impl) {
  (void)impl;
  ++watch_init_backend_calls;
  return SALTS_EIO;
}
static void watch_init_event(void *user, const fs_watch_event *event) {
  (void)user; (void)event;
}
#define sync_mutex_init watch_init_mutex_fail
#define fs_watch_backend_open watch_init_backend_open
#define fs_watch_backend_request_close watch_init_backend_close
#define fs_watch_backend_destroy watch_init_backend_destroy
#define fs_watch_backend_set fs_watch_backend_set_with_init_failure
#define fs_watch_backend_get fs_watch_backend_get_with_init_failure
#define fs_watch_close_requested fs_watch_close_requested_with_init_failure
#define fs_watch_backend_mark_done fs_watch_backend_mark_done_with_init_failure
#define fs_watch_publish_loss fs_watch_publish_loss_with_init_failure
#define fs_watch_publish fs_watch_publish_with_init_failure
#define fs_watch_open_notified fs_watch_open_notified_with_init_failure
#define fs_watch_open fs_watch_open_with_init_failure
#define fs_watch_has_ready_or_done fs_watch_has_ready_or_done_with_init_failure
#define fs_watch_backend_done_and_empty fs_watch_backend_done_and_empty_with_init_failure
#define fs_watch_run_ready fs_watch_run_ready_with_init_failure
#define fs_watch_acknowledge_rescan fs_watch_acknowledge_rescan_with_init_failure
#define fs_watch_close fs_watch_close_with_init_failure
#define fs_watch_is_quiescent fs_watch_is_quiescent_with_init_failure
#define fs_watch_get_stats fs_watch_get_stats_with_init_failure
#define fs_watch_destroy fs_watch_destroy_with_init_failure
#include "../src/file/fs_watch.c"
#undef fs_watch_backend_set
#undef fs_watch_backend_get
#undef fs_watch_close_requested
#undef fs_watch_backend_mark_done
#undef fs_watch_publish_loss
#undef fs_watch_publish
#undef fs_watch_open_notified
#undef fs_watch_open
#undef fs_watch_has_ready_or_done
#undef fs_watch_backend_done_and_empty
#undef fs_watch_run_ready
#undef fs_watch_acknowledge_rescan
#undef fs_watch_close
#undef fs_watch_is_quiescent
#undef fs_watch_get_stats
#undef fs_watch_destroy
#undef fs_watch_backend_open
#undef fs_watch_backend_request_close
#undef fs_watch_backend_destroy
#undef sync_mutex_init

spec("Native filesystem watch initialization failures") {
  it("fails mutex allocation before opening any native watcher") {
    fs_watch watch = {0};
    fs_watch_config config = {.event_capacity = 1u, .watch_capacity = 1u,
        .path_capacity = 32u, .native_buffer_capacity = 1024u, .event = watch_init_event};
    watch_init_backend_calls = 0u;
    check_equal(fs_watch_open_with_init_failure(&watch, ".", &config), SALTS_ENOMEM);
    check_equal(watch_init_backend_calls, (size_t)0u);
    check_null(watch.impl);
  }
}
