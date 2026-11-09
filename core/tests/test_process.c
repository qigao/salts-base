#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define TINYTEST_NO_MAIN
#include <tinytest.h>
#include <core/process.h>
#include <core/error_codes.h>
#include <core/clock.h>
#include <threads/threads.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

static size_t retained_release_count;

static void release_retained_payload(void *payload) {
  ++retained_release_count;
  free(payload);
}

static int test_native_retainer(const char *self) {
  process_retainer_t owner = {0};
  process_retainer_token_t first = {0};
  process_retainer_token_t token = {0};
  process_retainer_token_t second = {99u, 99u};
  process_options_t opts;
  process_t *child = NULL;
  const char *const args[] = {"--core-child-sleep", NULL};
  void *payload = NULL;
  uint64_t deadline;
  size_t reaped = 0u;
  size_t pending = 0u;
  int status;

  retained_release_count = 0u;
  if (process_retainer_init(&owner, 1u) != SALTS_OK) return 20;
  if (process_retainer_reserve(&owner, &first) != SALTS_OK ||
      first.generation == 0u)
    return 21;
  if (process_retainer_reserve(&owner, &second) != SALTS_ENOBUFS ||
      second.index != 0u || second.generation != 0u)
    return 22;
  if (process_retainer_destroy(&owner) != SALTS_EBUSY) return 23;
  if (process_retainer_release(&owner, first) != SALTS_OK) return 24;
  if (process_retainer_reserve(&owner, &token) != SALTS_OK ||
      token.index != first.index || token.generation == 0u ||
      token.generation == first.generation)
    return 25;
  if (process_retainer_release(&owner, first) != SALTS_EINVAL) return 26;

  payload = malloc(1u);
  if (!payload) return 27;
  process_options_init(&opts);
  opts.program = self;
  opts.args = args;
  opts.flags = SALTS_PROCESS_CAPTURE_STDOUT | SALTS_PROCESS_CAPTURE_STDERR;
  opts.timeout_ms = 10000u;
  if (process_spawn(&opts, &child) != SALTS_OK || !child) return 28;

  if (process_retainer_adopt(
          &owner, token, &child, payload, release_retained_payload) !=
          SALTS_EBUSY ||
      !child)
    return 29;
  if (process_terminate(child) != SALTS_OK) return 30;
  if (process_retainer_adopt(
          &owner, token, &child, payload, release_retained_payload) !=
          SALTS_OK ||
      child != NULL)
    return 31;
  payload = NULL;
  if (process_retainer_release(&owner, token) != SALTS_EINVAL) return 32;

  deadline = monotonic_ms() + 15000u;
  for (;;) {
    reaped = SIZE_MAX;
    pending = SIZE_MAX;
    status = process_retainer_run_ready(&owner, 1u, &reaped, &pending);
    if (status != SALTS_OK && status != SALTS_EBUSY) return 33;
    if (pending == 0u) {
      if (status != SALTS_OK || reaped != 1u || retained_release_count != 1u)
        return 34;
      break;
    }
    if (reaped != 0u || retained_release_count != 0u) return 35;
    if (monotonic_ms() >= deadline) return 36;
    sleep_ms(1u);
  }

  if (process_retainer_destroy(&owner) != SALTS_OK || owner.impl != NULL)
    return 37;
  if (process_retainer_destroy(&owner) != SALTS_EINVAL) return 38;
  return 0;
}

