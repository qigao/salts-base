#include "native_io_internal.h"

#include <error_codes.h>
#include <coro_pool.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_io_coroutine_request_owner {
  native_io_coroutine *coroutine;
  uint32_t generation;
} native_io_coroutine_request_owner;

struct native_io_coroutine {
  native_io_coroutine_owner *owner;
  coro_t *frame;
  native_io_coroutine_entry_fn entry;
  void *user_data;
  native_io_coroutine_task task;
  native_io_request request;
  native_io_completion completion;
  bool active;
  bool waiting;
  bool completion_ready;
};

struct native_io_coroutine_owner {
  cmeta_io_impl *impl;
  coro_pool_t *pool;
  native_io_coroutine *tasks;
  native_io_coroutine_request_owner *request_owners;
  native_io_completion *raw_completions;
  native_io_coroutine **ready_coroutines;
  uint32_t *free_tasks;
  size_t task_capacity;
  size_t raw_completion_capacity;
  size_t free_task_count;
  size_t active_task_count;
};

static uint32_t native_io_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static void native_io_coroutine_entry(coro_t *frame, void *argument) {
  native_io_coroutine *coroutine = (native_io_coroutine *)argument;
  (void)frame;
  coroutine->entry(coroutine, coroutine->user_data);
}

static int native_io_coroutine_retire(native_io_coroutine *coroutine, bool abandon_frame) {
  native_io_coroutine_owner *owner = coroutine->owner;
  const uint32_t index = coroutine->task.slot - 1u;
  const uint32_t generation = coroutine->task.generation;
  coro_t *frame = coroutine->frame;

  if (coroutine->waiting && native_io_request_valid(coroutine->request) &&
      coroutine->request.slot <= owner->task_capacity) {
    native_io_coroutine_request_owner *request_owner =
        &owner->request_owners[coroutine->request.slot - 1u];
    if (request_owner->coroutine == coroutine &&
        request_owner->generation == coroutine->request.generation)
      *request_owner = (native_io_coroutine_request_owner){0};
  }
  memset(coroutine, 0, sizeof(*coroutine));
  coroutine->task.generation = generation;
  owner->free_tasks[owner->free_task_count++] = index;
  --owner->active_task_count;
  if (abandon_frame)
    return coro_pool_abandon(owner->pool, frame) == 0 ? SALTS_OK : SALTS_EIO;
  coro_pool_release(owner->pool, frame);
  return SALTS_OK;
}

static int native_io_coroutine_resume(native_io_coroutine *coroutine) {
  if (coro_resume(coroutine->frame) != 0) return SALTS_EIO;
  if (coro_state(coroutine->frame) == coro_DEAD)
    return native_io_coroutine_retire(coroutine, false);
  if (coroutine->waiting) return SALTS_OK;
  return native_io_coroutine_retire(coroutine, true) == SALTS_OK ? SALTS_EPROTO : SALTS_EIO;
}

static native_io_coroutine_owner *
native_io_coroutine_owner_create(cmeta_io_impl *impl, size_t task_capacity,
                                 size_t completion_capacity) {
  native_io_coroutine_owner *owner;
  coro_pool_config_t pool_config = {
      .initial_capacity = 0u,
      .max_capacity = task_capacity,
      .stack_size = 0u,
      .storage_size = 0u,
  };

  owner = (native_io_coroutine_owner *)calloc(1u, sizeof(*owner));
  if (owner == NULL) return NULL;
  owner->tasks = (native_io_coroutine *)calloc(task_capacity, sizeof(*owner->tasks));
  owner->request_owners = (native_io_coroutine_request_owner *)calloc(
      task_capacity, sizeof(*owner->request_owners));
  owner->raw_completions = (native_io_completion *)calloc(completion_capacity,
                                                          sizeof(*owner->raw_completions));
  owner->ready_coroutines =
      (native_io_coroutine **)calloc(completion_capacity, sizeof(*owner->ready_coroutines));
  owner->free_tasks = (uint32_t *)calloc(task_capacity, sizeof(*owner->free_tasks));
  owner->pool = coro_pool_create(&pool_config);
  if (owner->tasks == NULL || owner->request_owners == NULL || owner->raw_completions == NULL ||
      owner->ready_coroutines == NULL || owner->free_tasks == NULL || owner->pool == NULL) {
    coro_pool_destroy(owner->pool);
    free(owner->free_tasks);
    free(owner->ready_coroutines);
    free(owner->raw_completions);
    free(owner->request_owners);
    free(owner->tasks);
    free(owner);
    return NULL;
  }
  owner->impl = impl;
  owner->task_capacity = task_capacity;
  owner->raw_completion_capacity = completion_capacity;
  owner->free_task_count = task_capacity;
  for (size_t index = 0u; index < task_capacity; ++index)
    owner->free_tasks[index] = (uint32_t)(task_capacity - index - 1u);
  return owner;
}

