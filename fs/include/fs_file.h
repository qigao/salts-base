#ifndef SALTS_FS_FILE_H
#define SALTS_FS_FILE_H
#include <native_io.h>
/**
 * Bounded asynchronous regular files. Zero-initialize handles and retain their
 * addresses until destroy; copying a live handle does not share ownership.
 * Calls return SALTS_OK or a Salts/negative native error. Invalid arguments
 * return EINVAL, allocation failure ENOMEM, unsupported operations ENOTSUP.
 * Lifecycle/drive require one owner; submission/cancel support MPSC. Join
 * producers before lifecycle calls. Callbacks must return normally and run
 * only from run_ready; their completion pointer is borrowed during that call.
 */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct salts_fs_file_runtime { void *impl; } salts_fs_file_runtime;
typedef struct salts_fs_file { void *impl; } salts_fs_file;
enum {
  SALTS_FS_FILE_READ = 1u, SALTS_FS_FILE_WRITE = 2u,
  SALTS_FS_FILE_CREATE = 4u, SALTS_FS_FILE_TRUNCATE = 8u
};
typedef void (*salts_fs_file_completion_fn)(void *user, uint64_t request_id,
    native_io_operation_kind operation, const native_io_completion *completion);
typedef struct {
  native_io_backend_kind backend_kind;
  size_t file_capacity, request_capacity, completion_batch_capacity;
} salts_fs_file_runtime_config;
typedef struct {
  native_io_backend_kind backend_kind;
  size_t request_capacity, completion_batch_capacity;
  unsigned open_flags;
  /** POSIX creation permissions (subject to umask); ignored on Windows. */
  unsigned create_mode;
  salts_fs_file_completion_fn completion;
  void *completion_user;
  /** Borrowed shared runtime through file destroy. When set, backend/capacity
   * fields above must be zero; otherwise open owns a one-file private runtime. */
  salts_fs_file_runtime *runtime;
} salts_fs_file_config;

/** Initialize a zero handle. Backend is explicit: IOCP read/write, Linux
 * io_uring read/write/flush, macOS Darwin AIO read/write/flush. No fallback.
 * Capacities must be positive, batch <= requests. Failure leaves runtime empty. */
int salts_fs_file_runtime_init(salts_fs_file_runtime *runtime, const salts_fs_file_runtime_config *config);
/** max_steps > 0 and progressed != NULL. Counts bounded transitions/callbacks;
 * EBUSY rejects concurrent/reentrant drive. Native driver errors propagate. */
int salts_fs_file_runtime_run_ready(salts_fs_file_runtime *runtime, size_t max_steps, size_t *progressed);
/** Stop all runtime admission and cancel pending requests. */
int salts_fs_file_runtime_close(salts_fs_file_runtime *runtime);
/** True after runtime close and callback drain; open files may still exist. */
bool salts_fs_file_runtime_is_quiescent(const salts_fs_file_runtime *runtime);
/** EBUSY preserves a runtime with open files or pending terminal callbacks. */
int salts_fs_file_runtime_destroy(salts_fs_file_runtime *runtime);
/** Synchronous path admission/open; data operations are asynchronous. Unsupported
 * backends fail before creation/truncation. Lifecycle/open/drive belong to one
 * owner; submission/cancel are MPSC. Stop producers before close/destroy.
 * config is copied; callback user is borrowed through destroy. An injected
 * runtime must remain alive. File capacity exhaustion returns ENOBUFS before
 * creation/truncation. Native open errors propagate and leave file empty. */
int salts_fs_file_open(salts_fs_file *file, const char *utf8_path, const salts_fs_file_config *config);
/** Borrow writable/immutable bytes respectively until terminal callback return.
 * Rejection clears request_id and retains nothing. Partial I/O is reported.
 * size must be 1..UINT32_MAX and offset + size <= INT64_MAX. Admission returns
 * ENOBUFS for full capacity, ESHUTDOWN after close, EPERM for wrong access, or
 * ERANGE when IDs are exhausted. Exactly one callback follows acceptance;
 * kind/status/bytes carry OK, EOF, CANCELLED or FAILED. Native operation errors
 * after acceptance are callback results, not admission failures. */
int salts_fs_file_read_at(salts_fs_file *file, void *buffer, size_t size, uint64_t offset, uint64_t *request_id);
int salts_fs_file_write_at(salts_fs_file *file, const void *buffer, size_t size, uint64_t offset, uint64_t *request_id);
/** Call after writes have completed when durability is required; a flush is not
 * an ordering fence for concurrent producer submissions. IOCP returns ENOTSUP. */
int salts_fs_file_flush(salts_fs_file *file, uint64_t *request_id);
/** Nonzero ID must belong to this file. ENOENT means no cancellable request
 * belongs to this file. Success does not imply a CANCELLED result. */
int salts_fs_file_cancel(salts_fs_file *file, uint64_t request_id);
/** Drives this file's runtime; a shared runtime may deliver other files' callbacks. */
int salts_fs_file_run_ready(salts_fs_file *file, size_t max_steps, size_t *progressed);
/** Idempotently close this file's admission and request cancellation. */
int salts_fs_file_close(salts_fs_file *file);
bool salts_fs_file_is_quiescent(const salts_fs_file *file);
/** Only after close and all borrows return. File handle closes before backend
 * metadata release. EBUSY preserves ownership. Private runtime is also destroyed. */
int salts_fs_file_destroy(salts_fs_file *file);
#ifdef __cplusplus
}
#endif
#endif