static int saltscore_legacy_process_entry(int argc, char **argv) {
  process_options_t opts;
  process_result_t result;
  process_t *child = NULL;
  const char *const args[] = {"--core-child", NULL};
  char bytes[128] = {0};
  size_t count = 0;
  int retainer_status;

  if (argc > 1 && strcmp(argv[1], "--core-child") == 0) {
    fputs("SaltsProcessChild:ok\n", stdout);
    fflush(stdout);
    return 19;
  }
  if (argc > 1 && strcmp(argv[1], "--core-child-sleep") == 0) {
    sleep_ms(5000u);
    return 0;
  }
  if (!argv || !argv[0] || !argv[0][0]) return 1;

  process_options_init(&opts);
  opts.program = argv[0];
  opts.args = args;
  opts.flags = SALTS_PROCESS_CAPTURE_STDOUT | SALTS_PROCESS_CAPTURE_STDERR;
  opts.timeout_ms = 10000;
  if (process_spawn(&opts, &child) != SALTS_OK || !child) return 2;
  if (process_wait_for(child, 12000, &result) != SALTS_OK) return 3;
  if (result.state != SALTS_PROCESS_EXITED || result.exit_code != 19) return 4;
  if (process_read_stdout(child, bytes, sizeof(bytes), &count) != SALTS_OK)
    return 5;
  if (count < sizeof("SaltsProcessChild:ok") - 1u ||
      memcmp(bytes, "SaltsProcessChild:ok", sizeof("SaltsProcessChild:ok") - 1u) != 0)
    return 6;
  process_destroy(child);
  child = NULL;
  if (process_spawn(NULL, &child) == SALTS_OK || child) return 7;

  retainer_status = test_native_retainer(argv[0]);
  if (retainer_status != 0) return retainer_status;

  puts("Salts::Core real child, exit and bounded retainer PASS");
  return 0;
}

/* Child modes are not TinyTest tests: the parent verifies exact stdout and
 * the deliberate exit code 19. Keep their process contract byte-identical. */
static int saltscore_process_argc;
static char **saltscore_process_argv;

static process_t *first_process;
static process_t *second_process;

static int spawn_child_mode(const char *mode, unsigned flags,
                            process_t **out) {
  process_options_t options;
  const char *const args[] = {mode, NULL};
  process_options_init(&options);
  options.program = saltscore_process_argv[0];
  options.args = args;
  options.flags = flags;
  options.timeout_ms = 10000u;
  return process_spawn(&options, out);
}

static int regression_child(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--core-child-eof") == 0) {
    while (getchar() != EOF) {}
    if (ferror(stdin)) return 1;
    fputs("eof", stdout);
    return fflush(stdout) == 0 ? 0 : 1;
  }
#ifndef _WIN32
  if (argc == 3 && strcmp(argv[1], "--core-child-closed-stdio") == 0) {
    unsigned mask = (unsigned)strtoul(argv[2], NULL, 10);
    process_options_t options;
    process_t *child = NULL;
    process_result_t result;
    const char *const args[] = {"--core-child", NULL};
    char output[128];
    size_t count = 0u;
    int rc = 1;
    for (unsigned fd = 0u; fd < 3u; ++fd)
      if (mask & (1u << fd)) close((int)fd);
    process_options_init(&options);
    options.program = argv[0];
    options.args = args;
    options.flags |= SALTS_PROCESS_PIPE_STDIN;
    options.timeout_ms = 2000u;
    if (process_spawn(&options, &child) != SALTS_OK) goto done;
    if (process_close_stdin(child) != SALTS_OK ||
        process_wait_for(child, 3000u, &result) != SALTS_OK ||
        result.state != SALTS_PROCESS_EXITED || result.exit_code != 19 ||
        process_read_stdout(child, output, sizeof(output), &count) != SALTS_OK ||
        count != sizeof("SaltsProcessChild:ok\n") - 1u ||
        memcmp(output, "SaltsProcessChild:ok\n", count) != 0) goto done;
    process_destroy(child);
    child = NULL;
    /* Exercise the exec-error pipe with the same closed standard descriptors. */
    options.program = "/dev/null/saltscore-missing";
    if (process_spawn(&options, &child) >= 0 || child != NULL) goto done;
    rc = 0;
done:
    process_destroy(child);
    return rc;
  }
#endif
  if (argc == 2 && strcmp(argv[1], "--core-child-descendant") == 0) {
#ifdef _WIN32
    wchar_t program[32768];
    wchar_t command[32820];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION info = {0};
    DWORD length = GetModuleFileNameW(NULL, program, 32768u);
    if (length == 0u || length >= 32768u ||
        swprintf(command, 32820u, L"\"%ls\" --core-child-sleep", program) < 0)
      return 1;
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    /* A raw child inherits the outer owner's job; creating another Salts
     * owner here would kill its own job during root process teardown.
     */
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                         NULL, NULL, &startup, &info)) return 1;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    if (printf("%lu\n", (unsigned long)info.dwProcessId) < 0 || fflush(stdout) != 0)
      return 1;
