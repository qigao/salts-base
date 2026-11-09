/*
 * Copyright (c) 2026 Salts Project
 */

#ifndef SALTS_SERIAL_INTERNAL_H
#define SALTS_SERIAL_INTERNAL_H

#include <serial.h>
#include <core/tstr.h>
#include <stdbool.h>
#include <stdlib.h>

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serial_native_handle serial_native_handle_t;
typedef struct serial_event_set_impl serial_event_set_impl_t;

typedef struct serial_port_info_storage {
  serial_port_info_t view;
  tstr name;
  tstr description;
  tstr usb_manufacturer;
  tstr usb_product;
  tstr usb_serial;
  tstr bluetooth_address;
} serial_port_info_storage_t;

/* sizeof a complete C object already includes its alignment padding. Keep the
 * original maximum count for this one private, statically known storage type. */
static inline size_t serial_port_list_entry_limit(void) {
  return SIZE_MAX / sizeof(serial_port_info_storage_t);
}

static inline void serial_port_info_storage_refresh_view(
    serial_port_info_storage_t *storage) {
  storage->view.name = storage->name;
  storage->view.description = storage->description;
  storage->view.usb_manufacturer = storage->usb_manufacturer;
  storage->view.usb_product = storage->usb_product;
  storage->view.usb_serial = storage->usb_serial;
  storage->view.bluetooth_address = storage->bluetooth_address;
}

static inline void serial_port_info_storage_destroy(void *value) {
  serial_port_info_storage_t *storage =
      (serial_port_info_storage_t *)value;
  if (!storage) return;
  tstr_freep(&storage->name);
  tstr_freep(&storage->description);
  tstr_freep(&storage->usb_manufacturer);
  tstr_freep(&storage->usb_product);
  tstr_freep(&storage->usb_serial);
  tstr_freep(&storage->bluetooth_address);
  memset(storage, 0, sizeof(*storage));
}

typedef bool (*serial_clone_string_fn)(tstr *out, tstr source);

static inline bool serial_port_info_storage_clone_string(tstr *out,
                                                                tstr source) {
  if (!source) {
    *out = NULL;
    return true;
  }
  *out = tstr_clone(source);
  return *out != NULL;
}

static inline bool serial_port_info_storage_copy_with(
    void *destination_, const void *source_, serial_clone_string_fn clone) {
  serial_port_info_storage_t temporary = {0};
  serial_port_info_storage_t *destination =
      (serial_port_info_storage_t *)destination_;
  const serial_port_info_storage_t *source =
      (const serial_port_info_storage_t *)source_;
  if (!destination || !source || !clone) return false;
  temporary.view = source->view;
  if (!clone(&temporary.name, source->name) ||
      !clone(&temporary.description, source->description) ||
      !clone(&temporary.usb_manufacturer, source->usb_manufacturer) ||
      !clone(&temporary.usb_product, source->usb_product) ||
      !clone(&temporary.usb_serial, source->usb_serial) ||
      !clone(&temporary.bluetooth_address, source->bluetooth_address)) {
    serial_port_info_storage_destroy(&temporary);
    return false;
  }
  serial_port_info_storage_refresh_view(&temporary);
  *destination = temporary;
  return true;
}

static inline bool serial_port_info_storage_copy(void *destination,
                                                        const void *source) {
  return serial_port_info_storage_copy_with(
      destination, source, serial_port_info_storage_clone_string);
}

static inline void serial_port_info_storage_move(void *destination_,
                                                        void *source_) {
  serial_port_info_storage_t *destination =
      (serial_port_info_storage_t *)destination_;
  serial_port_info_storage_t *source =
      (serial_port_info_storage_t *)source_;
  if (!destination || !source || destination == source) return;
  *destination = *source;
  memset(source, 0, sizeof(*source));
  serial_port_info_storage_refresh_view(destination);
}

/* One module-private owned array replaces the reflected generic Vec. The
 * append transaction remains prepare-copy, allocate, move, then publish size. */
typedef struct serial_port_info_vec {
  serial_port_info_storage_t *data;
  size_t size;
  size_t capacity;
  bool initialized;
} serial_port_info_vec_t;

static inline serial_result_t serial_port_info_vec_t_init(
    serial_port_info_vec_t *vec) {
  if (!vec || vec->initialized) return SALTS_SERIAL_INVALID_VALUE;
  memset(vec, 0, sizeof(*vec));
  vec->initialized = true;
  return SALTS_SERIAL_OK;
}

/* Allocation injection is private and must return malloc-compatible storage.
 * Production uses malloc; tests drive the same transaction through failures. */
typedef void *(*serial_storage_allocate_fn)(size_t bytes);

