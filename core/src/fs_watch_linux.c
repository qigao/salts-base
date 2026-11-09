#include "fs_watch_internal.h"

#include <error_codes.h>
#include <thread.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct salts_fs_watch_linux_entry {
    int descriptor;
    char *relative_path;
} salts_fs_watch_linux_entry;

typedef struct salts_fs_watch_linux {
    salts_fs_watch_impl *owner;
    int inotify_fd;
    int stop_pipe[2];
    cmeta_thread_t thread;
    unsigned char *buffer;
    salts_fs_watch_linux_entry *watches;
    char *watch_paths;
    char *root_path;
    char *absolute_path;
    char *event_path;
    char *tree_path;
    char *rename_old_path;
    size_t buffer_capacity;
    size_t watch_capacity;
    size_t watch_count;
    size_t root_length;
    size_t absolute_capacity;
    size_t path_capacity;
    uint32_t rename_cookie;
    salts_fs_watch_entry_type rename_type;
    bool recursive;
    bool rename_pending;
} salts_fs_watch_linux;

static const uint32_t watch_linux_mask =
    IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
    IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
    IN_UNMOUNT | IN_ONLYDIR;

static void watch_linux_remove_prefix(salts_fs_watch_linux *backend,
                                      const char *prefix);

static bool watch_linux_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static void watch_linux_loss(salts_fs_watch_linux *backend) {
    if (backend->recursive && backend->rename_pending &&
        backend->rename_type == SALTS_FS_WATCH_ENTRY_DIRECTORY)
        watch_linux_remove_prefix(backend, backend->rename_old_path);
    backend->rename_pending = false;
    backend->rename_cookie = 0u;
    salts_fs_watch_publish_loss(backend->owner);
}

static bool watch_linux_join_relative(char *out, size_t capacity,
                                      const char *base, const char *name) {
    size_t base_length = strlen(base);
    size_t name_length = strlen(name);
    size_t separator = base_length != 0u ? 1u : 0u;
    if (base_length > SIZE_MAX - separator ||
        base_length + separator > SIZE_MAX - name_length ||
        base_length + separator + name_length >= capacity)
        return false;
    if (base_length != 0u)
        memcpy(out, base, base_length);
    if (separator != 0u)
        out[base_length] = '/';
    memcpy(out + base_length + separator, name, name_length + 1u);
    return true;
}

static bool watch_linux_absolute(salts_fs_watch_linux *backend,
                                 const char *relative) {
    size_t relative_length = strlen(relative);
    size_t separator = relative_length != 0u &&
        backend->root_path[backend->root_length - 1u] != '/' ? 1u : 0u;
    if (backend->root_length > SIZE_MAX - separator ||
        backend->root_length + separator > SIZE_MAX - relative_length ||
        backend->root_length + separator + relative_length >=
            backend->absolute_capacity)
        return false;
    memcpy(backend->absolute_path, backend->root_path,
           backend->root_length);
    if (separator != 0u)
        backend->absolute_path[backend->root_length] = '/';
    memcpy(backend->absolute_path + backend->root_length + separator,
           relative, relative_length + 1u);
    return true;
}

static size_t watch_linux_find(const salts_fs_watch_linux *backend,
                               int descriptor) {
    size_t index;
    for (index = 0u; index < backend->watch_count; ++index) {
        if (backend->watches[index].descriptor == descriptor)
            return index;
    }
    return SIZE_MAX;
}

static void watch_linux_remove_index(salts_fs_watch_linux *backend,
                                     size_t index, bool remove_native) {
    size_t last;
    if (index >= backend->watch_count)
        return;
    if (remove_native && backend->watches[index].descriptor >= 0)
        (void)inotify_rm_watch(backend->inotify_fd,
                               backend->watches[index].descriptor);
    last = backend->watch_count - 1u;
    if (index != last) {
        backend->watches[index].descriptor =
            backend->watches[last].descriptor;
        memcpy(backend->watches[index].relative_path,
               backend->watches[last].relative_path,
               strlen(backend->watches[last].relative_path) + 1u);
    }
    backend->watches[last].descriptor = -1;
    backend->watches[last].relative_path[0] = '\0';
    --backend->watch_count;
}

