#include <fs_async.h>

#include <thread_pool.h>
#include <error_codes.h>
#include <thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef enum salts_fs_slot_phase {
    SALTS_FS_SLOT_FREE = 0,
    SALTS_FS_SLOT_QUEUED,
    SALTS_FS_SLOT_RUNNING,
    SALTS_FS_SLOT_COMPLETE,
    SALTS_FS_SLOT_DELIVERING
} salts_fs_slot_phase;

typedef struct salts_fs_impl salts_fs_impl;

typedef struct salts_fs_slot {
    salts_fs_impl *owner;
    char *path;
    char *second_path;
    atomic_int phase;
    atomic_bool cancel_requested;
    uint64_t request_id;
    salts_fs_operation_kind operation;
    union {
        salts_fs_stat_t *stat_out;
        salts_fs_dir_buffer *directory_out;
        int mode;
    } arguments;
    int result;
} salts_fs_slot;

struct salts_fs_impl {
    cmeta_threadpool_t *executor;
    salts_fs_slot *slots;
    char *paths;
    size_t capacity;
    size_t path_capacity;
    cmeta_mutex_t gate;
    uint64_t next_request_id;
    salts_fs_completion_fn completion;
    void *completion_user;
    atomic_size_t accepted;
    atomic_size_t completed;
    atomic_size_t cancelled;
    atomic_size_t in_use;
    atomic_size_t rejected_full;
    atomic_size_t rejected_closed;
    atomic_bool close_requested;
    atomic_bool driver_active;
};

static salts_fs_submit_result fs_submit_result(salts_fs_submit_status status,
                                                uint64_t request_id) {
    salts_fs_submit_result result = {
        .status = status,
        .request_id = request_id,
    };
    return result;
}

static bool fs_checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static bool fs_path_fits(const char *path, size_t capacity,
                         size_t *length_out) {
    size_t length = 0u;
    if (path == NULL || capacity == 0u)
        return false;
    while (length < capacity && path[length] != '\0')
        ++length;
    if (length == 0u || length == capacity)
        return false;
    if (length_out != NULL)
        *length_out = length;
    return true;
}

static void fs_slot_reset(salts_fs_slot *slot) {
    slot->request_id = 0u;
    slot->operation = SALTS_FS_STAT;
    memset(&slot->arguments, 0, sizeof(slot->arguments));
    slot->result = SALTS_OK;
    slot->path[0] = '\0';
    slot->second_path[0] = '\0';
    atomic_store(&slot->cancel_requested, false);
    atomic_store(&slot->phase, SALTS_FS_SLOT_FREE);
}

static int fs_read_directory(salts_fs_slot *slot) {
    salts_fs_dir_buffer *out = slot->arguments.directory_out;
    salts_fs_dir_t *directory = NULL;
    salts_fs_dirent_t entry;
    size_t entry_count = 0u;
    size_t names_used = 0u;
    int status;

    out->entry_count = 0u;
    out->names_used = 0u;
    status = salts_fs_opendir(slot->path, &directory);
    if (status != SALTS_OK)
        return status;
    for (;;) {
        size_t name_length;
        status = salts_fs_readdir(directory, &entry);
        if (status <= 0)
            break;
        name_length = strlen(entry.name) + 1u;
        if (entry_count == out->entry_capacity ||
            name_length > out->names_capacity - names_used) {
            status = SALTS_ENOBUFS;
            break;
        }
        memcpy(out->names + names_used, entry.name, name_length);
        out->entries[entry_count].name = out->names + names_used;
        out->entries[entry_count].type = entry.type;
        names_used += name_length;
        ++entry_count;
    }
    {
        const int close_status = salts_fs_closedir(directory);
        if (status == SALTS_OK && close_status != SALTS_OK)
            status = close_status;
    }
    if (status != SALTS_OK) {
        out->entry_count = 0u;
        out->names_used = 0u;
        return status;
    }
    out->entry_count = entry_count;
    out->names_used = names_used;
    return SALTS_OK;
}

