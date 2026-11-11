#include <native_ipc.h>

#include <limits.h>
#include <string.h>

enum { SALTS_IPC_PIPE_NAME_CAPACITY = 256, SALTS_IPC_PIPE_MAX_REQUEST_CAPACITY = 255 };

bool ipc_platform_capability_supported(ipc_pipe_capability capability);
int ipc_platform_endpoint_close(uintptr_t handle);
int ipc_platform_server_init(ipc_pipe_server *server,
                                   const ipc_pipe_server_config *config);
int ipc_platform_server_try_accept(ipc_pipe_server *server,
                                         ipc_request_id *out_request_id);
int ipc_platform_server_cancel(ipc_pipe_server *server,
                                     ipc_request_id request_id);
int ipc_platform_server_observe(ipc_pipe_server *server, size_t max_events,
                                      size_t *out_count);
int ipc_platform_server_wait_sources(const ipc_pipe_server *server,
                                           uintptr_t *out_handles, size_t capacity,
                                           size_t *out_count, bool *out_ready);
int ipc_platform_server_close(ipc_pipe_server *server);
bool ipc_platform_server_is_quiescent(const ipc_pipe_server *server);
bool ipc_platform_server_get_stats(const ipc_pipe_server *server,
                                         ipc_pipe_server_stats *out_stats);
int ipc_platform_server_destroy(ipc_pipe_server *server);
#if defined(_WIN32)
int ipc_platform_named_pipe_create_instance(const char *name, ipc_pipe_direction direction,
                                            size_t input_buffer_size, size_t output_buffer_size,
                                            ipc_pipe_endpoint *out_endpoint);
#endif
int ipc_platform_named_pipe_connect(const char *name, ipc_pipe_direction direction,
                                          ipc_pipe_endpoint *out_endpoint);
int ipc_platform_fifo_open(const char *path, ipc_pipe_direction direction,
                                 ipc_pipe_endpoint *out_endpoint);

static bool ipc_direction_valid(ipc_pipe_direction direction) {
  return direction == SALTS_IPC_PIPE_READ || direction == SALTS_IPC_PIPE_WRITE ||
         direction == SALTS_IPC_PIPE_DUPLEX;
}

static bool ipc_name_valid(const char *name) {
  return name != NULL && name[0] != '\0' &&
         memchr(name, '\0', SALTS_IPC_PIPE_NAME_CAPACITY) != NULL;
}

bool ipc_pipe_capability_supported(ipc_pipe_capability capability) {
  return ipc_platform_capability_supported(capability);
}

void ipc_pipe_endpoint_init(ipc_pipe_endpoint *endpoint) {
  if (endpoint == NULL) return;
  endpoint->handle = UINTPTR_MAX;
  endpoint->native_io_flags = 0u;
}

bool ipc_pipe_endpoint_valid(const ipc_pipe_endpoint *endpoint) {
  return endpoint != NULL && endpoint->handle != UINTPTR_MAX;
}

int ipc_pipe_endpoint_close(ipc_pipe_endpoint *endpoint) {
  uintptr_t handle;
  if (endpoint == NULL) return SALTS_EINVAL;
  if (!ipc_pipe_endpoint_valid(endpoint)) return SALTS_OK;
  handle = endpoint->handle;
  ipc_pipe_endpoint_init(endpoint);
  return ipc_platform_endpoint_close(handle);
}

int ipc_pipe_server_init(ipc_pipe_server *server,
                               const ipc_pipe_server_config *config) {
  if (server == NULL || server->impl != NULL || config == NULL ||
      !ipc_name_valid(config->name) || !ipc_direction_valid(config->direction) ||
      config->request_capacity == 0u ||
      config->request_capacity > SALTS_IPC_PIPE_MAX_REQUEST_CAPACITY ||
      config->input_buffer_size == 0u || config->input_buffer_size > UINT32_MAX ||
      config->output_buffer_size == 0u || config->output_buffer_size > UINT32_MAX ||
      config->completion == NULL)
    return SALTS_EINVAL;
  return ipc_platform_server_init(server, config);
}

