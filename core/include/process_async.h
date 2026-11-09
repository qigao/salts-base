#ifndef SALTS_PROCESS_ASYNC_H
#define SALTS_PROCESS_ASYNC_H
#include <process.h>
#include <native_io.h>
/** Zero-initialize handles and retain their addresses until destroy. Copying
 * a live handle does not share ownership. Calls return Salts/negative native
 * errors. Callbacks must return normally; their event pointer is borrowed only
 * during the call. Join producers before owner lifecycle operations. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct salts_process_async { void *impl; } salts_process_async;
typedef enum {
  SALTS_PROCESS_STDIN = 0, SALTS_PROCESS_STDOUT = 1, SALTS_PROCESS_STDERR = 2
} salts_process_stream;
typedef void (*salts_process_async_completion_fn)(void *user, uint64_t request_id,
    salts_process_stream stream, const native_io_completion *completion);
typedef struct {
  native_io_backend_kind backend_kind;
  size_t request_capacity, completion_batch_capacity;
  salts_process_async_completion_fn completion;
  void *completion_user;
} salts_process_async_config;
typedef struct {
  size_t request_capacity, pending_requests;
  size_t stdin_pending, stdout_pending, stderr_pending;
  bool stdin_open, close_requested;
} salts_process_async_stats;

/** Start one Core process owner with three asynchronous native pipes. Options
 * and config are borrowed only for this call. Core capture/pipe flags conflict
 * and are rejected. Shell-command options retain the Core shell contract.
 * IOCP, epoll, io_uring or kqueue is selected explicitly without fallback.
 * Lifecycle and run_ready have one owner; submissions/cancel are MPSC.
 * Capacities must be positive, batch <= requests. Callback user is borrowed
 * through destroy. Failure leaves process empty; invalid arguments, allocation
 * failure and unsupported backends return EINVAL, ENOMEM and ENOTSUP.
 * Native spawn errors propagate. */
int salts_process_async_start(salts_process_async *process,
    const salts_process_options_t *options, const salts_process_async_config *config);
/** Accepted buffers remain borrowed through exactly one terminal callback.
 * Partial transfers and EOF are reported without copying or implicit retries.
 * Rejection clears id and retains no storage. Size must be 1..UINT32_MAX.
 * Admission errors include EINVAL, ENOBUFS (capacity), ESHUTDOWN (stream or
 * process closed) and ERANGE (IDs exhausted). Accepted native failures are
 * FAILED callback results. Concurrent read buffers must not overlap. */
int salts_process_async_write_stdin(salts_process_async *process, const void *data, size_t size, uint64_t *id);
int salts_process_async_read_stdout(salts_process_async *process, void *data, size_t size, uint64_t *id);
int salts_process_async_read_stderr(salts_process_async *process, void *data, size_t size, uint64_t *id);
/** Cancel a nonzero live ID; ENOENT means no cancellable request remains.
 * Success does not imply CANCELLED; completion may already have won. */
int salts_process_async_cancel(salts_process_async *process, uint64_t id);
/** Owner only. EBUSY preserves stdin while a queued/in-flight write borrows it.
 * Later writes return ESHUTDOWN. Repeated close is OK. */
int salts_process_async_close_stdin(salts_process_async *process);
/** Copy the Core terminal result, or return EBUSY while the child is running. */
int salts_process_async_poll(const salts_process_async *process, salts_process_result_t *result);
/** Request Core process-tree termination without closing pipe admission. */
int salts_process_async_terminate(salts_process_async *process);
/** Owner-only driver; callbacks execute here. Reentrant/concurrent drive is
 * rejected. This drives pipes, while the Core owner retains process state. */
int salts_process_async_run_ready(salts_process_async *process, size_t max_steps, size_t *progressed);
/** Owner-only snapshot; false for an empty handle or NULL destination. */
bool salts_process_async_get_stats(const salts_process_async *process, salts_process_async_stats *stats);
/** Join producers first. Close admission, cancel pipes and terminate the child;
 * continue driving until quiescent, then destroy. No pipe closes before drain. */
int salts_process_async_close(salts_process_async *process);
bool salts_process_async_is_quiescent(const salts_process_async *process);
int salts_process_async_destroy(salts_process_async *process);
#ifdef __cplusplus
}
#endif
#endif
