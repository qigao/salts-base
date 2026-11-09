#include "native_io_internal.h"

#include <salts/error_codes.h>

bool native_io_platform_backend_supported(native_io_backend_kind kind) {
  (void)kind;
  return false;
}

bool native_io_platform_pipe_supported(native_io_backend_kind kind) {
  (void)kind;
  return false;
}

int native_io_platform_backend_init(native_io_backend *backend,
                                   const native_io_backend_config *config) {
  (void)backend;
  (void)config;
  return SALTS_ENOTSUP;
}