static int fs_execute(salts_fs_slot *slot) {
    switch (slot->operation) {
        case SALTS_FS_STAT:
            return salts_fs_stat(slot->path, slot->arguments.stat_out);
        case SALTS_FS_LSTAT:
            return salts_fs_lstat(slot->path, slot->arguments.stat_out);
        case SALTS_FS_READ_DIRECTORY:
            return fs_read_directory(slot);
        case SALTS_FS_MKDIR:
            return salts_fs_mkdir(slot->path, slot->arguments.mode);
        case SALTS_FS_RMDIR:
            return salts_fs_rmdir(slot->path);
        case SALTS_FS_RENAME:
            return salts_fs_rename(slot->path, slot->second_path);
        case SALTS_FS_UNLINK:
            return salts_fs_unlink(slot->path);
        default:
            return SALTS_EINVAL;
    }
}

static void fs_worker_run(void *user) {
    salts_fs_slot *slot = (salts_fs_slot *)user;
    int expected = SALTS_FS_SLOT_QUEUED;
    if (slot == NULL ||
        !atomic_compare_exchange_strong(&slot->phase, &expected,
                                        SALTS_FS_SLOT_RUNNING))
        return;
    if (atomic_load(&slot->cancel_requested))
        slot->result = SALTS_ECANCELED;
    else
        slot->result = fs_execute(slot);
    atomic_store(&slot->phase, SALTS_FS_SLOT_COMPLETE);
}

static void fs_worker_cancel(void *user) {
    salts_fs_slot *slot = (salts_fs_slot *)user;
    int expected = SALTS_FS_SLOT_QUEUED;
    if (slot == NULL ||
        !atomic_compare_exchange_strong(&slot->phase, &expected,
                                        SALTS_FS_SLOT_RUNNING))
        return;
    slot->result = SALTS_ECANCELED;
    atomic_store(&slot->phase, SALTS_FS_SLOT_COMPLETE);
}

static salts_fs_submit_result fs_submit(
    salts_fs_service *service, salts_fs_operation_kind operation,
    const char *path, const char *second_path, void *output, int mode) {
    salts_fs_impl *impl;
    salts_fs_slot *slot = NULL;
    cmeta_threadpool_task_t task;
    int admission;
    size_t path_length;
    size_t second_length = 0u;
    size_t index;
    uint64_t request_id;

    if (service == NULL || service->impl == NULL)
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    impl = (salts_fs_impl *)service->impl;
    if (!fs_path_fits(path, impl->path_capacity, &path_length) ||
        (second_path != NULL &&
         !fs_path_fits(second_path, impl->path_capacity, &second_length)))
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    if (atomic_load(&impl->close_requested)) {
        atomic_fetch_add(&impl->rejected_closed, 1u);
        return fs_submit_result(SALTS_FS_SUBMIT_CLOSED, 0u);
    }

    cmeta_mutex_lock(&impl->gate);
    if (atomic_load(&impl->close_requested)) {
        cmeta_mutex_unlock(&impl->gate);
        atomic_fetch_add(&impl->rejected_closed, 1u);
        return fs_submit_result(SALTS_FS_SUBMIT_CLOSED, 0u);
    }
    if (impl->next_request_id == UINT64_MAX) {
        cmeta_mutex_unlock(&impl->gate);
        return fs_submit_result(SALTS_FS_SUBMIT_ID_EXHAUSTED, 0u);
    }
    for (index = 0u; index < impl->capacity; ++index) {
        if (atomic_load(&impl->slots[index].phase) == SALTS_FS_SLOT_FREE) {
            slot = &impl->slots[index];
            break;
        }
    }
    if (slot == NULL) {
        cmeta_mutex_unlock(&impl->gate);
        atomic_fetch_add(&impl->rejected_full, 1u);
        return fs_submit_result(SALTS_FS_SUBMIT_FULL, 0u);
    }
    request_id = ++impl->next_request_id;
    memcpy(slot->path, path, path_length + 1u);
    if (second_path != NULL)
        memcpy(slot->second_path, second_path, second_length + 1u);
    else
        slot->second_path[0] = '\0';
    slot->request_id = request_id;
    slot->operation = operation;
    slot->result = SALTS_OK;
    atomic_store(&slot->cancel_requested, false);
    switch (operation) {
        case SALTS_FS_STAT:
        case SALTS_FS_LSTAT:
            slot->arguments.stat_out = (salts_fs_stat_t *)output;
            break;
        case SALTS_FS_READ_DIRECTORY:
            slot->arguments.directory_out = (salts_fs_dir_buffer *)output;
            break;
        case SALTS_FS_MKDIR:
            slot->arguments.mode = mode;
            break;
        default:
            memset(&slot->arguments, 0, sizeof(slot->arguments));
            break;
    }
    atomic_store(&slot->phase, SALTS_FS_SLOT_QUEUED);
    atomic_fetch_add(&impl->in_use, 1u);
    cmeta_mutex_unlock(&impl->gate);

    task = (cmeta_threadpool_task_t){
        .run = fs_worker_run,
        .cancel = fs_worker_cancel,
        .finalize = NULL,
        .arg = slot,
    };
    admission = cmeta_threadpool_try_submit_task(impl->executor, &task);
    if (admission != SALTS_OK) {
        cmeta_mutex_lock(&impl->gate);
        fs_slot_reset(slot);
        atomic_fetch_sub(&impl->in_use, 1u);
        cmeta_mutex_unlock(&impl->gate);
        if (admission == SALTS_ENOBUFS) {
            atomic_fetch_add(&impl->rejected_full, 1u);
            return fs_submit_result(SALTS_FS_SUBMIT_FULL, 0u);
        }
        if (admission == SALTS_ESHUTDOWN) {
            atomic_fetch_add(&impl->rejected_closed, 1u);
            return fs_submit_result(SALTS_FS_SUBMIT_CLOSED, 0u);
        }
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    }
    atomic_fetch_add(&impl->accepted, 1u);
    return fs_submit_result(SALTS_FS_SUBMIT_ACCEPTED, request_id);
}

