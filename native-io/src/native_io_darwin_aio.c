#include "native_io_internal.h"
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <threads/threads.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

/* Port of the native Darwin AIO file path from Salts CFlow. The NativeIO
 * observer owns aio_return and reclamation; cancellation never releases a cb.
 */
typedef struct { int fd; uint32_t generation; size_t pending; bool active; } aio_endpoint;
typedef struct {
  struct aiocb control;
  native_io_operation operation;
  native_io_request request;
  bool active;
} aio_request;
typedef struct {
  io_impl base;
  native_io_backend_config config;
  aio_endpoint *endpoints;
  aio_request *requests;
  const struct aiocb **waiting;
  native_io_backend_stats stats;
  atomic_bool wake_pending;
} aio_impl;

enum { DARWIN_AIO_WAKE_SLICE_MS = 10 };
static void aio_counter_increment(uint64_t *counter) {
  if (*counter != UINT64_MAX) ++*counter;
}

static uint32_t aio_generation(uint32_t old) { return old == UINT32_MAX ? 1u : old + 1u; }
static aio_endpoint *aio_endpoint_find(aio_impl *impl, native_io_endpoint handle) {
  if (!handle.slot || handle.slot > impl->config.endpoint_capacity) return NULL;
  aio_endpoint *endpoint = &impl->endpoints[handle.slot - 1u];
  return endpoint->active && endpoint->generation == handle.generation ? endpoint : NULL;
}
static int aio_attach(io_impl *base, uintptr_t value, native_io_endpoint *out) {
  aio_impl *impl = (aio_impl *)base;
  if (!impl->stats.admission_open) return SALTS_ESHUTDOWN;
  if (value > INT_MAX) return SALTS_EINVAL;
  struct stat info;
  if (fstat((int)value, &info) != 0) return -errno;
  int flags = fcntl((int)value, F_GETFL);
  if (flags < 0) return -errno;
  if (!S_ISREG(info.st_mode) || (flags & O_APPEND)) return SALTS_EINVAL;
  for (size_t i = 0u; i < impl->config.endpoint_capacity; ++i)
    if (impl->endpoints[i].active && impl->endpoints[i].fd == (int)value) return SALTS_EALREADY;
  for (size_t i = 0u; i < impl->config.endpoint_capacity; ++i) {
    aio_endpoint *endpoint = &impl->endpoints[i];
    if (endpoint->active) continue;
    endpoint->fd = (int)value; endpoint->active = true;
    endpoint->generation = aio_generation(endpoint->generation);
    *out = (native_io_endpoint){(uint32_t)i + 1u, endpoint->generation};
    ++impl->stats.endpoint_count;
    return SALTS_OK;
  }
  return SALTS_ENOBUFS;
}
static int aio_release(io_impl *base, native_io_endpoint handle) {
  aio_impl *impl = (aio_impl *)base;
  aio_endpoint *endpoint = aio_endpoint_find(impl, handle);
  if (!endpoint) return SALTS_ENOENT;
  if (endpoint->pending) return SALTS_EBUSY;
  endpoint->active = false;
  --impl->stats.endpoint_count;
  return SALTS_OK;
}
static int aio_submit_request(io_impl *base, const native_io_operation *operation,
                              native_io_request *out) {
  aio_impl *impl = (aio_impl *)base;
  if (!impl->stats.admission_open) return SALTS_ESHUTDOWN;
  aio_endpoint *endpoint = aio_endpoint_find(impl, operation->endpoint);
  if (!endpoint) return SALTS_ENOENT;
  if (native_io_operation_resource_kind(operation->kind) != SALTS_IO_RESOURCE_FILE) return SALTS_ENOTSUP;
  for (size_t i = 0u; i < impl->config.request_capacity; ++i) {
    aio_request *request = &impl->requests[i];
    if (request->active) continue;
    request->control = (struct aiocb){0};
    request->control.aio_fildes = endpoint->fd;
    request->control.aio_offset = (off_t)operation->offset;
    request->control.aio_buf = operation->buffer;
    request->control.aio_nbytes = operation->length;
    request->control.aio_sigevent.sigev_notify = SIGEV_NONE;
    int status = operation->kind == NATIVE_IO_OPERATION_FILE_FLUSH ? aio_fsync(O_SYNC, &request->control) :
        operation->kind == NATIVE_IO_OPERATION_FILE_READ_AT ? aio_read(&request->control) : aio_write(&request->control);
    if (status < 0) { aio_counter_increment(&impl->stats.native_submit_errors); return -errno; }
    request->operation = *operation;
    request->request = (native_io_request){(uint32_t)i + 1u, aio_generation(request->request.generation)};
    request->active = true; ++endpoint->pending;
    ++impl->stats.active_requests; aio_counter_increment(&impl->stats.submitted);
    *out = request->request;
    return SALTS_OK;
  }
  aio_counter_increment(&impl->stats.rejected_full);
  return SALTS_ENOBUFS;
}
static int aio_cancel_request(io_impl *base, native_io_request handle) {
  aio_impl *impl = (aio_impl *)base;
  if (!handle.slot || handle.slot > impl->config.request_capacity) return SALTS_ENOENT;
  aio_request *request = &impl->requests[handle.slot - 1u];
  if (!request->active || request->request.generation != handle.generation) return SALTS_ENOENT;
  if (aio_cancel(request->control.aio_fildes, &request->control) >= 0) return SALTS_OK;
  aio_counter_increment(&impl->stats.native_cancel_errors);
  return -errno;
}
static int aio_observe(io_impl *base, native_io_completion *events, size_t capacity,
                        uint32_t timeout_ms, size_t *out_count) {
  aio_impl *impl = (aio_impl *)base;
  uint64_t start = monotonic_ms();
  *out_count = 0u;
  for (;;) {
    if (atomic_exchange(&impl->wake_pending, false)) return SALTS_OK;
    size_t waiting = 0u;
    for (size_t i = 0u; i < impl->config.request_capacity; ++i) {
      aio_request *request = &impl->requests[i];
      if (!request->active) continue;
      int error = aio_error(&request->control);
      if (error == EINPROGRESS) { impl->waiting[waiting++] = &request->control; continue; }
      if (*out_count == capacity) break;
      if (error < 0) return -errno;
      ssize_t bytes = aio_return(&request->control);
      native_io_completion event = {.request = request->request, .endpoint = request->operation.endpoint,
          .kind = NATIVE_IO_COMPLETION_OK, .user_data = request->operation.user_data};
      if (error == ECANCELED) {
        event.kind = NATIVE_IO_COMPLETION_CANCELLED; event.status = SALTS_ECANCELED; event.native_status = ECANCELED; aio_counter_increment(&impl->stats.cancelled);
      } else if (error || bytes < 0) {
        event.kind = NATIVE_IO_COMPLETION_FAILED; event.native_status = (uint32_t)(error ? error : errno);
        event.status = -(int)event.native_status; aio_counter_increment(&impl->stats.failed);
      } else {
        event.bytes = (size_t)bytes;
        if (!bytes && request->operation.kind == NATIVE_IO_OPERATION_FILE_READ_AT) {
          event.kind = NATIVE_IO_COMPLETION_EOF; event.status = SALTS_EOF;
        }
      }
      aio_endpoint *endpoint = aio_endpoint_find(impl, event.endpoint);
      if (!endpoint || !endpoint->pending) abort();
      --endpoint->pending; request->active = false;
      --impl->stats.active_requests; aio_counter_increment(&impl->stats.completed);
      events[(*out_count)++] = event;
    }
    if (*out_count) return SALTS_OK;
    uint32_t remaining = native_io_remaining_timeout(start, timeout_ms);
    if (!remaining) return SALTS_ETIMEDOUT;
    uint32_t slice = remaining > DARWIN_AIO_WAKE_SLICE_MS ? DARWIN_AIO_WAKE_SLICE_MS : remaining;
    if (!waiting) { sleep_ms(slice); continue; }
    struct timespec time = {0, (long)slice * 1000000L};
    if (aio_suspend(impl->waiting, (int)waiting, &time) != 0 && errno != EAGAIN && errno != EINTR)
      return -errno;
  }
}
static int aio_wake(io_impl *base) {
  aio_impl *impl = (aio_impl *)base;
  if (!impl->stats.admission_open) return SALTS_ESHUTDOWN;
  atomic_store(&impl->wake_pending, true);
  return SALTS_OK;
}
static int aio_close(io_impl *base) {
  aio_impl *impl = (aio_impl *)base;
  if (!impl->stats.admission_open) return SALTS_EALREADY;
  impl->stats.admission_open = false;
  return SALTS_OK;
}
static int aio_destroy(io_impl *base) {
  aio_impl *impl = (aio_impl *)base;
  if (impl->stats.admission_open || impl->stats.active_requests || impl->stats.endpoint_count) return SALTS_EBUSY;
  free(impl->waiting); free(impl->requests); free(impl->endpoints); free(impl);
  return SALTS_OK;
}
static bool aio_stats(const io_impl *base, native_io_backend_stats *out) {
  *out = ((const aio_impl *)base)->stats; return true;
}
static const io_impl_ops aio_ops = {
    .attach_file = aio_attach, .release_file = aio_release, .submit = aio_submit_request,
    .cancel = aio_cancel_request, .observe = aio_observe, .wake = aio_wake,
    .close = aio_close, .destroy = aio_destroy, .get_stats = aio_stats};