static bool watch_linux_has_prefix(const char *path, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return strcmp(path, prefix) == 0 ||
           (prefix_length != 0u &&
            strncmp(path, prefix, prefix_length) == 0 &&
            path[prefix_length] == '/');
}

static void watch_linux_remove_prefix(salts_fs_watch_linux *backend,
                                      const char *prefix) {
    size_t index = 0u;
    while (index < backend->watch_count) {
        if (watch_linux_has_prefix(
                backend->watches[index].relative_path, prefix))
            watch_linux_remove_index(backend, index, true);
        else
            ++index;
    }
}

static int watch_linux_update_prefix(salts_fs_watch_linux *backend,
                                     const char *old_prefix,
                                     const char *new_prefix) {
    size_t old_length = strlen(old_prefix);
    size_t new_length = strlen(new_prefix);
    size_t index;
    for (index = 0u; index < backend->watch_count; ++index) {
        const char *path = backend->watches[index].relative_path;
        if (watch_linux_has_prefix(path, old_prefix)) {
            size_t suffix_length = strlen(path + old_length);
            if (new_length > SIZE_MAX - suffix_length ||
                new_length + suffix_length >= backend->path_capacity)
                return SALTS_ENOBUFS;
        }
    }
    for (index = 0u; index < backend->watch_count; ++index) {
        char *path = backend->watches[index].relative_path;
        if (watch_linux_has_prefix(path, old_prefix)) {
            size_t suffix_length = strlen(path + old_length);
            memmove(path + new_length, path + old_length,
                    suffix_length + 1u);
            memcpy(path, new_prefix, new_length);
        }
    }
    return SALTS_OK;
}

static int watch_linux_add(salts_fs_watch_linux *backend,
                           const char *relative) {
    salts_fs_watch_linux_entry *entry;
    int descriptor;
    size_t existing;
    if (strlen(relative) >= backend->path_capacity ||
        !watch_linux_absolute(backend, relative))
        return SALTS_ENOBUFS;
    descriptor = inotify_add_watch(backend->inotify_fd,
                                   backend->absolute_path,
                                   watch_linux_mask);
    if (descriptor < 0)
        return -errno;
    existing = watch_linux_find(backend, descriptor);
    if (existing != SIZE_MAX)
        return SALTS_OK;
    if (backend->watch_count == backend->watch_capacity) {
        (void)inotify_rm_watch(backend->inotify_fd, descriptor);
        return SALTS_ENOBUFS;
    }
    entry = &backend->watches[backend->watch_count++];
    entry->descriptor = descriptor;
    memcpy(entry->relative_path, relative, strlen(relative) + 1u);
    return SALTS_OK;
}

static int watch_linux_child_is_directory(
    salts_fs_watch_linux *backend, const struct dirent *child,
    const char *relative, bool *is_directory) {
    struct stat info;
    *is_directory = false;
    if (child->d_type == DT_DIR) {
        *is_directory = true;
        return SALTS_OK;
    }
    if (child->d_type != DT_UNKNOWN)
        return SALTS_OK;
    if (!watch_linux_absolute(backend, relative))
        return SALTS_ENOBUFS;
    if (lstat(backend->absolute_path, &info) != 0)
        return errno == ENOENT ? SALTS_OK : -errno;
    *is_directory = S_ISDIR(info.st_mode);
    return SALTS_OK;
}

static int watch_linux_add_tree(salts_fs_watch_linux *backend,
                                const char *relative) {
    size_t scan = backend->watch_count;
    int status = watch_linux_add(backend, relative);
    if (status != SALTS_OK || !backend->recursive)
        return status;
    while (scan < backend->watch_count) {
        DIR *directory;
        struct dirent *child;
        if (!watch_linux_absolute(
                backend, backend->watches[scan].relative_path))
            return SALTS_ENOBUFS;
        directory = opendir(backend->absolute_path);
        if (directory == NULL)
            return errno == ENOENT ? SALTS_OK : -errno;
        errno = 0;
        while ((child = readdir(directory)) != NULL) {
            bool is_directory;
            if (strcmp(child->d_name, ".") == 0 ||
                strcmp(child->d_name, "..") == 0)
                continue;
            if (!watch_linux_join_relative(
                    backend->event_path, backend->path_capacity,
                    backend->watches[scan].relative_path, child->d_name)) {
                status = SALTS_ENOBUFS;
                break;
            }
            status = watch_linux_child_is_directory(
                backend, child, backend->event_path, &is_directory);
            if (status != SALTS_OK)
                break;
            if (is_directory) {
                status = watch_linux_add(backend, backend->event_path);
                if (status != SALTS_OK)
                    break;
            }
            errno = 0;
        }
        if (status == SALTS_OK && errno != 0)
            status = -errno;
        (void)closedir(directory);
        if (status != SALTS_OK)
            return status;
        ++scan;
    }
    return SALTS_OK;
}

