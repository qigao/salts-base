#ifndef SALTS_FS_WATCH_INTERNAL_H
#define SALTS_FS_WATCH_INTERNAL_H

#include <file/fs_watch.h>

typedef struct fs_watch_impl fs_watch_impl;
typedef void (*fs_watch_ready_fn)(void *user);

int fs_watch_open_notified(fs_watch *watch, const char *path,
                                 const fs_watch_config *config,
                                 fs_watch_ready_fn ready,
                                 void *ready_user);
bool fs_watch_has_ready_or_done(const fs_watch *watch);
bool fs_watch_backend_done_and_empty(const fs_watch *watch);

int fs_watch_backend_open(fs_watch_impl *impl,
                                const char *path,
                                const fs_watch_config *config);
void fs_watch_backend_request_close(fs_watch_impl *impl);
int fs_watch_backend_destroy(fs_watch_impl *impl);

int fs_watch_publish(fs_watch_impl *impl,
                           fs_watch_event_kind kind,
                           const char *path,
                           const char *old_path,
                           fs_watch_entry_type entry_type);
void fs_watch_publish_loss(fs_watch_impl *impl);
void fs_watch_backend_mark_done(fs_watch_impl *impl);
void fs_watch_backend_set(fs_watch_impl *impl, void *backend);
void *fs_watch_backend_get(fs_watch_impl *impl);
bool fs_watch_close_requested(const fs_watch_impl *impl);

#endif /* SALTS_FS_WATCH_INTERNAL_H */
