/**
 * @file process.h
 * @brief Cross-platform child process management without an event-loop dependency.
 *
 * A salts_process_t exclusively owns the child process, its process group or
 * Windows Job Object, redirected standard-I/O handles, monitor thread, and
 * terminal result. Operations are thread-safe except destroy, which requires
 * exclusive ownership. Destroying a running handle terminates and reaps the
 * owned process tree.
 * When the root exits, remaining members of its process group/Job Object are
 * terminated before its result is published. POSIX descendants that leave
 * the owned process group are outside this ownership.
 */

#ifndef SALTS_PROCESS_H
#define SALTS_PROCESS_H

#include <error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_PROCESS_DEFAULT_MAX_OUTPUT_BYTES (16U * 1024U * 1024U)

typedef struct salts_process_s salts_process_t;

typedef enum {
  SALTS_PROCESS_STARTING = 0,
  SALTS_PROCESS_RUNNING,
  SALTS_PROCESS_EXITED,
  SALTS_PROCESS_SIGNALED,
  SALTS_PROCESS_TIMED_OUT,
  SALTS_PROCESS_TERMINATED,
  SALTS_PROCESS_OUTPUT_LIMIT_EXCEEDED,
  SALTS_PROCESS_WAIT_FAILED
} salts_process_state_t;

typedef enum {
  SALTS_PROCESS_PIPE_STDIN = 1U << 0,
  SALTS_PROCESS_CAPTURE_STDOUT = 1U << 1,
  SALTS_PROCESS_CAPTURE_STDERR = 1U << 2,
  /** Do not inherit parent variables; env becomes the complete child environment. */
  SALTS_PROCESS_CLEAN_ENVIRONMENT = 1U << 3,
  /**
   * Interpret program as a shell command; args must be NULL. Remaining job/
   * process-group members are terminated when the root shell exits, before
   * publishing its result. Background jobs cannot outlive this command owner.
   */
  SALTS_PROCESS_SHELL_COMMAND = 1U << 4
} salts_process_flags_t;

typedef struct {
  /**
   * Executable path/name, or command text with SALTS_PROCESS_SHELL_COMMAND.
   * Explicit shell mode uses /bin/sh -c on POSIX and the system cmd.exe
   * /d /s /c on Windows. Command text is interpreted verbatim by that shell;
   * it is not an argv escaping API. Empty commands are rejected.
   */
  const char *program;
  /** NULL-terminated arguments excluding argv[0], or NULL for no arguments. */
  const char *const *args;
  /**
   * NULL inherits the parent environment. A non-NULL, NULL-terminated list of
   * KEY=VALUE entries overrides matching parent variables. With
   * SALTS_PROCESS_CLEAN_ENVIRONMENT, this list is the complete child
   * environment and NULL creates an empty environment.
   */
  const char *const *env;
  /** Child working directory, or NULL to inherit the current directory. */
  const char *cwd;
  /** Combination of salts_process_flags_t. */
  unsigned int flags;
  /** Execution deadline in milliseconds. Zero disables the deadline. */
  uint64_t timeout_ms;
  /** Shared stdout/stderr capture limit. Zero selects the default limit. */
  size_t max_output_bytes;
} salts_process_options_t;

typedef struct {
  salts_process_state_t state;
  int pid;
  int exit_code;
  int term_signal;
  int error_code;
} salts_process_result_t;

/** Select parent standard-handle inheritance for one spawn binding. */
#define SALTS_PROCESS_STDIO_INHERIT UINTPTR_MAX

/**
 * Child-side standard handles borrowed until salts_process_spawn_with_stdio()
 * returns. Each value is a native HANDLE/fd or SALTS_PROCESS_STDIO_INHERIT.
 * The spawn call never closes or consumes a supplied handle.
 */
typedef struct {
  uintptr_t stdin_handle;
  uintptr_t stdout_handle;
  uintptr_t stderr_handle;
} salts_process_stdio_bindings_t;

/** Fill options with defaults: stdout/stderr capture and no execution deadline. */
void salts_process_options_init(salts_process_options_t *options);

/** Return the stable string name of a process state. */
const char *salts_process_state_name(salts_process_state_t state);

/**
 * Start a child process and its monitor thread.
 *
 * On success, *out_process owns all resources and must be destroyed. On
 * failure, *out_process is NULL and the return value is a Salts or
 * negative native platform error code.
 */
int salts_process_spawn(const salts_process_options_t *options,
                                  salts_process_t **out_process);

/**
 * Start a child with explicitly borrowed child-side standard handles.
 *
 * A supplied stdin handle conflicts with SALTS_PROCESS_PIPE_STDIN. Supplied
 * stdout/stderr handles conflict with their corresponding capture flags.
 * Contradictory bindings return SALTS_EINVAL without consuming any handle.
 * The returned process owns its lifecycle but never reads or closes a supplied
 * parent handle.
 */
int salts_process_spawn_with_stdio(const salts_process_options_t *options,
                                               const salts_process_stdio_bindings_t *bindings,
                                               salts_process_t **out_process);

