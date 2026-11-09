#ifndef SALTS_NATIVE_IO_INTERNAL_ACCEPT_H
#define SALTS_NATIVE_IO_INTERNAL_ACCEPT_H

#include <native_io.h>

#include <stdint.h>

/*
 * Repository-private listener-accept seam.
 *
 * This header is intentionally not installed. It exists only so CNet and
 * NativeIO backend code can compose opaque accepted transports without
 * exposing fd/SOCKET/HANDLE values through the public NativeIO ABI.
 */
int native_io_internal_submit_stream_accept(
    native_io_backend *backend,
    native_io_endpoint listener,
    native_io_request *out_request);

int native_io_internal_take_stream_accept(
    native_io_backend *backend,
    native_io_request request,
    uintptr_t *out_transport);

#endif /* SALTS_NATIVE_IO_INTERNAL_ACCEPT_H */