static inline serial_result_t serial_port_info_vec_t_push_with(
    serial_port_info_vec_t *vec,
    serial_port_info_storage_t value,
    serial_clone_string_fn clone,
    serial_storage_allocate_fn allocate) {
  serial_port_info_storage_t *prepared;
  const size_t limit = serial_port_list_entry_limit();
  if (!vec || !vec->initialized || !clone || !allocate)
    return SALTS_SERIAL_INVALID_VALUE;
  if (vec->size >= limit) return SALTS_SERIAL_NO_MEMORY;

  prepared = (serial_port_info_storage_t *)allocate(sizeof(*prepared));
  if (!prepared) return SALTS_SERIAL_NO_MEMORY;
  if (!serial_port_info_storage_copy_with(prepared, &value, clone)) {
    free(prepared);
    return SALTS_SERIAL_NO_MEMORY;
  }

  if (vec->size == vec->capacity) {
    size_t capacity = vec->capacity ? vec->capacity : 8u;
    size_t index;
    serial_port_info_storage_t *grown;
    if (capacity > limit) capacity = limit;
    while (capacity < vec->size + 1u) {
      if (capacity > limit - capacity) {
        capacity = limit;
        break;
      }
      capacity *= 2u;
    }
    grown = (serial_port_info_storage_t *)allocate(
        capacity * sizeof(*grown));
    if (!grown) {
      serial_port_info_storage_destroy(prepared);
      free(prepared);
      return SALTS_SERIAL_NO_MEMORY;
    }
    for (index = 0u; index < vec->size; ++index) {
      serial_port_info_storage_move(&grown[index], &vec->data[index]);
      serial_port_info_storage_destroy(&vec->data[index]);
    }
    free(vec->data);
    vec->data = grown;
    vec->capacity = capacity;
  }

  serial_port_info_storage_move(&vec->data[vec->size], prepared);
  serial_port_info_storage_destroy(prepared);
  free(prepared);
  ++vec->size;
  return SALTS_SERIAL_OK;
}

static inline serial_result_t serial_port_info_vec_t_push(
    serial_port_info_vec_t *vec,
    serial_port_info_storage_t value) {
  return serial_port_info_vec_t_push_with(
      vec, value, serial_port_info_storage_clone_string, malloc);
}

static inline serial_port_info_storage_t *serial_port_info_vec_t_at(
    serial_port_info_vec_t *vec, size_t index) {
  return vec && vec->initialized && index < vec->size ? &vec->data[index] : NULL;
}

static inline const serial_port_info_storage_t *
serial_port_info_vec_t_at_const(const serial_port_info_vec_t *vec,
                                      size_t index) {
  return vec && vec->initialized && index < vec->size ? &vec->data[index] : NULL;
}

static inline size_t serial_port_info_vec_t_size(
    const serial_port_info_vec_t *vec) {
  return vec && vec->initialized ? vec->size : 0u;
}

static inline void serial_port_info_vec_t_destroy(
    serial_port_info_vec_t *vec) {
  size_t index;
  if (!vec || !vec->initialized) return;
  for (index = 0u; index < vec->size; ++index)
    serial_port_info_storage_destroy(&vec->data[index]);
  free(vec->data);
  memset(vec, 0, sizeof(*vec));
}

typedef struct serial_backend_ops serial_backend_ops_t;

struct serial_backend_ops {
  serial_result_t (*list_ports)(serial_port_info_vec_t *vec);
  serial_result_t (*get_port_info)(const char *port_name,
                                         serial_port_info_storage_t *storage);

  serial_result_t (*open)(serial_native_handle_t **handle_out, const char *port_name,
                               serial_mode_t mode);
  void (*close)(serial_native_handle_t *handle);
  serial_result_t (*configure)(serial_native_handle_t *handle,
                                     const serial_config_t *config);

  serial_result_t (*blocking_read)(serial_native_handle_t *handle, void *buf, size_t count,
                                         unsigned int timeout_ms, size_t *bytes_read);
  serial_result_t (*blocking_write)(serial_native_handle_t *handle, const void *buf,
                                          size_t count, unsigned int timeout_ms,
                                          size_t *bytes_written);

  serial_result_t (*nonblocking_read)(serial_native_handle_t *handle, void *buf, size_t count,
                                            size_t *bytes_read);
  serial_result_t (*nonblocking_write)(serial_native_handle_t *handle, const void *buf,
                                             size_t count, size_t *bytes_written);

  serial_result_t (*new_event_set)(serial_event_set_impl_t **set_out);
  void (*free_event_set)(serial_event_set_impl_t *set);
  serial_result_t (*add_port_events)(serial_event_set_impl_t *set,
                                           serial_native_handle_t *handle, unsigned int events);
  serial_result_t (*wait)(serial_event_set_impl_t *set, unsigned int timeout_ms,
                                unsigned int *events_out);
};

const serial_backend_ops_t *serial_get_native_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SERIAL_INTERNAL_H */
