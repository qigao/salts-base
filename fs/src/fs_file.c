#include <fs_file.h>
#include <native_io_driver.h>
#include <stdatomic.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
  native_io_driver driver;
  native_io_backend_kind kind;
  size_t file_capacity, open_files;
  bool closing;
} file_runtime;
typedef struct {
  salts_fs_file_runtime private_runtime;
  file_runtime *runtime;
  uintptr_t handle;
  native_io_endpoint endpoint;
  salts_fs_file_config config;
  atomic_size_t pending;
  atomic_bool closing;
  bool handle_closed;
} file_impl;

int salts_fs_file_runtime_init(salts_fs_file_runtime *runtime, const salts_fs_file_runtime_config *config) {
  if (!runtime || runtime->impl || !config || !config->file_capacity) return SALTS_EINVAL;
  if (!native_io_backend_kind_supports_file(config->backend_kind, NATIVE_IO_OPERATION_FILE_READ_AT)) return SALTS_ENOTSUP;
  file_runtime *impl = calloc(1u, sizeof(*impl));
  if (!impl) return SALTS_ENOMEM;
  native_io_backend_config native = {config->backend_kind, config->file_capacity,
      config->request_capacity, config->completion_batch_capacity};
  int status = native_io_driver_init(&impl->driver, &native);
  if (status != SALTS_OK) { free(impl); return status; }
  impl->kind = config->backend_kind; impl->file_capacity = config->file_capacity;
  runtime->impl = impl;
  return SALTS_OK;
}
int salts_fs_file_runtime_run_ready(salts_fs_file_runtime *runtime, size_t steps, size_t *progressed) {
  file_runtime *impl = runtime ? runtime->impl : NULL;
  return impl ? native_io_driver_run_ready(&impl->driver, steps, progressed) : SALTS_EINVAL;
}
int salts_fs_file_runtime_close(salts_fs_file_runtime *runtime) {
  file_runtime *impl = runtime ? runtime->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  impl->closing = true;
  return native_io_driver_close(&impl->driver);
}
bool salts_fs_file_runtime_is_quiescent(const salts_fs_file_runtime *runtime) {
  file_runtime *impl = runtime ? runtime->impl : NULL;
  return impl && native_io_driver_is_quiescent(&impl->driver);
}
int salts_fs_file_runtime_destroy(salts_fs_file_runtime *runtime) {
  file_runtime *impl = runtime ? runtime->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  if (impl->open_files) return SALTS_EBUSY;
  int status = native_io_driver_destroy(&impl->driver);
  if (status != SALTS_OK) return status;
  free(impl); runtime->impl = NULL; return SALTS_OK;
}

static int file_open_native(const char *path, const salts_fs_file_config *config, uintptr_t *out) {
#ifdef _WIN32
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (count <= 0) return -(int)GetLastError();
  WCHAR *wide = calloc((size_t)count, sizeof(*wide));
  if (!wide) return SALTS_ENOMEM;
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
    int status = -(int)GetLastError(); free(wide); return status;
  }
  DWORD access = ((config->open_flags & SALTS_FS_FILE_READ) ? GENERIC_READ : 0u) |
                 ((config->open_flags & SALTS_FS_FILE_WRITE) ? GENERIC_WRITE : 0u);
  DWORD creation = config->open_flags & SALTS_FS_FILE_CREATE ?
      (config->open_flags & SALTS_FS_FILE_TRUNCATE ? CREATE_ALWAYS : OPEN_ALWAYS) :
      (config->open_flags & SALTS_FS_FILE_TRUNCATE ? TRUNCATE_EXISTING : OPEN_EXISTING);
  HANDLE handle = CreateFileW(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL, creation, FILE_FLAG_OVERLAPPED, NULL);
  int status = handle == INVALID_HANDLE_VALUE ? -(int)GetLastError() : SALTS_OK;
  free(wide); if (status == SALTS_OK) *out = (uintptr_t)handle; return status;
