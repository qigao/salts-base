#include <process_async.h>
#include <native_io_driver.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

enum { SALTS_PROCESS_ASYNC_PIPE_NAME_CAPACITY = 192, SALTS_PROCESS_ASYNC_PIPE_BUFFER_CAPACITY = 4096 };
typedef struct { struct { uintptr_t handle; uint32_t flags; } parent; uintptr_t child; } process_pipe_pair;

typedef struct {
  native_io_driver driver;
  process_t *process;
  process_async_config config;
  uintptr_t handles[3];
  native_io_endpoint endpoints[3];
  atomic_size_t pending[3];
  atomic_bool closing;
} process_async_impl;

static int process_async_handle_close(uintptr_t *handle) {
  if (*handle == UINTPTR_MAX) return SALTS_OK;
  uintptr_t native = *handle;
  *handle = UINTPTR_MAX;
#ifdef _WIN32
  return CloseHandle((HANDLE)native) ? SALTS_OK : -(int)GetLastError();
#else
  return close((int)native) == 0 ? SALTS_OK : -errno;
#endif
}
#if defined(_WIN32)

static int process_pipe_pair_create(process_pipe_pair *pair, bool parent_writes,
                                    unsigned int discriminator) {
  static volatile LONG pipe_sequence = 0;
  char name[SALTS_PROCESS_ASYNC_PIPE_NAME_CAPACITY];
  SECURITY_ATTRIBUTES child_security = {sizeof(child_security), NULL, TRUE};
  OVERLAPPED overlapped;
  HANDLE event = NULL;
  HANDLE parent = INVALID_HANDLE_VALUE;
  HANDLE child = INVALID_HANDLE_VALUE;
  DWORD server_access = parent_writes ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND;
  DWORD child_access = parent_writes ? GENERIC_READ : GENERIC_WRITE;
  DWORD error;
  DWORD bytes = 0u;
  int status = SALTS_OK;

  snprintf(name, sizeof(name), "\\\\.\\pipe\\cflow-process-%lu-%lu-%u",
           (unsigned long)GetCurrentProcessId(),
           (unsigned long)InterlockedIncrement(&pipe_sequence), discriminator);
  parent = CreateNamedPipeA(
      name, server_access | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1u,
      SALTS_PROCESS_ASYNC_PIPE_BUFFER_CAPACITY, SALTS_PROCESS_ASYNC_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (parent == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    status = -(int)GetLastError();
    goto failed;
  }
  memset(&overlapped, 0, sizeof(overlapped));
  overlapped.hEvent = event;
  if (!ConnectNamedPipe(parent, &overlapped)) {
    error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      status = -(int)error;
      goto failed;
    }
  }
  child = CreateFileA(name, child_access, 0u, &child_security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                      NULL);
  if (child == INVALID_HANDLE_VALUE) {
    status = -(int)GetLastError();
    (void)CancelIoEx(parent, &overlapped);
    (void)GetOverlappedResult(parent, &overlapped, &bytes, TRUE);
    goto failed;
  }
  if (!GetOverlappedResult(parent, &overlapped, &bytes, TRUE)) {
    status = -(int)GetLastError();
    goto failed;
  }
  (void)CloseHandle(event);
  pair->parent.handle = (uintptr_t)parent;
  pair->parent.flags = NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
  pair->child = (uintptr_t)child;
  return SALTS_OK;

failed:
  if (child != INVALID_HANDLE_VALUE) (void)CloseHandle(child);
  if (event != NULL) (void)CloseHandle(event);
  if (parent != INVALID_HANDLE_VALUE) (void)CloseHandle(parent);
  return status;
}

#else

static int process_pipe_pair_create(process_pipe_pair *pair, bool parent_writes,
                                    unsigned int discriminator) {
  int handles[2] = {-1, -1};
  int parent_index = parent_writes ? 1 : 0;
  int child_index = parent_writes ? 0 : 1;
  int flags;
  int status;
  (void)discriminator;
  extern int process_create_pipe(int fds[2]);
  int created = process_create_pipe(handles);
  if (created != SALTS_OK) return created;
#if defined(__APPLE__)
  /* Darwin directs pipe SIGPIPE at the process, so a writer thread's signal mask is insufficient. */
  if (parent_writes && fcntl(handles[parent_index], F_SETNOSIGPIPE, 1) != 0) {
    status = -errno;
    goto failed;
  }
#endif
  flags = fcntl(handles[parent_index], F_GETFL, 0);
  if (flags < 0 || fcntl(handles[parent_index], F_SETFL, flags | O_NONBLOCK) != 0) {
    status = -errno;
    goto failed;
  }
  pair->parent.handle = (uintptr_t)handles[parent_index];
  pair->parent.flags = NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
  pair->child = (uintptr_t)handles[child_index];
  return SALTS_OK;

failed:
  (void)close(handles[0]);
  (void)close(handles[1]);
  return status;
}

