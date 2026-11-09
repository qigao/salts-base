#include "fs_watch_internal.h"

#include <salts/error_codes.h>
#include <salts/threads.h>

#if defined(interface)
#undef interface
#endif
#include <windows.h>

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

typedef struct fs_watch_windows {
    fs_watch_impl *owner;
    HANDLE directory;
    HANDLE stop_event;
    HANDLE io_event;
    HANDLE ready_event;
    sync_thread_t thread;
    unsigned char *buffer;
    DWORD buffer_capacity;
    DWORD notify_filter;
    BOOL recursive;
    char *rename_old_path;
    char *scratch_path;
    size_t path_capacity;
    bool rename_pending;
    volatile LONG start_status;
} fs_watch_windows;

static int watch_windows_path(const char *path, wchar_t **out) {
    int count;
    wchar_t *wide;
    *out = NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                path, -1, NULL, 0);
    if (count <= 0)
        return SALTS_EINVAL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL)
        return SALTS_ENOMEM;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            path, -1, wide, count) != count) {
        free(wide);
        return SALTS_EINVAL;
    }
    *out = wide;
    return SALTS_OK;
}

static int watch_windows_name(const FILE_NOTIFY_INFORMATION *native,
                              char *out, size_t capacity) {
    int wide_count = (int)(native->FileNameLength / sizeof(wchar_t));
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                     native->FileName, wide_count,
                                     NULL, 0, NULL, NULL);
    size_t index;
    if (needed <= 0 || (size_t)needed >= capacity)
        return SALTS_ENOBUFS;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            native->FileName, wide_count,
                            out, needed, NULL, NULL) != needed)
        return SALTS_EINVAL;
    out[needed] = '\0';
    for (index = 0u; index < (size_t)needed; ++index) {
        if (out[index] == '\\')
            out[index] = '/';
    }
    return SALTS_OK;
}

static void watch_windows_process(fs_watch_windows *backend,
                                  DWORD bytes) {
    size_t offset = 0u;
    char *path = backend->scratch_path;
    while (offset < (size_t)bytes) {
        FILE_NOTIFY_INFORMATION *native =
            (FILE_NOTIFY_INFORMATION *)(backend->buffer + offset);
        const size_t available = (size_t)bytes - offset;
        const size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (available < header ||
            native->FileNameLength > available - header ||
            native->FileNameLength % sizeof(wchar_t) != 0u) {
            fs_watch_publish_loss(backend->owner);
            break;
        }
        int status = watch_windows_name(native, path, backend->path_capacity);
        if (status != SALTS_OK) {
            backend->rename_pending = false;
            fs_watch_publish_loss(backend->owner);
        } else {
            switch (native->Action) {
                case FILE_ACTION_ADDED:
                    if (backend->rename_pending) {
                        backend->rename_pending = false;
                        fs_watch_publish_loss(backend->owner);
                    }
                    (void)fs_watch_publish(
                        backend->owner, SALTS_FS_WATCH_CREATED, path, NULL,
                        SALTS_FS_WATCH_ENTRY_UNKNOWN);
                    break;
                case FILE_ACTION_REMOVED:
                    if (backend->rename_pending) {
                        backend->rename_pending = false;
                        fs_watch_publish_loss(backend->owner);
                    }
                    (void)fs_watch_publish(
                        backend->owner, SALTS_FS_WATCH_REMOVED, path, NULL,
                        SALTS_FS_WATCH_ENTRY_UNKNOWN);
                    break;
                case FILE_ACTION_MODIFIED:
                    if (backend->rename_pending) {
                        backend->rename_pending = false;
                        fs_watch_publish_loss(backend->owner);
                    }
                    (void)fs_watch_publish(
                        backend->owner, SALTS_FS_WATCH_MODIFIED, path, NULL,
                        SALTS_FS_WATCH_ENTRY_UNKNOWN);
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    if (backend->rename_pending)
                        fs_watch_publish_loss(backend->owner);
                    memcpy(backend->rename_old_path, path, strlen(path) + 1u);
                    backend->rename_pending = true;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    if (!backend->rename_pending) {
                        fs_watch_publish_loss(backend->owner);
                    } else {
                        (void)fs_watch_publish(
                            backend->owner, SALTS_FS_WATCH_RENAMED, path,
                            backend->rename_old_path,
                            SALTS_FS_WATCH_ENTRY_UNKNOWN);
                        backend->rename_pending = false;
                    }
                    break;
                default:
                    fs_watch_publish_loss(backend->owner);
                    break;
            }
        }
        if (native->NextEntryOffset == 0u)
            break;
        if ((size_t)native->NextEntryOffset <
                header + native->FileNameLength ||
            (size_t)native->NextEntryOffset > available) {
            fs_watch_publish_loss(backend->owner);
            break;
        }
        offset += native->NextEntryOffset;
    }
}