#else
  unsigned access = config->open_flags & (SALTS_FS_FILE_READ | SALTS_FS_FILE_WRITE);
  int flags = access == SALTS_FS_FILE_READ ? O_RDONLY : access == SALTS_FS_FILE_WRITE ? O_WRONLY : O_RDWR;
  if (config->open_flags & SALTS_FS_FILE_CREATE) flags |= O_CREAT;
  if (config->open_flags & SALTS_FS_FILE_TRUNCATE) flags |= O_TRUNC;
  int fd = open(path, flags | O_CLOEXEC | O_NONBLOCK, config->create_mode);
  if (fd < 0) return -errno;
  struct stat info;
  int status = SALTS_OK;
  if (fstat(fd, &info) != 0) status = -errno;
  else if (!S_ISREG(info.st_mode)) status = SALTS_EINVAL;
  else {
    int current = fcntl(fd, F_GETFL);
    if (current < 0 || fcntl(fd, F_SETFL, current & ~O_NONBLOCK) != 0) status = -errno;
  }
  if (status != SALTS_OK) { close(fd); return status; }
  *out = (uintptr_t)fd; return SALTS_OK;
#endif
}
static int file_close_native(uintptr_t handle) {
#ifdef _WIN32
  return CloseHandle((HANDLE)handle) ? SALTS_OK : -(int)GetLastError();
#else
  return close((int)handle) == 0 ? SALTS_OK : -errno;
#endif
}
int salts_fs_file_open(salts_fs_file *file, const char *path, const salts_fs_file_config *config) {
  const unsigned access_flags = SALTS_FS_FILE_READ | SALTS_FS_FILE_WRITE;
  const unsigned valid_flags = access_flags | SALTS_FS_FILE_CREATE | SALTS_FS_FILE_TRUNCATE;
  if (!file || file->impl || !path || !*path || !config || !config->completion ||
      (config->open_flags & ~valid_flags) || !(config->open_flags & access_flags) ||
      ((config->open_flags & (SALTS_FS_FILE_CREATE | SALTS_FS_FILE_TRUNCATE)) &&
       !(config->open_flags & SALTS_FS_FILE_WRITE))) return SALTS_EINVAL;
  if (config->runtime && (config->backend_kind || config->request_capacity || config->completion_batch_capacity)) return SALTS_EINVAL;
  file_impl *impl = calloc(1u, sizeof(*impl));
  if (!impl) return SALTS_ENOMEM;
  impl->handle = UINTPTR_MAX; impl->config = *config;
  atomic_init(&impl->pending, 0u); atomic_init(&impl->closing, false);
  int status = SALTS_OK;
  if (!config->runtime) {
    salts_fs_file_runtime_config runtime_config = {config->backend_kind, 1u,
        config->request_capacity, config->completion_batch_capacity};
    status = salts_fs_file_runtime_init(&impl->private_runtime, &runtime_config);
    if (status != SALTS_OK) { free(impl); return status; }
  }
  salts_fs_file_runtime *runtime = config->runtime ? config->runtime : &impl->private_runtime;
  impl->runtime = runtime->impl;
  if (!impl->runtime) { status = SALTS_EINVAL; goto error; }
  if (impl->runtime->closing) { status = SALTS_ESHUTDOWN; goto error; }
  if (impl->runtime->open_files == impl->runtime->file_capacity) { status = SALTS_ENOBUFS; goto error; }
  status = file_open_native(path, config, &impl->handle);
  if (status == SALTS_OK) status = native_io_backend_attach_file(
      native_io_driver_backend(&impl->runtime->driver), impl->handle, &impl->endpoint);
  if (status != SALTS_OK) goto error;
  ++impl->runtime->open_files; file->impl = impl; return SALTS_OK;
error:
  if (impl->handle != UINTPTR_MAX) file_close_native(impl->handle);
  if (impl->private_runtime.impl) {
    salts_fs_file_runtime_close(&impl->private_runtime);
    salts_fs_file_runtime_destroy(&impl->private_runtime);
  }
  free(impl); return status;
}

