#include <native_io.h>
#include <native_ipc.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(interface)
  #undef interface
#endif
#include <windows.h>

typedef enum cmeta_ipc_slot_phase {
  SALTS_IPC_SLOT_FREE = 0,
  SALTS_IPC_SLOT_PENDING,
  SALTS_IPC_SLOT_READY_OK,
  SALTS_IPC_SLOT_READY_CANCELLED,
  SALTS_IPC_SLOT_READY_FAILED
} cmeta_ipc_slot_phase;

typedef struct cmeta_ipc_slot {
  cmeta_ipc_slot_phase phase;
  cmeta_ipc_request_id request_id;
  uintptr_t handle;
  int status;
  uint32_t native_status;
  OVERLAPPED overlapped;
  HANDLE event;
} cmeta_ipc_slot;

typedef struct cmeta_ipc_server_impl {
  cmeta_ipc_slot *slots;
  char *name;
  size_t capacity;
  size_t active;
  size_t cursor;
  size_t input_buffer_size;
  size_t output_buffer_size;
  cmeta_ipc_request_id next_request_id;
  cmeta_ipc_pipe_direction direction;
  cmeta_ipc_pipe_accept_fn completion;
  void *completion_user;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  bool close_requested;
  bool driver_active;
} cmeta_ipc_server_impl;

static int cmeta_ipc_windows_error(DWORD error) {
  if (error == ERROR_SUCCESS || error > (DWORD)INT_MAX) return SALTS_EIO;
  return -(int)error;
}

static DWORD cmeta_ipc_server_access(cmeta_ipc_pipe_direction direction) {
  if (direction == SALTS_IPC_PIPE_READ) return PIPE_ACCESS_INBOUND;
  if (direction == SALTS_IPC_PIPE_WRITE) return PIPE_ACCESS_OUTBOUND;
  return PIPE_ACCESS_DUPLEX;
}

static DWORD cmeta_ipc_client_access(cmeta_ipc_pipe_direction direction) {
  DWORD access = 0u;
  if ((direction & SALTS_IPC_PIPE_READ) != 0u) access |= GENERIC_READ;
  if ((direction & SALTS_IPC_PIPE_WRITE) != 0u) access |= GENERIC_WRITE | FILE_READ_ATTRIBUTES;
  return access;
}

static void cmeta_ipc_slot_release(cmeta_ipc_slot *slot, bool close_handle) {
  if (close_handle && slot->handle != UINTPTR_MAX) (void)CloseHandle((HANDLE)slot->handle);
  if (slot->event != NULL) (void)CloseHandle(slot->event);
  memset(slot, 0, sizeof(*slot));
  slot->handle = UINTPTR_MAX;
}

static void cmeta_ipc_slot_poll(cmeta_ipc_slot *slot) {
  DWORD bytes = 0u;
  DWORD error;
  if (slot->phase != SALTS_IPC_SLOT_PENDING ||
      WaitForSingleObject(slot->event, 0u) != WAIT_OBJECT_0)
    return;
  if (GetOverlappedResult((HANDLE)slot->handle, &slot->overlapped, &bytes, FALSE)) {
    slot->phase = SALTS_IPC_SLOT_READY_OK;
    return;
  }
  error = GetLastError();
  if (error == ERROR_IO_INCOMPLETE) return;
  slot->native_status = error;
  if (error == ERROR_OPERATION_ABORTED) {
    slot->phase = SALTS_IPC_SLOT_READY_CANCELLED;
    slot->status = SALTS_ECANCELED;
  } else {
    slot->phase = SALTS_IPC_SLOT_READY_FAILED;
    slot->status = cmeta_ipc_windows_error(error);
  }
}

bool cmeta_ipc_platform_capability_supported(cmeta_ipc_pipe_capability capability) {
  return capability == SALTS_IPC_WINDOWS_SERVER_ACCEPT ||
         capability == SALTS_IPC_WINDOWS_CLIENT_CONNECT;
}

int cmeta_ipc_platform_endpoint_close(uintptr_t handle) {
  return CloseHandle((HANDLE)handle) ? SALTS_OK : cmeta_ipc_windows_error(GetLastError());
}