int salts_fs_service_init(salts_fs_service *service,
                          const salts_fs_config *config) {
    salts_fs_impl *impl;
    size_t slot_bytes;
    size_t paths_per_slot;
    size_t path_bytes;
    size_t index;

    if (service == NULL || service->impl != NULL || config == NULL ||
        config->worker_count == 0u ||
        config->worker_count > (size_t)INT_MAX ||
        config->request_capacity == 0u || config->path_capacity < 2u ||
        config->completion == NULL ||
        !fs_checked_multiply(config->request_capacity,
                             sizeof(salts_fs_slot), &slot_bytes) ||
        !fs_checked_multiply(config->path_capacity, 2u, &paths_per_slot) ||
        !fs_checked_multiply(config->request_capacity, paths_per_slot,
                             &path_bytes))
        return SALTS_EINVAL;
    impl = (salts_fs_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return SALTS_ENOMEM;
    impl->slots = (salts_fs_slot *)calloc(1u, slot_bytes);
    impl->paths = (char *)calloc(1u, path_bytes);
    if (impl->slots == NULL || impl->paths == NULL) {
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return SALTS_ENOMEM;
    }
    cmeta_mutex_init(&impl->gate);
    if (impl->gate == NULL) {
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return SALTS_ENOMEM;
    }
    impl->capacity = config->request_capacity;
    impl->path_capacity = config->path_capacity;
    impl->completion = config->completion;
    impl->completion_user = config->completion_user;
    for (index = 0u; index < impl->capacity; ++index) {
        impl->slots[index].owner = impl;
        impl->slots[index].path =
            impl->paths + index * paths_per_slot;
        impl->slots[index].second_path =
            impl->slots[index].path + impl->path_capacity;
        atomic_init(&impl->slots[index].phase, SALTS_FS_SLOT_FREE);
        atomic_init(&impl->slots[index].cancel_requested, false);
    }
    atomic_init(&impl->close_requested, false);
    atomic_init(&impl->driver_active, false);
    atomic_init(&impl->accepted, 0u);
    atomic_init(&impl->completed, 0u);
    atomic_init(&impl->cancelled, 0u);
    atomic_init(&impl->in_use, 0u);
    atomic_init(&impl->rejected_full, 0u);
    atomic_init(&impl->rejected_closed, 0u);
    const cmeta_threadpool_config_t worker_config = {
        (int)config->worker_count, config->request_capacity};
    impl->executor = cmeta_threadpool_create_with_config(&worker_config);
    if (impl->executor == NULL) {
        cmeta_mutex_destroy(&impl->gate);
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return SALTS_ENOMEM;
    }
    service->impl = impl;
    return SALTS_OK;
}

salts_fs_submit_result salts_fs_try_stat(
    salts_fs_service *service, const char *path, salts_fs_stat_t *out) {
    if (out == NULL)
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, SALTS_FS_STAT, path, NULL, out, 0);
}

