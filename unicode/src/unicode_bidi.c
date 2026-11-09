#include <unicode.h>
#include "unicode_bidi_data.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t unicode_bidi_lookup(uint32_t scalar) {
  size_t lo = 0u;
  size_t hi = SALTS_UNICODE_BIDI_RANGES_COUNT;

  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const unicode_bidi_range *range = &unicode_bidi_ranges[mid];
    if (scalar < range->first) {
      hi = mid;
    } else if (scalar > range->last) {
      lo = mid + 1u;
    } else {
      return range->value;
    }
  }

  return SALTS_UNICODE_BIDI_L;
}

unicode_status unicode_bidi_class_of(
    uint32_t scalar, unicode_bidi_class *out_class) {
  if (out_class == NULL || scalar > 0x10ffffu ||
      (scalar >= 0xd800u && scalar <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  *out_class = (unicode_bidi_class)unicode_bidi_lookup(scalar);
  return SALTS_UNICODE_OK;
}

unicode_status unicode_bidi_paragraph_level(
    vstr input, uint8_t *out_level) {
  size_t cursor = 0u;
  size_t isolate_depth = 0u;
  int have_level = 0;
  uint8_t level = 0u;
  unicode_scalar scalar;

  if (out_level == NULL || (input.data == NULL && input.len != 0u))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  while (cursor < input.len) {
    unicode_status status =
        unicode_utf8_next(input, &cursor, &scalar);
    unicode_bidi_class bidi;

    if (status != SALTS_UNICODE_OK) return status;
    bidi = (unicode_bidi_class)unicode_bidi_lookup(scalar.value);

    if (bidi == SALTS_UNICODE_BIDI_LRI ||
        bidi == SALTS_UNICODE_BIDI_RLI ||
        bidi == SALTS_UNICODE_BIDI_FSI) {
      ++isolate_depth;
      continue;
    }

    if (bidi == SALTS_UNICODE_BIDI_PDI) {
      if (isolate_depth != 0u) --isolate_depth;
      continue;
    }

    if (isolate_depth != 0u || have_level) continue;

    if (bidi == SALTS_UNICODE_BIDI_L) {
      level = 0u;
      have_level = 1;
    } else if (bidi == SALTS_UNICODE_BIDI_R ||
               bidi == SALTS_UNICODE_BIDI_AL) {
      level = 1u;
      have_level = 1;
    }
  }

  *out_level = level;
  return SALTS_UNICODE_OK;
}
