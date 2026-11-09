#ifndef SALTS_NATIVE_IO_INTERNAL_H
#define SALTS_NATIVE_IO_INTERNAL_H

#include <native_io.h>
#include "native_io_internal_accept.h"
#include <salts/clock.h>

static inline uint32_t native_io_remaining_timeout(uint64_t started_ms, uint32_t timeout_ms) {
  uint64_t elapsed;
  if (timeout_ms == UINT32_MAX) return UINT32_MAX;
  elapsed = monotonic_ms() - started_ms;
  return elapsed >= timeout_ms ? 0u : timeout_ms - (uint32_t)elapsed;
}

typedef struct io_impl io_impl;
typedef struct native_io_coroutine_owner native_io_coroutine_owner;

typedef enum io_resource_kind {
  SALTS_IO_RESOURCE_STREAM_SOCKET = 1,
  SALTS_IO_RESOURCE_DATAGRAM_SOCKET = 2,
  SALTS_IO_RESOURCE_BYTE_PIPE = 3,
  SALTS_IO_RESOURCE_FILE = 4
} io_resource_kind;

static inline bool native_io_resource_kind_is_socket(io_resource_kind kind) {
  return kind == SALTS_IO_RESOURCE_STREAM_SOCKET || kind == SALTS_IO_RESOURCE_DATAGRAM_SOCKET;
}

static inline io_resource_kind
native_io_operation_resource_kind(native_io_operation_kind kind) {
  if (kind == NATIVE_IO_OPERATION_STREAM_RECV || kind == NATIVE_IO_OPERATION_STREAM_SEND ||
      kind == NATIVE_IO_OPERATION_STREAM_CONNECT)
    return SALTS_IO_RESOURCE_STREAM_SOCKET;
  if (kind == NATIVE_IO_OPERATION_UDP_RECV_FROM || kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
    return SALTS_IO_RESOURCE_DATAGRAM_SOCKET;
  if (kind == NATIVE_IO_OPERATION_PIPE_READ || kind == NATIVE_IO_OPERATION_PIPE_WRITE)
    return SALTS_IO_RESOURCE_BYTE_PIPE;
  if (kind == NATIVE_IO_OPERATION_FILE_READ_AT || kind == NATIVE_IO_OPERATION_FILE_WRITE_AT ||
      kind == NATIVE_IO_OPERATION_FILE_FLUSH) return SALTS_IO_RESOURCE_FILE;
  return (io_resource_kind)0;
}

typedef struct io_impl_ops {
  int (*attach_socket)(io_impl *impl, uintptr_t native_socket,
                       native_io_endpoint *out_endpoint);
  int (*release_socket)(io_impl *impl, native_io_endpoint endpoint);
  int (*submit)(io_impl *impl, const native_io_operation *operation,
                native_io_request *out_request);
  int (*cancel)(io_impl *impl, native_io_request request);
  int (*observe)(io_impl *impl, native_io_completion *events, size_t event_capacity,
                 uint32_t timeout_ms, size_t *out_count);
  int (*wake)(io_impl *impl);
  int (*close)(io_impl *impl);
  int (*destroy)(io_impl *impl);
  bool (*get_stats)(const io_impl *impl, native_io_backend_stats *out_stats);
  int (*attach_pipe)(io_impl *impl, uintptr_t native_handle, uint32_t flags,
                     native_io_endpoint *out_endpoint);
  int (*release_pipe)(io_impl *impl, native_io_endpoint endpoint);
  int (*prepare)(io_impl *impl, const native_io_operation *operation,
                 native_io_request *out_request);
  /* NULL means this driver starts work immediately and has no staged SQ. */
  int (*flush)(io_impl *impl);
  int (*submit_vector)(io_impl *impl, const native_io_vector_operation *operation,
                       native_io_request *out_request);
  bool (*supports_vector_write)(const io_impl *impl, native_io_endpoint endpoint);

  /*
   * Private listener-accept seam for CNet/shared-progress composition.
   * Existing backends leave these NULL until #698 backend work supplies them.
   * Accepted native transports never enter the public NativeIO ABI.
   */
  int (*submit_stream_accept)(io_impl *impl, native_io_endpoint listener,
                              native_io_request *out_request);
  int (*take_stream_accept)(io_impl *impl, native_io_request request,
                            uintptr_t *out_transport);
  int (*attach_file)(io_impl *impl, uintptr_t native_handle,
                     native_io_endpoint *out_endpoint);
  int (*release_file)(io_impl *impl, native_io_endpoint endpoint);
} io_impl_ops;

struct io_impl {
  const io_impl_ops *ops;
  native_io_backend_kind kind;
  native_io_coroutine_owner *coroutine_owner;
  size_t coroutine_capacity;
  size_t coroutine_completion_capacity;
};

bool native_io_platform_backend_supported(native_io_backend_kind kind);
bool native_io_platform_pipe_supported(native_io_backend_kind kind);
int native_io_platform_backend_init(native_io_backend *backend,
                                    const native_io_backend_config *config);

#endif /* SALTS_NATIVE_IO_INTERNAL_H */