static void native_io_coroutine_owner_destroy(native_io_coroutine_owner *owner) {
  if (owner == NULL) return;
  coro_pool_destroy(owner->pool);
  free(owner->free_tasks);
  free(owner->ready_coroutines);
  free(owner->raw_completions);
  free(owner->request_owners);
  free(owner->tasks);
  free(owner);
}

static native_io_coroutine *native_io_coroutine_find(native_io_coroutine_owner *owner,
                                                     native_io_coroutine_task task) {
  native_io_coroutine *coroutine;
  if (owner == NULL || !native_io_coroutine_task_valid(task) || task.slot > owner->task_capacity)
    return NULL;
  coroutine = &owner->tasks[task.slot - 1u];
  return coroutine->active && coroutine->task.generation == task.generation ? coroutine : NULL;
}

static int native_io_coroutine_route_completion(native_io_coroutine_owner *owner,
                                                const native_io_completion *completion,
                                                native_io_coroutine **out_ready,
                                                bool *out_consumed) {
  native_io_coroutine_request_owner *request_owner;
  native_io_coroutine *coroutine;
  *out_ready = NULL;
  *out_consumed = false;
  if (!native_io_request_valid(completion->request) ||
      completion->request.slot > owner->task_capacity)
    return SALTS_OK;
  request_owner = &owner->request_owners[completion->request.slot - 1u];
  if (request_owner->coroutine == NULL ||
      request_owner->generation != completion->request.generation)
    return SALTS_OK;
  coroutine = request_owner->coroutine;
  *request_owner = (native_io_coroutine_request_owner){0};
  coroutine->request = (native_io_request){0};
  coroutine->completion = *completion;
  coroutine->completion_ready = true;
  coroutine->waiting = false;
  *out_consumed = true;
  *out_ready = coroutine;
  return SALTS_OK;
}

static cmeta_io_impl *native_io_impl(native_io_backend *backend) {
  return backend != NULL ? (cmeta_io_impl *)backend->impl : NULL;
}

static const cmeta_io_impl *native_io_const_impl(const native_io_backend *backend) {
  return backend != NULL ? (const cmeta_io_impl *)backend->impl : NULL;
}

native_io_model native_io_backend_kind_model(native_io_backend_kind kind) {
  if (kind == NATIVE_IO_BACKEND_IOCP || kind == NATIVE_IO_BACKEND_IO_URING ||
      kind == NATIVE_IO_BACKEND_DARWIN_AIO)
    return NATIVE_IO_MODEL_COMPLETION;
  if (kind == NATIVE_IO_BACKEND_EPOLL || kind == NATIVE_IO_BACKEND_KQUEUE)
    return NATIVE_IO_MODEL_READINESS;
  return NATIVE_IO_MODEL_NONE;
}

bool native_io_backend_kind_supported(native_io_backend_kind kind) {
  return native_io_backend_kind_model(kind) != NATIVE_IO_MODEL_NONE &&
         native_io_platform_backend_supported(kind);
}

bool native_io_backend_kind_supports_pipe(native_io_backend_kind kind) {
  return native_io_backend_kind_supported(kind) && native_io_platform_pipe_supported(kind);
}

bool native_io_endpoint_valid(native_io_endpoint endpoint) {
  return endpoint.slot != 0u && endpoint.generation != 0u;
}

bool native_io_request_valid(native_io_request request) {
  return request.slot != 0u && request.generation != 0u;
}

bool native_io_coroutine_task_valid(native_io_coroutine_task task) {
  return task.slot != 0u && task.generation != 0u;
}