int cmeta_ipc_platform_server_init(cmeta_ipc_pipe_server *server,
                                   const cmeta_ipc_pipe_server_config *config) {
  cmeta_ipc_server_impl *impl;
  const size_t name_length = strlen(config->name);
  size_t index;
  if (config->request_capacity > SIZE_MAX / sizeof(cmeta_ipc_slot)) return SALTS_ERANGE;
  impl = (cmeta_ipc_server_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->slots = (cmeta_ipc_slot *)calloc(config->request_capacity, sizeof(*impl->slots));
  impl->name = (char *)malloc(name_length + 1u);
  if (impl->slots == NULL || impl->name == NULL) {
    free(impl->name);
    free(impl->slots);
    free(impl);
    return SALTS_ENOMEM;
  }
  memcpy(impl->name, config->name, name_length + 1u);
  impl->capacity = config->request_capacity;
  impl->input_buffer_size = config->input_buffer_size;
  impl->output_buffer_size = config->output_buffer_size;
  impl->direction = config->direction;
  impl->completion = config->completion;
  impl->completion_user = config->completion_user;
  impl->next_request_id = 1u;
  for (index = 0u; index < impl->capacity; ++index)
    impl->slots[index].handle = UINTPTR_MAX;
  server->impl = impl;
  return SALTS_OK;
}

int cmeta_ipc_platform_server_try_accept(cmeta_ipc_pipe_server *server,
                                         cmeta_ipc_request_id *out_request_id) {
  cmeta_ipc_server_impl *impl = (cmeta_ipc_server_impl *)server->impl;
  cmeta_ipc_slot *slot = NULL;
  HANDLE handle;
  DWORD error;
  size_t index;
  if (impl->close_requested) return SALTS_ESHUTDOWN;
  if (impl->active == impl->capacity) {
    ++impl->rejected_full;
    return SALTS_ENOBUFS;
  }
  if (impl->next_request_id == 0u) return SALTS_ERANGE;
  for (index = 0u; index < impl->capacity; ++index) {
    if (impl->slots[index].phase == SALTS_IPC_SLOT_FREE) {
      slot = &impl->slots[index];
      break;
    }
  }
  if (slot == NULL) {
    ++impl->rejected_full;
    return SALTS_ENOBUFS;
  }
  slot->event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (slot->event == NULL) return cmeta_ipc_windows_error(GetLastError());
  memset(&slot->overlapped, 0, sizeof(slot->overlapped));
  slot->overlapped.hEvent = slot->event;
  handle =
      CreateNamedPipeA(impl->name, cmeta_ipc_server_access(impl->direction) | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                       PIPE_UNLIMITED_INSTANCES, (DWORD)impl->output_buffer_size,
                       (DWORD)impl->input_buffer_size, 0u, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    cmeta_ipc_slot_release(slot, false);
    return cmeta_ipc_windows_error(error);
  }
  slot->handle = (uintptr_t)handle;
  slot->request_id = impl->next_request_id++;
  if (ConnectNamedPipe(handle, &slot->overlapped)) {
    slot->phase = SALTS_IPC_SLOT_READY_OK;
  } else {
    error = GetLastError();
    if (error == ERROR_IO_PENDING) slot->phase = SALTS_IPC_SLOT_PENDING;
    else if (error == ERROR_PIPE_CONNECTED) slot->phase = SALTS_IPC_SLOT_READY_OK;
    else {
      cmeta_ipc_slot_release(slot, true);
      return cmeta_ipc_windows_error(error);
    }
  }
  ++impl->active;
  ++impl->submitted;
  *out_request_id = slot->request_id;
  return SALTS_OK;
}

int cmeta_ipc_platform_server_cancel(cmeta_ipc_pipe_server *server,
                                     cmeta_ipc_request_id request_id) {
  cmeta_ipc_server_impl *impl = (cmeta_ipc_server_impl *)server->impl;
  size_t index;
  for (index = 0u; index < impl->capacity; ++index) {
    cmeta_ipc_slot *slot = &impl->slots[index];
    DWORD error;
    if (slot->request_id != request_id || slot->phase == SALTS_IPC_SLOT_FREE) continue;
    cmeta_ipc_slot_poll(slot);
    if (slot->phase != SALTS_IPC_SLOT_PENDING) return SALTS_EALREADY;
    if (CancelIoEx((HANDLE)slot->handle, &slot->overlapped)) return SALTS_OK;
    error = GetLastError();
    return error == ERROR_NOT_FOUND ? SALTS_OK : cmeta_ipc_windows_error(error);
  }
  return SALTS_ENOENT;
}

int cmeta_ipc_platform_server_observe(cmeta_ipc_pipe_server *server, size_t max_events,
                                      size_t *out_count) {
  cmeta_ipc_server_impl *impl = (cmeta_ipc_server_impl *)server->impl;
  size_t visited = 0u;
  if (impl->driver_active) return SALTS_EBUSY;
  impl->driver_active = true;
  while (*out_count < max_events && visited < impl->capacity) {
    cmeta_ipc_slot *slot = &impl->slots[impl->cursor];
    impl->cursor = (impl->cursor + 1u) % impl->capacity;
    ++visited;
    cmeta_ipc_slot_poll(slot);
    if (slot->phase == SALTS_IPC_SLOT_READY_OK || slot->phase == SALTS_IPC_SLOT_READY_CANCELLED ||
        slot->phase == SALTS_IPC_SLOT_READY_FAILED) {
      cmeta_ipc_completion completion = {slot->request_id, SALTS_IPC_COMPLETION_OK, SALTS_OK, 0u};
      cmeta_ipc_pipe_endpoint endpoint;
      const cmeta_ipc_slot_phase phase = slot->phase;
      cmeta_ipc_pipe_endpoint_init(&endpoint);
      if (phase == SALTS_IPC_SLOT_READY_OK) {
        endpoint.handle = slot->handle;
        endpoint.native_io_flags = NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
        slot->handle = UINTPTR_MAX;
      } else if (phase == SALTS_IPC_SLOT_READY_CANCELLED) {
        completion.kind = SALTS_IPC_COMPLETION_CANCELLED;
        completion.status = slot->status;
        completion.native_status = slot->native_status;
        ++impl->cancelled;
      } else {
        completion.kind = SALTS_IPC_COMPLETION_FAILED;
        completion.status = slot->status;
        completion.native_status = slot->native_status;
        ++impl->failed;
      }
      ++impl->completed;
      cmeta_ipc_slot_release(slot, true);
      --impl->active;
      ++*out_count;
      impl->completion(impl->completion_user, &completion, endpoint);
    }
  }
  impl->driver_active = false;
  return SALTS_OK;
}

int cmeta_ipc_platform_server_close(cmeta_ipc_pipe_server *server) {
  cmeta_ipc_server_impl *impl = (cmeta_ipc_server_impl *)server->impl;
  size_t index;
  int first_status = SALTS_OK;
  if (impl->driver_active) return SALTS_EBUSY;
  impl->close_requested = true;
  for (index = 0u; index < impl->capacity; ++index) {
    cmeta_ipc_slot *slot = &impl->slots[index];
    cmeta_ipc_slot_poll(slot);
    if (slot->phase == SALTS_IPC_SLOT_PENDING) {
      const int status = cmeta_ipc_platform_server_cancel(server, slot->request_id);
      if (status != SALTS_OK && status != SALTS_EALREADY && first_status == SALTS_OK)
        first_status = status;
    }
  }
  return first_status;
}

int cmeta_ipc_platform_server_wait_sources(const cmeta_ipc_pipe_server *server,
                                           uintptr_t *out_handles, size_t capacity,
                                           size_t *out_count, bool *out_ready) {
  const cmeta_ipc_server_impl *impl = (const cmeta_ipc_server_impl *)server->impl;
  size_t count = 0u;
  if (impl->driver_active) return SALTS_EBUSY;
  for (size_t index = 0u; index < impl->capacity; ++index) {
    const cmeta_ipc_slot *slot = &impl->slots[index];
    if (slot->phase == SALTS_IPC_SLOT_PENDING) ++count;
    else if (slot->phase != SALTS_IPC_SLOT_FREE) *out_ready = true;
  }
  *out_count = count;
  if (count > capacity) return SALTS_ENOBUFS;
  count = 0u;
  for (size_t index = 0u; index < impl->capacity; ++index) {
    const cmeta_ipc_slot *slot = &impl->slots[index];
    if (slot->phase == SALTS_IPC_SLOT_PENDING) out_handles[count++] = (uintptr_t)slot->event;
  }
  return SALTS_OK;
}

bool cmeta_ipc_platform_server_is_quiescent(const cmeta_ipc_pipe_server *server) {
  const cmeta_ipc_server_impl *impl = (const cmeta_ipc_server_impl *)server->impl;
  return impl->close_requested && impl->active == 0u;
}

bool cmeta_ipc_platform_server_get_stats(const cmeta_ipc_pipe_server *server,
                                         cmeta_ipc_pipe_server_stats *out_stats) {
  const cmeta_ipc_server_impl *impl = (const cmeta_ipc_server_impl *)server->impl;
  *out_stats = (cmeta_ipc_pipe_server_stats){
      impl->capacity,  impl->active, impl->submitted,     impl->completed,
      impl->cancelled, impl->failed, impl->rejected_full, !impl->close_requested};
  return true;
}

int cmeta_ipc_platform_server_destroy(cmeta_ipc_pipe_server *server) {
  cmeta_ipc_server_impl *impl = (cmeta_ipc_server_impl *)server->impl;
  if (!impl->close_requested || impl->active != 0u || impl->driver_active) return SALTS_EBUSY;
  free(impl->name);
  free(impl->slots);
  free(impl);
  server->impl = NULL;
  return SALTS_OK;
}

int cmeta_ipc_platform_named_pipe_connect(const char *name, cmeta_ipc_pipe_direction direction,
                                          cmeta_ipc_pipe_endpoint *out_endpoint) {
  HANDLE handle = CreateFileA(name, cmeta_ipc_client_access(direction), 0u, NULL, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (error == ERROR_PIPE_BUSY) return SALTS_EBUSY;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return SALTS_ENOENT;
    return cmeta_ipc_windows_error(error);
  }
  /* CreateFile also opens regular files; only a pipe may become an IPC endpoint. */
  if (GetFileType(handle) != FILE_TYPE_PIPE) {
    (void)CloseHandle(handle);
    return SALTS_ENOTSUP;
  }
  out_endpoint->handle = (uintptr_t)handle;
  out_endpoint->native_io_flags = NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
  return SALTS_OK;
}

int cmeta_ipc_platform_fifo_open(const char *path, cmeta_ipc_pipe_direction direction,
                                 cmeta_ipc_pipe_endpoint *out_endpoint) {
  (void)path;
  (void)direction;
  (void)out_endpoint;
  return SALTS_ENOTSUP;
}