/** Return the owned child PID, or -1 for an invalid handle. */
int salts_process_pid(const salts_process_t *process);

/** Return the current lifecycle state. */
salts_process_state_t salts_process_state(const salts_process_t *process);

/** Return true while the owned child has not reached a terminal state. */
bool salts_process_is_running(const salts_process_t *process);

/**
 * Copy the terminal result without blocking.
 *
 * Returns SALTS_OK when terminal, SALTS_EBUSY while running, or SALTS_EINVAL
 * for invalid arguments.
 */
int salts_process_poll(const salts_process_t *process,
                                 salts_process_result_t *out_result);

/** Wait indefinitely for the terminal result. */
int salts_process_wait(salts_process_t *process, salts_process_result_t *out_result);

/**
 * Wait at most timeout_ms for the terminal result.
 *
 * SALTS_ETIMEDOUT only describes this wait operation and does not terminate
 * the process. Use options.timeout_ms for an execution deadline.
 */
int salts_process_wait_for(salts_process_t *process, uint64_t timeout_ms,
                                     salts_process_result_t *out_result);

/** Terminate the owned process tree. This operation is idempotent. */
int salts_process_terminate(salts_process_t *process);

/**
 * Queue bytes for the child's stdin pipe.
 *
 * This call may block until the OS pipe accepts the bytes. It returns
 * SALTS_EPIPE after stdin has been closed or when no stdin pipe was requested.
 */
int salts_process_write_stdin(salts_process_t *process, const void *data, size_t size,
                                        size_t *out_written);

/** Close the parent's stdin pipe so the child observes EOF. */
int salts_process_close_stdin(salts_process_t *process);

/**
 * Consume currently captured stdout bytes.
 *
 * Returns SALTS_OK with *out_read possibly zero, or SALTS_EOF after the stream
 * is closed and all captured bytes have been consumed.
 */
int salts_process_read_stdout(salts_process_t *process, void *buffer, size_t capacity,
                                        size_t *out_read);

/** Consume currently captured stderr bytes; semantics match stdout. */
int salts_process_read_stderr(salts_process_t *process, void *buffer, size_t capacity,
                                        size_t *out_read);

/**
 * Check whether a PID currently names a live process.
 *
 * This is observation only. Managed termination uses the retained native
 * handle to avoid PID-reuse races.
 */
int salts_process_is_pid_alive(int pid, bool *out_alive);

/** Terminate, reap, close, and free a process handle; no concurrent calls are allowed. */
void salts_process_destroy(salts_process_t *process);

/*
 * Bounded native recovery owner for Process lifetimes. This is the Core-native
 * owner used below the CFlow adapter; it does not create a worker, queue, or
 * second process state machine. All retainer operations require one exclusive
 * caller and are not thread-safe with each other.
 */
typedef struct salts_process_retainer {
  void *impl;
} salts_process_retainer_t;

typedef struct salts_process_retainer_token {
  size_t index;
  uint64_t generation;
} salts_process_retainer_token_t;

/* Called exactly once after an adopted Process has reached a terminal state,
 * been destroyed, and its retainer slot has been cleared. The callback must
 * not reenter the same retainer.
 */
typedef void (*salts_process_retainer_release_fn)(void *payload);

/** Allocate a fixed number of recovery slots. */
int salts_process_retainer_init(salts_process_retainer_t *owner, size_t capacity);

/** Reserve one slot before starting work that may require bounded recovery. */
int salts_process_retainer_reserve(salts_process_retainer_t *owner,
                                   salts_process_retainer_token_t *out_token);

/** Return an unused exact-generation reservation. */
int salts_process_retainer_release(salts_process_retainer_t *owner,
                                   salts_process_retainer_token_t token);

/**
 * Transfer an already-closing or terminal Process into a reserved slot.
 *
 * The caller must have requested salts_process_terminate() first unless the
 * Process is already terminal. On success, *source_process becomes NULL and
 * the retainer exclusively owns the Process and payload. On failure ownership
 * remains with the caller.
 */
int salts_process_retainer_adopt(salts_process_retainer_t *owner,
                                 salts_process_retainer_token_t token,
                                 salts_process_t **source_process,
                                 void *payload,
                                 salts_process_retainer_release_fn release);

/**
 * Advance at most max_steps adopted Process slots without blocking.
 *
 * Terminal Processes are destroyed and their payload is released exactly once.
 * Reserved and still-running Processes remain pending. Returns SALTS_EBUSY
 * while anything remains, otherwise SALTS_OK; a native terminate/poll error is
 * returned without force-freeing the retained Process.
 */
int salts_process_retainer_run_ready(salts_process_retainer_t *owner,
                                     size_t max_steps,
                                     size_t *out_reaped,
                                     size_t *out_pending);

/** Refuse destruction while any slot is reserved or adopted. */
int salts_process_retainer_destroy(salts_process_retainer_t *owner);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PROCESS_H */