#else
    pid_t descendant = fork();
    if (descendant < 0) return 1;
    if (descendant == 0) {
      /* Keep the bound stdout pipe open until the owner's group is killed. */
      for (;;) pause();
    }
    if (printf("%ld\n", (long)descendant) < 0 || fflush(stdout) != 0) return 1;
#endif
    return 0;
  }
  return -1;
}

#ifdef _WIN32
static int test_descendant_cleanup(const char *self) {
  HANDLE read_pipe = NULL, write_pipe = NULL;
  process_t *child = NULL;
  process_options_t options;
  process_result_t result;
  const char *const args[] = {"--core-child-descendant", NULL};
  char marker[64] = {0};
  size_t length = 0u;
  bool eof = false;
  int rc = 1;
  uint64_t deadline;
  if (!CreatePipe(&read_pipe, &write_pipe, NULL, 0u)) goto done;
  process_stdio_bindings_t bindings = {
      SALTS_PROCESS_STDIO_INHERIT, (uintptr_t)write_pipe, SALTS_PROCESS_STDIO_INHERIT};
  process_options_init(&options);
  options.program = self;
  options.args = args;
  options.flags = 0u;
  options.timeout_ms = 3000u;
  if (process_spawn_with_stdio(&options, &bindings, &child) != SALTS_OK) goto done;
  CloseHandle(write_pipe);
  write_pipe = NULL;
  if (process_wait_for(child, 4000u, &result) != SALTS_OK ||
      result.state != SALTS_PROCESS_EXITED || result.exit_code != 0) goto done;
  deadline = monotonic_ms() + 2000u;
  while (monotonic_ms() < deadline) {
    DWORD available = 0u, count = 0u;
    if (!PeekNamedPipe(read_pipe, NULL, 0u, NULL, &available, NULL)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) eof = true;
      break;
    }
    if (available > 0u) {
      if (length + 1u == sizeof(marker) ||
          !ReadFile(read_pipe, marker + length, 1u, &count, NULL) || count != 1u)
        goto done;
      ++length;
    } else sleep_ms(1u);
  }
  unsigned long pid = 0u;
  if (eof && sscanf(marker, "%lu", &pid) == 1 && pid > 0u) rc = 0;
done:
  process_destroy(child);
  if (read_pipe) CloseHandle(read_pipe);
  if (write_pipe) CloseHandle(write_pipe);
  return rc;
}
#else
static int test_descendant_cleanup(const char *self) {
  int pipe_fds[2] = {-1, -1};
  process_t *child = NULL;
  process_options_t options;
  process_result_t result;
  const char *const args[] = {"--core-child-descendant", NULL};
  char marker[64] = {0};
  size_t length = 0u;
  long descendant = -1;
  bool eof = false;
  int rc = 1;
  uint64_t deadline;
  if (pipe(pipe_fds) != 0) goto done;
  if (fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) != 0 ||
      fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0 ||
      fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK) != 0) goto done;
  process_stdio_bindings_t bindings = {
      SALTS_PROCESS_STDIO_INHERIT, (uintptr_t)pipe_fds[1], SALTS_PROCESS_STDIO_INHERIT};
  process_options_init(&options);
  options.program = self;
  options.args = args;
  options.flags = 0u;
  options.timeout_ms = 3000u;
  if (process_spawn_with_stdio(&options, &bindings, &child) != SALTS_OK) goto done;
  close(pipe_fds[1]);
  pipe_fds[1] = -1;
  deadline = monotonic_ms() + 2000u;
  while (length + 1u < sizeof(marker) && monotonic_ms() < deadline) {
    ssize_t count = read(pipe_fds[0], marker + length, 1u);
    if (count > 0) {
      if (marker[length++] == '\n') break;
    } else if (count == 0) {
      eof = true;
      break;
    } else if (errno != EINTR && errno != EAGAIN) goto done;
    sleep_ms(1u);
  }
  if (sscanf(marker, "%ld", &descendant) != 1 || descendant <= 0) goto done;
  if (process_wait_for(child, 4000u, &result) != SALTS_OK ||
      result.state != SALTS_PROCESS_EXITED || result.exit_code != 0) goto done;
  /* EOF proves the descendant released its native writer, including when it
   * remains a zombie awaiting the system reaper; kill(pid, 0) cannot prove it.
   */
  deadline = monotonic_ms() + 2000u;
  while (!eof && monotonic_ms() < deadline) {
    char byte;
    ssize_t count = read(pipe_fds[0], &byte, 1u);
    if (count == 0) eof = true;
    else if (count > 0 || (errno != EINTR && errno != EAGAIN)) goto done;
    if (!eof) sleep_ms(1u);
  }
  if (eof) rc = 0;
