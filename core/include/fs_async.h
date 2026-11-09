#ifndef SALTS_FS_ASYNC_H
#define SALTS_FS_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <fs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct salts_fs_service {
    void *impl;
} salts_fs_service;

typedef enum salts_fs_operation_kind {
    SALTS_FS_STAT = 0,
    SALTS_FS_LSTAT,
    SALTS_FS_READ_DIRECTORY,
    SALTS_FS_MKDIR,
    SALTS_FS_RMDIR,
    SALTS_FS_RENAME,
    SALTS_FS_UNLINK
} salts_fs_operation_kind;

typedef enum salts_fs_submit_status {
    SALTS_FS_SUBMIT_ACCEPTED = 0,
    SALTS_FS_SUBMIT_INVALID_ARGUMENT,
    SALTS_FS_SUBMIT_FULL,
    SALTS_FS_SUBMIT_CLOSED,
    SALTS_FS_SUBMIT_ID_EXHAUSTED
} salts_fs_submit_status;

typedef struct salts_fs_submit_result {
    salts_fs_submit_status status;
    uint64_t request_id;
} salts_fs_submit_result;

typedef enum salts_fs_cancel_status {
    SALTS_FS_CANCEL_REQUESTED = 0,
    SALTS_FS_CANCEL_ALREADY_RUNNING,
    SALTS_FS_CANCEL_NOT_FOUND,
    SALTS_FS_CANCEL_CLOSED,
    SALTS_FS_CANCEL_INVALID_ARGUMENT
} salts_fs_cancel_status;

typedef enum salts_fs_lifecycle {
    SALTS_FS_OPEN = 0,
    SALTS_FS_CLOSING,
    SALTS_FS_CLOSED
} salts_fs_lifecycle;

typedef struct salts_fs_dir_buffer {
    /* Entries and names are caller-owned through terminal callback return. */
    salts_fs_dirent_t *entries;
    size_t entry_capacity;
    char *names;
    size_t names_capacity;
    size_t entry_count;
    size_t names_used;
} salts_fs_dir_buffer;

typedef void (*salts_fs_completion_fn)(
    void *user, uint64_t request_id,
    salts_fs_operation_kind operation, int result);

typedef struct salts_fs_config {
    size_t worker_count;
    size_t request_capacity;
    size_t path_capacity;
    salts_fs_completion_fn completion;
    void *completion_user;
} salts_fs_config;

typedef struct salts_fs_stats {
    size_t capacity;
    size_t accepted;
    size_t running;
    size_t completed;
    size_t cancelled;
    size_t in_use;
    size_t rejected_full;
    size_t rejected_closed;
    salts_fs_lifecycle lifecycle;
} salts_fs_stats;

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
int salts_fs_service_init(salts_fs_service *service,
                          const salts_fs_config *config);
/**
 * Submit metadata lookup following links.
 * @param service Initialized service.
 * @param path Nonempty path copied on accepted submission.
 * @param out Caller-owned result borrowed through callback return.
 * @return Exact admission result; rejection does not borrow out.
 */
salts_fs_submit_result salts_fs_try_stat(
    salts_fs_service *service, const char *path, salts_fs_stat_t *out);
/** Same contract as salts_fs_try_stat(), without following the final link. */
salts_fs_submit_result salts_fs_try_lstat(
    salts_fs_service *service, const char *path, salts_fs_stat_t *out);
/**
 * Submit bounded directory enumeration.
 * @param out Caller-owned entries and name arena borrowed through callback.
 * @return Accepted work reports SALTS_ENOBUFS through the callback if either
 * capacity cannot hold the complete listing; used counts are then zero.
 */
salts_fs_submit_result salts_fs_try_read_directory(
    salts_fs_service *service, const char *path,
    salts_fs_dir_buffer *out);
/** Submit creation of one directory; parents must already exist. */
salts_fs_submit_result salts_fs_try_mkdir(
    salts_fs_service *service, const char *path, int mode);
/** Submit removal of one empty directory. */
salts_fs_submit_result salts_fs_try_rmdir(
    salts_fs_service *service, const char *path);
/** Submit same-filesystem rename/replacement with both paths copied. */
salts_fs_submit_result salts_fs_try_rename(
    salts_fs_service *service, const char *old_path,
    const char *new_path);
/** Submit deletion of a non-directory filesystem object. */
salts_fs_submit_result salts_fs_try_unlink(
    salts_fs_service *service, const char *path);
/**
 * Request cooperative cancellation.
 * Queued work can be cancelled. An already-running blocking syscall cannot be
 * revoked, and its actual result remains authoritative.
 */
salts_fs_cancel_status salts_fs_try_cancel(
    salts_fs_service *service, uint64_t request_id);
/**
 * Deliver at most max_completions terminal callbacks on the calling thread.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EBUSY for another/reentrant driver.
 */
int salts_fs_run_ready(salts_fs_service *service, size_t max_completions,
                       size_t *completed);
/** Stop admission and cancel queued work without blocking on running syscalls. */
int salts_fs_service_close(salts_fs_service *service);
/** Return true after close, worker settlement, and all callback releases. */
bool salts_fs_is_quiescent(const salts_fs_service *service);
/** Copy an observational bounded-protocol snapshot to out. */
bool salts_fs_get_stats(const salts_fs_service *service,
                        salts_fs_stats *out);
/** Destroy a quiescent service, or return SALTS_EBUSY while work remains. */
int salts_fs_destroy(salts_fs_service *service);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_FS_ASYNC_H */