#endif

static int process_async_release(process_async_impl *impl, size_t stream) {
  if (atomic_load(&impl->pending[stream])) return SALTS_EBUSY;
  int status = process_async_handle_close(&impl->handles[stream]);
  if (status != SALTS_OK) return status;
  if (native_io_endpoint_valid(impl->endpoints[stream])) {
    status = native_io_backend_release_pipe(native_io_driver_backend(&impl->driver), impl->endpoints[stream]);
    if (status != SALTS_OK) return status;
    impl->endpoints[stream] = (native_io_endpoint){0};
  }
  return SALTS_OK;
}

int process_async_start(process_async *process,
    const process_options_t *options, const process_async_config *config) {
  unsigned conflict = SALTS_PROCESS_PIPE_STDIN | SALTS_PROCESS_CAPTURE_STDOUT | SALTS_PROCESS_CAPTURE_STDERR;
  if (!process || process->impl || !options || (options->flags & conflict) || !config ||
      !config->completion || !config->request_capacity || !config->completion_batch_capacity) return SALTS_EINVAL;
  if (!native_io_backend_kind_supports_pipe(config->backend_kind)) return SALTS_ENOTSUP;
  process_async_impl *impl = calloc(1u, sizeof(*impl));
  if (!impl) return SALTS_ENOMEM;
  impl->config = *config;
  atomic_init(&impl->closing, false);
  for (size_t i = 0u; i < 3u; ++i) {
    impl->handles[i] = UINTPTR_MAX; atomic_init(&impl->pending[i], 0u);
  }
  native_io_backend_config backend = {config->backend_kind, 3u, config->request_capacity,
      config->completion_batch_capacity};
  int status = native_io_driver_init(&impl->driver, &backend);
  if (status != SALTS_OK) { free(impl); return status; }
  process_pipe_pair pairs[3];
  for (size_t i = 0u; i < 3u; ++i) pairs[i] = (process_pipe_pair){.parent = {UINTPTR_MAX, 0u}, .child = UINTPTR_MAX};
  for (size_t i = 0u; i < 3u; ++i) {
    status = process_pipe_pair_create(&pairs[i], i == SALTS_PROCESS_STDIN, (unsigned)i);
    if (status != SALTS_OK) goto error;
    impl->handles[i] = pairs[i].parent.handle;
    pairs[i].parent.handle = UINTPTR_MAX;
    status = native_io_backend_attach_pipe(native_io_driver_backend(&impl->driver),
        impl->handles[i], pairs[i].parent.flags, &impl->endpoints[i]);
    if (status != SALTS_OK) goto error;
  }
  process_stdio_bindings_t bindings = {pairs[0].child, pairs[1].child, pairs[2].child};
  status = process_spawn_with_stdio(options, &bindings, &impl->process);
  if (status != SALTS_OK) goto error;
  for (size_t i = 0u; i < 3u; ++i) process_async_handle_close(&pairs[i].child);
  process->impl = impl;
  return SALTS_OK;
error:
  for (size_t i = 0u; i < 3u; ++i) {
    process_async_handle_close(&pairs[i].child);
    process_async_handle_close(&pairs[i].parent.handle);
    process_async_release(impl, i);
  }
  native_io_driver_close(&impl->driver);
  native_io_driver_destroy(&impl->driver);
  free(impl);
  return status;
}