bool native_io_vector_operation_valid(const native_io_vector_operation *operation) {
  size_t total = 0u;
  if (operation == NULL || !native_io_endpoint_valid(operation->endpoint) ||
      operation->spans == NULL || operation->span_count == 0u ||
      operation->span_count > NATIVE_IO_VECTOR_MAX)
    return false;
  if (operation->kind != NATIVE_IO_OPERATION_STREAM_SEND &&
      operation->kind != NATIVE_IO_OPERATION_PIPE_WRITE)
    return false;
  for (size_t index = 0u; index < operation->span_count; ++index) {
    const native_io_buffer_span *span = &operation->spans[index];
    if (span->data == NULL || span->length == 0u || span->length > (size_t)UINT32_MAX ||
        span->length > (size_t)UINT32_MAX - total)
      return false;
    total += span->length;
  }
  return total != 0u;
}

bool native_io_operation_valid(const native_io_operation *operation) {
  if (operation == NULL || !native_io_endpoint_valid(operation->endpoint)) return false;
  if (native_io_operation_resource_kind(operation->kind) == SALTS_IO_RESOURCE_FILE) {
    if (operation->address != NULL || operation->address_capacity != 0u ||
        operation->address_length != 0u) return false;
    if (operation->kind == NATIVE_IO_OPERATION_FILE_FLUSH)
      return operation->buffer == NULL && operation->length == 0u && operation->offset == 0u;
    return operation->buffer != NULL && operation->length != 0u &&
           operation->length <= UINT32_MAX && operation->offset <= INT64_MAX &&
           operation->length <= (uint64_t)INT64_MAX - operation->offset;
  }
  if (operation->offset != 0u) return false;
  if (operation->kind == NATIVE_IO_OPERATION_STREAM_CONNECT)
    return operation->buffer == NULL && operation->length == 0u && operation->address != NULL &&
           operation->address_length != 0u &&
           operation->address_length <= operation->address_capacity &&
           operation->address_length <= (size_t)INT_MAX;
  if (operation->buffer == NULL || (operation->length == 0u && operation->kind != NATIVE_IO_OPERATION_UDP_SEND_TO) ||
      operation->length > (size_t)UINT32_MAX)
    return false;
  if (operation->kind == NATIVE_IO_OPERATION_STREAM_RECV ||
      operation->kind == NATIVE_IO_OPERATION_STREAM_SEND ||
      operation->kind == NATIVE_IO_OPERATION_PIPE_READ ||
      operation->kind == NATIVE_IO_OPERATION_PIPE_WRITE)
    return operation->address == NULL && operation->address_capacity == 0u &&
           operation->address_length == 0u;
  if (operation->kind == NATIVE_IO_OPERATION_UDP_RECV_FROM)
    return (operation->address == NULL && operation->address_capacity == 0u &&
            operation->address_length == 0u) ||
           (operation->address != NULL && operation->address_capacity != 0u &&
            operation->address_capacity <= (size_t)INT_MAX && operation->address_length == 0u);
  if (operation->kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
    return (operation->address == NULL && operation->address_capacity == 0u &&
            operation->address_length == 0u) ||
           (operation->address != NULL && operation->address_length != 0u &&
            operation->address_length <= operation->address_capacity &&
            operation->address_length <= (size_t)INT_MAX);
  return false;
}

int native_io_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  int status;
  cmeta_io_impl *impl;
  if (backend == NULL) return SALTS_EINVAL;
  backend->impl = NULL;
  if (config == NULL || native_io_backend_kind_model(config->kind) == NATIVE_IO_MODEL_NONE ||
      config->endpoint_capacity == 0u || config->request_capacity == 0u ||
      config->completion_batch_capacity == 0u ||
      config->completion_batch_capacity > config->request_capacity)
    return SALTS_EINVAL;
  if (config->endpoint_capacity > UINT32_MAX || config->request_capacity > UINT32_MAX)
    return SALTS_ERANGE;
  if (!native_io_platform_backend_supported(config->kind)) return SALTS_ENOTSUP;
  status = native_io_platform_backend_init(backend, config);
  if (status != SALTS_OK) return status;
  impl = native_io_impl(backend);
  impl->coroutine_capacity = config->request_capacity;
  impl->coroutine_completion_capacity = config->completion_batch_capacity;
  return SALTS_OK;
}

