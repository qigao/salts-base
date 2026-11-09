#include <fs_file.h>
#include <threads/threads.h>
#include <core/clock.h>
#include <tinytest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

enum { FILE_TEST_CAPACITY = 64, FILE_TEST_TIMEOUT_MS = 5000,
       FILE_TEST_PRODUCERS = 4, FILE_TEST_WRITES_PER_PRODUCER = 4 };
static fs_file_runtime runtime;
static fs_file files[2];
static char *paths[2];
static unsigned char read_buffers[2][64];
static const unsigned char payload[] = {0, 'n', '\n', 0xff, 'x'};
typedef struct {
  size_t count;
  uint64_t ids[FILE_TEST_CAPACITY];
  native_io_completion events[FILE_TEST_CAPACITY];
  int reentrant_status, destroy_status;
  bool try_reentrant;
} file_probe;
static file_probe probes[2];
static sync_thread_t producers[FILE_TEST_PRODUCERS];
static struct { size_t index; int status; uint64_t ids[FILE_TEST_WRITES_PER_PRODUCER]; }
    producer_data[FILE_TEST_PRODUCERS];
static unsigned char producer_bytes[FILE_TEST_PRODUCERS * FILE_TEST_WRITES_PER_PRODUCER];
static atomic_size_t producers_done;

static void file_producer(void *argument) {
  size_t index = *(size_t *)argument;
  for (size_t i = 0; i < FILE_TEST_WRITES_PER_PRODUCER; ++i) {
    size_t offset = index * FILE_TEST_WRITES_PER_PRODUCER + i;
    int status = fs_file_write_at(&files[0], &producer_bytes[offset], 1u,
        offset, &producer_data[index].ids[i]);
    if (status != SALTS_OK) { producer_data[index].status = status; break; }
  }
  atomic_fetch_add(&producers_done, 1u);
}