static void process_async_completed(void *user, uint64_t id, const native_io_operation *operation,
                                     const native_io_completion *completion) {
  process_async_impl *impl = user;
  for (size_t stream = 0u; stream < 3u; ++stream) {
    native_io_endpoint endpoint = impl->endpoints[stream];
    if (endpoint.slot != operation->endpoint.slot || endpoint.generation != operation->endpoint.generation) continue;
    impl->config.completion(impl->config.completion_user, id, (process_stream)stream, completion);
    atomic_fetch_sub(&impl->pending[stream], 1u);
    return;
  }
  abort(); /* A live request must retain its exact endpoint until delivery. */
}
static int process_async_submit(process_async *process, process_stream stream,
                                 void *data, size_t size, uint64_t *id) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (id) *id = 0u;
  if (!impl || !id) return SALTS_EINVAL;
  if (atomic_load(&impl->closing) || impl->handles[stream] == UINTPTR_MAX) return SALTS_ESHUTDOWN;
  native_io_operation operation = {.kind = stream == SALTS_PROCESS_STDIN ?
      NATIVE_IO_OPERATION_PIPE_WRITE : NATIVE_IO_OPERATION_PIPE_READ,
      .endpoint = impl->endpoints[stream], .buffer = data, .length = size};
  atomic_fetch_add(&impl->pending[stream], 1u);
  int status = native_io_driver_submit(&impl->driver, &operation, process_async_completed, impl, id);
  if (status != SALTS_OK) atomic_fetch_sub(&impl->pending[stream], 1u);
  return status;
}
int process_async_write_stdin(process_async *process, const void *data, size_t size, uint64_t *id) {
  return process_async_submit(process, SALTS_PROCESS_STDIN, (void *)data, size, id);
}
int process_async_read_stdout(process_async *process, void *data, size_t size, uint64_t *id) {
  return process_async_submit(process, SALTS_PROCESS_STDOUT, data, size, id);
}
int process_async_read_stderr(process_async *process, void *data, size_t size, uint64_t *id) {
  return process_async_submit(process, SALTS_PROCESS_STDERR, data, size, id);
}
int process_async_cancel(process_async *process, uint64_t id) {
  process_async_impl *impl = process ? process->impl : NULL;
  return impl ? native_io_driver_cancel(&impl->driver, id) : SALTS_EINVAL;
}
int process_async_close_stdin(process_async *process) {
  process_async_impl *impl = process ? process->impl : NULL;
  return impl ? process_async_release(impl, SALTS_PROCESS_STDIN) : SALTS_EINVAL;
}
int process_async_poll(const process_async *process, process_result_t *result) {
  process_async_impl *impl = process ? process->impl : NULL;
  return impl ? process_poll(impl->process, result) : SALTS_EINVAL;
}
int process_async_terminate(process_async *process) {
  process_async_impl *impl = process ? process->impl : NULL;
  return impl ? process_terminate(impl->process) : SALTS_EINVAL;
}
int process_async_run_ready(process_async *process, size_t steps, size_t *progressed) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  int status = native_io_driver_run_ready(&impl->driver, steps, progressed);
  if (status != SALTS_OK) return status;
  if (atomic_load(&impl->closing) && native_io_driver_is_quiescent(&impl->driver))
    for (size_t i = 0u; i < 3u; ++i) {
      status = process_async_release(impl, i);
      if (status != SALTS_OK) return status;
    }
  return SALTS_OK;
}
bool process_async_get_stats(const process_async *process, process_async_stats *stats) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (!impl || !stats) return false;
  *stats = (process_async_stats){.request_capacity = impl->config.request_capacity,
      .stdin_pending = atomic_load(&impl->pending[0]), .stdout_pending = atomic_load(&impl->pending[1]),
      .stderr_pending = atomic_load(&impl->pending[2]), .stdin_open = impl->handles[0] != UINTPTR_MAX,
      .close_requested = atomic_load(&impl->closing)};
  stats->pending_requests = stats->stdin_pending + stats->stdout_pending + stats->stderr_pending;
  return true;
}
int process_async_close(process_async *process) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  atomic_store(&impl->closing, true);
  int status = native_io_driver_close(&impl->driver);
  if (status != SALTS_OK) return status;
  return process_terminate(impl->process);
}
bool process_async_is_quiescent(const process_async *process) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (!impl || !atomic_load(&impl->closing) || !native_io_driver_is_quiescent(&impl->driver)) return false;
  for (size_t i = 0u; i < 3u; ++i)
    if (impl->handles[i] != UINTPTR_MAX || native_io_endpoint_valid(impl->endpoints[i])) return false;
  process_result_t result;
  return process_poll(impl->process, &result) == SALTS_OK;
}
int process_async_destroy(process_async *process) {
  process_async_impl *impl = process ? process->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  if (!process_async_is_quiescent(process)) return SALTS_EBUSY;
  int status = native_io_driver_destroy(&impl->driver);
  if (status != SALTS_OK) return status;
  process_destroy(impl->process);
  free(impl); process->impl = NULL;
  return SALTS_OK;
}