int native_io_backend_attach_socket(native_io_backend *backend, uintptr_t native_socket,
                                    native_io_endpoint *out_endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_endpoint != NULL) *out_endpoint = (native_io_endpoint){0};
  if (impl == NULL || impl->ops == NULL ||
      out_endpoint == NULL || native_socket == UINTPTR_MAX)
    return SALTS_EINVAL;
  if (impl->ops->attach_socket == NULL) return SALTS_ENOTSUP;
  return impl->ops->attach_socket(impl, native_socket, out_endpoint);
}

int native_io_backend_release_socket(native_io_backend *backend, native_io_endpoint endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL ||
      !native_io_endpoint_valid(endpoint))
    return SALTS_EINVAL;
  if (impl->ops->release_socket == NULL) return SALTS_ENOTSUP;
  return impl->ops->release_socket(impl, endpoint);
}

int native_io_backend_attach_pipe(native_io_backend *backend, uintptr_t native_handle,
                                  uint32_t flags, native_io_endpoint *out_endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_endpoint != NULL) *out_endpoint = (native_io_endpoint){0};
  if (out_endpoint == NULL || native_handle == UINTPTR_MAX ||
      flags != NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE)
    return SALTS_EINVAL;
  if (impl == NULL || impl->ops == NULL) return SALTS_EINVAL;
  if (impl->ops->attach_pipe == NULL) return SALTS_ENOTSUP;
  return impl->ops->attach_pipe(impl, native_handle, flags, out_endpoint);
}

int native_io_backend_release_pipe(native_io_backend *backend, native_io_endpoint endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || !native_io_endpoint_valid(endpoint)) return SALTS_EINVAL;
  if (impl->ops->release_pipe == NULL) return SALTS_ENOTSUP;
  return impl->ops->release_pipe(impl, endpoint);
}

bool native_io_backend_kind_supports_file(native_io_backend_kind kind,
                                          native_io_operation_kind operation) {
  if (!native_io_backend_kind_supported(kind) ||
      native_io_operation_resource_kind(operation) != SALTS_IO_RESOURCE_FILE) return false;
  if (kind == NATIVE_IO_BACKEND_IOCP)
    return operation != NATIVE_IO_OPERATION_FILE_FLUSH;
  return kind == NATIVE_IO_BACKEND_IO_URING || kind == NATIVE_IO_BACKEND_DARWIN_AIO;
}

int native_io_backend_attach_file(native_io_backend *backend, uintptr_t native_handle,
                                  native_io_endpoint *out_endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_endpoint) *out_endpoint = (native_io_endpoint){0};
  if (!impl || !impl->ops || !out_endpoint || native_handle == UINTPTR_MAX) return SALTS_EINVAL;
  if (!impl->ops->attach_file) return SALTS_ENOTSUP;
  return impl->ops->attach_file(impl, native_handle, out_endpoint);
}

int native_io_backend_release_file(native_io_backend *backend, native_io_endpoint endpoint) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (!impl || !impl->ops || !native_io_endpoint_valid(endpoint)) return SALTS_EINVAL;
  if (!impl->ops->release_file) return SALTS_ENOTSUP;
  return impl->ops->release_file(impl, endpoint);
}

bool native_io_backend_endpoint_supports_vector_write(const native_io_backend *backend,
                                                      native_io_endpoint endpoint) {
  const cmeta_io_impl *impl = native_io_const_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->supports_vector_write == NULL ||
      !native_io_endpoint_valid(endpoint))
    return false;
  return impl->ops->supports_vector_write(impl, endpoint);
}

int native_io_internal_submit_stream_accept(
    native_io_backend *backend,
    native_io_endpoint listener,
    native_io_request *out_request) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (native_io_request){0};
  if (impl == NULL || impl->ops == NULL ||
      !native_io_endpoint_valid(listener) || out_request == NULL)
    return SALTS_EINVAL;
  if (impl->ops->submit_stream_accept == NULL)
    return SALTS_ENOTSUP;
  return impl->ops->submit_stream_accept(
      impl, listener, out_request);
}

int native_io_internal_take_stream_accept(
    native_io_backend *backend,
    native_io_request request,
    uintptr_t *out_transport) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_transport != NULL) *out_transport = UINTPTR_MAX;
  if (impl == NULL || impl->ops == NULL ||
      !native_io_request_valid(request) || out_transport == NULL)
    return SALTS_EINVAL;
  if (impl->ops->take_stream_accept == NULL)
    return SALTS_ENOTSUP;
  return impl->ops->take_stream_accept(
      impl, request, out_transport);
}

