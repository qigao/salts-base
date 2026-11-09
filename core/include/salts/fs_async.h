#ifndef SALTS_FS_ASYNC_H
#define SALTS_FS_ASYNC_H

#include <salts/api.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <salts/fs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fs_service {
    void *impl;
} fs_service;

typedef enum fs_operation_kind {
    SALTS_FS_STAT = 0,
    SALTS_FS_LSTAT,
    SALTS_FS_READ_DIRECTORY,
    SALTS_FS_MKDIR,
    SALTS_FS_RMDIR,
    SALTS_FS_RENAME,
    SALTS_FS_UNLINK
} fs_operation_kind;

typedef enum fs_submit_status {
    SALTS_FS_SUBMIT_ACCEPTED = 0,
    SALTS_FS_SUBMIT_INVALID_ARGUMENT,
    SALTS_FS_SUBMIT_FULL,
    SALTS_FS_SUBMIT_CLOSED,
    SALTS_FS_SUBMIT_ID_EXHAUSTED
} fs_submit_status;

typedef struct fs_submit_result {
    fs_submit_status status;
    uint64_t request_id;
} fs_submit_result;

typedef enum fs_cancel_status {
    SALTS_FS_CANCEL_REQUESTED = 0,
    SALTS_FS_CANCEL_ALREADY_RUNNING,
    SALTS_FS_CANCEL_NOT_FOUND,
    SALTS_FS_CANCEL_CLOSED,
    SALTS_FS_CANCEL_INVALID_ARGUMENT
} fs_cancel_status;

typedef enum fs_lifecycle {
    SALTS_FS_OPEN = 0,
    SALTS_FS_CLOSING,
    SALTS_FS_CLOSED
} fs_lifecycle;

typedef struct fs_dir_buffer {
    /* Entries and names are caller-owned through terminal callback return. */
    fs_dirent_t *entries;
    size_t entry_capacity;
    char *names;
    size_t names_capacity;
    size_t entry_count;
    size_t names_used;
} fs_dir_buffer;

typedef void (*fs_completion_fn)(
    void *user, uint64_t request_id,
    fs_operation_kind operation, int result);

typedef struct fs_config {
    size_t worker_count;
    size_t request_capacity;
    size_t path_capacity;
    fs_completion_fn completion;
    void *completion_user;
} fs_config;

typedef struct fs_stats {
    size_t capacity;
    size_t accepted;
    size_t running;
    size_t completed;
    size_t cancelled;
    size_t in_use;
    size_t rejected_full;
    size_t rejected_closed;
    fs_lifecycle lifecycle;
} fs_stats;

/**
 * Initialize an owning bounded worker-backed filesystem service.
 *
 * @param service Zero-initialized destination handle.
 * @param config Positive worker/request/path capacities and terminal callback.
 * @return SALTS_OK, SALTS_EINVAL for an invalid contract, or SALTS_ENOMEM.
 *
 * No pathname side effect occurs during initialization. Successful destroy
 * restores service to the zero state.
 */
SALTS_API int fs_service_init(fs_service *service,
                          const fs_config *config);
/**
 * Submit metadata lookup following links.
 * @param service Initialized service.
 * @param path Nonempty path copied on accepted submission.
 * @param out Caller-owned result borrowed through callback return.
 * @return Exact admission result; rejection does not borrow out.
 */
SALTS_API fs_submit_result fs_try_stat(
    fs_service *service, const char *path, fs_stat_t *out);
/** Same contract as fs_try_stat(), without following the final link. */
SALTS_API fs_submit_result fs_try_lstat(
    fs_service *service, const char *path, fs_stat_t *out);
/**
 * Submit bounded directory enumeration.
 * @param out Caller-owned entries and name arena borrowed through callback.
 * @return Accepted work reports SALTS_ENOBUFS through the callback if either
 * capacity cannot hold the complete listing; used counts are then zero.
 */
SALTS_API fs_submit_result fs_try_read_directory(
    fs_service *service, const char *path,
    fs_dir_buffer *out);
/** Submit creation of one directory; parents must already exist. */
SALTS_API fs_submit_result fs_try_mkdir(
    fs_service *service, const char *path, int mode);
/** Submit removal of one empty directory. */
SALTS_API fs_submit_result fs_try_rmdir(
    fs_service *service, const char *path);
/** Submit same-filesystem rename/replacement with both paths copied. */
SALTS_API fs_submit_result fs_try_rename(
    fs_service *service, const char *old_path,
    const char *new_path);
/** Submit deletion of a non-directory filesystem object. */
SALTS_API fs_submit_result fs_try_unlink(
    fs_service *service, const char *path);
/**
 * Request cooperative cancellation.
 * Queued work can be cancelled. An already-running blocking syscall cannot be
 * revoked, and its actual result remains authoritative.
 */
SALTS_API fs_cancel_status fs_try_cancel(
    fs_service *service, uint64_t request_id);
/**
 * Deliver at most max_completions terminal callbacks on the calling thread.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EBUSY for another/reentrant driver.
 */
SALTS_API int fs_run_ready(fs_service *service, size_t max_completions,
                       size_t *completed);
/** Stop admission and cancel queued work without blocking on running syscalls. */
SALTS_API int fs_service_close(fs_service *service);
/** Return true after close, worker settlement, and all callback releases. */
SALTS_API bool fs_is_quiescent(const fs_service *service);
/** Copy an observational bounded-protocol snapshot to out. */
SALTS_API bool fs_get_stats(const fs_service *service,
                        fs_stats *out);
/** Destroy a quiescent service, or return SALTS_EBUSY while work remains. */
SALTS_API int fs_destroy(fs_service *service);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_FS_ASYNC_H */
