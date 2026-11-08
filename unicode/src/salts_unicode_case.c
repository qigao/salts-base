#include "unicode.h"

#include <string.h>

typedef struct salts_unicode_case_mapping {
  uint32_t scalar;
  uint32_t values_offset;
  uint8_t length;
} salts_unicode_case_mapping;

typedef struct salts_unicode_case_property_range {
  uint32_t first;
  uint32_t last;
  uint8_t flags;
} salts_unicode_case_property_range;

enum {
  SALTS_UNICODE_CASED = 1u << 0,
  SALTS_UNICODE_CASE_IGNORABLE = 1u << 1,
  SALTS_UNICODE_GREEK_CAPITAL_SIGMA = 0x03a3u,
  SALTS_UNICODE_GREEK_FINAL_SIGMA = 0x03c2u
};

#include "unicode_case_data.h"

static const salts_unicode_case_mapping *salts_unicode_case_lookup(
    const salts_unicode_case_mapping *entries, size_t count, uint32_t scalar) {
  size_t first = 0u;
  size_t last = count;
  while (first < last) {
    const size_t middle = first + (last - first) / 2u;
    if (scalar < entries[middle].scalar) last = middle;
    else if (scalar > entries[middle].scalar) first = middle + 1u;
    else return &entries[middle];
  }
  return NULL;
}

static uint8_t salts_unicode_case_flags(uint32_t scalar) {
  size_t first = 0u;
  size_t last = sizeof(salts_unicode_case_properties) / sizeof(salts_unicode_case_properties[0]);
  while (first < last) {
    const size_t middle = first + (last - first) / 2u;
    const salts_unicode_case_property_range *range = &salts_unicode_case_properties[middle];
    if (scalar < range->first) last = middle;
    else if (scalar > range->last) first = middle + 1u;
    else return range->flags;
  }
  return 0u;
}

static size_t salts_unicode_case_encode(uint32_t scalar, char output[4]) {
  if (scalar <= 0x7fu) { output[0] = (char)scalar; return 1u; }
  if (scalar <= 0x7ffu) {
    output[0] = (char)(0xc0u | (scalar >> 6u)); output[1] = (char)(0x80u | (scalar & 0x3fu));
    return 2u;
  }
  if (scalar <= 0xffffu) {
    output[0] = (char)(0xe0u | (scalar >> 12u)); output[1] = (char)(0x80u | ((scalar >> 6u) & 0x3fu));
    output[2] = (char)(0x80u | (scalar & 0x3fu)); return 3u;
  }
  output[0] = (char)(0xf0u | (scalar >> 18u)); output[1] = (char)(0x80u | ((scalar >> 12u) & 0x3fu));
  output[2] = (char)(0x80u | ((scalar >> 6u) & 0x3fu)); output[3] = (char)(0x80u | (scalar & 0x3fu));
  return 4u;
}

static int salts_unicode_case_has_cased_after(vstr input, size_t cursor) {
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) return 0;
    const uint8_t properties = salts_unicode_case_flags(scalar.value);
    if ((properties & SALTS_UNICODE_CASE_IGNORABLE) == 0u)
      return (properties & SALTS_UNICODE_CASED) != 0u;
  }
  return 0;
}

static salts_unicode_status salts_unicode_case_emit(uint32_t scalar,
    salts_unicode_case_write write, void *userdata) {
  char bytes[4];
  const size_t length = salts_unicode_case_encode(scalar, bytes);
  return write(bytes, length, userdata) == 0 ? SALTS_UNICODE_OK : SALTS_UNICODE_ERR_CALLBACK;
}

salts_unicode_status salts_unicode_case_transform(vstr input, salts_unicode_case_mode mode,
    salts_unicode_case_write write, void *userdata) {
  const salts_unicode_case_mapping *entries;
  const uint32_t *values;
  size_t entry_count;
  size_t cursor = 0u;
  int prior_cased = 0;
  if (write == NULL || (input.data == NULL && input.len != 0u) ||
      (mode != SALTS_UNICODE_CASE_UPPER && mode != SALTS_UNICODE_CASE_LOWER &&
       mode != SALTS_UNICODE_CASE_TITLE))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return SALTS_UNICODE_ERR_INVALID_UTF8;
  }
  if (mode == SALTS_UNICODE_CASE_UPPER) {
    entries = salts_unicode_upper_mappings;
    values = salts_unicode_upper_values;
    entry_count = sizeof(salts_unicode_upper_mappings) / sizeof(salts_unicode_upper_mappings[0]);
  } else if (mode == SALTS_UNICODE_CASE_LOWER) {
    entries = salts_unicode_lower_mappings;
    values = salts_unicode_lower_values;
    entry_count = sizeof(salts_unicode_lower_mappings) / sizeof(salts_unicode_lower_mappings[0]);
  } else {
    entries = salts_unicode_title_mappings;
    values = salts_unicode_title_values;
    entry_count = sizeof(salts_unicode_title_mappings) / sizeof(salts_unicode_title_mappings[0]);
  }
  cursor = 0u;
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    const salts_unicode_case_mapping *mapping;
    uint8_t properties;
    salts_unicode_status status = salts_unicode_utf8_next(input, &cursor, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    properties = salts_unicode_case_flags(scalar.value);
    if (mode == SALTS_UNICODE_CASE_LOWER && scalar.value == SALTS_UNICODE_GREEK_CAPITAL_SIGMA &&
        prior_cased && !salts_unicode_case_has_cased_after(input, cursor))
      status = salts_unicode_case_emit(SALTS_UNICODE_GREEK_FINAL_SIGMA, write, userdata);
    else {
      mapping = salts_unicode_case_lookup(entries, entry_count, scalar.value);
      if (mapping == NULL) status = salts_unicode_case_emit(scalar.value, write, userdata);
      else {
        status = SALTS_UNICODE_OK;
        for (size_t i = 0u; i < mapping->length && status == SALTS_UNICODE_OK; ++i)
          status = salts_unicode_case_emit(values[mapping->values_offset + i], write, userdata);
      }
    }
    if (status != SALTS_UNICODE_OK) return status;
    if ((properties & SALTS_UNICODE_CASE_IGNORABLE) == 0u)
      prior_cased = (properties & SALTS_UNICODE_CASED) != 0u;
  }
  return SALTS_UNICODE_OK;
}
