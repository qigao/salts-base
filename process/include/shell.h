#ifndef SALTS_SHELL_H
#define SALTS_SHELL_H

#include <process_async.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct salts_shell { void *impl; } salts_shell;

typedef enum salts_shell_outcome {
  SALTS_SHELL_EXITED = 0,
  SALTS_SHELL_SIGNALED,
  SALTS_SHELL_TIMED_OUT,
  SALTS_SHELL_CANCELLED,
  SALTS_SHELL_OUTPUT_LIMIT,
  SALTS_SHELL_FAILED
} salts_shell_outcome;

/** Bytes are borrowed only during the callback; no calls on this shell may reenter it. */
typedef void (*salts_shell_output_fn)(void *user, salts_process_stream stream,
                                     const void *bytes, size_t length);

typedef struct salts_shell_options {
  /** Nonempty command text, interpreted by the platform's system shell. */
  const char *command;
  /** Binary input copied at start; NULL is valid only with input_size == 0. */
  const void *input;
  size_t input_size;
  /** NULL inherits the parent's working directory. */
  const char *cwd;
  /** NULL-terminated KEY=VALUE overrides, or NULL to inherit. */
  const char *const *env;
  /** If true, env is the complete child environment. */
  bool clean_environment;
  /** Core execution deadline; zero disables it. */
  uint64_t timeout_ms;
  /** Nonzero input admission limit and shared stdout+stderr capture limit. */
  size_t max_input_bytes;
  size_t max_output_bytes;
  /** Nonzero maximum bytes per I/O operation. */
  size_t io_chunk_bytes;
  /** Explicit NativeIO backend; unsupported pipe backends return ENOTSUP. */
  native_io_backend_kind backend_kind;
  /** Optional byte-chunk callback, per stream; runs on the driver thread. */
  salts_shell_output_fn output;
  void *output_user;
} salts_shell_options;

typedef struct salts_shell_result {
  salts_shell_outcome outcome;
  salts_process_result_t process;
  int error;
  size_t stdin_bytes;
  size_t stdout_bytes;
  size_t stderr_bytes;
} salts_shell_result;

/** 30s deadline, 16 MiB input/output limits, 4 KiB chunks, native platform backend. */
void salts_shell_options_init(salts_shell_options *options);

/**
 * Starts an explicit system-shell command. shell must be zero initialized.
 * Options are borrowed for this call; input is copied, output_user is borrowed
 * through destroy. All APIs and callbacks are single-threaded, non-reentrant.
 * Empty commands, zero capacities and input beyond its limit return EINVAL;
 * allocation/backend/spawn errors propagate and leave shell empty.
 * Output has a shared stdout+stderr byte limit; overflow stops the process tree
 * and reports OUTPUT_LIMIT after cleanup. Captured bytes are never discarded.
 */
int salts_shell_start(salts_shell *shell, const salts_shell_options *options);

/**
 * Drives up to max_steps process transitions and one submission per stream.
 * max_steps must be positive, progressed non-NULL; returns EINVAL otherwise.
 * progressed counts transitions plus admissions, and can exceed max_steps by 3.
 * Reentry returns EBUSY. Other driver errors propagate; the handle stays owned.
 */
int salts_shell_run_ready(salts_shell *shell, size_t max_steps, size_t *progressed);

/**
 * Returns EBUSY until the child, output EOF and I/O cleanup have settled;
 * otherwise copies the terminal result. A nonzero exit is an EXITED result,
 * not an API error. Invalid arguments return EINVAL. Does not drive execution.
 */
int salts_shell_poll(const salts_shell *shell, salts_shell_result *result);

/**
 * Stops admission and requests process-tree termination; continue driving to
 * terminal. Returns OK idempotently, EINVAL for an empty handle, EBUSY on reentry,
 * or the process close error. A completed natural exit keeps its original result.
 */
int salts_shell_cancel(salts_shell *shell);

/**
 * Starts and drives to terminal, retaining shell and its captured output.
 * Returns start/driver errors or OK with result (including timeout/cancellation).
 * On a driver error, a nonempty shell remains caller-owned for cancel/drain.
 */
int salts_shell_execute(salts_shell *shell, const salts_shell_options *options,
                         salts_shell_result *result);

/**
 * Borrowed NUL-terminated byte buffers, valid until destroy. Embedded NUL is
 * supported; use result byte counts. NULL for an invalid shell. Inspect only
 * between driver calls; callbacks receive their own bounded byte view.
 */
const char *salts_shell_stdout(const salts_shell *shell);
const char *salts_shell_stderr(const salts_shell *shell);

/**
 * Returns EBUSY before terminal cleanup, EINVAL for an empty shell. Propagates
 * cleanup errors; shell is cleared whenever the underlying owner was destroyed.
 */
int salts_shell_destroy(salts_shell *shell);

#ifdef __cplusplus
}
#endif
#endif