int native_io_darwin_aio_init(native_io_backend *backend, const native_io_backend_config *config) {
  if (config->request_capacity > INT_MAX || config->endpoint_capacity > SIZE_MAX / sizeof(aio_endpoint) ||
      config->request_capacity > SIZE_MAX / sizeof(aio_request)) return SALTS_ERANGE;
  aio_impl *impl = calloc(1u, sizeof(*impl));
  if (!impl) return SALTS_ENOMEM;
  impl->endpoints = calloc(config->endpoint_capacity, sizeof(*impl->endpoints));
  impl->requests = calloc(config->request_capacity, sizeof(*impl->requests));
  impl->waiting = calloc(config->request_capacity, sizeof(*impl->waiting));
  if (!impl->endpoints || !impl->requests || !impl->waiting) {
    free(impl->waiting); free(impl->requests); free(impl->endpoints); free(impl); return SALTS_ENOMEM;
  }
  impl->base.ops = &aio_ops; impl->base.kind = config->kind; impl->config = *config;
  atomic_init(&impl->wake_pending, false);
  impl->stats = (native_io_backend_stats){.endpoint_capacity = config->endpoint_capacity,
      .request_capacity = config->request_capacity, .admission_open = true};
  backend->impl = impl;
  return SALTS_OK;
}
