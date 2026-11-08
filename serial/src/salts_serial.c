/*
 * Copyright (c) 2026 Salts Project
 */

#include "serial_internal.h"
#include "ring_buffer_spsc.h"
#include "tstr.h"
#include "thread.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct salts_serial_base {
  salts_serial_config_t config;
} salts_serial_base_t;

typedef struct salts_serial_async {
  cmeta_thread_t worker_thread;
  cmeta_mutex_t worker_mutex;
  cmeta_cond_t worker_cond;
  atomic_bool async_running;
  atomic_bool wake_requested;
} salts_serial_async_t;

typedef struct salts_serial_error {
  atomic_int last_error;
} salts_serial_error_t;

struct salts_serial_port_list {
  salts_serial_port_info_vec_t items;
};

struct salts_serial_event_set {
  salts_serial_event_set_impl_t *impl;
};

typedef struct salts_serial_size_result {
  salts_serial_result_t code;
  size_t count;
} salts_serial_size_result_t;

#ifdef SALTS_SERIAL_TESTING
#include "serial_test.h"
static const salts_serial_backend_ops_t *active_backend_ops = NULL;
static salts_serial_test_thread_create_fn active_thread_create = cmeta_thread_create;

void salts_serial_set_backend_ops_for_testing(const salts_serial_backend_ops_t *ops) {
  active_backend_ops = ops;
}

void salts_serial_test_set_thread_create(salts_serial_test_thread_create_fn create) {
  active_thread_create = create ? create : cmeta_thread_create;
}
#endif

static const salts_serial_backend_ops_t *default_backend_ops(void) {
#ifdef SALTS_SERIAL_TESTING
  return active_backend_ops ? active_backend_ops : salts_serial_get_native_backend();
#else
  return salts_serial_get_native_backend();
#endif
}

struct salts_serial_t {
  const salts_serial_backend_ops_t *ops;
  salts_serial_base_t base;
  salts_port_handle_t *handle;
  ring_spsc_t rx_ring;
  ring_spsc_t tx_ring;
  uint8_t *rx_storage;
  uint8_t *tx_storage;
  salts_serial_async_t async;
  salts_serial_error_t error;
};

#ifdef SALTS_SERIAL_TESTING
void salts_serial_test_set_handle(salts_serial_t *serial, salts_port_handle_t *handle) {
  if (serial) {
    serial->handle = handle;
  }
}
#endif