int ipc_pipe_server_try_accept(ipc_pipe_server *server,
                                     ipc_request_id *out_request_id) {
  if (out_request_id != NULL) *out_request_id = 0u;
  if (server == NULL || server->impl == NULL || out_request_id == NULL) return SALTS_EINVAL;
  return ipc_platform_server_try_accept(server, out_request_id);
}

int ipc_pipe_server_cancel(ipc_pipe_server *server, ipc_request_id request_id) {
  if (server == NULL || server->impl == NULL || request_id == 0u) return SALTS_EINVAL;
  return ipc_platform_server_cancel(server, request_id);
}

int ipc_pipe_server_observe(ipc_pipe_server *server, size_t max_events,
                                  size_t *out_count) {
  if (out_count != NULL) *out_count = 0u;
  if (server == NULL || server->impl == NULL || max_events == 0u || out_count == NULL)
    return SALTS_EINVAL;
  return ipc_platform_server_observe(server, max_events, out_count);
}

int ipc_pipe_server_close(ipc_pipe_server *server) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return ipc_platform_server_close(server);
}

int ipc_pipe_server_wait_sources(const ipc_pipe_server *server,
                                       uintptr_t *out_handles, size_t capacity,
                                       size_t *out_count, bool *out_ready) {
  if (out_count != NULL) *out_count = 0u;
  if (out_ready != NULL) *out_ready = false;
  if (server == NULL || server->impl == NULL || out_count == NULL || out_ready == NULL ||
      (capacity != 0u && out_handles == NULL)) return SALTS_EINVAL;
  return ipc_platform_server_wait_sources(server, out_handles, capacity, out_count, out_ready);
}

bool ipc_pipe_server_is_quiescent(const ipc_pipe_server *server) {
  return server != NULL && server->impl != NULL && ipc_platform_server_is_quiescent(server);
}

bool ipc_pipe_server_get_stats(const ipc_pipe_server *server,
                                     ipc_pipe_server_stats *out_stats) {
  return server != NULL && server->impl != NULL && out_stats != NULL &&
         ipc_platform_server_get_stats(server, out_stats);
}

int ipc_pipe_server_destroy(ipc_pipe_server *server) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return ipc_platform_server_destroy(server);
}

int ipc_named_pipe_create_instance(const char *name, ipc_pipe_direction direction,
                                   size_t input_buffer_size, size_t output_buffer_size,
                                   ipc_pipe_endpoint *out_endpoint) {
  if (!ipc_name_valid(name) || !ipc_direction_valid(direction) ||
      input_buffer_size == 0u || input_buffer_size > UINT32_MAX ||
      output_buffer_size == 0u || output_buffer_size > UINT32_MAX ||
      out_endpoint == NULL || ipc_pipe_endpoint_valid(out_endpoint))
    return SALTS_EINVAL;
#if defined(_WIN32)
  return ipc_platform_named_pipe_create_instance(name, direction, input_buffer_size,
                                                 output_buffer_size, out_endpoint);
#else
  return SALTS_ENOTSUP;
#endif
}

int ipc_named_pipe_connect(const char *name, ipc_pipe_direction direction,
                                 ipc_pipe_endpoint *out_endpoint) {
  if (!ipc_name_valid(name) || !ipc_direction_valid(direction) ||
      out_endpoint == NULL || ipc_pipe_endpoint_valid(out_endpoint))
    return SALTS_EINVAL;
  return ipc_platform_named_pipe_connect(name, direction, out_endpoint);
}

int ipc_fifo_open(const char *path, ipc_pipe_direction direction,
                        ipc_pipe_endpoint *out_endpoint) {
  if (path == NULL || path[0] == '\0' ||
      (direction != SALTS_IPC_PIPE_READ && direction != SALTS_IPC_PIPE_WRITE) ||
      out_endpoint == NULL || ipc_pipe_endpoint_valid(out_endpoint))
    return SALTS_EINVAL;
  return ipc_platform_fifo_open(path, direction, out_endpoint);
}
