#ifndef SALTS_SHELL_H
#define SALTS_SHELL_H

#include <core/api.h>

#include <process_async.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct shell { void *impl; } shell;

typedef enum shell_outcome {
  SALTS_SHELL_EXITED = 0,
  SALTS_SHELL_SIGNALED,
  SALTS_SHELL_TIMED_OUT,
  SALTS_SHELL_CANCELLED,
  SALTS_SHELL_OUTPUT_LIMIT,
  SALTS_SHELL_FAILED
} shell_outcome;

/** Bytes are borrowed only during the callback; no calls on this shell may reenter it. */
typedef void (*shell_output_fn)(void *user, process_stream stream,
                                     const void *bytes, size_t length);

typedef struct shell_options {
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
  shell_output_fn output;
  void *output_user;
} shell_options;

typedef struct shell_result {
  shell_outcome outcome;
  process_result_t process;
  int error;
  size_t stdin_bytes;
  size_t stdout_bytes;
  size_t stderr_bytes;
} shell_result;

/** 30s deadline, 16 MiB input/output limits, 4 KiB chunks, native platform backend. */
void shell_options_init(shell_options *options);

/**
 * Starts an explicit system-shell command. shell must be zero initialized.
 * Options are borrowed for this call; input is copied, output_user is borrowed
 * through destroy. All APIs and callbacks are single-threaded, non-reentrant.
 * Empty commands, zero capacities and input beyond its limit return EINVAL;
 * allocation/backend/spawn errors propagate and leave shell empty.
 * Output has a shared stdout+stderr byte limit; overflow stops the process tree
 * and reports OUTPUT_LIMIT after cleanup. Captured bytes are never discarded.
 */
int shell_start(shell *shell, const shell_options *options);

/**
 * Drives up to max_steps process transitions and one submission per stream.
 * max_steps must be positive, progressed non-NULL; returns EINVAL otherwise.
 * progressed counts transitions plus admissions, and can exceed max_steps by 3.
 * Reentry returns EBUSY. Other driver errors propagate; the handle stays owned.
 */
int shell_run_ready(shell *shell, size_t max_steps, size_t *progressed);

/**
 * Returns EBUSY until the child, output EOF and I/O cleanup have settled;
 * otherwise copies the terminal result. A nonzero exit is an EXITED result,
 * not an API error. Invalid arguments return EINVAL. Does not drive execution.
 */
int shell_poll(const shell *shell, shell_result *result);

/**
 * Stops admission and requests process-tree termination; continue driving to
 * terminal. Returns OK idempotently, EINVAL for an empty handle, EBUSY on reentry,
 * or the process close error. A completed natural exit keeps its original result.
 */
int shell_cancel(shell *shell);

/**
 * Starts and drives to terminal, retaining shell and its captured output.
 * Returns start/driver errors or OK with result (including timeout/cancellation).
 * On a driver error, a nonempty shell remains caller-owned for cancel/drain.
 */
int shell_execute(shell *shell, const shell_options *options,
                         shell_result *result);

/**
 * Borrowed NUL-terminated byte buffers, valid until destroy. Embedded NUL is
 * supported; use result byte counts. NULL for an invalid shell. Inspect only
 * between driver calls; callbacks receive their own bounded byte view.
 */
const char *shell_stdout(const shell *shell);
const char *shell_stderr(const shell *shell);

/**
 * Returns EBUSY before terminal cleanup, EINVAL for an empty shell. Propagates
 * cleanup errors; shell is cleared whenever the underlying owner was destroyed.
 */
int shell_destroy(shell *shell);

#ifdef __cplusplus
}
#endif
#endif
