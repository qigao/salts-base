#include <native_io_driver.h>
#include "native_io_internal.h"
#include <threads/threads.h>
#include <stdlib.h>
#include <string.h>

typedef enum { DRIVER_FREE, DRIVER_QUEUED, DRIVER_SUBMITTING,
               DRIVER_PENDING, DRIVER_DELIVERING } driver_phase;
typedef struct {
  driver_phase phase;
  bool cancel;
  bool cancel_sent;
  uint64_t id;
  native_io_operation operation;
  native_io_request request;
  native_io_driver_completion_fn completion;
  void *user;
} driver_slot;
typedef struct {
  native_io_backend backend;
  native_io_backend_config config;
  sync_mutex_t gate;
  driver_slot *slots;
  native_io_completion *events;
  size_t event_count, event_index, pending;
  uint64_t next_id;
  bool closing, driving;
} driver_impl;

int native_io_driver_init(native_io_driver *driver, const native_io_backend_config *config) {
  if (!driver || driver->impl || !config || !config->endpoint_capacity || !config->request_capacity ||
      !config->completion_batch_capacity || config->completion_batch_capacity > config->request_capacity)
    return SALTS_EINVAL;
  if (config->endpoint_capacity > UINT32_MAX || config->request_capacity > UINT32_MAX ||
      config->request_capacity > SIZE_MAX / sizeof(driver_slot) ||
      config->completion_batch_capacity > SIZE_MAX / sizeof(native_io_completion)) return SALTS_ERANGE;
  driver_impl *impl = calloc(1u, sizeof(*impl));
  if (!impl) return SALTS_ENOMEM;
  impl->slots = calloc(config->request_capacity, sizeof(*impl->slots));
  impl->events = calloc(config->completion_batch_capacity, sizeof(*impl->events));
  sync_mutex_init(&impl->gate);
  int status = SALTS_ENOMEM;
  if (!impl->slots || !impl->events || !impl->gate) goto error;
  status = native_io_backend_init(&impl->backend, config);
  if (status != SALTS_OK) goto error;
  impl->config = *config;
  driver->impl = impl;
  return SALTS_OK;
error:
  sync_mutex_destroy(&impl->gate);
  free(impl->events); free(impl->slots); free(impl);
  return status;
}

native_io_backend *native_io_driver_backend(native_io_driver *driver) {
  driver_impl *impl = driver ? driver->impl : NULL;
  return impl ? &impl->backend : NULL;
}

int native_io_driver_submit(native_io_driver *driver, const native_io_operation *operation,
    native_io_driver_completion_fn completion, void *user, uint64_t *id) {
  driver_impl *impl = driver ? driver->impl : NULL;
  if (id) *id = 0u;
  if (!impl || !completion || !id || !native_io_operation_valid(operation)) return SALTS_EINVAL;
  sync_mutex_lock(&impl->gate);
  int status = impl->closing ? SALTS_ESHUTDOWN :
               impl->next_id == UINT64_MAX ? SALTS_ERANGE : SALTS_ENOBUFS;
  if (!impl->closing && impl->next_id != UINT64_MAX) {
    for (size_t i = 0u; i < impl->config.request_capacity; ++i) {
      driver_slot *slot = &impl->slots[i];
      if (slot->phase != DRIVER_FREE) continue;
      *slot = (driver_slot){.phase = DRIVER_QUEUED, .id = ++impl->next_id,
          .operation = *operation, .completion = completion, .user = user};
      ++impl->pending;
      *id = slot->id;
      status = SALTS_OK;
      break;
    }
  }
  sync_mutex_unlock(&impl->gate);
  return status;
}

int native_io_driver_cancel_endpoint(native_io_driver *driver, native_io_endpoint endpoint, uint64_t id) {
  driver_impl *impl = driver ? driver->impl : NULL;
  if (!impl || (!id && !native_io_endpoint_valid(endpoint))) return SALTS_EINVAL;
  sync_mutex_lock(&impl->gate);
  int status = SALTS_ENOENT;
  for (size_t i = 0u; i < impl->config.request_capacity; ++i) {
    driver_slot *slot = &impl->slots[i];
    if (slot->phase == DRIVER_FREE || slot->phase == DRIVER_DELIVERING ||
        (id && slot->id != id) || (native_io_endpoint_valid(endpoint) &&
        (slot->operation.endpoint.slot != endpoint.slot ||
         slot->operation.endpoint.generation != endpoint.generation))) continue;
    slot->cancel = true;
    status = SALTS_OK;
    if (id) break;
  }
  sync_mutex_unlock(&impl->gate);
  return status;
}

int native_io_driver_cancel(native_io_driver *driver, uint64_t id) {
  if (!id) return SALTS_EINVAL;
  return native_io_driver_cancel_endpoint(driver, (native_io_endpoint){0}, id);
}

static void driver_deliver(driver_impl *impl, driver_slot *slot, native_io_completion event) {
  sync_mutex_lock(&impl->gate);
  slot->phase = DRIVER_DELIVERING;
  event.user_data = slot->operation.user_data;
  sync_mutex_unlock(&impl->gate);
  slot->completion(slot->user, slot->id, &slot->operation, &event);
  sync_mutex_lock(&impl->gate);
  memset(slot, 0, sizeof(*slot));
  --impl->pending;
  sync_mutex_unlock(&impl->gate);
}

