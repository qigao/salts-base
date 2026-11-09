#include <native_io.h>
#include <native_ipc.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

bool cmeta_ipc_platform_capability_supported(cmeta_ipc_pipe_capability capability) {
  return capability == SALTS_IPC_POSIX_FIFO_OPEN;
}

int cmeta_ipc_platform_endpoint_close(uintptr_t handle) {
  if (handle > (uintptr_t)INT_MAX) return SALTS_EINVAL;
  return close((int)handle) == 0 ? SALTS_OK : -errno;
}

int cmeta_ipc_platform_server_init(cmeta_ipc_pipe_server *server,
                                   const cmeta_ipc_pipe_server_config *config) {
  (void)server;
  (void)config;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_server_try_accept(cmeta_ipc_pipe_server *server,
                                         cmeta_ipc_request_id *out_request_id) {
  (void)server;
  (void)out_request_id;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_server_cancel(cmeta_ipc_pipe_server *server,
                                     cmeta_ipc_request_id request_id) {
  (void)server;
  (void)request_id;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_server_observe(cmeta_ipc_pipe_server *server, size_t max_events,
                                      size_t *out_count) {
  (void)server;
  (void)max_events;
  (void)out_count;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_server_close(cmeta_ipc_pipe_server *server) {
  (void)server;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_server_wait_sources(const cmeta_ipc_pipe_server *server,
                                           uintptr_t *out_handles, size_t capacity,
                                           size_t *out_count, bool *out_ready) {
  (void)server;
  (void)out_handles;
  (void)capacity;
  (void)out_count;
  (void)out_ready;
  return SALTS_ENOTSUP;
}

bool cmeta_ipc_platform_server_is_quiescent(const cmeta_ipc_pipe_server *server) {
  (void)server;
  return false;
}

bool cmeta_ipc_platform_server_get_stats(const cmeta_ipc_pipe_server *server,
                                         cmeta_ipc_pipe_server_stats *out_stats) {
  (void)server;
  (void)out_stats;
  return false;
}

int cmeta_ipc_platform_server_destroy(cmeta_ipc_pipe_server *server) {
  (void)server;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_named_pipe_connect(const char *name, cmeta_ipc_pipe_direction direction,
                                          cmeta_ipc_pipe_endpoint *out_endpoint) {
  (void)name;
  (void)direction;
  (void)out_endpoint;
  return SALTS_ENOTSUP;
}

int cmeta_ipc_platform_fifo_open(const char *path, cmeta_ipc_pipe_direction direction,
                                 cmeta_ipc_pipe_endpoint *out_endpoint) {
  struct stat info;
  int flags = direction == SALTS_IPC_PIPE_READ ? O_RDONLY : O_WRONLY;
  int descriptor;
  int status;
  flags |= O_NONBLOCK;
#if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
#endif
  do {
    descriptor = open(path, flags);
  } while (descriptor < 0 && errno == EINTR);
  if (descriptor < 0) return errno == ENXIO ? SALTS_EPIPE : -errno;
  do {
    status = fstat(descriptor, &info);
  } while (status < 0 && errno == EINTR);
  if (status < 0) {
    status = -errno;
    (void)close(descriptor);
    return status;
  }
  if (!S_ISFIFO(info.st_mode)) {
    (void)close(descriptor);
    return SALTS_ENOTSUP;
  }
#if !defined(O_CLOEXEC)
  do {
    status = fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  } while (status < 0 && errno == EINTR);
  if (status < 0) {
    status = -errno;
    (void)close(descriptor);
    return status;
  }
#endif
  out_endpoint->handle = (uintptr_t)descriptor;
  out_endpoint->native_io_flags = NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
  return SALTS_OK;
}