int native_io_backend_submit(native_io_backend *backend, const native_io_operation *operation,
                             native_io_request *out_request) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (native_io_request){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->submit == NULL ||
      !native_io_operation_valid(operation) || out_request == NULL)
    return SALTS_EINVAL;
  return impl->ops->submit(impl, operation, out_request);
}

int native_io_backend_submit_vector(native_io_backend *backend,
                                    const native_io_vector_operation *operation,
                                    native_io_request *out_request) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (native_io_request){0};
  if (impl == NULL || impl->ops == NULL || !native_io_vector_operation_valid(operation) ||
      out_request == NULL)
    return SALTS_EINVAL;
  if (impl->ops->submit_vector == NULL) return SALTS_ENOTSUP;
  return impl->ops->submit_vector(impl, operation, out_request);
}

int native_io_backend_prepare(native_io_backend *backend, const native_io_operation *operation,
                              native_io_request *out_request) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (native_io_request){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->prepare == NULL ||
      !native_io_operation_valid(operation) || out_request == NULL)
    return SALTS_EINVAL;
  return impl->ops->prepare(impl, operation, out_request);
}

int native_io_backend_flush(native_io_backend *backend) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL) return SALTS_EINVAL;
  return impl->ops->flush != NULL ? impl->ops->flush(impl) : SALTS_OK;
}

int native_io_backend_spawn_coroutine(native_io_backend *backend,
                                      native_io_coroutine_entry_fn entry, void *user_data,
                                      native_io_coroutine_task *out_task) {
  cmeta_io_impl *impl = native_io_impl(backend);
  native_io_coroutine_owner *owner;
  native_io_coroutine *coroutine;
  native_io_backend_stats stats;
  uint32_t index;
  uint32_t generation;
  int status;

  if (out_task != NULL) *out_task = (native_io_coroutine_task){0};
  if (impl == NULL || impl->ops == NULL || entry == NULL || out_task == NULL)
    return SALTS_EINVAL;
  if (!impl->ops->get_stats(impl, &stats)) return SALTS_EIO;
  if (!stats.admission_open) return SALTS_ESHUTDOWN;
  if (impl->coroutine_owner == NULL) {
    impl->coroutine_owner = native_io_coroutine_owner_create(
        impl, impl->coroutine_capacity, impl->coroutine_completion_capacity);
    if (impl->coroutine_owner == NULL) return SALTS_ENOMEM;
  }
  owner = impl->coroutine_owner;
  if (owner->free_task_count == 0u) return SALTS_ENOBUFS;

  index = owner->free_tasks[--owner->free_task_count];
  coroutine = &owner->tasks[index];
  generation = native_io_next_generation(coroutine->task.generation);
  memset(coroutine, 0, sizeof(*coroutine));
  coroutine->owner = owner;
  coroutine->entry = entry;
  coroutine->user_data = user_data;
  coroutine->task = (native_io_coroutine_task){index + 1u, generation};
  coroutine->active = true;
  coroutine->frame = coro_pool_acquire(owner->pool, native_io_coroutine_entry, coroutine);
  if (coroutine->frame == NULL) {
    coroutine->active = false;
    owner->free_tasks[owner->free_task_count++] = index;
    return SALTS_ENOMEM;
  }
  ++owner->active_task_count;
  *out_task = coroutine->task;
  status = native_io_coroutine_resume(coroutine);
  if (status != SALTS_OK && native_io_coroutine_find(owner, *out_task) == NULL)
    *out_task = (native_io_coroutine_task){0};
  return status;
}

