#include "native_io_internal.h"

#include <error_codes.h>

int cmeta_io_epoll_backend_init(native_io_backend *backend, const native_io_backend_config *config);
int cmeta_io_uring_backend_init(native_io_backend *backend, const native_io_backend_config *config);

bool native_io_platform_backend_supported(native_io_backend_kind kind) {
  return kind == NATIVE_IO_BACKEND_EPOLL || kind == NATIVE_IO_BACKEND_IO_URING;
}

bool native_io_platform_pipe_supported(native_io_backend_kind kind) {
  return kind == NATIVE_IO_BACKEND_EPOLL || kind == NATIVE_IO_BACKEND_IO_URING;
}

int native_io_platform_backend_init(native_io_backend *backend,
                                   const native_io_backend_config *config) {
  if (config->kind == NATIVE_IO_BACKEND_EPOLL) return cmeta_io_epoll_backend_init(backend, config);
  if (config->kind == NATIVE_IO_BACKEND_IO_URING)
    return cmeta_io_uring_backend_init(backend, config);
  return SALTS_ENOTSUP;
}