static void watch_linux_process(salts_fs_watch_linux *backend,
                                ssize_t bytes) {
    size_t offset = 0u;
    while (offset + sizeof(struct inotify_event) <= (size_t)bytes) {
        const struct inotify_event *native =
            (const struct inotify_event *)(backend->buffer + offset);
        salts_fs_watch_entry_type entry_type;
        size_t watch_index;
        const char *path;
        if ((size_t)native->len >
            (size_t)bytes - offset - sizeof(*native)) {
            watch_linux_loss(backend);
            return;
        }
        if ((native->mask & IN_Q_OVERFLOW) != 0u) {
            watch_linux_loss(backend);
            offset += sizeof(*native) + native->len;
            continue;
        }
        watch_index = watch_linux_find(backend, native->wd);
        if ((native->mask & IN_IGNORED) != 0u) {
            if (watch_index != SIZE_MAX)
                watch_linux_remove_index(backend, watch_index, false);
            offset += sizeof(*native) + native->len;
            continue;
        }
        if (watch_index == SIZE_MAX) {
            /* Native removal can leave already-queued events for that wd. */
            offset += sizeof(*native) + native->len;
            continue;
        }
        if ((native->mask & (IN_UNMOUNT | IN_DELETE_SELF)) != 0u) {
            if (backend->watches[watch_index].relative_path[0] == '\0') {
                (void)salts_fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, SALTS_FS_WATCH_ENTRY_DIRECTORY);
            } else if ((native->mask & IN_DELETE_SELF) != 0u) {
                watch_linux_remove_index(backend, watch_index, false);
            }
            offset += sizeof(*native) + native->len;
            continue;
        }
        if ((native->mask & IN_MOVE_SELF) != 0u) {
            if (backend->watches[watch_index].relative_path[0] == '\0')
                (void)salts_fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, SALTS_FS_WATCH_ENTRY_DIRECTORY);
            offset += sizeof(*native) + native->len;
            continue;
        }
        if (native->len == 0u ||
            !watch_linux_join_relative(
                backend->event_path, backend->path_capacity,
                backend->watches[watch_index].relative_path, native->name)) {
            watch_linux_loss(backend);
            offset += sizeof(*native) + native->len;
            continue;
        }
        path = backend->event_path;
        entry_type = (native->mask & IN_ISDIR) != 0u
            ? SALTS_FS_WATCH_ENTRY_DIRECTORY
            : SALTS_FS_WATCH_ENTRY_FILE;
        if ((native->mask & IN_MOVED_FROM) != 0u) {
            size_t length = strlen(path);
            if (backend->rename_pending)
                watch_linux_loss(backend);
            memcpy(backend->rename_old_path, path, length + 1u);
            backend->rename_cookie = native->cookie;
            backend->rename_type = entry_type;
            backend->rename_pending = true;
        } else if ((native->mask & IN_MOVED_TO) != 0u) {
            if (!backend->rename_pending || native->cookie == 0u ||
                native->cookie != backend->rename_cookie) {
                if (backend->recursive &&
                    entry_type == SALTS_FS_WATCH_ENTRY_DIRECTORY &&
                    watch_linux_add_tree(backend, path) != SALTS_OK)
                    watch_linux_loss(backend);
                else
                    watch_linux_loss(backend);
            } else {
                if (backend->recursive &&
                    entry_type == SALTS_FS_WATCH_ENTRY_DIRECTORY &&
                    watch_linux_update_prefix(
                        backend, backend->rename_old_path, path) != SALTS_OK) {
                    watch_linux_loss(backend);
                } else {
                    (void)salts_fs_watch_publish(
                        backend->owner, SALTS_FS_WATCH_RENAMED, path,
                        backend->rename_old_path, backend->rename_type);
                }
                backend->rename_pending = false;
                backend->rename_cookie = 0u;
            }
        } else if ((native->mask & IN_CREATE) != 0u) {
            if (backend->recursive &&
                entry_type == SALTS_FS_WATCH_ENTRY_DIRECTORY) {
                memcpy(backend->tree_path, path, strlen(path) + 1u);
                if (watch_linux_add_tree(backend,
                                         backend->tree_path) != SALTS_OK)
                    watch_linux_loss(backend);
                else
                    (void)salts_fs_watch_publish(
                        backend->owner, SALTS_FS_WATCH_CREATED,
                        backend->tree_path, NULL, entry_type);
            } else {
                (void)salts_fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_CREATED, path,
                    NULL, entry_type);
            }
        } else if ((native->mask & IN_DELETE) != 0u) {
            (void)salts_fs_watch_publish(
                backend->owner, SALTS_FS_WATCH_REMOVED, path,
                NULL, entry_type);
            if (backend->recursive &&
                entry_type == SALTS_FS_WATCH_ENTRY_DIRECTORY)
                watch_linux_remove_prefix(backend, path);
        } else if ((native->mask & IN_ATTRIB) != 0u) {
            (void)salts_fs_watch_publish(
                backend->owner, SALTS_FS_WATCH_ATTRIBUTES, path,
                NULL, entry_type);
        } else if ((native->mask & (IN_MODIFY | IN_CLOSE_WRITE)) != 0u) {
            (void)salts_fs_watch_publish(
                backend->owner, SALTS_FS_WATCH_MODIFIED, path,
                NULL, entry_type);
        }
        offset += sizeof(*native) + native->len;
    }
    if (offset != (size_t)bytes)
        watch_linux_loss(backend);
    else if (backend->rename_pending)
        watch_linux_loss(backend);
}