static int native_io_coroutine_await_impl(native_io_coroutine *coroutine,
                                         const native_io_operation *operation,
                                         native_io_completion *out_completion, bool prepared) {
  native_io_coroutine_owner *owner;
  native_io_coroutine_request_owner *request_owner;
  native_io_request request = {0};
  int status;

  if (out_completion != NULL) *out_completion = (native_io_completion){0};
  if (coroutine == NULL || !coroutine->active || coroutine->owner == NULL ||
      coroutine->frame == NULL || coro_running() != coroutine->frame ||
      !native_io_operation_valid(operation) || out_completion == NULL || coroutine->waiting)
    return SALTS_EINVAL;
  owner = coroutine->owner;
  status = prepared ? owner->impl->ops->prepare(owner->impl, operation, &request)
                    : owner->impl->ops->submit(owner->impl, operation, &request);
  if (status != SALTS_OK) return status;
  if (request.slot > owner->task_capacity) {
    (void)owner->impl->ops->cancel(owner->impl, request);
    return SALTS_EPROTO;
  }
  request_owner = &owner->request_owners[request.slot - 1u];
  if (request_owner->coroutine != NULL) {
    (void)owner->impl->ops->cancel(owner->impl, request);
    return SALTS_EPROTO;
  }
  request_owner->coroutine = coroutine;
  request_owner->generation = request.generation;
  coroutine->request = request;
  coroutine->waiting = true;
  coroutine->completion_ready = false;
  if (coro_yield() != 0 || !coroutine->completion_ready) return SALTS_EPROTO;
  *out_completion = coroutine->completion;
  coroutine->completion = (native_io_completion){0};
  coroutine->completion_ready = false;
  return SALTS_OK;
}

int native_io_coroutine_await(native_io_coroutine *coroutine, const native_io_operation *operation,
                              native_io_completion *out_completion) {
  return native_io_coroutine_await_impl(coroutine, operation, out_completion, false);
}

int native_io_coroutine_await_prepared(native_io_coroutine *coroutine,
                                       const native_io_operation *operation,
                                       native_io_completion *out_completion) {
  return native_io_coroutine_await_impl(coroutine, operation, out_completion, true);
}

int native_io_coroutine_await_vector(native_io_coroutine *coroutine,
                                     const native_io_vector_operation *operation,
                                     native_io_completion *out_completion) {
  native_io_coroutine_owner *owner;
  native_io_coroutine_request_owner *request_owner;
  native_io_request request = {0};
  int status;

  if (out_completion != NULL) *out_completion = (native_io_completion){0};
  if (coroutine == NULL || !coroutine->active || coroutine->owner == NULL ||
      coroutine->frame == NULL || coro_running() != coroutine->frame ||
      !native_io_vector_operation_valid(operation) || out_completion == NULL ||
      coroutine->waiting)
    return SALTS_EINVAL;
  owner = coroutine->owner;
  if (owner->impl->ops->submit_vector == NULL) return SALTS_ENOTSUP;
  status = owner->impl->ops->submit_vector(owner->impl, operation, &request);
  if (status != SALTS_OK) return status;
  if (request.slot > owner->task_capacity) {
    (void)owner->impl->ops->cancel(owner->impl, request);
    return SALTS_EPROTO;
  }
  request_owner = &owner->request_owners[request.slot - 1u];
  if (request_owner->coroutine != NULL) {
    (void)owner->impl->ops->cancel(owner->impl, request);
    return SALTS_EPROTO;
  }
  request_owner->coroutine = coroutine;
  request_owner->generation = request.generation;
  coroutine->request = request;
  coroutine->waiting = true;
  coroutine->completion_ready = false;
  if (coro_yield() != 0 || !coroutine->completion_ready) return SALTS_EPROTO;
  *out_completion = coroutine->completion;
  coroutine->completion = (native_io_completion){0};
  coroutine->completion_ready = false;
  return SALTS_OK;
}

int native_io_backend_cancel_coroutine(native_io_backend *backend, native_io_coroutine_task task) {
  cmeta_io_impl *impl = native_io_impl(backend);
  native_io_coroutine *coroutine;
  if (impl == NULL || impl->ops == NULL || impl->coroutine_owner == NULL ||
      !native_io_coroutine_task_valid(task))
    return SALTS_EINVAL;
  coroutine = native_io_coroutine_find(impl->coroutine_owner, task);
  if (coroutine == NULL) return SALTS_ENOENT;
  if (!coroutine->waiting || !native_io_request_valid(coroutine->request)) return SALTS_EALREADY;
  return impl->ops->cancel(impl, coroutine->request);
}

int native_io_backend_cancel(native_io_backend *backend, native_io_request request) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->cancel == NULL ||
      !native_io_request_valid(request))
    return SALTS_EINVAL;
  return impl->ops->cancel(impl, request);
}

