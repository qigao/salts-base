#include <unicode.h>
#include "unicode_ascii_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct unicode_fold_mapping {
  uint32_t scalar;
  uint32_t values_offset;
  uint8_t length;
} unicode_fold_mapping;

#include "unicode_casefold_data.h"

static const unicode_fold_mapping *unicode_casefold_lookup(uint32_t scalar) {
  size_t begin = 0u;
  size_t end = sizeof(unicode_casefold_mappings) /
               sizeof(unicode_casefold_mappings[0]);
  while (begin < end) {
    const size_t mid = begin + (end - begin) / 2u;
    const unicode_fold_mapping *item = &unicode_casefold_mappings[mid];
    if (scalar < item->scalar) end = mid;
    else if (scalar > item->scalar) begin = mid + 1u;
    else return item;
  }
  return NULL;
}

static size_t unicode_fold_utf8_length(uint32_t scalar) {
  return scalar <= 0x7fu ? 1u :
         scalar <= 0x7ffu ? 2u :
         scalar <= 0xffffu ? 3u : 4u;
}

static size_t unicode_fold_encode(uint32_t scalar, char output[4]) {
  if (scalar <= 0x7fu) {
    output[0] = (char)scalar; return 1u;
  }
  if (scalar <= 0x7ffu) {
    output[0] = (char)(0xc0u | (scalar >> 6u));
    output[1] = (char)(0x80u | (scalar & 0x3fu)); return 2u;
  }
  if (scalar <= 0xffffu) {
    output[0] = (char)(0xe0u | (scalar >> 12u));
    output[1] = (char)(0x80u | ((scalar >> 6u) & 0x3fu));
    output[2] = (char)(0x80u | (scalar & 0x3fu)); return 3u;
  }
  output[0] = (char)(0xf0u | (scalar >> 18u));
  output[1] = (char)(0x80u | ((scalar >> 12u) & 0x3fu));
  output[2] = (char)(0x80u | ((scalar >> 6u) & 0x3fu));
  output[3] = (char)(0x80u | (scalar & 0x3fu));
  return 4u;
}

/* Two-pass, no-heap, failure-atomic counted UTF-8 Default Full Case Folding.
 * The caller must preserve an immutable and non-overlapping input buffer. */
unicode_status unicode_case_fold(
    vstr input, char *output, size_t capacity, size_t *out_length) {
  size_t cursor = 0u;
  size_t required = 0u;
  if (out_length == NULL || (input.data == NULL && input.len != 0u) ||
      (output == NULL && capacity != 0u))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  if (output != NULL && input.data != NULL && input.len != 0u) {
    const uintptr_t src = (uintptr_t)input.data;
    const uintptr_t dst = (uintptr_t)output;
    if ((dst >= src && dst - src < input.len) ||
        (src > dst && src - dst < capacity))
      return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  }

  while (cursor < input.len) {
    if ((unsigned char)input.data[cursor] < 0x80u) {
      size_t ascii;
      const unicode_status status = unicode_ascii_prefix(
          input.data + cursor, input.len - cursor, &ascii);
      if (status != SALTS_UNICODE_OK) return status;
      if (ascii > SIZE_MAX - required) return SALTS_UNICODE_ERR_CAPACITY;
      required += ascii;
      cursor += ascii;
      if (cursor == input.len) break;
    }
    unicode_scalar current;
    const unicode_fold_mapping *mapped;
    unicode_status status = unicode_utf8_next(input, &cursor, &current);
    if (status != SALTS_UNICODE_OK) return status;
    mapped = unicode_casefold_lookup(current.value);
    const size_t count = mapped != NULL ? mapped->length : 1u;
    for (size_t j = 0u; j < count; ++j) {
      const uint32_t scalar = mapped != NULL ?
          unicode_casefold_values[mapped->values_offset + j] : current.value;
      const size_t needed = unicode_fold_utf8_length(scalar);
      if (needed > SIZE_MAX - required) return SALTS_UNICODE_ERR_CAPACITY;
      required += needed;
    }
  }

  if (output == NULL) {
    *out_length = required;
    return SALTS_UNICODE_OK;
  }
  if (capacity < required) return SALTS_UNICODE_ERR_CAPACITY;

  cursor = 0u;
  size_t written = 0u;
  while (cursor < input.len) {
    if ((unsigned char)input.data[cursor] < 0x80u) {
      size_t ascii;
      const unicode_status status = unicode_ascii_prefix(
          input.data + cursor, input.len - cursor, &ascii);
      if (status != SALTS_UNICODE_OK) return status;
      for (size_t i = 0u; i < ascii; ++i) {
        const unsigned char byte = (unsigned char)input.data[cursor + i];
        output[written + i] = (char)(byte >= 'A' && byte <= 'Z' ?
                                    byte + ('a' - 'A') : byte);
      }
      written += ascii;
      cursor += ascii;
      if (cursor == input.len) break;
    }
    unicode_scalar current;
    const unicode_fold_mapping *mapped;
    unicode_status status = unicode_utf8_next(input, &cursor, &current);
    if (status != SALTS_UNICODE_OK) return status;
    mapped = unicode_casefold_lookup(current.value);
    const size_t count = mapped != NULL ? mapped->length : 1u;
    for (size_t j = 0u; j < count; ++j) {
      const uint32_t scalar = mapped != NULL ?
          unicode_casefold_values[mapped->values_offset + j] : current.value;
      char bytes[4];
      const size_t length = unicode_fold_encode(scalar, bytes);
      memcpy(output + written, bytes, length);
      written += length;
    }
  }
  *out_length = written;
  return SALTS_UNICODE_OK;
}
