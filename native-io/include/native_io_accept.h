#ifndef SALTS_NATIVE_IO_ACCEPT_H
#define SALTS_NATIVE_IO_ACCEPT_H

#include <native_io.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same-owner listener composition for CNet and other transport owners.
 * These existing symbols retain their names and ABI after the SDK split.
 * Calls borrow backend/listener; no worker or independent observer is created.
 *
 * Submit one accept on an attached listening stream endpoint. Success admits
 * one request, returned in out_request, within the backend's request capacity.
 * The backend owns any accepted native transport until take succeeds. Route
 * the matching real completion before taking it; admission is not completion.
 * Invalid arguments return SALTS_EINVAL, unsupported backends SALTS_ENOTSUP;
 * backend admission/capacity/closed errors propagate unchanged. A non-NULL
 * out_request is cleared on entry. Failure admits no new request.
 */
int native_io_internal_submit_stream_accept(
    native_io_backend *backend,
    native_io_endpoint listener,
    native_io_request *out_request);

/* After the matching successful completion, transfer its accepted native
 * transport exactly once to out_transport. The caller then owns closing it
 * or transferring it to the endpoint/transport owner. This value is an OS
 * socket (SOCKET on Windows, fd on POSIX), not a NativeIO endpoint token.
 * Invalid arguments return SALTS_EINVAL; unsupported backends SALTS_ENOTSUP;
 * absent/stale/already-taken request errors propagate from the backend. On
 * error a non-NULL output is UINTPTR_MAX and ownership does not transfer.
 * Cancellation does not grant a transport: keep routing terminals before
 * backend close/destroy so escrowed transports can be retired by their owner.
 *
 * Example, after routing the successful completion for the admitted request:
 *   uintptr_t transport;
 *   int status = native_io_internal_take_stream_accept(backend,
 *       completion.request, &transport);
 *   if (status == SALTS_OK) { // adopt transport, or close it on adoption error
 *   }
 */
int native_io_internal_take_stream_accept(
    native_io_backend *backend,
    native_io_request request,
    uintptr_t *out_transport);

/** Submit one accept on a fresh overlapped Windows named-pipe server instance
 * already attached with native_io_backend_attach_pipe(). Use the default IOCP
 * notification mode. The caller owns the native handle throughout. Keep the
 * attachment and handle live until observing the matching completion, including
 * after cancellation. Success connects the same attachment for pipe data I/O.
 * Transfer the endpoint token together with the native handle to the same
 * backend's data owner. The attachment allows one successful accept.
 * Pending accepts block other operations on this endpoint. Non-IOCP backends
 * return SALTS_ENOTSUP. Failure admits no request and clears out_request.
 */
int native_io_internal_submit_pipe_accept(native_io_backend *backend,
                                          native_io_endpoint endpoint,
                                          native_io_request *out_request);

#ifdef __cplusplus
}
#endif
#endif /* SALTS_NATIVE_IO_ACCEPT_H */
