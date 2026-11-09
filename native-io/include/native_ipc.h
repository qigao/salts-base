#ifndef SALTS_NATIVE_IPC_H
#define SALTS_NATIVE_IPC_H

#include <error_codes.h>

#ifndef __cplusplus
  #include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cmeta_ipc_request_id;

typedef enum cmeta_ipc_pipe_direction {
  SALTS_IPC_PIPE_READ = 1u,
  SALTS_IPC_PIPE_WRITE = 2u,
  SALTS_IPC_PIPE_DUPLEX = 3u
} cmeta_ipc_pipe_direction;

typedef enum cmeta_ipc_pipe_capability {
  SALTS_IPC_WINDOWS_SERVER_ACCEPT = 0,
  SALTS_IPC_WINDOWS_CLIENT_CONNECT,
  SALTS_IPC_POSIX_FIFO_OPEN
} cmeta_ipc_pipe_capability;

/**
 * Owns one native pipe handle until close or field-by-field transfer. C copy
 * syntax does not duplicate ownership. native_io_flags is suitable for
 * native_io_backend_attach_pipe().
 */
typedef struct cmeta_ipc_pipe_endpoint {
  uintptr_t handle;
  uint32_t native_io_flags;
} cmeta_ipc_pipe_endpoint;

typedef struct cmeta_ipc_pipe_server {
  void *impl;
} cmeta_ipc_pipe_server;

typedef enum cmeta_ipc_completion_kind {
  SALTS_IPC_COMPLETION_OK = 1,
  SALTS_IPC_COMPLETION_CANCELLED,
  SALTS_IPC_COMPLETION_FAILED
} cmeta_ipc_completion_kind;

typedef struct cmeta_ipc_completion {
  cmeta_ipc_request_id request_id;
  cmeta_ipc_completion_kind kind;
  int status;
  uint32_t native_status;
} cmeta_ipc_completion;

/**
 * Receives one terminal accept. completion is borrowed only for the callback.
 * A valid endpoint transfers ownership to the callback and must eventually be
 * closed or transferred field by field; copying it does not duplicate ownership.
 */
typedef void (*cmeta_ipc_pipe_accept_fn)(void *user, const cmeta_ipc_completion *completion,
                                         cmeta_ipc_pipe_endpoint endpoint);

typedef struct cmeta_ipc_pipe_server_config {
  const char *name;
  cmeta_ipc_pipe_direction direction;
  size_t request_capacity;
  size_t input_buffer_size;
  size_t output_buffer_size;
  cmeta_ipc_pipe_accept_fn completion;
  void *completion_user;
} cmeta_ipc_pipe_server_config;

typedef struct cmeta_ipc_pipe_server_stats {
  size_t request_capacity;
  size_t active_requests;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  bool admission_open;
} cmeta_ipc_pipe_server_stats;

/** Returns compile-time support for one explicit rendezvous capability. */
bool cmeta_ipc_pipe_capability_supported(cmeta_ipc_pipe_capability capability);

/** Initializes endpoint to the invalid, unowned state. NULL is ignored. */
void cmeta_ipc_pipe_endpoint_init(cmeta_ipc_pipe_endpoint *endpoint);

/** Returns whether endpoint currently owns one native handle or descriptor. */
bool cmeta_ipc_pipe_endpoint_valid(const cmeta_ipc_pipe_endpoint *endpoint);

/**
 * Invalidates endpoint, then closes its owned identity. Closing an invalid
 * endpoint is idempotent. Returns SALTS_EINVAL for NULL or a negative native
 * error when close fails.
 */
int cmeta_ipc_pipe_endpoint_close(cmeta_ipc_pipe_endpoint *endpoint);

/**
 * Initializes a fixed-capacity, single-owner Windows named-pipe accept server.
 * server must be zero-initialized and remains the sole lifecycle fact source.
 * name must be nonempty and shorter than 256 bytes; direction must be READ,
 * WRITE, or DUPLEX; request_capacity and both buffer sizes must be positive.
 * request_capacity bounds server-owned accepts; an endpoint transferred by a
 * successful callback no longer consumes a request slot.
 * Returns SALTS_ENOTSUP outside Windows, SALTS_ENOMEM on allocation failure,
 * or SALTS_EINVAL for malformed state/configuration.
 */