static void file_completed(void *user, uint64_t id, const native_io_operation *operation,
                            const native_io_completion *completion) {
  file_impl *impl = user;
  impl->config.completion(impl->config.completion_user, id, operation->kind, completion);
  atomic_fetch_sub(&impl->pending, 1u);
}
static int file_submit(salts_fs_file *file, native_io_operation_kind kind, void *buffer,
                        size_t size, uint64_t offset, uint64_t *id) {
  file_impl *impl = file ? file->impl : NULL;
  if (id) *id = 0u;
  if (!impl || !id) return SALTS_EINVAL;
  if (atomic_load(&impl->closing)) return SALTS_ESHUTDOWN;
  if (!native_io_backend_kind_supports_file(impl->runtime->kind, kind)) return SALTS_ENOTSUP;
  if ((kind == NATIVE_IO_OPERATION_FILE_READ_AT && !(impl->config.open_flags & SALTS_FS_FILE_READ)) ||
      (kind != NATIVE_IO_OPERATION_FILE_READ_AT && !(impl->config.open_flags & SALTS_FS_FILE_WRITE))) return SALTS_EPERM;
  native_io_operation operation = {.kind = kind, .endpoint = impl->endpoint,
      .buffer = buffer, .length = size, .offset = offset};
  atomic_fetch_add(&impl->pending, 1u);
  int status = native_io_driver_submit(&impl->runtime->driver, &operation, file_completed, impl, id);
  if (status != SALTS_OK) atomic_fetch_sub(&impl->pending, 1u);
  return status;
}
int salts_fs_file_read_at(salts_fs_file *file, void *buffer, size_t size, uint64_t offset, uint64_t *id) {
  return file_submit(file, NATIVE_IO_OPERATION_FILE_READ_AT, buffer, size, offset, id);
}
int salts_fs_file_write_at(salts_fs_file *file, const void *buffer, size_t size, uint64_t offset, uint64_t *id) {
  return file_submit(file, NATIVE_IO_OPERATION_FILE_WRITE_AT, (void *)buffer, size, offset, id);
}
int salts_fs_file_flush(salts_fs_file *file, uint64_t *id) {
  return file_submit(file, NATIVE_IO_OPERATION_FILE_FLUSH, NULL, 0u, 0u, id);
}
int salts_fs_file_cancel(salts_fs_file *file, uint64_t id) {
  file_impl *impl = file ? file->impl : NULL;
  return impl && id ? native_io_driver_cancel_endpoint(&impl->runtime->driver, impl->endpoint, id) : SALTS_EINVAL;
}
int salts_fs_file_run_ready(salts_fs_file *file, size_t steps, size_t *progressed) {
  file_impl *impl = file ? file->impl : NULL;
  return impl ? native_io_driver_run_ready(&impl->runtime->driver, steps, progressed) : SALTS_EINVAL;
}
int salts_fs_file_close(salts_fs_file *file) {
  file_impl *impl = file ? file->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  atomic_store(&impl->closing, true);
  int status = native_io_driver_cancel_endpoint(&impl->runtime->driver, impl->endpoint, 0u);
  return status == SALTS_ENOENT ? SALTS_OK : status;
}
bool salts_fs_file_is_quiescent(const salts_fs_file *file) {
  file_impl *impl = file ? file->impl : NULL;
  return impl && atomic_load(&impl->closing) && atomic_load(&impl->pending) == 0u;
}
int salts_fs_file_destroy(salts_fs_file *file) {
  file_impl *impl = file ? file->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  if (!salts_fs_file_is_quiescent(file)) return SALTS_EBUSY;
  if (!impl->handle_closed) {
    int status = file_close_native(impl->handle);
    impl->handle_closed = true;
    if (status != SALTS_OK) return status;
  }
  if (native_io_endpoint_valid(impl->endpoint)) {
    int status = native_io_backend_release_file(native_io_driver_backend(&impl->runtime->driver), impl->endpoint);
    if (status != SALTS_OK) return status;
    impl->endpoint = (native_io_endpoint){0}; --impl->runtime->open_files;
  }
  if (impl->private_runtime.impl) {
    int status = salts_fs_file_runtime_close(&impl->private_runtime);
    if (status == SALTS_OK) status = salts_fs_file_runtime_destroy(&impl->private_runtime);
    if (status != SALTS_OK) return status;
  }
  free(impl); file->impl = NULL; return SALTS_OK;
}
