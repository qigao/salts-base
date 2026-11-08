#include "unicode.h"
#include "unicode_grapheme_data.h"

#include <stddef.h>
#include <stdint.h>

typedef struct salts_unicode_grapheme_props {
  salts_unicode_grapheme_break gcb;
  uint8_t incb;
  uint8_t extended_pictographic;
} salts_unicode_grapheme_props;

typedef struct salts_unicode_grapheme_state {
  uint32_t regional_indicator_run;
  uint8_t extended_pictographic_state;
  uint8_t incb_state;
} salts_unicode_grapheme_state;

enum {
  SALTS_UNICODE_EP_NONE = 0,
  SALTS_UNICODE_EP_EXTEND_CHAIN = 1,
  SALTS_UNICODE_EP_ZWJ = 2,
  SALTS_UNICODE_INCB_STATE_NONE = 0,
  SALTS_UNICODE_INCB_STATE_CONSONANT = 1,
  SALTS_UNICODE_INCB_STATE_LINKER = 2
};

static uint8_t salts_unicode_lookup_range(
    const salts_unicode_grapheme_range *ranges, size_t count,
    uint32_t scalar, uint8_t default_value) {
  size_t lo = 0u;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const salts_unicode_grapheme_range *range = &ranges[mid];
    if (scalar < range->first) {
      hi = mid;
    } else if (scalar > range->last) {
      lo = mid + 1u;
    } else {
      return range->value;
    }
  }
  return default_value;
}

static salts_unicode_grapheme_break
salts_unicode_lookup_gcb(uint32_t scalar) {
  return (salts_unicode_grapheme_break)salts_unicode_lookup_range(
      salts_unicode_gcb_ranges, SALTS_UNICODE_GCB_RANGES_COUNT, scalar,
      SALTS_UNICODE_GRAPHEME_OTHER);
}

static uint8_t salts_unicode_lookup_incb(uint32_t scalar) {
  return salts_unicode_lookup_range(
      salts_unicode_incb_ranges, SALTS_UNICODE_INCB_RANGES_COUNT, scalar,
      SALTS_UNICODE_INCB_NONE);
}

static uint8_t salts_unicode_lookup_extended_pictographic(uint32_t scalar) {
  return salts_unicode_lookup_range(
      salts_unicode_extended_pictographic_ranges,
      SALTS_UNICODE_EXTENDED_PICTOGRAPHIC_RANGES_COUNT, scalar, 0u);
}

static salts_unicode_grapheme_props
salts_unicode_grapheme_properties(uint32_t scalar) {
  salts_unicode_grapheme_props result;
  result.gcb = salts_unicode_lookup_gcb(scalar);
  result.incb = salts_unicode_lookup_incb(scalar);
  result.extended_pictographic =
      salts_unicode_lookup_extended_pictographic(scalar);
  return result;
}