static native_io_backend_kind file_backend(void) {
#ifdef _WIN32
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_DARWIN_AIO;
#else
  return NATIVE_IO_BACKEND_IO_URING;
#endif
}
static void file_completion(void *user, uint64_t id, native_io_operation_kind operation,
                            const native_io_completion *event) {
  file_probe *probe = user;
  (void)operation;
  if (probe->count < FILE_TEST_CAPACITY) {
    probe->ids[probe->count] = id;
    probe->events[probe->count++] = *event;
  }
  if (probe->try_reentrant) {
    size_t progressed;
    probe->try_reentrant = false;
    probe->reentrant_status = fs_file_run_ready(&files[0], 1u, &progressed);
    fs_file_close(&files[0]);
    probe->destroy_status = fs_file_destroy(&files[0]);
  }
}
static int open_files(size_t count, size_t capacity) {
  fs_file_runtime_config config = {file_backend(), count, capacity, capacity};
  int status = fs_file_runtime_init(&runtime, &config);
  if (status != SALTS_OK) return status;
  for (size_t i = 0; i < count; ++i) {
    paths[i] = tt_make_temp_file("salts-file-", ".dat");
    if (!paths[i]) return SALTS_ENOMEM;
    fs_file_config file_config = {.open_flags = SALTS_FS_FILE_READ | SALTS_FS_FILE_WRITE |
        SALTS_FS_FILE_TRUNCATE, .completion = file_completion,
        .completion_user = &probes[i], .runtime = &runtime};
    status = fs_file_open(&files[i], paths[i], &file_config);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}
static int drive_until(size_t first, size_t second) {
  uint64_t deadline = monotonic_ms() + FILE_TEST_TIMEOUT_MS;
  while (probes[0].count < first || probes[1].count < second) {
    size_t progressed;
    int status = fs_file_runtime_run_ready(&runtime, 16u, &progressed);
    if (status != SALTS_OK) return status;
    if (monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
    if (!progressed) sleep_ms(1u);
  }
  return SALTS_OK;
}

spec("Native asynchronous regular files") {
  before_each() {
    memset(probes, 0, sizeof(probes));
    memset(read_buffers, 0, sizeof(read_buffers));
  }
  after_each() {
    for (size_t i = 0; i < FILE_TEST_PRODUCERS; ++i)
      if (producers[i]) check_equal(sync_thread_join(&producers[i]), 0);
    for (size_t i = 0; i < 2u; ++i)
      if (files[i].impl) check_equal(fs_file_close(&files[i]), SALTS_OK);
    if (runtime.impl) {
      check_equal(fs_file_runtime_close(&runtime), SALTS_OK);
      uint64_t deadline = monotonic_ms() + FILE_TEST_TIMEOUT_MS;
      while (!fs_file_runtime_is_quiescent(&runtime) && monotonic_ms() < deadline) {
        size_t progressed;
        check_equal(fs_file_runtime_run_ready(&runtime, 16u, &progressed), SALTS_OK);
        if (!progressed) sleep_ms(1u);
      }
      check_true(fs_file_runtime_is_quiescent(&runtime));
    }
    for (size_t i = 0; i < 2u; ++i) {
      if (files[i].impl) check_equal(fs_file_destroy(&files[i]), SALTS_OK);
      if (paths[i]) { check_equal(tt_remove_file(paths[i]), 0); free(paths[i]); paths[i] = NULL; }
    }
    if (runtime.impl) check_equal(fs_file_runtime_destroy(&runtime), SALTS_OK);
  }
  it("admits concurrent producers while one owner drives and preserves each positional write") {
    const size_t requests = sizeof(producer_bytes);
    check_equal(open_files(1u, requests), SALTS_OK);
    memset(producer_data, 0, sizeof(producer_data));
    atomic_store(&producers_done, 0u);
    for (size_t i = 0; i < requests; ++i) producer_bytes[i] = (unsigned char)(i + 1u);
    for (size_t i = 0; i < FILE_TEST_PRODUCERS; ++i) {
      producer_data[i].index = i;
      check_equal(sync_thread_create(&producers[i], file_producer, &producer_data[i].index), 0);
    }
    uint64_t deadline = monotonic_ms() + FILE_TEST_TIMEOUT_MS;
    while (atomic_load(&producers_done) != FILE_TEST_PRODUCERS && monotonic_ms() < deadline) {
      size_t progressed;
      check_equal(fs_file_run_ready(&files[0], 1u, &progressed), SALTS_OK);
      if (!progressed) sync_thread_yield();
    }
    check_equal(atomic_load(&producers_done), (size_t)FILE_TEST_PRODUCERS);
    for (size_t i = 0; i < FILE_TEST_PRODUCERS; ++i) {
      check_equal(sync_thread_join(&producers[i]), 0);
      check_equal(producer_data[i].status, SALTS_OK);
    }
    check_equal(drive_until(requests, 0u), SALTS_OK);
    for (size_t producer = 0; producer < FILE_TEST_PRODUCERS; ++producer)
      for (size_t request = 0; request < FILE_TEST_WRITES_PER_PRODUCER; ++request) {
        size_t matches = 0u;
        for (size_t i = 0; i < probes[0].count; ++i)
          if (probes[0].ids[i] == producer_data[producer].ids[request]) {
            ++matches;
            check_equal(probes[0].events[i].kind, NATIVE_IO_COMPLETION_OK);
            check_equal(probes[0].events[i].bytes, (size_t)1u);
          }
        check_equal(matches, (size_t)1u);
      }
    uint64_t id;
    check_equal(fs_file_read_at(&files[0], read_buffers[0], sizeof(read_buffers[0]), 0u, &id), SALTS_OK);
    check_equal(drive_until(requests + 1u, 0u), SALTS_OK);
    check_equal(probes[0].events[requests].bytes, requests);
    check_equal(read_buffers[0], producer_bytes, requests);
  }
  it("drains a cancellation racing an already submitted file completion") {
    uint64_t id;
    check_equal(open_files(1u, 1u), SALTS_OK);
    check_equal(fs_file_write_at(&files[0], payload, sizeof(payload), 0u, &id), SALTS_OK);
    size_t progressed;
    check_equal(fs_file_run_ready(&files[0], 1u, &progressed), SALTS_OK);
    sleep_ms(10u);
    check_equal(fs_file_cancel(&files[0], id), SALTS_OK);
    check_equal(drive_until(1u, 0u), SALTS_OK);
    check_equal(probes[0].ids[0], id);
    check_true(probes[0].events[0].kind == NATIVE_IO_COMPLETION_OK ||
        probes[0].events[0].kind == NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(fs_file_run_ready(&files[0], 8u, &progressed), SALTS_OK);
    check_equal(probes[0].count, (size_t)1u);
  }
  it("shares a runtime and preserves positional binary I/O, partial reads and EOF") {
    uint64_t ids[2];
    check_equal(open_files(2u, 2u), SALTS_OK);
    check_equal(fs_file_runtime_destroy(&runtime), SALTS_EBUSY);
    for (size_t i = 0; i < 2u; ++i)
      check_equal(fs_file_write_at(&files[i], payload, sizeof(payload), 7u + i, &ids[i]), SALTS_OK);
    check_equal(probes[0].count, (size_t)0u);
    check_equal(drive_until(1u, 1u), SALTS_OK);
    for (size_t i = 0; i < 2u; ++i) {
      check_equal(probes[i].ids[0], ids[i]);
      check_equal(probes[i].events[0].kind, NATIVE_IO_COMPLETION_OK);
      check_equal(probes[i].events[0].bytes, sizeof(payload));
      check_equal(fs_file_read_at(&files[i], read_buffers[i], sizeof(read_buffers[i]), 7u + i, &ids[i]), SALTS_OK);
    }
    check_equal(drive_until(2u, 2u), SALTS_OK);
    for (size_t i = 0; i < 2u; ++i) {
      check_equal(probes[i].events[1].kind, NATIVE_IO_COMPLETION_OK);
      check_equal(probes[i].events[1].bytes, sizeof(payload));
      check_equal(read_buffers[i], payload, sizeof(payload));
      check_equal(fs_file_read_at(&files[i], read_buffers[i], 1u, 1000u, &ids[i]), SALTS_OK);
    }
    check_equal(drive_until(3u, 3u), SALTS_OK);
    check_equal(probes[0].events[2].kind, NATIVE_IO_COMPLETION_EOF);
    check_equal(probes[1].events[2].kind, NATIVE_IO_COMPLETION_EOF);
    check_equal(probes[0].events[2].bytes, (size_t)0u);
#ifdef _WIN32
    ids[0] = UINT64_MAX;
    check_equal(fs_file_flush(&files[0], &ids[0]), SALTS_ENOTSUP);
    check_equal(ids[0], (uint64_t)0u);
#else
    check_equal(fs_file_flush(&files[0], &ids[0]), SALTS_OK);
    check_equal(drive_until(4u, 3u), SALTS_OK);
    check_equal(probes[0].events[3].kind, NATIVE_IO_COMPLETION_OK);
#endif
  }
  it("bounds admission, scopes cancellation to the file and returns borrows exactly once") {
    uint64_t id, rejected = UINT64_MAX;
    check_equal(open_files(2u, 1u), SALTS_OK);
    check_equal(fs_file_write_at(&files[0], payload, sizeof(payload), 0u, &id), SALTS_OK);
    check_equal(fs_file_read_at(&files[1], read_buffers[1], 1u, 0u, &rejected), SALTS_ENOBUFS);
    check_equal(rejected, (uint64_t)0u);
    check_equal(fs_file_cancel(&files[1], id), SALTS_ENOENT);
    check_equal(fs_file_cancel(&files[0], id), SALTS_OK);
    check_equal(drive_until(1u, 0u), SALTS_OK);
    check_equal(probes[0].ids[0], id);
    check_equal(probes[0].events[0].kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(fs_file_cancel(&files[0], id), SALTS_ENOENT);
    check_equal(fs_file_read_at(&files[0], read_buffers[0], 1u, 0u, &id), SALTS_OK);
    check_equal(drive_until(2u, 0u), SALTS_OK);
    check_equal(probes[0].events[1].kind, NATIVE_IO_COMPLETION_EOF);
    probes[0].try_reentrant = true;
    check_equal(fs_file_write_at(&files[0], payload, sizeof(payload), 0u, &id), SALTS_OK);
    check_equal(drive_until(3u, 0u), SALTS_OK);
    check_equal(probes[0].reentrant_status, SALTS_EBUSY);
    check_equal(probes[0].destroy_status, SALTS_EBUSY);
    check_equal(fs_file_read_at(&files[0], read_buffers[0], 1u, 0u, &id), SALTS_ESHUTDOWN);
    check_equal(fs_file_destroy(&files[0]), SALTS_OK);
    check_equal(fs_file_write_at(&files[1], payload, sizeof(payload), 0u, &id), SALTS_OK);
    check_equal(fs_file_close(&files[1]), SALTS_OK);
    check_equal(fs_file_destroy(&files[1]), SALTS_EBUSY);
    check_equal(drive_until(3u, 1u), SALTS_OK);
    check_equal(probes[1].events[0].kind, NATIVE_IO_COMPLETION_CANCELLED);
    size_t progressed;
    check_equal(fs_file_runtime_run_ready(&runtime, 16u, &progressed), SALTS_OK);
    check_equal(probes[0].count, (size_t)3u);
    check_equal(probes[1].count, (size_t)1u);
  }
  it("rejects an unsupported backend before truncation and validates offset arithmetic") {
    paths[0] = tt_make_temp_file("salts-file-", ".dat");
    check_not_null(paths[0]);
    FILE *stream = fopen(paths[0], "wb");
    check_not_null(stream);
    size_t written = fwrite(payload, 1u, sizeof(payload), stream);
    int closed = fclose(stream);
    check_equal(written, sizeof(payload)); check_equal(closed, 0);
    fs_file_config config = {.backend_kind = NATIVE_IO_BACKEND_EPOLL,
        .request_capacity = 1u, .completion_batch_capacity = 1u,
        .open_flags = SALTS_FS_FILE_WRITE | SALTS_FS_FILE_TRUNCATE,
        .completion = file_completion, .completion_user = &probes[0]};
    check_equal(fs_file_open(&files[0], paths[0], &config), SALTS_ENOTSUP);
    check_null(files[0].impl);
    stream = fopen(paths[0], "rb"); check_not_null(stream);
    size_t read = fread(read_buffers[0], 1u, sizeof(read_buffers[0]), stream);
    closed = fclose(stream);
    check_equal(closed, 0); check_equal(read, sizeof(payload));
    check_equal(read_buffers[0], payload, sizeof(payload));
    config.backend_kind = file_backend(); config.open_flags = SALTS_FS_FILE_READ;
    check_equal(fs_file_open(&files[0], paths[0], &config), SALTS_OK);
    uint64_t id = UINT64_MAX;
    check_equal(fs_file_read_at(&files[0], read_buffers[0], 2u, INT64_MAX, &id), SALTS_EINVAL);
    check_equal(id, (uint64_t)0u);
    check_equal(fs_file_write_at(&files[0], payload, sizeof(payload), 0u, &id), SALTS_EPERM);
  }
}
