#include <process_async.h>
#include <thread.h>
#include <clock.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

enum { PROCESS_TEST_CAPACITY = 32, PROCESS_TEST_TIMEOUT_MS = 6000 };
static const char *self_path;
static salts_process_async process;
static unsigned char output[128], errors[128];
static const unsigned char payload[] = {0, 'e', '\n', 0xff, 'o'};
static const char marker[] = "stderr-marker";
static struct {
  size_t count;
  uint64_t ids[PROCESS_TEST_CAPACITY];
  salts_process_stream streams[PROCESS_TEST_CAPACITY];
  native_io_completion events[PROCESS_TEST_CAPACITY];
  bool reentrant;
  int reentrant_status, destroy_status;
} probe;

static native_io_backend_kind process_backend(void) {
#ifdef _WIN32
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void completed(void *user, uint64_t id, salts_process_stream stream,
                       const native_io_completion *event) {
  (void)user;
  if (probe.count < PROCESS_TEST_CAPACITY) {
    probe.ids[probe.count] = id;
    probe.streams[probe.count] = stream;
    probe.events[probe.count++] = *event;
  }
  if (probe.reentrant) {
    size_t progressed;
    probe.reentrant = false;
    probe.reentrant_status = salts_process_async_run_ready(&process, 1u, &progressed);
    probe.destroy_status = salts_process_async_destroy(&process);
  }
}
static int start_child(size_t capacity, native_io_backend_kind backend) {
  const char *const args[] = {"--async-child", NULL};
  salts_process_options_t options;
  salts_process_options_init(&options);
  options.program = self_path; options.args = args; options.flags = 0u;
  options.timeout_ms = PROCESS_TEST_TIMEOUT_MS;
  salts_process_async_config config = {backend, capacity, capacity, completed, NULL};
  return salts_process_async_start(&process, &options, &config);
}
static int wait_events(size_t expected) {
  uint64_t deadline = cmeta_monotonic_ms() + PROCESS_TEST_TIMEOUT_MS;
  while (probe.count < expected) {
    size_t progressed;
    int status = salts_process_async_run_ready(&process, 32u, &progressed);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
    if (!progressed) cmeta_sleep_ms(1u);
  }
  return SALTS_OK;
}
static const native_io_completion *event_for(uint64_t id, salts_process_stream stream) {
  for (size_t i = 0; i < probe.count; ++i)
    if (probe.ids[i] == id && probe.streams[i] == stream) return &probe.events[i];
  return NULL;
}
static int wait_exit(salts_process_result_t *result) {
  uint64_t deadline = cmeta_monotonic_ms() + PROCESS_TEST_TIMEOUT_MS;
  int status;
  while ((status = salts_process_async_poll(&process, result)) == SALTS_EBUSY &&
         cmeta_monotonic_ms() < deadline) cmeta_sleep_ms(1u);
  return status;
}

spec("Native asynchronous process streams") {
  before_each() {
    memset(&probe, 0, sizeof(probe));
    memset(output, 0, sizeof(output)); memset(errors, 0, sizeof(errors));
  }
  after_each() {
    if (process.impl) {
      check_equal(salts_process_async_close(&process), SALTS_OK);
      uint64_t deadline = cmeta_monotonic_ms() + PROCESS_TEST_TIMEOUT_MS;
      while (!salts_process_async_is_quiescent(&process) && cmeta_monotonic_ms() < deadline) {
        size_t progressed;
        check_equal(salts_process_async_run_ready(&process, 32u, &progressed), SALTS_OK);
        if (!progressed) cmeta_sleep_ms(1u);
      }
      check_true(salts_process_async_is_quiescent(&process));
      check_equal(salts_process_async_destroy(&process), SALTS_OK);
      check_null(process.impl);
    }
  }
  it("exchanges binary stdin/stdout and independent stderr, then observes EOF and exit") {
    uint64_t write_id, out_id, err_id;
    check_equal(start_child(3u, process_backend()), SALTS_OK);
    probe.reentrant = true;
    check_equal(salts_process_async_write_stdin(&process, payload, sizeof(payload), &write_id), SALTS_OK);
    check_equal(salts_process_async_close_stdin(&process), SALTS_EBUSY);
    check_equal(salts_process_async_read_stdout(&process, output, sizeof(output), &out_id), SALTS_OK);
    check_equal(salts_process_async_read_stderr(&process, errors, sizeof(errors), &err_id), SALTS_OK);
    check_equal(probe.count, (size_t)0u);
    check_equal(wait_events(3u), SALTS_OK);
    const native_io_completion *written = event_for(write_id, SALTS_PROCESS_STDIN);
    const native_io_completion *read_out = event_for(out_id, SALTS_PROCESS_STDOUT);
    const native_io_completion *read_err = event_for(err_id, SALTS_PROCESS_STDERR);
    check_not_null(written); check_not_null(read_out); check_not_null(read_err);
    check_equal(written->kind, NATIVE_IO_COMPLETION_OK);
    check_equal(written->bytes, sizeof(payload));
    check_equal(read_out->kind, NATIVE_IO_COMPLETION_OK);
    check_equal(read_err->kind, NATIVE_IO_COMPLETION_OK);
    size_t out_bytes = read_out->bytes, err_bytes = read_err->bytes;
    check_equal(probe.reentrant_status, SALTS_EBUSY);
    check_equal(probe.destroy_status, SALTS_EBUSY);
    check_equal(salts_process_async_close_stdin(&process), SALTS_OK);
    check_equal(salts_process_async_close_stdin(&process), SALTS_OK);
    bool out_eof = false, err_eof = false;
    while (!out_eof || !err_eof) {
      size_t expected = probe.count;
      if (!out_eof) {
        check_true(out_bytes < sizeof(output));
        check_equal(salts_process_async_read_stdout(&process, output + out_bytes,
            sizeof(output) - out_bytes, &out_id), SALTS_OK);
        ++expected;
      }
      if (!err_eof) {
        check_true(err_bytes < sizeof(errors));
        check_equal(salts_process_async_read_stderr(&process, errors + err_bytes,
            sizeof(errors) - err_bytes, &err_id), SALTS_OK);
        ++expected;
      }
      check_equal(wait_events(expected), SALTS_OK);
      if (!out_eof) {
        read_out = event_for(out_id, SALTS_PROCESS_STDOUT); check_not_null(read_out);
        check_true(read_out->kind == NATIVE_IO_COMPLETION_OK || read_out->kind == NATIVE_IO_COMPLETION_EOF);
        out_eof = read_out->kind == NATIVE_IO_COMPLETION_EOF; out_bytes += read_out->bytes;
      }
      if (!err_eof) {
        read_err = event_for(err_id, SALTS_PROCESS_STDERR); check_not_null(read_err);
        check_true(read_err->kind == NATIVE_IO_COMPLETION_OK || read_err->kind == NATIVE_IO_COMPLETION_EOF);
        err_eof = read_err->kind == NATIVE_IO_COMPLETION_EOF; err_bytes += read_err->bytes;
      }
    }
    check_equal(out_bytes, sizeof(payload)); check_equal(err_bytes, sizeof(marker) - 1u);
    check_equal(output, payload, sizeof(payload));
    check_equal(errors, marker, sizeof(marker) - 1u);
    salts_process_result_t result;
    check_equal(wait_exit(&result), SALTS_OK);
    check_equal(result.state, SALTS_PROCESS_EXITED); check_equal(result.exit_code, 0);
    salts_process_async_stats stats;
    check_true(salts_process_async_get_stats(&process, &stats));
    check_equal(stats.pending_requests, (size_t)0u); check_false(stats.stdin_open);
  }
  it("bounds requests and cancels queued and submitted reads before shutdown drain") {
    uint64_t id, rejected = UINT64_MAX;
    check_equal(start_child(1u, process_backend()), SALTS_OK);
    check_equal(salts_process_async_read_stdout(&process, output, sizeof(output), &id), SALTS_OK);
    check_equal(salts_process_async_read_stderr(&process, errors, sizeof(errors), &rejected), SALTS_ENOBUFS);
    check_equal(rejected, (uint64_t)0u);
    check_equal(salts_process_async_cancel(&process, id), SALTS_OK);
    check_equal(wait_events(1u), SALTS_OK);
    check_equal(probe.events[0].kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(salts_process_async_cancel(&process, id), SALTS_ENOENT);
    check_equal(salts_process_async_read_stdout(&process, output, sizeof(output), &id), SALTS_OK);
    size_t progressed;
    check_equal(salts_process_async_run_ready(&process, 1u, &progressed), SALTS_OK);
    check_equal(probe.count, (size_t)1u);
    check_equal(salts_process_async_cancel(&process, id), SALTS_OK);
    check_equal(wait_events(2u), SALTS_OK);
    check_equal(probe.ids[1], id); check_equal(probe.events[1].kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(salts_process_async_read_stdout(&process, output, sizeof(output), &id), SALTS_OK);
    check_equal(salts_process_async_close(&process), SALTS_OK);
    check_equal(salts_process_async_destroy(&process), SALTS_EBUSY);
    check_equal(salts_process_async_read_stderr(&process, errors, sizeof(errors), &rejected), SALTS_ESHUTDOWN);
    check_equal(rejected, (uint64_t)0u);
    check_equal(wait_events(3u), SALTS_OK);
    check_equal(probe.events[2].kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(salts_process_async_run_ready(&process, 32u, &progressed), SALTS_OK);
    check_equal(probe.count, (size_t)3u);
  }
  it("preserves explicit shell mode and rejects conflicting capture flags") {
    salts_process_options_t options;
    salts_process_options_init(&options); options.program = "echo async-shell";
    salts_process_async_config config = {process_backend(), 2u, 2u, completed, NULL};
    check_equal(salts_process_async_start(&process, &options, &config), SALTS_EINVAL);
    check_null(process.impl);
    options.flags = SALTS_PROCESS_SHELL_COMMAND;
    check_equal(salts_process_async_start(&process, &options, &config), SALTS_OK);
    uint64_t id;
    check_equal(salts_process_async_read_stdout(&process, output, sizeof(output), &id), SALTS_OK);
    check_equal(wait_events(1u), SALTS_OK);
    check_equal(probe.events[0].kind, NATIVE_IO_COMPLETION_OK);
    check_true(probe.events[0].bytes >= sizeof("async-shell") - 1u);
    check_equal(output, "async-shell", sizeof("async-shell") - 1u);
  }
#if defined(__linux__)
  it("uses the same process contract with the explicit io_uring backend") {
    check_equal(start_child(1u, NATIVE_IO_BACKEND_IO_URING), SALTS_OK);
    check_equal(salts_process_async_close_stdin(&process), SALTS_OK);
    salts_process_result_t result;
    check_equal(wait_exit(&result), SALTS_OK);
    check_equal(result.exit_code, 0);
  }
#endif
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--async-child") == 0) {
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) < 0 || _setmode(_fileno(stdout), _O_BINARY) < 0 ||
        _setmode(_fileno(stderr), _O_BINARY) < 0) return 10;
#endif
    unsigned char bytes[sizeof(payload)];
    size_t count = fread(bytes, 1u, sizeof(bytes), stdin);
    if (ferror(stdin)) return 11;
    if (count && (fwrite(bytes, 1u, count, stdout) != count || fflush(stdout) != 0)) return 12;
    if (fwrite(marker, 1u, sizeof(marker) - 1u, stderr) != sizeof(marker) - 1u || fflush(stderr) != 0) return 13;
    while (fgetc(stdin) != EOF) {}
    return ferror(stdin) ? 14 : 0;
  }
  self_path = argv[0];
  return ttest_main__(argc, argv, TTEST_INVOKE_SPEC_ADAPTER__, TT_USE_COLOR != 0,
      TT_USE_TAP != 0, TTEST_IS_ATTY__() != 0, TTEST_PRINT_TRACE_DEFAULT__);
}