salts_unicode_status salts_unicode_grapheme_break_class(
    uint32_t scalar, salts_unicode_grapheme_break *out_class) {
  if (out_class == NULL || scalar > 0x10ffffu ||
      (scalar >= 0xd800u && scalar <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  *out_class = salts_unicode_lookup_gcb(scalar);
  return SALTS_UNICODE_OK;
}


salts_unicode_status salts_unicode_is_extended_pictographic(
    uint32_t scalar, int *result) {
  if (result == NULL || scalar > 0x10ffffu ||
      (scalar >= 0xd800u && scalar <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  *result = salts_unicode_lookup_extended_pictographic(scalar) != 0u;
  return SALTS_UNICODE_OK;
}

static int salts_unicode_is_control(salts_unicode_grapheme_break value) {
  return value == SALTS_UNICODE_GRAPHEME_CONTROL ||
         value == SALTS_UNICODE_GRAPHEME_CR ||
         value == SALTS_UNICODE_GRAPHEME_LF;
}

static void salts_unicode_grapheme_state_update(
    salts_unicode_grapheme_state *state,
    const salts_unicode_grapheme_props *props) {
  if (props->gcb == SALTS_UNICODE_GRAPHEME_REGIONAL_INDICATOR) {
    ++state->regional_indicator_run;
  } else {
    state->regional_indicator_run = 0u;
  }

  if (props->extended_pictographic) {
    state->extended_pictographic_state = SALTS_UNICODE_EP_EXTEND_CHAIN;
  } else if (props->gcb == SALTS_UNICODE_GRAPHEME_EXTEND &&
             state->extended_pictographic_state ==
                 SALTS_UNICODE_EP_EXTEND_CHAIN) {
    /* Extended_Pictographic Extend* */
  } else if (props->gcb == SALTS_UNICODE_GRAPHEME_ZWJ &&
             state->extended_pictographic_state ==
                 SALTS_UNICODE_EP_EXTEND_CHAIN) {
    state->extended_pictographic_state = SALTS_UNICODE_EP_ZWJ;
  } else {
    state->extended_pictographic_state = SALTS_UNICODE_EP_NONE;
  }

  if (props->incb == SALTS_UNICODE_INCB_CONSONANT) {
    state->incb_state = SALTS_UNICODE_INCB_STATE_CONSONANT;
  } else if (props->incb == SALTS_UNICODE_INCB_LINKER &&
             state->incb_state != SALTS_UNICODE_INCB_STATE_NONE) {
    state->incb_state = SALTS_UNICODE_INCB_STATE_LINKER;
  } else if (props->incb == SALTS_UNICODE_INCB_EXTEND &&
             state->incb_state != SALTS_UNICODE_INCB_STATE_NONE) {
    /* Preserve whether a linker has already occurred. */
  } else {
    state->incb_state = SALTS_UNICODE_INCB_STATE_NONE;
  }
}

static int salts_unicode_grapheme_break_before(
    salts_unicode_grapheme_break previous,
    const salts_unicode_grapheme_props *current,
    const salts_unicode_grapheme_state *state) {
  const salts_unicode_grapheme_break next = current->gcb;

  /* GB3 */
  if (previous == SALTS_UNICODE_GRAPHEME_CR &&
      next == SALTS_UNICODE_GRAPHEME_LF)
    return 0;

  /* GB4 / GB5 */
  if (salts_unicode_is_control(previous) || salts_unicode_is_control(next))
    return 1;

  /* GB6 */
  if (previous == SALTS_UNICODE_GRAPHEME_L &&
      (next == SALTS_UNICODE_GRAPHEME_L ||
       next == SALTS_UNICODE_GRAPHEME_V ||
       next == SALTS_UNICODE_GRAPHEME_LV ||
       next == SALTS_UNICODE_GRAPHEME_LVT))
    return 0;

  /* GB7 */
  if ((previous == SALTS_UNICODE_GRAPHEME_LV ||
       previous == SALTS_UNICODE_GRAPHEME_V) &&
      (next == SALTS_UNICODE_GRAPHEME_V ||
       next == SALTS_UNICODE_GRAPHEME_T))
    return 0;

  /* GB8 */
  if ((previous == SALTS_UNICODE_GRAPHEME_LVT ||
       previous == SALTS_UNICODE_GRAPHEME_T) &&
      next == SALTS_UNICODE_GRAPHEME_T)
    return 0;

  /* GB9 */
  if (next == SALTS_UNICODE_GRAPHEME_EXTEND ||
      next == SALTS_UNICODE_GRAPHEME_ZWJ)
    return 0;

  /* GB9a */
  if (next == SALTS_UNICODE_GRAPHEME_SPACING_MARK)
    return 0;

  /* GB9b */
  if (previous == SALTS_UNICODE_GRAPHEME_PREPEND)
    return 0;

  /* GB9c, Unicode 17 / UAX #29 revision 47. */
  if (current->incb == SALTS_UNICODE_INCB_CONSONANT &&
      state->incb_state == SALTS_UNICODE_INCB_STATE_LINKER)
    return 0;

  /* GB11 */
  if (current->extended_pictographic &&
      state->extended_pictographic_state == SALTS_UNICODE_EP_ZWJ)
    return 0;

  /* GB12 / GB13 */
  if (previous == SALTS_UNICODE_GRAPHEME_REGIONAL_INDICATOR &&
      next == SALTS_UNICODE_GRAPHEME_REGIONAL_INDICATOR &&
      (state->regional_indicator_run & 1u) != 0u)
    return 0;

  /* GB999 */
  return 1;
}

static salts_unicode_status salts_unicode_validate_utf8_boundary(
    vstr input, size_t target) {
  size_t cursor = 0u;
  int found = target == 0u;
  salts_unicode_scalar scalar;

  if ((input.data == NULL && input.len != 0u) || target > input.len)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  while (cursor < input.len) {
    const salts_unicode_status status =
        salts_unicode_utf8_next(input, &cursor, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    if (cursor == target) found = 1;
  }

  return found ? SALTS_UNICODE_OK : SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}

static salts_unicode_status salts_unicode_grapheme_next_unchecked(
    vstr input, size_t *cursor, vstr *cluster) {
  const size_t start = *cursor;
  size_t scan = start;
  salts_unicode_scalar scalar;
  salts_unicode_grapheme_props previous_props;
  salts_unicode_grapheme_state state = {0u, 0u, 0u};
  salts_unicode_status status;

  if (start == input.len) return SALTS_UNICODE_END;

  status = salts_unicode_utf8_next(input, &scan, &scalar);
  if (status != SALTS_UNICODE_OK) return status;
  previous_props = salts_unicode_grapheme_properties(scalar.value);
  salts_unicode_grapheme_state_update(&state, &previous_props);

  while (scan < input.len) {
    size_t candidate = scan;
    salts_unicode_scalar current_scalar;
    salts_unicode_grapheme_props current_props;

    status = salts_unicode_utf8_next(input, &candidate, &current_scalar);
    if (status != SALTS_UNICODE_OK) return status;
    current_props = salts_unicode_grapheme_properties(current_scalar.value);

    if (salts_unicode_grapheme_break_before(previous_props.gcb,
                                             &current_props, &state))
      break;

    scan = candidate;
    previous_props = current_props;
    salts_unicode_grapheme_state_update(&state, &current_props);
  }

  *cluster = vstr_from_buf(input.data + start, scan - start);
  *cursor = scan;
  return SALTS_UNICODE_OK;
}

salts_unicode_status salts_unicode_grapheme_next(
    vstr input, size_t *cursor, vstr *cluster) {
  salts_unicode_status status;
  size_t candidate;
  vstr result;

  if (cursor == NULL || cluster == NULL)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  candidate = *cursor;
  status = salts_unicode_validate_utf8_boundary(input, candidate);
  if (status != SALTS_UNICODE_OK) return status;
  if (candidate == input.len) return SALTS_UNICODE_END;

  result = (vstr){0};
  status = salts_unicode_grapheme_next_unchecked(input, &candidate, &result);
  if (status != SALTS_UNICODE_OK) return status;

  *cluster = result;
  *cursor = candidate;
  return SALTS_UNICODE_OK;
}

salts_unicode_status salts_unicode_grapheme_prev(
    vstr input, size_t *cursor, vstr *cluster) {
  salts_unicode_status status;
  const size_t target = cursor == NULL ? 0u : *cursor;
  size_t scan = 0u;

  if (cursor == NULL || cluster == NULL)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  status = salts_unicode_validate_utf8_boundary(input, target);
  if (status != SALTS_UNICODE_OK) return status;
  if (target == 0u) return SALTS_UNICODE_END;

  while (scan < target) {
    const size_t start = scan;
    size_t next = scan;
    vstr candidate = {0};

    status = salts_unicode_grapheme_next_unchecked(input, &next, &candidate);
    if (status != SALTS_UNICODE_OK) return status;
    if (next > target) return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    if (next == target) {
      *cluster = candidate;
      *cursor = start;
      return SALTS_UNICODE_OK;
    }
    scan = next;
  }

  return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}