static void watch_windows_thread(void *user) {
    fs_watch_windows *backend = (fs_watch_windows *)user;
    HANDLE waits[2] = {backend->stop_event, backend->io_event};
    bool first_request = true;
    while (!fs_watch_close_requested(backend->owner)) {
        OVERLAPPED overlapped;
        DWORD bytes = 0u;
        DWORD wait_status;
        BOOL started;
        memset(&overlapped, 0, sizeof(overlapped));
        ResetEvent(backend->io_event);
        overlapped.hEvent = backend->io_event;
        started = ReadDirectoryChangesW(
            backend->directory, backend->buffer, backend->buffer_capacity,
            backend->recursive, backend->notify_filter, NULL,
            &overlapped, NULL);
        if (!started) {
            if (first_request) {
                InterlockedExchange(&backend->start_status,
                                    -(LONG)GetLastError());
                SetEvent(backend->ready_event);
                first_request = false;
            }
            if (!fs_watch_close_requested(backend->owner))
                (void)fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, SALTS_FS_WATCH_ENTRY_UNKNOWN);
            break;
        }
        if (first_request) {
            InterlockedExchange(&backend->start_status, SALTS_OK);
            SetEvent(backend->ready_event);
            first_request = false;
        }
        wait_status = WaitForMultipleObjects(2u, waits, FALSE, INFINITE);
        if (wait_status == WAIT_OBJECT_0) {
            (void)CancelIoEx(backend->directory, &overlapped);
            (void)GetOverlappedResult(
                backend->directory, &overlapped, &bytes, TRUE);
            break;
        }
        if (wait_status != WAIT_OBJECT_0 + 1u ||
            !GetOverlappedResult(
                backend->directory, &overlapped, &bytes, FALSE)) {
            DWORD error = GetLastError();
            if (error != ERROR_OPERATION_ABORTED &&
                !fs_watch_close_requested(backend->owner))
                (void)fs_watch_publish(
                    backend->owner, SALTS_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, SALTS_FS_WATCH_ENTRY_UNKNOWN);
            break;
        }
        if (bytes == 0u)
            fs_watch_publish_loss(backend->owner);
        else
            watch_windows_process(backend, bytes);
    }
    fs_watch_backend_mark_done(backend->owner);
}

int fs_watch_backend_open(fs_watch_impl *impl,
                                const char *path,
                                const fs_watch_config *config) {
    fs_watch_windows *backend;
    wchar_t *wide_path;
    int thread_status;
    int path_status;
    backend = (fs_watch_windows *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return SALTS_ENOMEM;
    path_status = watch_windows_path(path, &wide_path);
    if (path_status != SALTS_OK) {
        free(backend);
        return path_status;
    }
    backend->directory = CreateFileW(
        wide_path, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    free(wide_path);
    if (backend->directory == INVALID_HANDLE_VALUE) {
        const int status = -(int)GetLastError();
        free(backend);
        return status;
    }
    backend->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    backend->io_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    backend->ready_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    backend->buffer = (unsigned char *)malloc(config->native_buffer_capacity);
    backend->rename_old_path = (char *)malloc(config->path_capacity);
    backend->scratch_path = (char *)malloc(config->path_capacity);
    if (backend->stop_event == NULL || backend->io_event == NULL ||
        backend->ready_event == NULL ||
        backend->buffer == NULL || backend->rename_old_path == NULL ||
        backend->scratch_path == NULL) {
        if (backend->io_event != NULL) CloseHandle(backend->io_event);
        if (backend->stop_event != NULL) CloseHandle(backend->stop_event);
        if (backend->ready_event != NULL) CloseHandle(backend->ready_event);
        CloseHandle(backend->directory);
        free(backend->scratch_path);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return SALTS_ENOMEM;
    }
    backend->owner = impl;
    backend->buffer_capacity = (DWORD)config->native_buffer_capacity;
    backend->recursive = config->recursive ? TRUE : FALSE;
    backend->path_capacity = config->path_capacity;
    backend->notify_filter =
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    backend->start_status = SALTS_EBUSY;
    fs_watch_backend_set(impl, backend);
    thread_status = sync_thread_create(
        &backend->thread, watch_windows_thread, backend);
    if (thread_status != SALTS_OK) {
        fs_watch_backend_set(impl, NULL);
        CloseHandle(backend->io_event);
        CloseHandle(backend->stop_event);
        CloseHandle(backend->ready_event);
        CloseHandle(backend->directory);
        free(backend->scratch_path);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return thread_status;
    }
    if (WaitForSingleObject(backend->ready_event, INFINITE) != WAIT_OBJECT_0 ||
        InterlockedCompareExchange(&backend->start_status, 0, 0) != SALTS_OK) {
        const int status =
            (int)InterlockedCompareExchange(&backend->start_status, 0, 0);
        (void)sync_thread_join(&backend->thread);
        fs_watch_backend_set(impl, NULL);
        CloseHandle(backend->io_event);
        CloseHandle(backend->stop_event);
        CloseHandle(backend->ready_event);
        CloseHandle(backend->directory);
        free(backend->scratch_path);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return status == SALTS_EBUSY ? SALTS_EIO : status;
    }
    return SALTS_OK;
}

void fs_watch_backend_request_close(fs_watch_impl *impl) {
    fs_watch_windows *backend =
        (fs_watch_windows *)fs_watch_backend_get(impl);
    if (backend == NULL)
        return;
    SetEvent(backend->stop_event);
    (void)CancelIoEx(backend->directory, NULL);
}

int fs_watch_backend_destroy(fs_watch_impl *impl) {
    fs_watch_windows *backend =
        (fs_watch_windows *)fs_watch_backend_get(impl);
    int status = SALTS_OK;
    if (backend == NULL)
        return SALTS_EINVAL;
    if (sync_thread_join(&backend->thread) != SALTS_OK)
        status = SALTS_EIO;
    if (!CloseHandle(backend->io_event) && status == SALTS_OK)
        status = -(int)GetLastError();
    if (!CloseHandle(backend->stop_event) && status == SALTS_OK)
        status = -(int)GetLastError();
    if (!CloseHandle(backend->ready_event) && status == SALTS_OK)
        status = -(int)GetLastError();
    if (!CloseHandle(backend->directory) && status == SALTS_OK)
        status = -(int)GetLastError();
    free(backend->scratch_path);
    free(backend->rename_old_path);
    free(backend->buffer);
    free(backend);
    fs_watch_backend_set(impl, NULL);
    return status;
}