static void watch_linux_thread(void *user) {
    salts_fs_watch_linux *backend = (salts_fs_watch_linux *)user;
    struct pollfd descriptors[2] = {
        {backend->stop_pipe[0], POLLIN, 0},
        {backend->inotify_fd, POLLIN, 0},
    };
    while (!salts_fs_watch_close_requested(backend->owner)) {
        int ready;
        do {
            ready = poll(descriptors, 2u, -1);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0)
            break;
        if ((descriptors[0].revents & POLLIN) != 0)
            break;
        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (!salts_fs_watch_close_requested(backend->owner))
                (void)salts_fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, SALTS_FS_WATCH_ENTRY_DIRECTORY);
            break;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            ssize_t bytes;
            do {
                bytes = read(backend->inotify_fd, backend->buffer,
                             backend->buffer_capacity);
            } while (bytes < 0 && errno == EINTR);
            if (bytes > 0)
                watch_linux_process(backend, bytes);
            else if (bytes < 0 && errno != EAGAIN)
                break;
        }
    }
    salts_fs_watch_backend_mark_done(backend->owner);
}

static void watch_linux_release(salts_fs_watch_linux *backend) {
    size_t index;
    if (backend == NULL)
        return;
    for (index = 0u; index < backend->watch_count; ++index) {
        if (backend->watches[index].descriptor >= 0)
            (void)inotify_rm_watch(backend->inotify_fd,
                                   backend->watches[index].descriptor);
    }
    if (backend->stop_pipe[1] >= 0)
        (void)close(backend->stop_pipe[1]);
    if (backend->stop_pipe[0] >= 0)
        (void)close(backend->stop_pipe[0]);
    if (backend->inotify_fd >= 0)
        (void)close(backend->inotify_fd);
    free(backend->event_path);
    free(backend->absolute_path);
    free(backend->root_path);
    free(backend->watch_paths);
    free(backend->watches);
    free(backend->buffer);
    free(backend);
}