int native_io_backend_observe(native_io_backend *backend, native_io_completion *events,
                              size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  cmeta_io_impl *impl = native_io_impl(backend);
  native_io_coroutine_owner *owner;
  size_t raw_count = 0u;
  size_t direct_count = 0u;
  size_t ready_count = 0u;
  size_t limit;
  int status;
  if (out_count != NULL) *out_count = 0u;
  if (impl == NULL || impl->ops == NULL || impl->ops->observe == NULL || events == NULL ||
      event_capacity == 0u || out_count == NULL)
    return SALTS_EINVAL;
  owner = impl->coroutine_owner;
  if (owner == NULL || owner->active_task_count == 0u)
    return impl->ops->observe(impl, events, event_capacity, timeout_ms, out_count);
  limit = event_capacity < owner->raw_completion_capacity ? event_capacity
                                                          : owner->raw_completion_capacity;
  status = impl->ops->observe(impl, owner->raw_completions, limit, timeout_ms, &raw_count);
  if (status != SALTS_OK) return status;
  for (size_t index = 0u; index < raw_count; ++index) {
    native_io_coroutine *ready = NULL;
    bool consumed = false;
    status = native_io_coroutine_route_completion(owner, &owner->raw_completions[index], &ready,
                                                  &consumed);
    if (status != SALTS_OK) return status;
    if (ready != NULL) owner->ready_coroutines[ready_count++] = ready;
    if (!consumed) events[direct_count++] = owner->raw_completions[index];
  }
  status = SALTS_OK;
  for (size_t index = 0u; index < ready_count; ++index) {
    const int resume_status = native_io_coroutine_resume(owner->ready_coroutines[index]);
    if (status == SALTS_OK && resume_status != SALTS_OK) status = resume_status;
  }
  *out_count = direct_count;
  return status;
}

int native_io_backend_wake(native_io_backend *backend) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->wake == NULL) return SALTS_EINVAL;
  return impl->ops->wake(impl);
}

int native_io_backend_close(native_io_backend *backend) {
  cmeta_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->close == NULL) return SALTS_EINVAL;
  return impl->ops->close(impl);
}

int native_io_backend_destroy(native_io_backend *backend) {
  cmeta_io_impl *impl = native_io_impl(backend);
  native_io_coroutine_owner *owner;
  int status;
  if (impl == NULL || impl->ops == NULL || impl->ops->destroy == NULL) return SALTS_EINVAL;
  owner = impl->coroutine_owner;
  if (owner != NULL && owner->active_task_count != 0u) return SALTS_EBUSY;
  status = impl->ops->destroy(impl);
  if (status == SALTS_OK) {
    native_io_coroutine_owner_destroy(owner);
    backend->impl = NULL;
  }
  return status;
}

bool native_io_backend_get_stats(const native_io_backend *backend,
                                 native_io_backend_stats *out_stats) {
  const cmeta_io_impl *impl = native_io_const_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->get_stats == NULL || out_stats == NULL)
    return false;
  return impl->ops->get_stats(impl, out_stats);
}

bool native_io_backend_get_config(const native_io_backend *backend,
                                  native_io_backend_config *out_config) {
  const cmeta_io_impl *impl = native_io_const_impl(backend);
  native_io_backend_stats stats = {0};
  if (impl == NULL || out_config == NULL ||
      !native_io_backend_get_stats(backend, &stats) ||
      impl->coroutine_completion_capacity == 0u)
    return false;
  *out_config = (native_io_backend_config){
      impl->kind,
      stats.endpoint_capacity,
      stats.request_capacity,
      impl->coroutine_completion_capacity};
  return true;
}

bool native_io_backend_get_coroutine_stats(const native_io_backend *backend,
                                           native_io_coroutine_stats *out_stats) {
  const cmeta_io_impl *impl = native_io_const_impl(backend);
  native_io_coroutine_stats stats = NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER;
  if (impl == NULL || out_stats == NULL ||
      out_stats->abi_version != NATIVE_IO_COROUTINE_STATS_ABI_V1 ||
      out_stats->struct_size != sizeof(*out_stats))
    return false;
  stats.capacity = impl->coroutine_capacity;
  if (impl->coroutine_owner != NULL) {
    stats.active = impl->coroutine_owner->active_task_count;
    stats.retained_frames = coro_pool_retained_count(impl->coroutine_owner->pool);
  }
  *out_stats = stats;
  return true;
}
