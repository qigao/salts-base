#include "unicode.h"
#include "unicode_emoji.h"
#include "unicode_emoji_data.h"

#include <stddef.h>
#include <stdint.h>

uint32_t salts_unicode_emoji_properties(uint32_t scalar) {
  size_t lo = 0u;
  size_t hi = SALTS_UNICODE_EMOJI_RANGES_COUNT;

  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const salts_unicode_emoji_range *range = &salts_unicode_emoji_ranges[mid];
    if (scalar < range->first) {
      hi = mid;
    } else if (scalar > range->last) {
      lo = mid + 1u;
    } else {
      return range->flags;
    }
  }
  return SALTS_UNICODE_PROPERTY_NONE;
}
