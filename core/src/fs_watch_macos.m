#include "fs_watch_internal.h"

#include <error_codes.h>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct salts_fs_watch_macos {
    salts_fs_watch_impl *owner;
    FSEventStreamRef stream;
    dispatch_queue_t queue;
    char *root;
    size_t root_length;
    size_t path_capacity;
    bool recursive;
} salts_fs_watch_macos;

static char watch_macos_queue_key;

static const char *watch_macos_relative(salts_fs_watch_macos *backend,
                                        const char *path) {
    const char *relative;
    if (path == NULL || strncmp(path, backend->root,
                                backend->root_length) != 0)
        return NULL;
    relative = path + backend->root_length;
    if (*relative == '/')
        ++relative;
    else if (*relative != '\0')
        return NULL;
    if (!backend->recursive && strchr(relative, '/') != NULL)
        return NULL;
    return relative;
}

static salts_fs_watch_entry_type watch_macos_entry_type(
    FSEventStreamEventFlags flags) {
    if ((flags & kFSEventStreamEventFlagItemIsDir) != 0u)
        return SALTS_FS_WATCH_ENTRY_DIRECTORY;
    if ((flags & kFSEventStreamEventFlagItemIsFile) != 0u)
        return SALTS_FS_WATCH_ENTRY_FILE;
    return SALTS_FS_WATCH_ENTRY_UNKNOWN;
}

static void watch_macos_publish(salts_fs_watch_macos *backend,
                                const char *relative,
                                FSEventStreamEventFlags flags) {
    salts_fs_watch_entry_type entry_type =
        watch_macos_entry_type(flags);
    if ((flags & kFSEventStreamEventFlagItemCreated) != 0u) {
        (void)salts_fs_watch_publish(backend->owner,
                                    SALTS_FS_WATCH_CREATED, relative,
                                    NULL, entry_type);
    } else if ((flags & kFSEventStreamEventFlagItemRemoved) != 0u) {
        (void)salts_fs_watch_publish(backend->owner,
                                    SALTS_FS_WATCH_REMOVED, relative,
                                    NULL, entry_type);
    } else if ((flags & kFSEventStreamEventFlagItemInodeMetaMod) != 0u ||
               (flags & kFSEventStreamEventFlagItemFinderInfoMod) != 0u ||
               (flags & kFSEventStreamEventFlagItemChangeOwner) != 0u ||
               (flags & kFSEventStreamEventFlagItemXattrMod) != 0u) {
        (void)salts_fs_watch_publish(backend->owner,
                                    SALTS_FS_WATCH_ATTRIBUTES, relative,
                                    NULL, entry_type);
    } else {
        (void)salts_fs_watch_publish(backend->owner,
                                    SALTS_FS_WATCH_MODIFIED, relative,
                                    NULL, entry_type);
    }
}

static void watch_macos_events(ConstFSEventStreamRef stream,
                               void *user, size_t count,
                               void *event_paths,
                               const FSEventStreamEventFlags flags[],
                               const FSEventStreamEventId ids[]) {
    salts_fs_watch_macos *backend = (salts_fs_watch_macos *)user;
    char **paths = (char **)event_paths;
    size_t index;
    (void)stream;
    (void)ids;
    for (index = 0u; index < count; ++index) {
        const FSEventStreamEventFlags native = flags[index];
        const char *relative;
        if (salts_fs_watch_close_requested(backend->owner))
            return;
        if ((native & (kFSEventStreamEventFlagMustScanSubDirs |
                       kFSEventStreamEventFlagUserDropped |
                       kFSEventStreamEventFlagKernelDropped |
                       kFSEventStreamEventFlagEventIdsWrapped)) != 0u) {
            salts_fs_watch_publish_loss(backend->owner);
            continue;
        }
        if ((native & (kFSEventStreamEventFlagRootChanged |
                       kFSEventStreamEventFlagMount |
                       kFSEventStreamEventFlagUnmount)) != 0u) {
            (void)salts_fs_watch_publish(
                backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                NULL, NULL, SALTS_FS_WATCH_ENTRY_DIRECTORY);
            continue;
        }
        relative = watch_macos_relative(backend, paths[index]);
        if (relative == NULL)
            continue;
        if (strlen(relative) >= backend->path_capacity) {
            salts_fs_watch_publish_loss(backend->owner);
            continue;
        }
        if ((native & kFSEventStreamEventFlagItemRenamed) != 0u) {
            /* FSEvents supplies paths but no correlation cookie. */
            salts_fs_watch_publish_loss(backend->owner);
            continue;
        }
        watch_macos_publish(backend, relative, native);
    }
}

