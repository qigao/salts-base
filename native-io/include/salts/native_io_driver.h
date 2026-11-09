#ifndef SALTS_NATIVE_IO_DRIVER_H
#define SALTS_NATIVE_IO_DRIVER_H
#include <salts/native_io.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct native_io_driver { void *impl; } native_io_driver;
typedef void (*native_io_driver_completion_fn)(void *user, uint64_t id,
    const native_io_operation *operation, const native_io_completion *completion);

/** Bounded MPSC admission around one NativeIO owner. Config capacities bound
 * endpoints, all queued/in-flight/delivering requests, and observation batches.
 * No worker or alternative backend is created. Zero-initialize the handle. */
int native_io_driver_init(native_io_driver *driver, const native_io_backend_config *config);
/** Owner-only endpoint attach/release access. Never submit, observe, close or
 * destroy this borrowed backend directly; the driver owns those operations. */
native_io_backend *native_io_driver_backend(native_io_driver *driver);
/** Copy the descriptor and borrow buffers through exactly one terminal callback.
 * MPSC admission uses a short mutex and never waits for capacity or native I/O.
 * Rejection clears id and retains no caller storage. Accepted native submission
 * errors become FAILED terminal callbacks. Callbacks must return normally;
 * completion/operation pointers are borrowed only during that callback. */
int native_io_driver_submit(native_io_driver *driver, const native_io_operation *operation,
    native_io_driver_completion_fn completion, void *user, uint64_t *id);
/** Request cancellation; authoritative terminal delivery still releases borrow. */
int native_io_driver_cancel(native_io_driver *driver, uint64_t id);
/** Endpoint-scoped cancellation. id zero requests every pending operation for
 * this endpoint; another endpoint's request is never affected. */
int native_io_driver_cancel_endpoint(native_io_driver *driver, native_io_endpoint endpoint, uint64_t id);
/** Exactly one owner drives bounded state transitions/callbacks. Reentrant or
 * concurrent drive returns EBUSY. Producers may submit/cancel during callbacks. */
int native_io_driver_run_ready(native_io_driver *driver, size_t max_steps, size_t *progressed);
/** Close admission and request cancellation without releasing requests. Stop
 * producers before close/destroy; continue driving to quiescence. */
int native_io_driver_close(native_io_driver *driver);
bool native_io_driver_is_quiescent(const native_io_driver *driver);
/** Owner only; preserves the driver with EBUSY until drained and all borrowed
 * endpoint handles have been closed and their backend metadata released. */
int native_io_driver_destroy(native_io_driver *driver);

#ifdef __cplusplus
}
#endif
#endif
