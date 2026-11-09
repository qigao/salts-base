#ifndef SALTS_FS_WATCH_H
#define SALTS_FS_WATCH_H

#include <core/api.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fs_watch {
    void *impl;
} fs_watch;

typedef enum fs_watch_event_kind {
    SALTS_FS_WATCH_CREATED = 0,
    SALTS_FS_WATCH_REMOVED,
    SALTS_FS_WATCH_MODIFIED,
    SALTS_FS_WATCH_ATTRIBUTES,
    SALTS_FS_WATCH_RENAMED,
    SALTS_FS_WATCH_ROOT_CHANGED,
    SALTS_FS_WATCH_RESCAN_REQUIRED
} fs_watch_event_kind;

typedef enum fs_watch_entry_type {
    SALTS_FS_WATCH_ENTRY_UNKNOWN = 0,
    SALTS_FS_WATCH_ENTRY_FILE,
    SALTS_FS_WATCH_ENTRY_DIRECTORY
} fs_watch_entry_type;

typedef struct fs_watch_event {
    /* path and old_path are normalized UTF-8 borrows valid during callback. */
    fs_watch_event_kind kind;
    const char *path;
    const char *old_path;
    fs_watch_entry_type entry_type;
} fs_watch_event;

typedef void (*fs_watch_event_fn)(
    void *user, const fs_watch_event *event);

typedef struct fs_watch_config {
    /* Recursive support is backend capability checked during open. */
    bool recursive;
    /* Detailed event slots; one rescan marker lives in control state. */
    size_t event_capacity;
    /* Maximum native directory registrations for recursive backends. */
    size_t watch_capacity;
    /* Per-event relative-path capacity, including the trailing NUL. */
    size_t path_capacity;
    /* One bounded native read buffer; accepted range is 1024..65536. */
    size_t native_buffer_capacity;
    fs_watch_event_fn event;
    void *event_user;
} fs_watch_config;

typedef enum fs_watch_lifecycle {
    SALTS_FS_WATCH_OPEN = 0,
    SALTS_FS_WATCH_CLOSING,
    SALTS_FS_WATCH_CLOSED
} fs_watch_lifecycle;

typedef struct fs_watch_stats {
    size_t capacity;
    size_t queued;
    size_t delivered;
    size_t suppressed;
    size_t rescan_required;
    bool awaiting_rescan;
    fs_watch_lifecycle lifecycle;
} fs_watch_stats;

/**
 * Open and arm a native directory event source before returning.
 *
 * @param watch Zero-initialized owning destination.
 * @param path Existing directory encoded as UTF-8.
 * @param config Positive capacities and a non-NULL callback.
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ENOMEM, SALTS_ENOTSUP for an
 * unsupported recursive/backend contract, or a negative native error.
 */
SALTS_API int fs_watch_open(fs_watch *watch, const char *path,
                        const fs_watch_config *config);
/**
 * Deliver at most max_events callbacks on the calling driver thread.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EBUSY for concurrent/reentrant use.
 */
SALTS_API int fs_watch_run_ready(fs_watch *watch, size_t max_events,
                             size_t *delivered);
/**
 * Resume detailed publication after the rescan callback has been delivered
 * and the caller has rebuilt its authoritative filesystem view. If native
 * events were suppressed after marker delivery, acknowledgement retains the
 * loss state and queues another rescan marker instead of losing that race.
 */
SALTS_API int fs_watch_acknowledge_rescan(fs_watch *watch);
/** Stop native publication and wake the backend thread without blocking. */
SALTS_API int fs_watch_close(fs_watch *watch);
/** Return true after close, backend termination, and event delivery drain. */
SALTS_API bool fs_watch_is_quiescent(const fs_watch *watch);
/** Copy an observational capacity, loss, and lifecycle snapshot. */
SALTS_API bool fs_watch_get_stats(const fs_watch *watch,
                              fs_watch_stats *out);
/** Join and release a quiescent source, restoring watch to zero. */
SALTS_API int fs_watch_destroy(fs_watch *watch);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_FS_WATCH_H */
