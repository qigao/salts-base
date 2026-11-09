#include <shell.h>
#include <core/clock.h>
#include <core/error_codes.h>
#include <threads/threads.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

enum {
  SHELL_TEST_LIMIT = 1024,
  SHELL_TEST_CHUNK = 7,
  SHELL_TEST_TIMEOUT_MS = 5000,
  SHELL_TEST_DUPLEX_BYTES = 256 * 1024,
  SHELL_TEST_LARGE_CHUNK = 4096,
  SHELL_TEST_STEPS = 64
};

#ifdef _WIN32
  #define SHELL_TEST_EXIT "exit /b 0"
  #define SHELL_TEST_OUTPUT "echo stdout-tail& echo stderr-tail 1>&2& exit /b 7"
  #define SHELL_TEST_SLEEP "ping -n 30 127.0.0.1 >nul"
  #define SHELL_TEST_ECHO \
    "powershell.exe -NoProfile -Command \"[Console]::OpenStandardInput().CopyTo([Console]::OpenStandardOutput())\""
  #define SHELL_TEST_BINARY \
    "powershell.exe -NoProfile -Command \"[Console]::OpenStandardOutput().Write([byte[]](65,0,66),0,3)\""
#else
  #define SHELL_TEST_EXIT "exit 0"
  #define SHELL_TEST_OUTPUT "printf stdout-tail; printf stderr-tail >&2; exit 7"
  #define SHELL_TEST_SLEEP "sleep 30"
  #define SHELL_TEST_ECHO "cat"
  #define SHELL_TEST_BINARY "printf 'A\\000B'"
#endif

typedef struct shell_output_probe {
  shell *shell;
  size_t stdout_bytes;
  size_t stderr_bytes;
  int reentrant_status;
} shell_output_probe;

static void shell_test_output(void *user, process_stream stream,
                              const void *bytes, size_t length) {
  shell_output_probe *probe = (shell_output_probe *)user;
  (void)bytes;
  if (stream == SALTS_PROCESS_STDOUT) probe->stdout_bytes += length;
  else probe->stderr_bytes += length;
  probe->reentrant_status = shell_cancel(probe->shell);
}

static int shell_test_drain(shell *shell, size_t steps, shell_result *result) {
  uint64_t started = hrtime();
  int status;
  while ((status = shell_poll(shell, result)) == SALTS_EBUSY) {
    size_t progressed = 0u;
    status = shell_run_ready(shell, steps, &progressed);
    if (status != SALTS_OK) return status;
    if (hrtime() - started > UINT64_C(10000000000)) return SALTS_ETIMEDOUT;
    if (progressed == 0u) sleep_ms(1u);
  }
  return status;
}