int cmeta_ipc_pipe_server_init(cmeta_ipc_pipe_server *server,
                               const cmeta_ipc_pipe_server_config *config);

/**
 * Starts one overlapped accept without waiting. On success, out_request_id is
 * nonzero and exactly one terminal callback follows. Returns SALTS_ENOBUFS at
 * capacity, SALTS_ESHUTDOWN after close, or a negative native error.
 */
int cmeta_ipc_pipe_server_try_accept(cmeta_ipc_pipe_server *server,
                                     cmeta_ipc_request_id *out_request_id);

/** Requests cancellation; the later callback remains authoritative. */
int cmeta_ipc_pipe_server_cancel(cmeta_ipc_pipe_server *server, cmeta_ipc_request_id request_id);

/**
 * Polls at most max_events terminal accepts on the owner thread. A successful
 * callback owns endpoint before invocation; failed/cancelled callbacks receive
 * an invalid endpoint. Callbacks must not reenter the server.
 */
int cmeta_ipc_pipe_server_observe(cmeta_ipc_pipe_server *server, size_t max_events,
                                  size_t *out_count);

/**
 * Copies borrowed Windows event handles for pending accepts, without waiting.
 * out_ready also reports synchronous/already-ready terminals that need observe
 * even when no event was signaled. Check it before entering the host wait.
 * On ENOBUFS, out_count is the required capacity and no partial array is written.
 * A zero-capacity NULL array is valid. Handles must never be closed/reset by the
 * caller; refresh after any server mutation and unregister before observe/close/
 * destroy can release them. Single-owner only; no callback reentry. The host
 * supplies its own wake source for stop/commands and respects native wait limits.
 * Returns SALTS_OK, SALTS_ENOBUFS, SALTS_EINVAL, SALTS_EBUSY, or SALTS_ENOTSUP.
 */
int cmeta_ipc_pipe_server_wait_sources(const cmeta_ipc_pipe_server *server,
                                       uintptr_t *out_handles, size_t capacity,
                                       size_t *out_count, bool *out_ready);

/** Stops admission and requests cancellation for every pending accept. */
int cmeta_ipc_pipe_server_close(cmeta_ipc_pipe_server *server);

/** Returns true only after close and authoritative terminal drain. */
bool cmeta_ipc_pipe_server_is_quiescent(const cmeta_ipc_pipe_server *server);

/** Copies fixed-capacity lifecycle counters without advancing the server. */
bool cmeta_ipc_pipe_server_get_stats(const cmeta_ipc_pipe_server *server,
                                     cmeta_ipc_pipe_server_stats *out_stats);

/** Destroys a closed, quiescent server; otherwise returns SALTS_EBUSY. */
int cmeta_ipc_pipe_server_destroy(cmeta_ipc_pipe_server *server);

/**
 * Performs one Windows CreateFile attempt and never waits or retries. Success
 * transfers one overlapped endpoint to out_endpoint. Missing/busy instances
 * return SALTS_ENOENT/SALTS_EBUSY; non-Windows hosts return SALTS_ENOTSUP.
 * An opened non-pipe handle is closed and rejected with SALTS_ENOTSUP; failure
 * leaves out_endpoint invalid and unowned.
 * out_endpoint must first be initialized to its invalid state.
 */
int cmeta_ipc_named_pipe_connect(const char *name, cmeta_ipc_pipe_direction direction,
                                 cmeta_ipc_pipe_endpoint *out_endpoint);

/**
 * Opens an existing POSIX FIFO as one nonblocking control-plane operation.
 * READ and WRITE are supported; DUPLEX is rejected. A writer without a reader
 * returns SALTS_EPIPE. The function never creates, unlinks, waits, or retries a
 * rendezvous failure; an interrupted syscall may be restarted. out_endpoint
 * must first be initialized to its invalid state.
 */
int cmeta_ipc_fifo_open(const char *path, cmeta_ipc_pipe_direction direction,
                        cmeta_ipc_pipe_endpoint *out_endpoint);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_NATIVE_IPC_H */