static void watch_macos_mark_done(void *user) {
    salts_fs_watch_macos *backend = (salts_fs_watch_macos *)user;
    salts_fs_watch_backend_mark_done(backend->owner);
}

static void watch_macos_stop_and_mark_done(void *user) {
    salts_fs_watch_macos *backend = (salts_fs_watch_macos *)user;
    FSEventStreamStop(backend->stream);
    FSEventStreamInvalidate(backend->stream);
    watch_macos_mark_done(backend);
}

int salts_fs_watch_backend_open(salts_fs_watch_impl *impl,
                                const char *path,
                                const salts_fs_watch_config *config) {
    salts_fs_watch_macos *backend;
    CFStringRef root_string;
    CFArrayRef paths;
    FSEventStreamContext context = {0};
    FSEventStreamCreateFlags flags =
        kFSEventStreamCreateFlagFileEvents |
        kFSEventStreamCreateFlagWatchRoot |
        kFSEventStreamCreateFlagNoDefer;
    backend = (salts_fs_watch_macos *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return SALTS_ENOMEM;
    /* FSEvents reports physical paths (for example /private/var for /var). */
    backend->root = realpath(path, NULL);
    if (backend->root == NULL) {
        free(backend);
        return -errno;
    }
    backend->root_length = strlen(backend->root);
    while (backend->root_length > 1u &&
           backend->root[backend->root_length - 1u] == '/')
        backend->root[--backend->root_length] = '\0';
    backend->owner = impl;
    backend->path_capacity = config->path_capacity;
    backend->recursive = config->recursive;
    root_string = CFStringCreateWithCString(
        kCFAllocatorDefault, backend->root, kCFStringEncodingUTF8);
    if (root_string == NULL) {
        free(backend->root);
        free(backend);
        return SALTS_EINVAL;
    }
    paths = CFArrayCreate(kCFAllocatorDefault,
                          (const void **)&root_string, 1u,
                          &kCFTypeArrayCallBacks);
    CFRelease(root_string);
    if (paths == NULL) {
        free(backend->root);
        free(backend);
        return SALTS_ENOMEM;
    }
    context.info = backend;
    backend->stream = FSEventStreamCreate(
        kCFAllocatorDefault, watch_macos_events, &context, paths,
        kFSEventStreamEventIdSinceNow, 0.01, flags);
    CFRelease(paths);
    if (backend->stream == NULL) {
        free(backend->root);
        free(backend);
        return SALTS_EIO;
    }
    backend->queue = dispatch_queue_create(
        "org.salts.cflow.fs-watch", DISPATCH_QUEUE_SERIAL);
    if (backend->queue == NULL) {
        FSEventStreamRelease(backend->stream);
        free(backend->root);
        free(backend);
        return SALTS_ENOMEM;
    }
    dispatch_queue_set_specific(backend->queue, &watch_macos_queue_key,
                                backend, NULL);
    FSEventStreamSetDispatchQueue(backend->stream, backend->queue);
    salts_fs_watch_backend_set(impl, backend);
    if (!FSEventStreamStart(backend->stream)) {
        salts_fs_watch_backend_set(impl, NULL);
        FSEventStreamInvalidate(backend->stream);
        FSEventStreamRelease(backend->stream);
        dispatch_release(backend->queue);
        free(backend->root);
        free(backend);
        return SALTS_EIO;
    }
    /* Match the synchronous readiness guarantee of the other native backends. */
    FSEventStreamFlushSync(backend->stream);
    return SALTS_OK;
}

void salts_fs_watch_backend_request_close(salts_fs_watch_impl *impl) {
    salts_fs_watch_macos *backend =
        (salts_fs_watch_macos *)salts_fs_watch_backend_get(impl);
    if (backend == NULL)
        return;
    if (dispatch_get_specific(&watch_macos_queue_key) != NULL) {
        dispatch_async_f(backend->queue, backend,
                         watch_macos_stop_and_mark_done);
        return;
    }
    dispatch_sync_f(backend->queue, backend,
                    watch_macos_stop_and_mark_done);
}

int salts_fs_watch_backend_destroy(salts_fs_watch_impl *impl) {
    salts_fs_watch_macos *backend =
        (salts_fs_watch_macos *)salts_fs_watch_backend_get(impl);
    if (backend == NULL)
        return SALTS_EINVAL;
    FSEventStreamRelease(backend->stream);
    dispatch_release(backend->queue);
    free(backend->root);
    free(backend);
    salts_fs_watch_backend_set(impl, NULL);
    return SALTS_OK;
}
