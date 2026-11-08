#ifndef SALTS_FS_WATCH_INTERNAL_H
#define SALTS_FS_WATCH_INTERNAL_H

#include <fs_watch.h>

typedef struct salts_fs_watch_impl salts_fs_watch_impl;
typedef void (*salts_fs_watch_ready_fn)(void *user);

int salts_fs_watch_open_notified(salts_fs_watch *watch, const char *path,
                                 const salts_fs_watch_config *config,
                                 salts_fs_watch_ready_fn ready,
                                 void *ready_user);
bool salts_fs_watch_has_ready_or_done(const salts_fs_watch *watch);
bool salts_fs_watch_backend_done_and_empty(const salts_fs_watch *watch);

int salts_fs_watch_backend_open(salts_fs_watch_impl *impl,
                                const char *path,
                                const salts_fs_watch_config *config);
void salts_fs_watch_backend_request_close(salts_fs_watch_impl *impl);
int salts_fs_watch_backend_destroy(salts_fs_watch_impl *impl);

int salts_fs_watch_publish(salts_fs_watch_impl *impl,
                           salts_fs_watch_event_kind kind,
                           const char *path,
                           const char *old_path,
                           salts_fs_watch_entry_type entry_type);
void salts_fs_watch_publish_loss(salts_fs_watch_impl *impl);
void salts_fs_watch_backend_mark_done(salts_fs_watch_impl *impl);
void salts_fs_watch_backend_set(salts_fs_watch_impl *impl, void *backend);
void *salts_fs_watch_backend_get(salts_fs_watch_impl *impl);
bool salts_fs_watch_close_requested(const salts_fs_watch_impl *impl);

#endif /* SALTS_FS_WATCH_INTERNAL_H */