static int is_power_of_two(size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

static size_t min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static salts_serial_size_result_t make_size_result(salts_serial_result_t code, size_t count) {
  salts_serial_size_result_t result;
  result.code = code;
  result.count = count;
  return result;
}

static salts_serial_result_t publish_size_result(salts_serial_size_result_t result,
                                                  size_t *count_out) {
  if (count_out) *count_out = result.count;
  return result.code;
}

static salts_serial_size_result_t write_ring(ring_spsc_t *ring, const void *buf, size_t count) {
  const uint8_t *src = (const uint8_t *)buf;
  size_t total = 0;

  if (!ring || (!buf && count != 0)) return make_size_result(SALTS_SERIAL_INVALID_VALUE, 0);

  while (total < count) {
    size_t chunk = min_size(count - total, ring->size - 1);
    uint8_t *dst = NULL;

    if (chunk == 0) break;

    dst = ring_spsc_write_acquire(ring, chunk);
    if (!dst) {
      const size_t available = ring_spsc_write_available(ring);
      if (available == 0) break;

      chunk = min_size(chunk, available);
      dst = ring_spsc_write_acquire(ring, chunk);
      if (!dst) {
        chunk = 1;
        dst = ring_spsc_write_acquire(ring, chunk);
      }
    }

    if (!dst) break;

    memcpy(dst, src + total, chunk);
    ring_spsc_write_release(ring, chunk);
    total += chunk;
  }

  return make_size_result(total == 0 && count != 0 ? SALTS_SERIAL_WOULD_BLOCK : SALTS_SERIAL_OK,
                          total);
}

static salts_serial_size_result_t read_ring(ring_spsc_t *ring, void *buf, size_t count) {
  uint8_t *dst = (uint8_t *)buf;
  size_t total = 0;

  if (!ring || (!buf && count != 0)) return make_size_result(SALTS_SERIAL_INVALID_VALUE, 0);

  while (total < count) {
    size_t available = 0;
    uint8_t *src = ring_spsc_read_acquire(ring, &available);
    size_t chunk = 0;

    if (!src || available == 0) break;

    chunk = min_size(count - total, available);
    memcpy(dst + total, src, chunk);
    ring_spsc_read_release(ring, chunk);
    total += chunk;
  }

  return make_size_result(total == 0 && count != 0 ? SALTS_SERIAL_WOULD_BLOCK : SALTS_SERIAL_OK,
                          total);
}

static void set_last_error(salts_serial_t *serial, salts_serial_result_t result) {
  if (serial && result != SALTS_SERIAL_OK) {
    atomic_store(&serial->error.last_error, (int)result);
  }
}

static void wake_worker(salts_serial_t *serial) {
  if (!serial) return;

  atomic_store(&serial->async.wake_requested, true);
  cmeta_mutex_lock(&serial->async.worker_mutex);
  cmeta_cond_signal(&serial->async.worker_cond);
  cmeta_mutex_unlock(&serial->async.worker_mutex);
}

static int pump_rx_once(salts_serial_t *serial) {
  size_t write_available;
  size_t chunk;
  uint8_t *dst;
  salts_serial_result_t res;
  size_t bytes_read = 0;

  if (!serial || !serial->handle) return 0;

  write_available = ring_spsc_write_available(&serial->rx_ring);
  if (write_available == 0) return 0;

  chunk = min_size(write_available, serial->base.config.io_chunk_size);
  dst = ring_spsc_write_acquire(&serial->rx_ring, chunk);
  if (!dst) {
    chunk = 1;
    dst = ring_spsc_write_acquire(&serial->rx_ring, chunk);
    if (!dst) return 0;
  }

  res = serial->ops->nonblocking_read(serial->handle, dst, chunk, &bytes_read);
  if (res != SALTS_SERIAL_OK) {
    set_last_error(serial, res);
    return 0;
  }
  if (bytes_read == 0) return 0;

  ring_spsc_write_release(&serial->rx_ring, bytes_read);
  return 1;
}

static int pump_tx_once(salts_serial_t *serial) {
  size_t available = 0;
  size_t chunk;
  uint8_t *src;
  salts_serial_result_t res;
  size_t bytes_written = 0;

  if (!serial || !serial->handle) return 0;

  src = ring_spsc_read_acquire(&serial->tx_ring, &available);
  if (!src || available == 0) return 0;

  chunk = min_size(available, serial->base.config.io_chunk_size);
  res = serial->ops->nonblocking_write(serial->handle, src, chunk, &bytes_written);
  if (res != SALTS_SERIAL_OK) {
    set_last_error(serial, res);
    return 0;
  }
  if (bytes_written == 0) return 0;

  ring_spsc_read_release(&serial->tx_ring, bytes_written);
  return 1;
}

static void pump_worker_main(void *arg) {
  salts_serial_t *serial = (salts_serial_t *)arg;
  size_t consecutive_progress = 0;

  while (atomic_load(&serial->async.async_running)) {
    int made_progress = 0;

    made_progress |= pump_rx_once(serial);
    made_progress |= pump_tx_once(serial);

    if (made_progress) {
      consecutive_progress++;
      if (consecutive_progress >= 64) {
        cmeta_sleep_ms(1);
        consecutive_progress = 0;
      } else {
        cmeta_thread_yield();
      }
      continue;
    }

    consecutive_progress = 0;

    cmeta_mutex_lock(&serial->async.worker_mutex);
    if (atomic_load(&serial->async.async_running) &&
        !atomic_exchange(&serial->async.wake_requested, false)) {
      (void)cmeta_cond_timedwait(
          &serial->async.worker_cond, &serial->async.worker_mutex,
          (uint64_t)serial->base.config.poll_interval_ms * 1000000ULL);
    }
    cmeta_mutex_unlock(&serial->async.worker_mutex);
  }
}

void salts_serial_config_default(salts_serial_config_t *config) {
  if (!config) return;

  config->baudrate = 115200;
  config->bits = 8;
  config->parity = SALTS_SERIAL_PARITY_NONE;
  config->stopbits = 1;
  config->flowcontrol = SALTS_SERIAL_FLOWCONTROL_NONE;
  config->rx_buffer_size = 4096;
  config->tx_buffer_size = 4096;
  config->io_chunk_size = 256;
  config->poll_interval_ms = 10;
}

#define SALTS_SERIAL_RESULT_NAME_X                               \
  X(SALTS_SERIAL_OK, "ok")                                      \
  X(SALTS_SERIAL_INVALID_VALUE, "invalid value")                \
  X(SALTS_SERIAL_INVALID_STATE, "invalid state")                \
  X(SALTS_SERIAL_IO_FAILED, "io failed")                        \
  X(SALTS_SERIAL_NO_MEMORY, "no memory")                        \
  X(SALTS_SERIAL_NOT_SUPPORTED, "not supported")                \
  X(SALTS_SERIAL_WOULD_BLOCK, "would block")

const char *salts_serial_result_name(salts_serial_result_t result) {
  switch (result) {
#define X(code, text) case code: return text;
    SALTS_SERIAL_RESULT_NAME_X
#undef X
  default:
    return "unknown";
  }
}

salts_serial_result_t salts_serial_list_ports(salts_serial_port_list_t **ports) {
  const salts_serial_backend_ops_t *ops = default_backend_ops();
  salts_serial_port_list_t *list = NULL;
  salts_serial_result_t result = SALTS_SERIAL_OK;

  if (!ports) return SALTS_SERIAL_INVALID_VALUE;
  *ports = NULL;

  list = (salts_serial_port_list_t *)calloc(1, sizeof(*list));
  if (!list) return SALTS_SERIAL_NO_MEMORY;

  result = salts_serial_port_info_vec_t_init(&list->items);
  if (result != SALTS_SERIAL_OK) {
    free(list);
    return result;
  }

  result = ops->list_ports(&list->items);
  if (result != SALTS_SERIAL_OK) {
    salts_serial_port_list_destroy(list);
    return result;
  }

  *ports = list;
  return SALTS_SERIAL_OK;
}

salts_serial_result_t salts_serial_port_info_by_name(const char *port_name,
                                                      salts_serial_port_list_t **ports,
                                                      const salts_serial_port_info_t **info) {
  const salts_serial_backend_ops_t *ops = default_backend_ops();
  salts_serial_port_list_t *list = NULL;
  salts_serial_port_info_storage_t storage;
  salts_serial_result_t result;

  if (!ports) return SALTS_SERIAL_INVALID_VALUE;
  *ports = NULL;
  if (info) *info = NULL;
  if (!port_name) return SALTS_SERIAL_INVALID_VALUE;

  list = (salts_serial_port_list_t *)calloc(1, sizeof(*list));
  if (!list) return SALTS_SERIAL_NO_MEMORY;

  result = salts_serial_port_info_vec_t_init(&list->items);
  if (result != SALTS_SERIAL_OK) {
    free(list);
    return result;
  }

  memset(&storage, 0, sizeof(storage));
  result = ops->get_port_info(port_name, &storage);
  if (result != SALTS_SERIAL_OK) {
    salts_serial_port_list_destroy(list);
    return result;
  }

  result = salts_serial_port_info_vec_t_push(&list->items, storage);
  salts_serial_port_info_storage_destroy(&storage);
  if (result != SALTS_SERIAL_OK) {
    salts_serial_port_list_destroy(list);
    return result;
  }

  *ports = list;
  if (info) {
    salts_serial_port_info_storage_t *stored =
        salts_serial_port_info_vec_t_at(&list->items, 0);
    *info = &stored->view;
  }
  return SALTS_SERIAL_OK;
}

void salts_serial_port_list_destroy(salts_serial_port_list_t *ports) {
  if (!ports) return;
  salts_serial_port_info_vec_t_destroy(&ports->items);
  free(ports);
}

size_t salts_serial_port_list_count(const salts_serial_port_list_t *ports) {
  return ports ? salts_serial_port_info_vec_t_size(&ports->items) : 0;
}

const salts_serial_port_info_t *salts_serial_port_list_get(const salts_serial_port_list_t *ports,
                                                             size_t index) {
  const salts_serial_port_info_storage_t *storage;

  if (!ports) return NULL;
  storage = salts_serial_port_info_vec_t_at_const(&ports->items, index);
  return storage ? &storage->view : NULL;
}

salts_serial_result_t salts_serial_create(salts_serial_t **serial,
                                           const salts_serial_config_t *config) {
  salts_serial_config_t actual;
  salts_serial_t *instance = NULL;
  salts_serial_result_t result = SALTS_SERIAL_OK;

  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  *serial = NULL;

  if (config) actual = *config;
  else salts_serial_config_default(&actual);

  if (!is_power_of_two(actual.rx_buffer_size) || !is_power_of_two(actual.tx_buffer_size) ||
      actual.rx_buffer_size < 2 || actual.tx_buffer_size < 2 || actual.io_chunk_size == 0 ||
      actual.poll_interval_ms == 0 || actual.baudrate <= 0 || actual.bits < 5 ||
      actual.bits > 8 || actual.stopbits < 1 || actual.stopbits > 2 ||
      actual.parity < SALTS_SERIAL_PARITY_NONE ||
      actual.parity > SALTS_SERIAL_PARITY_SPACE ||
      actual.flowcontrol < SALTS_SERIAL_FLOWCONTROL_NONE ||
      actual.flowcontrol > SALTS_SERIAL_FLOWCONTROL_DTRDSR) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  /* Clamp io_chunk_size to not exceed buffer capacity to prevent pump inefficiency */
  {
    size_t min_buffer = actual.rx_buffer_size < actual.tx_buffer_size ?
                        actual.rx_buffer_size : actual.tx_buffer_size;
    if (actual.io_chunk_size > min_buffer) {
      actual.io_chunk_size = min_buffer;
    }
  }

  instance = (salts_serial_t *)calloc(1, sizeof(*instance));
  if (!instance) return SALTS_SERIAL_NO_MEMORY;

  instance->ops = default_backend_ops();
  cmeta_mutex_init(&instance->async.worker_mutex);
  cmeta_cond_init(&instance->async.worker_cond);
  atomic_init(&instance->async.async_running, false);
  atomic_init(&instance->async.wake_requested, false);
  atomic_init(&instance->error.last_error, (int)SALTS_SERIAL_OK);

  instance->rx_storage = (uint8_t *)malloc(actual.rx_buffer_size);
  instance->tx_storage = (uint8_t *)malloc(actual.tx_buffer_size);
  if (!instance->rx_storage || !instance->tx_storage) {
    result = SALTS_SERIAL_NO_MEMORY;
    goto cleanup;
  }

  if (!ring_spsc_init(&instance->rx_ring, instance->rx_storage,
                      actual.rx_buffer_size) ||
      !ring_spsc_init(&instance->tx_ring, instance->tx_storage,
                      actual.tx_buffer_size)) {
    result = SALTS_SERIAL_INVALID_VALUE;
    goto cleanup;
  }

  instance->base.config = actual;
  *serial = instance;
  return SALTS_SERIAL_OK;

cleanup:
  salts_serial_destroy(instance);
  return result;
}

void salts_serial_destroy(salts_serial_t *serial) {
  if (!serial) return;

  (void)salts_serial_close(serial);
  cmeta_cond_destroy(&serial->async.worker_cond);
  cmeta_mutex_destroy(&serial->async.worker_mutex);
  free(serial->rx_storage);
  free(serial->tx_storage);
  free(serial);
}

salts_serial_result_t salts_serial_open(salts_serial_t *serial, const char *port_name,
                                         salts_serial_mode_t mode) {
  salts_serial_result_t result;

  if (!serial || !port_name) return SALTS_SERIAL_INVALID_VALUE;
  if (serial->handle) return SALTS_SERIAL_INVALID_STATE;

  result = serial->ops->open(&serial->handle, port_name, mode);
  if (result != SALTS_SERIAL_OK) return result;

  result = serial->ops->configure(serial->handle, &serial->base.config);
  if (result != SALTS_SERIAL_OK) {
    serial->ops->close(serial->handle);
    serial->handle = NULL;
    return result;
  }

  return SALTS_SERIAL_OK;
}

salts_serial_result_t salts_serial_close(salts_serial_t *serial) {
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  if (!serial->handle) return SALTS_SERIAL_OK;

  (void)salts_serial_stop_async(serial);

  serial->ops->close(serial->handle);
  serial->handle = NULL;

  return SALTS_SERIAL_OK;
}

salts_serial_result_t salts_serial_read(salts_serial_t *serial, void *buf, size_t count,
                                         unsigned int timeout_ms, size_t *bytes_read) {
  if (bytes_read) *bytes_read = 0;
  if (!serial || (!buf && count != 0)) return SALTS_SERIAL_INVALID_VALUE;
  if (!serial->handle) return SALTS_SERIAL_INVALID_STATE;
  if (atomic_load(&serial->async.async_running)) return SALTS_SERIAL_INVALID_STATE;

  return serial->ops->blocking_read(serial->handle, buf, count, timeout_ms, bytes_read);
}

salts_serial_result_t salts_serial_write(salts_serial_t *serial, const void *buf, size_t count,
                                          unsigned int timeout_ms, size_t *bytes_written) {
  if (bytes_written) *bytes_written = 0;
  if (!serial || (!buf && count != 0)) return SALTS_SERIAL_INVALID_VALUE;
  if (!serial->handle) return SALTS_SERIAL_INVALID_STATE;
  if (atomic_load(&serial->async.async_running)) return SALTS_SERIAL_INVALID_STATE;

  return serial->ops->blocking_write(serial->handle, buf, count, timeout_ms, bytes_written);
}

salts_serial_result_t salts_serial_start_async(salts_serial_t *serial) {
  int thread_result;

  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  if (!serial->handle) return SALTS_SERIAL_INVALID_STATE;
  if (atomic_load(&serial->async.async_running)) return SALTS_SERIAL_OK;

  atomic_store(&serial->error.last_error, (int)SALTS_SERIAL_OK);
  atomic_store(&serial->async.wake_requested, true);
  /* The worker can run before thread creation returns. Publish its loop
   * condition first, and roll it back if no thread was created. */
  atomic_store(&serial->async.async_running, true);

#ifdef SALTS_SERIAL_TESTING
  thread_result = active_thread_create(&serial->async.worker_thread, pump_worker_main, serial);
#else
  thread_result = cmeta_thread_create(&serial->async.worker_thread, pump_worker_main, serial);
#endif
  if (thread_result != 0) {
    atomic_store(&serial->async.async_running, false);
    atomic_store(&serial->async.wake_requested, false);
    return SALTS_SERIAL_IO_FAILED;
  }

  wake_worker(serial);
  return SALTS_SERIAL_OK;
}

salts_serial_result_t salts_serial_stop_async(salts_serial_t *serial) {
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  if (!atomic_load(&serial->async.async_running)) return SALTS_SERIAL_OK;

  atomic_store(&serial->async.async_running, false);
  wake_worker(serial);
  if (serial->async.worker_thread) {
    (void)cmeta_thread_join(&serial->async.worker_thread);
    memset(&serial->async.worker_thread, 0, sizeof(serial->async.worker_thread));
  }
  return SALTS_SERIAL_OK;
}

int salts_serial_async_running(const salts_serial_t *serial) {
  if (!serial) return 0;
  return atomic_load(&serial->async.async_running) ? 1 : 0;
}

salts_serial_result_t salts_serial_last_error(const salts_serial_t *serial) {
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  return (salts_serial_result_t)atomic_load(&serial->error.last_error);
}

salts_serial_result_t salts_serial_event_set_create(salts_serial_event_set_t **event_set) {
  const salts_serial_backend_ops_t *ops = default_backend_ops();
  salts_serial_event_set_t *instance = NULL;
  salts_serial_result_t res;

  if (!event_set) return SALTS_SERIAL_INVALID_VALUE;
  *event_set = NULL;

  instance = (salts_serial_event_set_t *)calloc(1, sizeof(*instance));
  if (!instance) return SALTS_SERIAL_NO_MEMORY;

  res = ops->new_event_set(&instance->impl);
  if (res != SALTS_SERIAL_OK) {
    free(instance);
    return res;
  }

  *event_set = instance;
  return SALTS_SERIAL_OK;
}

void salts_serial_event_set_destroy(salts_serial_event_set_t *event_set) {
  if (!event_set) return;

  if (event_set->impl) default_backend_ops()->free_event_set(event_set->impl);
  free(event_set);
}

salts_serial_result_t salts_serial_event_set_add(salts_serial_event_set_t *event_set,
                                                  const salts_serial_t *serial,
                                                  unsigned int events) {
  if (!event_set || !event_set->impl || !serial || !serial->ops) {
    return SALTS_SERIAL_INVALID_VALUE;
  }
  if (serial->ops != default_backend_ops()) return SALTS_SERIAL_INVALID_STATE;
  if ((events & ~(SALTS_SERIAL_EVENT_RX_READY | SALTS_SERIAL_EVENT_TX_READY |
                  SALTS_SERIAL_EVENT_ERROR)) != 0) {
    return SALTS_SERIAL_INVALID_VALUE;
  }
  if (!serial->handle) return SALTS_SERIAL_INVALID_STATE;

  return serial->ops->add_port_events(event_set->impl, serial->handle, events);
}

salts_serial_result_t salts_serial_event_wait(salts_serial_event_set_t *event_set,
                                               unsigned int timeout_ms) {
  return salts_serial_event_wait_ex(event_set, timeout_ms, NULL);
}

salts_serial_result_t salts_serial_event_wait_ex(salts_serial_event_set_t *event_set,
                                                  unsigned int timeout_ms,
                                                  unsigned int *events) {
  if (events) *events = 0;
  if (!event_set || !event_set->impl) return SALTS_SERIAL_INVALID_VALUE;

  return default_backend_ops()->wait(event_set->impl, timeout_ms, events);
}

size_t salts_serial_rx_available(const salts_serial_t *serial) {
  if (!serial) return 0;
  return ring_spsc_read_available(&serial->rx_ring);
}

size_t salts_serial_tx_available(const salts_serial_t *serial) {
  if (!serial) return 0;
  return ring_spsc_read_available(&serial->tx_ring);
}

size_t salts_serial_rx_capacity(const salts_serial_t *serial) {
  if (!serial) return 0;
  return serial->rx_ring.size - 1;
}

size_t salts_serial_tx_capacity(const salts_serial_t *serial) {
  if (!serial) return 0;
  return serial->tx_ring.size - 1;
}

salts_serial_result_t salts_serial_buffer_rx(salts_serial_t *serial, const void *buf,
                                              size_t count, size_t *bytes_buffered) {
  salts_serial_size_result_t result;

  if (bytes_buffered) *bytes_buffered = 0;
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  if (atomic_load(&serial->async.async_running)) return SALTS_SERIAL_INVALID_STATE;
  result = write_ring(&serial->rx_ring, buf, count);
  return publish_size_result(result, bytes_buffered);
}

salts_serial_result_t salts_serial_read_buffered(salts_serial_t *serial, void *buf,
                                                  size_t count, size_t *bytes_read) {
  salts_serial_size_result_t result;

  if (bytes_read) *bytes_read = 0;
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  result = read_ring(&serial->rx_ring, buf, count);
  return publish_size_result(result, bytes_read);
}

salts_serial_result_t salts_serial_write_buffered(salts_serial_t *serial, const void *buf,
                                                   size_t count, size_t *bytes_buffered) {
  salts_serial_size_result_t result;

  if (bytes_buffered) *bytes_buffered = 0;
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  result = write_ring(&serial->tx_ring, buf, count);
  if (result.code == SALTS_SERIAL_OK && result.count > 0) {
    wake_worker(serial);
  }
  return publish_size_result(result, bytes_buffered);
}

salts_serial_result_t salts_serial_drain_tx_buffer(salts_serial_t *serial, void *buf,
                                                    size_t count, size_t *bytes_read) {
  salts_serial_size_result_t result;

  if (bytes_read) *bytes_read = 0;
  if (!serial) return SALTS_SERIAL_INVALID_VALUE;
  if (atomic_load(&serial->async.async_running)) return SALTS_SERIAL_INVALID_STATE;
  result = read_ring(&serial->tx_ring, buf, count);
  return publish_size_result(result, bytes_read);
}