spec("Native shell") {
  static shell shell;
  static shell_options options;
  static shell_result result;
  static unsigned char *payload;
  static shell_output_probe probe;

  before_each() {
    memset(&shell, 0, sizeof(shell));
    memset(&result, 0, sizeof(result));
    payload = NULL;
    probe = (shell_output_probe){&shell, 0u, 0u, SALTS_OK};
    shell_options_init(&options);
    options.command = SHELL_TEST_EXIT;
    options.max_output_bytes = SHELL_TEST_LIMIT;
    options.io_chunk_bytes = SHELL_TEST_CHUNK;
    options.timeout_ms = SHELL_TEST_TIMEOUT_MS;
  }

  after_each() {
    if (shell.impl != NULL) {
      int cancel_status = shell_cancel(&shell);
      int drain_status = shell_test_drain(&shell, SHELL_TEST_STEPS, &result);
      int destroy_status = shell_destroy(&shell);
      check_equal(cancel_status, SALTS_OK);
      check_equal(drain_status, SALTS_OK);
      check_equal(destroy_status, SALTS_OK);
    }
    free(payload);
  }

  it("captures both streams and nonzero exit without losing the tail") {
    options.command = SHELL_TEST_OUTPUT;
    options.output = shell_test_output;
    options.output_user = &probe;
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    info("outcome=%d error=%d state=%d exit=%d stdout=%zu stderr=%zu",
         result.outcome, result.error, result.process.state, result.process.exit_code,
         result.stdout_bytes, result.stderr_bytes);
    check_equal(result.outcome, SALTS_SHELL_EXITED);
    check_equal(result.process.exit_code, 7);
    check_equal(result.error, SALTS_OK);
    check_contains(shell_stdout(&shell), "stdout-tail");
    check_contains(shell_stderr(&shell), "stderr-tail");
    check_equal(result.stdout_bytes, probe.stdout_bytes);
    check_equal(result.stderr_bytes, probe.stderr_bytes);
    check_equal(probe.reentrant_status, SALTS_EBUSY);
  }

  it("drives with one transition per call and closes empty stdin") {
    options.command = SHELL_TEST_ECHO;
    check_equal(shell_start(&shell, &options), SALTS_OK);
    check_equal(shell_poll(&shell, &result), SALTS_EBUSY);
    check_equal(shell_destroy(&shell), SALTS_EBUSY);
    check_equal(shell_test_drain(&shell, 1u, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_EXITED);
    check_equal(result.stdout_bytes, (size_t)0u);
  }

  it("copies input and pumps duplex payloads larger than a pipe") {
    size_t index;
    payload = (unsigned char *)malloc(SHELL_TEST_DUPLEX_BYTES);
    check_not_null(payload);
    for (index = 0u; index < SHELL_TEST_DUPLEX_BYTES; ++index) payload[index] = (unsigned char)index;
    options.command = SHELL_TEST_ECHO;
    options.input = payload;
    options.input_size = SHELL_TEST_DUPLEX_BYTES;
    options.max_output_bytes = SHELL_TEST_DUPLEX_BYTES;
    options.io_chunk_bytes = SHELL_TEST_LARGE_CHUNK;
    check_equal(shell_start(&shell, &options), SALTS_OK);
    memset(payload, 0, SHELL_TEST_DUPLEX_BYTES);
    check_equal(shell_test_drain(&shell, SHELL_TEST_STEPS, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_EXITED);
    check_equal(result.stdin_bytes, (size_t)SHELL_TEST_DUPLEX_BYTES);
    check_equal(result.stdout_bytes, (size_t)SHELL_TEST_DUPLEX_BYTES);
    for (index = 0u; index < SHELL_TEST_DUPLEX_BYTES; ++index)
      payload[index] = (unsigned char)index;
    check_equal(shell_stdout(&shell), payload, SHELL_TEST_DUPLEX_BYTES);
  }

  it("preserves binary output at the exact shared output limit") {
    static const char expected[] = {'A', '\0', 'B'};
    options.command = SHELL_TEST_BINARY;
    options.max_output_bytes = sizeof(expected);
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_EXITED);
    check_equal(result.stdout_bytes, sizeof(expected));
    check_equal(shell_stdout(&shell), expected, sizeof(expected));
  }

  it("reports output overflow while preserving the bounded prefix") {
    options.command = SHELL_TEST_BINARY;
    options.max_output_bytes = 2u;
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_OUTPUT_LIMIT);
    check_equal(result.error, SALTS_ERANGE);
    check_equal(result.stdout_bytes, options.max_output_bytes);
    check_equal(shell_stdout(&shell)[0], 'A');
  }

  it("enforces one shared quota across stdout and stderr") {
    options.command = SHELL_TEST_OUTPUT;
    options.max_output_bytes = 1u;
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_OUTPUT_LIMIT);
    check_equal(result.stdout_bytes + result.stderr_bytes, (size_t)1u);
  }

  it("reports execution timeout distinctly") {
    options.command = SHELL_TEST_SLEEP;
    options.timeout_ms = 50u;
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_TIMED_OUT);
    check_equal(result.process.state, SALTS_PROCESS_TIMED_OUT);
  }

  it("preserves timeout when terminating a blocked stdin write") {
    payload = (unsigned char *)calloc(SHELL_TEST_DUPLEX_BYTES, 1u);
    check_not_null(payload);
    options.command = SHELL_TEST_SLEEP;
    options.input = payload;
    options.input_size = SHELL_TEST_DUPLEX_BYTES;
    options.io_chunk_bytes = SHELL_TEST_DUPLEX_BYTES;
    options.timeout_ms = 50u;
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_TIMED_OUT);
  }

  it("settles inherited output handles when the root exits before its background child") {
#ifdef _WIN32
    options.command = "start \"\" /b ping -n 30 127.0.0.1 >nul & exit /b 0";
#else
    options.command = "sleep 30 & exit 0";
#endif
    check_equal(shell_start(&shell, &options), SALTS_OK);
    check_equal(shell_test_drain(&shell, SHELL_TEST_STEPS, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_EXITED);
    check_equal(result.process.exit_code, 0);
  }

  it("cancels a retained process with outstanding reads and drains it") {
    size_t progressed;
    options.command = SHELL_TEST_SLEEP;
    check_equal(shell_start(&shell, &options), SALTS_OK);
    check_equal(shell_run_ready(&shell, SHELL_TEST_STEPS, &progressed), SALTS_OK);
    check_equal(shell_cancel(&shell), SALTS_OK);
    check_equal(shell_cancel(&shell), SALTS_OK);
    check_equal(shell_test_drain(&shell, 1u, &result), SALTS_OK);
    check_equal(result.outcome, SALTS_SHELL_CANCELLED);
  }

  it("passes environment overrides and preserves quoted shell syntax") {
    static const char *env[] = {"SALTS_SHELL_TEST_VALUE=two words", NULL};
    options.env = env;
#ifdef _WIN32
    options.command = "echo \"%SALTS_SHELL_TEST_VALUE% & literal\"";
#else
    options.command = "printf '%s' \"$SALTS_SHELL_TEST_VALUE & literal\"";
#endif
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.process.exit_code, 0);
    check_contains(shell_stdout(&shell), "two words & literal");
    check_null(strchr(shell_stdout(&shell), '\\'));
  }

  it("applies the requested working directory") {
#ifdef _WIN32
    options.command = "if exist shell.c (echo cwd-ok) else (exit /b 9)";
#else
    options.command = "test -f shell.c && printf cwd-ok";
#endif
    /* CTest sets cwd to the source tests directory; ../src is stable across builds. */
    options.cwd = "../src";
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.process.exit_code, 0);
    check_contains(shell_stdout(&shell), "cwd-ok");
  }

  it("rejects invalid configuration before acquiring process ownership") {
    options.command = "";
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    options.command = SHELL_TEST_EXIT;
    options.input_size = 1u;
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    options.input_size = 0u;
    options.max_output_bytes = SIZE_MAX;
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    options.max_output_bytes = 0u;
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    options.max_output_bytes = SHELL_TEST_LIMIT;
    options.input = "x";
    options.input_size = 2u;
    options.max_input_bytes = 1u;
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    options.input_size = 0u;
    options.io_chunk_bytes = 0u;
    check_equal(shell_start(&shell, &options), SALTS_EINVAL);
    check_null(shell.impl);
  }

  it("propagates spawn failure without leaving an owned handle") {
    options.cwd = "cflow-shell-nonexistent-directory/child";
    check_not_equal(shell_start(&shell, &options), SALTS_OK);
    check_null(shell.impl);
  }

  it("fails unsupported pipe backends without fallback") {
    options.backend_kind = NATIVE_IO_BACKEND_DARWIN_AIO;
    check_equal(shell_start(&shell, &options), SALTS_ENOTSUP);
    check_null(shell.impl);
  }

  it("starts with only the explicit environment when requested") {
    static const char *env[] = {"SALTS_SHELL_TEST_VALUE=clean-value", NULL};
    options.clean_environment = true;
    options.env = env;
#ifdef _WIN32
    options.command = "if defined SALTS_SHELL_PARENT_SENTINEL (exit /b 9) else (echo %SALTS_SHELL_TEST_VALUE%)";
#else
    options.command = "test -z \"$SALTS_SHELL_PARENT_SENTINEL\" && printf '%s' \"$SALTS_SHELL_TEST_VALUE\"";
#endif
    check_equal(shell_execute(&shell, &options, &result), SALTS_OK);
    check_equal(result.process.exit_code, 0);
    check_contains(shell_stdout(&shell), "clean-value");
  }
}
