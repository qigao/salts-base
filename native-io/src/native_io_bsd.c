#include "native_io_internal.h"

#include <salts/error_codes.h>

int io_kqueue_backend_init(native_io_backend *backend, const native_io_backend_config *config);

bool native_io_platform_backend_supported(native_io_backend_kind kind) {
#ifdef SALTS_NATIVE_DARWIN_AIO
  if (kind == NATIVE_IO_BACKEND_DARWIN_AIO) return true;
#endif
  return kind == NATIVE_IO_BACKEND_KQUEUE;
}

bool native_io_platform_pipe_supported(native_io_backend_kind kind) {
  return kind == NATIVE_IO_BACKEND_KQUEUE;
}

int native_io_platform_backend_init(native_io_backend *backend,
                                   const native_io_backend_config *config) {
#ifdef SALTS_NATIVE_DARWIN_AIO
  int native_io_darwin_aio_init(native_io_backend *, const native_io_backend_config *);
  if (config->kind == NATIVE_IO_BACKEND_DARWIN_AIO) return native_io_darwin_aio_init(backend, config);
#endif
  if (config->kind == NATIVE_IO_BACKEND_KQUEUE) return io_kqueue_backend_init(backend, config);
  return SALTS_ENOTSUP;
}