salts_fs_submit_result salts_fs_try_lstat(
    salts_fs_service *service, const char *path, salts_fs_stat_t *out) {
    if (out == NULL)
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, SALTS_FS_LSTAT, path, NULL, out, 0);
}

salts_fs_submit_result salts_fs_try_read_directory(
    salts_fs_service *service, const char *path, salts_fs_dir_buffer *out) {
    if (out == NULL || out->entries == NULL || out->entry_capacity == 0u ||
        out->names == NULL || out->names_capacity == 0u)
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, SALTS_FS_READ_DIRECTORY, path, NULL, out, 0);
}

salts_fs_submit_result salts_fs_try_mkdir(
    salts_fs_service *service, const char *path, int mode) {
    if (mode < 0 || mode > 0777)
        return fs_submit_result(SALTS_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, SALTS_FS_MKDIR, path, NULL, NULL, mode);
}

salts_fs_submit_result salts_fs_try_rmdir(
    salts_fs_service *service, const char *path) {
    return fs_submit(service, SALTS_FS_RMDIR, path, NULL, NULL, 0);
}

salts_fs_submit_result salts_fs_try_rename(
    salts_fs_service *service, const char *old_path,
    const char *new_path) {
    return fs_submit(service, SALTS_FS_RENAME, old_path, new_path, NULL, 0);
}

salts_fs_submit_result salts_fs_try_unlink(
    salts_fs_service *service, const char *path) {
    return fs_submit(service, SALTS_FS_UNLINK, path, NULL, NULL, 0);
}

salts_fs_cancel_status salts_fs_try_cancel(
    salts_fs_service *service, uint64_t request_id) {
    salts_fs_impl *impl;
    size_t index;
    if (service == NULL || service->impl == NULL || request_id == 0u)
        return SALTS_FS_CANCEL_INVALID_ARGUMENT;
    impl = (salts_fs_impl *)service->impl;
    cmeta_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->capacity; ++index) {
        salts_fs_slot *slot = &impl->slots[index];
        const int phase = atomic_load(&slot->phase);
        if (slot->request_id != request_id || phase == SALTS_FS_SLOT_FREE)
            continue;
        if (phase == SALTS_FS_SLOT_QUEUED) {
            atomic_store(&slot->cancel_requested, true);
            if (atomic_load(&slot->phase) != SALTS_FS_SLOT_QUEUED) {
                cmeta_mutex_unlock(&impl->gate);
                return SALTS_FS_CANCEL_ALREADY_RUNNING;
            }
            cmeta_mutex_unlock(&impl->gate);
            return SALTS_FS_CANCEL_REQUESTED;
        }
        if (phase == SALTS_FS_SLOT_RUNNING) {
            cmeta_mutex_unlock(&impl->gate);
            return SALTS_FS_CANCEL_ALREADY_RUNNING;
        }
        cmeta_mutex_unlock(&impl->gate);
        return SALTS_FS_CANCEL_NOT_FOUND;
    }
    cmeta_mutex_unlock(&impl->gate);
    return atomic_load(&impl->close_requested)
        ? SALTS_FS_CANCEL_CLOSED : SALTS_FS_CANCEL_NOT_FOUND;
}