done:
  /* On regression failure the descendant still owns this pipe's only writer. */
  if (!eof && descendant > 0) kill((pid_t)descendant, SIGKILL);
  process_destroy(child);
  if (pipe_fds[0] >= 0) close(pipe_fds[0]);
  if (pipe_fds[1] >= 0) close(pipe_fds[1]);
  return rc;
}
#endif

spec("Core native Process spawn and retainer") {
  it("verifies real child exit, bounded output, cancel and retainer lifetime") {
    const int result = saltscore_legacy_process_entry(
        saltscore_process_argc, saltscore_process_argv);
    check(result == 0, "original Process contract returned %d", result);
  }
}

spec("Process standard streams and descendant ownership") {
  before_each() {
    first_process = second_process = NULL;
  }
  after_each() {
    process_destroy(first_process);
    process_destroy(second_process);
  }
  it("delivers stdin EOF while another spawned child remains alive") {
    process_result_t result;
    char output[16];
    size_t count = 0u;
    check_equal(spawn_child_mode("--core-child-eof",
        SALTS_PROCESS_PIPE_STDIN | SALTS_PROCESS_CAPTURE_STDOUT, &first_process), SALTS_OK);
    check_equal(spawn_child_mode("--core-child-sleep", 0u, &second_process), SALTS_OK);
    check_equal(process_close_stdin(first_process), SALTS_OK);
    check_equal(process_wait_for(first_process, 1000u, &result), SALTS_OK);
    check_equal(result.state, SALTS_PROCESS_EXITED);
    check_equal(result.exit_code, 0);
    check_true(process_is_running(second_process));
    check_equal(process_read_stdout(first_process, output, sizeof(output), &count), SALTS_OK);
    check_equal(count, sizeof("eof") - 1u);
    check_equal(output, "eof", count);
  }
#ifndef _WIN32
  it("redirects streams and reports exec errors with closed standard descriptors") {
    for (unsigned mask = 1u; mask < 8u; ++mask) {
      char mask_text[8];
      process_options_t options;
      process_result_t result;
      check(snprintf(mask_text, sizeof(mask_text), "%u", mask) > 0);
      const char *const args[] = {"--core-child-closed-stdio", mask_text, NULL};
      process_options_init(&options);
      options.program = saltscore_process_argv[0];
      options.args = args;
      options.timeout_ms = 5000u;
      check_equal(process_spawn(&options, &first_process), SALTS_OK);
      check_equal(process_wait_for(first_process, 6000u, &result), SALTS_OK);
      check_equal(result.state, SALTS_PROCESS_EXITED);
      check_equal(result.exit_code, 0);
      process_destroy(first_process);
      first_process = NULL;
    }
  }
#endif
  it("terminates descendants when a non-shell root exits") {
    check_equal(test_descendant_cleanup(saltscore_process_argv[0]), 0);
  }
}

int main(int argc, char **argv) {
  int child_status = regression_child(argc, argv);
  if (child_status >= 0) return child_status;
  if (argc > 1 && argv && argv[1] &&
      (strcmp(argv[1], "--core-child") == 0 ||
       strcmp(argv[1], "--core-child-sleep") == 0)) {
    return saltscore_legacy_process_entry(argc, argv);
  }
  saltscore_process_argc = argc;
  saltscore_process_argv = argv;
  char *runner_argv[] = {argv[0]};
  return ttest_main__(1, runner_argv, TTEST_INVOKE_SPEC_ADAPTER__,
                      TT_USE_COLOR != 0, TT_USE_TAP != 0,
                      TTEST_IS_ATTY__() != 0, TTEST_PRINT_TRACE_DEFAULT__);
}
