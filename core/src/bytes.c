#include <core/bytes.h>

#include <core/error_codes.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int core_bytes_view_init(core_bytes_view *out,
                         const void *data, size_t length) {
  core_bytes_view candidate;
  if (out == NULL || (data == NULL && length != 0u)) return SALTS_CORE_EINVAL;
  candidate.data = (const char *)data;
  candidate.len = length;
  *out = candidate;
  return SALTS_CORE_OK;
}

int core_bytes_view_slice(core_bytes_view view,
                          size_t start, size_t length,
                          core_bytes_view *out) {
  core_bytes_view candidate;
  if (out == NULL || (view.data == NULL && view.len != 0u) ||
      start > view.len || length > view.len - start) return SALTS_CORE_EINVAL;
  candidate.data = view.data == NULL ? NULL : view.data + start;
  candidate.len = length;
  *out = candidate;
  return SALTS_CORE_OK;
}

#define bytes_MIN_CAPACITY 256u

static int bytes_valid(const bytes_t *buffer) {
  if (!buffer || buffer->max_bytes == 0u || buffer->read_pos > buffer->write_pos ||
      buffer->write_pos > buffer->capacity || buffer->capacity > buffer->max_bytes)
    return 0;
  return (buffer->capacity == 0u) == (buffer->data == NULL);
}

static size_t bytes_unread(const bytes_t *buffer) {
  return buffer->write_pos - buffer->read_pos;
}

static size_t bytes_growth(const bytes_t *buffer, size_t required) {
  size_t grown;

  if (buffer->capacity == 0u) {
    grown = bytes_MIN_CAPACITY;
  } else if (buffer->capacity > buffer->max_bytes - buffer->capacity / 2u) {
    grown = buffer->max_bytes;
  } else {
    grown = buffer->capacity + buffer->capacity / 2u;
  }
  if (grown < required) grown = required;
  if (grown > buffer->max_bytes) grown = buffer->max_bytes;
  return grown;
}

static int bytes_source(const bytes_t *buffer, const void *data,
                                    size_t size, size_t *unread_offset, int *aliases) {
  uintptr_t base;
  uintptr_t end;
  uintptr_t source;
  uintptr_t source_end;

  *unread_offset = 0u;
  *aliases = 0;
  if (!buffer->data) return SALTS_OK;

  base = (uintptr_t)buffer->data;
  source = (uintptr_t)data;
  if (buffer->capacity > UINTPTR_MAX - base || size > UINTPTR_MAX - source) return SALTS_EINVAL;
  end = base + buffer->capacity;
  source_end = source + size;
  if (source >= end || source_end <= base) return SALTS_OK;

  if (source < base + buffer->read_pos || source > base + buffer->write_pos ||
      size > (base + buffer->write_pos) - source)
    return SALTS_EINVAL;
  *unread_offset = (size_t)(source - (base + buffer->read_pos));
  *aliases = 1;
  return SALTS_OK;
}

int bytes_init(bytes_t *buffer, size_t max_bytes) {
  if (!buffer || max_bytes == 0u) return SALTS_EINVAL;
  memset(buffer, 0, sizeof(*buffer));
  buffer->max_bytes = max_bytes;
  return SALTS_OK;
}

void bytes_destroy(bytes_t *buffer) {
  if (!buffer) return;
  free(buffer->data);
  memset(buffer, 0, sizeof(*buffer));
}

size_t bytes_size(const bytes_t *buffer) {
  return bytes_valid(buffer) ? bytes_unread(buffer) : 0u;
}

size_t bytes_available(const bytes_t *buffer) {
  return bytes_valid(buffer) ? buffer->max_bytes - bytes_unread(buffer)
                                         : 0u;
}

size_t bytes_capacity(const bytes_t *buffer) {
  return bytes_valid(buffer) ? buffer->capacity : 0u;
}

int bytes_append(bytes_t *buffer, const void *data, size_t size) {
  size_t unread;
  size_t source_offset;
  int aliases;
  int rc;

  if (!bytes_valid(buffer) || (!data && size != 0u)) return SALTS_EINVAL;
  unread = bytes_unread(buffer);
  if (size > buffer->max_bytes - unread) return SALTS_ENOSPC;
  if (size == 0u) return SALTS_OK;

  rc = bytes_source(buffer, data, size, &source_offset, &aliases);
  if (rc != SALTS_OK) return rc;

  if (size <= buffer->capacity - buffer->write_pos) {
    memmove(buffer->data + buffer->write_pos, data, size);
    buffer->write_pos += size;
    return SALTS_OK;
  }

  if (size <= buffer->capacity - unread) {
    memmove(buffer->data, buffer->data + buffer->read_pos, unread);
    if (aliases) data = buffer->data + source_offset;
    memmove(buffer->data + unread, data, size);
    buffer->read_pos = 0u;
    buffer->write_pos = unread + size;
    return SALTS_OK;
  }

  {
    size_t required = unread + size;
    size_t new_capacity = bytes_growth(buffer, required);
    uint8_t *next = (uint8_t *)malloc(new_capacity);
    if (!next) return SALTS_ENOMEM;

    if (unread != 0u) memcpy(next, buffer->data + buffer->read_pos, unread);
    if (aliases) data = next + source_offset;
    memmove(next + unread, data, size);
    free(buffer->data);
    buffer->data = next;
    buffer->read_pos = 0u;
    buffer->write_pos = required;
    buffer->capacity = new_capacity;
  }
  return SALTS_OK;
}

int bytes_view(const bytes_t *buffer, bytes_view_t *out) {
  bytes_view_t view;
  size_t unread;

  if (!bytes_valid(buffer) || !out) return SALTS_EINVAL;
  unread = bytes_unread(buffer);
  view.data = unread != 0u ? buffer->data + buffer->read_pos : NULL;
  view.size = unread;
  *out = view;
  return SALTS_OK;
}

int bytes_consume(bytes_t *buffer, size_t size) {
  size_t unread;

  if (!bytes_valid(buffer)) return SALTS_EINVAL;
  unread = bytes_unread(buffer);
  if (size > unread) return SALTS_ERANGE;
  if (size == unread) {
    buffer->read_pos = 0u;
    buffer->write_pos = 0u;
  } else {
    buffer->read_pos += size;
  }
  return SALTS_OK;
}

void bytes_reset(bytes_t *buffer) {
  if (!bytes_valid(buffer)) return;
  buffer->read_pos = 0u;
  buffer->write_pos = 0u;
}