int salts_fs_run_ready(salts_fs_service *service, size_t max_completions,
                       size_t *completed) {
    salts_fs_impl *impl;
    bool expected = false;
    size_t delivered = 0u;
    size_t index;
    if (service == NULL || service->impl == NULL ||
        max_completions == 0u || completed == NULL)
        return SALTS_EINVAL;
    impl = (salts_fs_impl *)service->impl;
    *completed = 0u;
    if (!atomic_compare_exchange_strong(&impl->driver_active, &expected, true))
        return SALTS_EBUSY;
    for (index = 0u; index < impl->capacity && delivered < max_completions;
         ++index) {
        salts_fs_slot *slot = &impl->slots[index];
        int phase = SALTS_FS_SLOT_COMPLETE;
        uint64_t request_id;
        salts_fs_operation_kind operation;
        int result;
        if (!atomic_compare_exchange_strong(&slot->phase, &phase,
                                            SALTS_FS_SLOT_DELIVERING))
            continue;
        request_id = slot->request_id;
        operation = slot->operation;
        result = slot->result;
        impl->completion(impl->completion_user, request_id, operation, result);
        if (result == SALTS_ECANCELED)
            atomic_fetch_add(&impl->cancelled, 1u);
        else
            atomic_fetch_add(&impl->completed, 1u);
        cmeta_mutex_lock(&impl->gate);
        fs_slot_reset(slot);
        atomic_fetch_sub(&impl->in_use, 1u);
        cmeta_mutex_unlock(&impl->gate);
        ++delivered;
    }
    atomic_store(&impl->driver_active, false);
    *completed = delivered;
    return SALTS_OK;
}

int salts_fs_service_close(salts_fs_service *service) {
    salts_fs_impl *impl;
    bool expected = false;
    if (service == NULL || service->impl == NULL)
        return SALTS_EINVAL;
    impl = (salts_fs_impl *)service->impl;
    if (!atomic_compare_exchange_strong(&impl->close_requested,
                                        &expected, true))
        return SALTS_EALREADY;
    int status = cmeta_threadpool_shutdown_with_policy(
        impl->executor, SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING);
    if (status != SALTS_OK) return status;
    return SALTS_OK;
}

bool salts_fs_is_quiescent(const salts_fs_service *service) {
    const salts_fs_impl *impl;
    cmeta_threadpool_stats_t executor_stats = {0};
    if (service == NULL || service->impl == NULL)
        return false;
    impl = (const salts_fs_impl *)service->impl;
    if (!atomic_load(&impl->close_requested)) return false;
    cmeta_threadpool_get_stats(impl->executor, &executor_stats);
    return (!executor_stats.accepting && executor_stats.pending_tasks == 0) &&
           atomic_load(&impl->in_use) == 0u;
}

bool salts_fs_get_stats(const salts_fs_service *service,
                        salts_fs_stats *out) {
    const salts_fs_impl *impl;
    cmeta_threadpool_stats_t executor_stats = {0};
    if (service == NULL || service->impl == NULL || out == NULL)
        return false;
    impl = (const salts_fs_impl *)service->impl;
    cmeta_threadpool_get_stats(impl->executor, &executor_stats);
    *out = (salts_fs_stats){
        .capacity = impl->capacity,
        .accepted = atomic_load(&impl->accepted),
        .running = (size_t)executor_stats.active_tasks,
        .completed = atomic_load(&impl->completed),
        .cancelled = atomic_load(&impl->cancelled),
        .in_use = atomic_load(&impl->in_use),
        .rejected_full = atomic_load(&impl->rejected_full),
        .rejected_closed = atomic_load(&impl->rejected_closed),
        .lifecycle = !atomic_load(&impl->close_requested)
            ? SALTS_FS_OPEN
            : ((!executor_stats.accepting && executor_stats.pending_tasks == 0) &&
               atomic_load(&impl->in_use) == 0u
                   ? SALTS_FS_CLOSED : SALTS_FS_CLOSING),
    };
    return true;
}

int salts_fs_destroy(salts_fs_service *service) {
    salts_fs_impl *impl;
    if (service == NULL || service->impl == NULL)
        return SALTS_EINVAL;
    impl = (salts_fs_impl *)service->impl;
    if (atomic_load(&impl->driver_active) || !salts_fs_is_quiescent(service))
        return SALTS_EBUSY;
    cmeta_threadpool_destroy(impl->executor);
    cmeta_mutex_destroy(&impl->gate);
    free(impl->paths);
    free(impl->slots);
    free(impl);
    service->impl = NULL;
    return SALTS_OK;
}