int native_io_driver_run_ready(native_io_driver *driver, size_t max_steps, size_t *progressed) {
  driver_impl *impl = driver ? driver->impl : NULL;
  if (progressed) *progressed = 0u;
  if (!impl || !max_steps || !progressed) return SALTS_EINVAL;
  sync_mutex_lock(&impl->gate);
  if (impl->driving) { sync_mutex_unlock(&impl->gate); return SALTS_EBUSY; }
  impl->driving = true;
  sync_mutex_unlock(&impl->gate);
  int status = SALTS_OK;
  size_t steps = 0u;
  while (steps < max_steps) {
    if (impl->event_index < impl->event_count) {
      native_io_completion event = impl->events[impl->event_index++];
      if (event.user_data >= impl->config.request_capacity) { status = SALTS_EPROTO; break; }
      driver_slot *slot = &impl->slots[event.user_data];
      if (slot->phase != DRIVER_PENDING || slot->request.slot != event.request.slot ||
          slot->request.generation != event.request.generation) { status = SALTS_EPROTO; break; }
      driver_deliver(impl, slot, event);
      ++steps;
      continue;
    }
    driver_slot *selected = NULL;
    bool queued = false, cancelled = false;
    size_t index = 0u;
    sync_mutex_lock(&impl->gate);
    for (size_t i = 0u; i < impl->config.request_capacity; ++i) {
      driver_slot *slot = &impl->slots[i];
      if ((slot->phase == DRIVER_QUEUED ||
           (slot->phase == DRIVER_PENDING && slot->cancel && !slot->cancel_sent)) &&
          (!selected || slot->id < selected->id)) { selected = slot; index = i; }
    }
    if (selected) {
      queued = selected->phase == DRIVER_QUEUED;
      cancelled = selected->cancel;
      if (queued) selected->phase = DRIVER_SUBMITTING;
      else selected->cancel_sent = true;
    }
    sync_mutex_unlock(&impl->gate);
    if (selected) {
      if (queued && cancelled) {
        native_io_completion event = {.endpoint = selected->operation.endpoint,
            .kind = NATIVE_IO_COMPLETION_CANCELLED, .status = SALTS_ECANCELED};
        driver_deliver(impl, selected, event);
      } else if (queued) {
        native_io_operation operation = selected->operation;
        operation.user_data = index;
        int admitted = native_io_backend_submit(&impl->backend, &operation, &selected->request);
        if (admitted == SALTS_OK) {
          sync_mutex_lock(&impl->gate);
          selected->phase = DRIVER_PENDING;
          sync_mutex_unlock(&impl->gate);
        } else {
          native_io_completion event = {.endpoint = operation.endpoint,
              .kind = NATIVE_IO_COMPLETION_FAILED, .status = admitted};
          driver_deliver(impl, selected, event);
        }
      } else {
        int cancelled_status = native_io_backend_cancel(&impl->backend, selected->request);
        if (cancelled_status != SALTS_OK && cancelled_status != SALTS_ENOENT &&
            cancelled_status != SALTS_EALREADY) {
          sync_mutex_lock(&impl->gate);
          selected->cancel_sent = false;
          sync_mutex_unlock(&impl->gate);
          status = cancelled_status;
          break;
        }
      }
      ++steps;
      continue;
    }
    impl->event_count = impl->event_index = 0u;
    status = native_io_backend_observe(&impl->backend, impl->events,
        impl->config.completion_batch_capacity, 0u, &impl->event_count);
    if (status == SALTS_ETIMEDOUT) status = SALTS_OK;
    if (status != SALTS_OK || impl->event_count == 0u) break;
  }
  sync_mutex_lock(&impl->gate);
  impl->driving = false;
  sync_mutex_unlock(&impl->gate);
  *progressed = steps;
  return status;
}

int native_io_driver_close(native_io_driver *driver) {
  driver_impl *impl = driver ? driver->impl : NULL;
  if (!impl) return SALTS_EINVAL;
  sync_mutex_lock(&impl->gate);
  impl->closing = true;
  for (size_t i = 0u; i < impl->config.request_capacity; ++i)
    if (impl->slots[i].phase != DRIVER_FREE) impl->slots[i].cancel = true;
  sync_mutex_unlock(&impl->gate);
  return SALTS_OK;
}

bool native_io_driver_is_quiescent(const native_io_driver *driver) {
  driver_impl *impl = driver ? driver->impl : NULL;
  if (!impl) return false;
  sync_mutex_lock(&impl->gate);
  bool ready = impl->closing && !impl->pending && !impl->driving;
  sync_mutex_unlock(&impl->gate);
  return ready;
}

int native_io_driver_destroy(native_io_driver *driver) {
  if (!native_io_driver_is_quiescent(driver)) return driver && driver->impl ? SALTS_EBUSY : SALTS_EINVAL;
  driver_impl *impl = driver->impl;
  native_io_backend_stats stats;
  if (!native_io_backend_get_stats(&impl->backend, &stats)) return SALTS_EIO;
  if (stats.endpoint_count) return SALTS_EBUSY;
  int status = native_io_backend_close(&impl->backend);
  if (status != SALTS_OK) return status;
  status = native_io_backend_destroy(&impl->backend);
  if (status != SALTS_OK) return status;
  sync_mutex_destroy(&impl->gate);
  free(impl->events); free(impl->slots); free(impl);
  driver->impl = NULL;
  return SALTS_OK;
}