int salts_fs_watch_backend_open(salts_fs_watch_impl *impl,
                                const char *path,
                                const salts_fs_watch_config *config) {
    salts_fs_watch_linux *backend;
    size_t watch_bytes;
    size_t watch_path_bytes;
    size_t path_storage_bytes;
    size_t root_length = strlen(path);
    size_t index;
    int status;
    if (!watch_linux_multiply(config->watch_capacity,
                              sizeof(salts_fs_watch_linux_entry),
                              &watch_bytes) ||
        !watch_linux_multiply(config->watch_capacity,
                              config->path_capacity,
                              &watch_path_bytes) ||
        !watch_linux_multiply(config->path_capacity, 3u,
                              &path_storage_bytes) ||
        root_length > SIZE_MAX - 2u ||
        config->path_capacity > SIZE_MAX - root_length - 2u)
        return SALTS_EINVAL;
    backend = (salts_fs_watch_linux *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return SALTS_ENOMEM;
    backend->inotify_fd = -1;
    backend->stop_pipe[0] = -1;
    backend->stop_pipe[1] = -1;
    backend->root_length = root_length;
    backend->absolute_capacity =
        backend->root_length + config->path_capacity + 2u;
    backend->buffer = (unsigned char *)malloc(
        config->native_buffer_capacity);
    backend->watches = (salts_fs_watch_linux_entry *)calloc(
        1u, watch_bytes);
    backend->watch_paths = (char *)calloc(1u, watch_path_bytes);
    backend->root_path = (char *)malloc(backend->root_length + 1u);
    backend->absolute_path = (char *)malloc(backend->absolute_capacity);
    backend->event_path = (char *)malloc(path_storage_bytes);
    if (backend->buffer == NULL || backend->watches == NULL ||
        backend->watch_paths == NULL || backend->root_path == NULL ||
        backend->absolute_path == NULL || backend->event_path == NULL) {
        watch_linux_release(backend);
        return SALTS_ENOMEM;
    }
    backend->tree_path = backend->event_path + config->path_capacity;
    backend->rename_old_path =
        backend->tree_path + config->path_capacity;
    memcpy(backend->root_path, path, backend->root_length + 1u);
    while (backend->root_length > 1u &&
           backend->root_path[backend->root_length - 1u] == '/')
        backend->root_path[--backend->root_length] = '\0';
    backend->owner = impl;
    backend->buffer_capacity = config->native_buffer_capacity;
    backend->watch_capacity = config->watch_capacity;
    backend->path_capacity = config->path_capacity;
    backend->recursive = config->recursive;
    for (index = 0u; index < backend->watch_capacity; ++index) {
        backend->watches[index].descriptor = -1;
        backend->watches[index].relative_path =
            backend->watch_paths + index * backend->path_capacity;
    }
    backend->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (backend->inotify_fd < 0) {
        status = -errno;
        watch_linux_release(backend);
        return status;
    }
    if (pipe(backend->stop_pipe) != 0) {
        status = -errno;
        watch_linux_release(backend);
        return status;
    }
    if (fcntl(backend->stop_pipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(backend->stop_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        status = -errno;
        watch_linux_release(backend);
        return status;
    }
    status = watch_linux_add_tree(backend, "");
    if (status != SALTS_OK) {
        watch_linux_release(backend);
        return status;
    }
    salts_fs_watch_backend_set(impl, backend);
    status = cmeta_thread_create(&backend->thread,
                                 watch_linux_thread, backend);
    if (status != SALTS_OK) {
        salts_fs_watch_backend_set(impl, NULL);
        watch_linux_release(backend);
        return status;
    }
    return SALTS_OK;
}

void salts_fs_watch_backend_request_close(salts_fs_watch_impl *impl) {
    salts_fs_watch_linux *backend =
        (salts_fs_watch_linux *)salts_fs_watch_backend_get(impl);
    const unsigned char stop = 1u;
    if (backend != NULL)
        (void)write(backend->stop_pipe[1], &stop, sizeof(stop));
}

int salts_fs_watch_backend_destroy(salts_fs_watch_impl *impl) {
    salts_fs_watch_linux *backend =
        (salts_fs_watch_linux *)salts_fs_watch_backend_get(impl);
    int status = SALTS_OK;
    if (backend == NULL)
        return SALTS_EINVAL;
    if (cmeta_thread_join(&backend->thread) != SALTS_OK)
        status = SALTS_EIO;
    cmeta_thread_destroy(&backend->thread);
    salts_fs_watch_backend_set(impl, NULL);
    watch_linux_release(backend);
    return status;
}
