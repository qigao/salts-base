#include <unicode.h>
#include "unicode_word_data.h"

#include <stddef.h>
#include <stdint.h>

typedef struct unicode_word_props {
  unicode_word_break wb;
  uint8_t extended_pictographic;
} unicode_word_props;

typedef struct unicode_word_context {
  unicode_word_props raw_left;
  unicode_word_props significant_left;
  unicode_word_props significant_before_left;
  size_t significant_count;
  uint32_t regional_indicator_run;
} unicode_word_context;

static uint8_t unicode_word_lookup_range(
    const unicode_word_range *ranges, size_t count,
    uint32_t scalar, uint8_t default_value) {
  size_t lo = 0u;
  size_t hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const unicode_word_range *range = &ranges[mid];
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

static unicode_word_break unicode_word_lookup_wb(uint32_t scalar) {
  return (unicode_word_break)unicode_word_lookup_range(
      unicode_word_break_ranges, SALTS_UNICODE_WORD_BREAK_RANGES_COUNT,
      scalar, SALTS_UNICODE_WORD_OTHER);
}

static uint8_t unicode_word_lookup_extended_pictographic(uint32_t scalar) {
  return unicode_word_lookup_range(
      unicode_word_extended_pictographic_ranges,
      SALTS_UNICODE_WORD_EXTENDED_PICTOGRAPHIC_RANGES_COUNT, scalar, 0u);
}

static unicode_word_props unicode_word_properties(uint32_t scalar) {
  unicode_word_props result;
  result.wb = unicode_word_lookup_wb(scalar);
  result.extended_pictographic =
      unicode_word_lookup_extended_pictographic(scalar);
  return result;
}

unicode_status unicode_word_break_class(
    uint32_t scalar, unicode_word_break *out_class) {
  if (out_class == NULL || scalar > 0x10ffffu ||
      (scalar >= 0xd800u && scalar <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  *out_class = unicode_word_lookup_wb(scalar);
  return SALTS_UNICODE_OK;
}

static int unicode_word_is_ignore(unicode_word_break value) {
  return value == SALTS_UNICODE_WORD_EXTEND ||
         value == SALTS_UNICODE_WORD_FORMAT ||
         value == SALTS_UNICODE_WORD_ZWJ;
}

static int unicode_word_is_newline(unicode_word_break value) {
  return value == SALTS_UNICODE_WORD_CR ||
         value == SALTS_UNICODE_WORD_LF ||
         value == SALTS_UNICODE_WORD_NEWLINE;
}

static int unicode_word_is_ahletter(unicode_word_break value) {
  return value == SALTS_UNICODE_WORD_ALETTER ||
         value == SALTS_UNICODE_WORD_HEBREW_LETTER;
}

static int unicode_word_is_mid_letter(unicode_word_break value) {
  return value == SALTS_UNICODE_WORD_MID_LETTER ||
         value == SALTS_UNICODE_WORD_MID_NUM_LET ||
         value == SALTS_UNICODE_WORD_SINGLE_QUOTE;
}

static int unicode_word_is_mid_num(unicode_word_break value) {
  return value == SALTS_UNICODE_WORD_MID_NUM ||
         value == SALTS_UNICODE_WORD_MID_NUM_LET ||
         value == SALTS_UNICODE_WORD_SINGLE_QUOTE;
}

static int unicode_word_is_ah_numeric_katakana(
    unicode_word_break value) {
  return unicode_word_is_ahletter(value) ||
         value == SALTS_UNICODE_WORD_NUMERIC ||
         value == SALTS_UNICODE_WORD_KATAKANA;
}

static int unicode_word_is_extend_num_left(
    unicode_word_break value) {
  return unicode_word_is_ah_numeric_katakana(value) ||
         value == SALTS_UNICODE_WORD_EXTEND_NUM_LET;
}

static unicode_status unicode_word_validate_utf8_boundary(
    vstr input, size_t target) {
  size_t cursor = 0u;
  int found = target == 0u;
  unicode_scalar scalar;

  if ((input.data == NULL && input.len != 0u) || target > input.len)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  while (cursor < input.len) {
    const unicode_status status =
        unicode_utf8_next(input, &cursor, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    if (cursor == target) found = 1;
  }

  return found ? SALTS_UNICODE_OK : SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}

static void unicode_word_context_update(
    unicode_word_context *context,
    const unicode_word_props *props) {
  context->raw_left = *props;
  if (unicode_word_is_ignore(props->wb)) return;

  context->significant_before_left = context->significant_left;
  context->significant_left = *props;
  ++context->significant_count;

  if (props->wb == SALTS_UNICODE_WORD_REGIONAL_INDICATOR) {
    ++context->regional_indicator_run;
  } else {
    context->regional_indicator_run = 0u;
  }
}

static unicode_status unicode_word_left_context(
    vstr input, size_t offset, unicode_word_context *context) {
  size_t cursor = 0u;
  unicode_scalar scalar;

  *context = (unicode_word_context){0};
  while (cursor < offset) {
    unicode_status status;
    unicode_word_props props;
    const size_t before = cursor;

    status = unicode_utf8_next(input, &cursor, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    if (cursor > offset || cursor <= before)
      return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

    props = unicode_word_properties(scalar.value);
    unicode_word_context_update(context, &props);
  }

  return cursor == offset ? SALTS_UNICODE_OK
                          : SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}

static unicode_status unicode_word_next_significant(
    vstr input, size_t cursor, unicode_word_props *out_props,
    int *found) {
  unicode_scalar scalar;

  *found = 0;
  while (cursor < input.len) {
    unicode_status status =
        unicode_utf8_next(input, &cursor, &scalar);
    unicode_word_props props;
    if (status != SALTS_UNICODE_OK) return status;

    props = unicode_word_properties(scalar.value);
    if (!unicode_word_is_ignore(props.wb)) {
      *out_props = props;
      *found = 1;
      return SALTS_UNICODE_OK;
    }
  }

  return SALTS_UNICODE_OK;
}

static unicode_status unicode_word_break_at_unchecked(
    vstr input, size_t offset, int *out_break) {
  unicode_word_context context;
  unicode_scalar right_scalar;
  unicode_word_props right;
  unicode_word_props right_after = {0};
  unicode_status status;
  size_t right_end = offset;
  int has_right_after = 0;
  unicode_word_break raw_left;
  unicode_word_break left;
  unicode_word_break before_left;

  if (offset == 0u || offset == input.len) {
    *out_break = 1;
    return SALTS_UNICODE_OK;
  }

  status = unicode_word_left_context(input, offset, &context);
  if (status != SALTS_UNICODE_OK) return status;
  if (context.significant_count == 0u &&
      context.raw_left.wb == SALTS_UNICODE_WORD_OTHER &&
      context.raw_left.extended_pictographic == 0u)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  status = unicode_utf8_next(input, &right_end, &right_scalar);
  if (status != SALTS_UNICODE_OK) return status;
  right = unicode_word_properties(right_scalar.value);

  raw_left = context.raw_left.wb;

  /* WB3 */  
  if (raw_left == SALTS_UNICODE_WORD_CR && right.wb == SALTS_UNICODE_WORD_LF) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB3a / WB3b */
  if (unicode_word_is_newline(raw_left) ||
      unicode_word_is_newline(right.wb)) {
    *out_break = 1;
    return SALTS_UNICODE_OK;
  }

  /* WB3c */
  if (raw_left == SALTS_UNICODE_WORD_ZWJ && right.extended_pictographic) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB3d */
  if (raw_left == SALTS_UNICODE_WORD_WSEG_SPACE &&
      right.wb == SALTS_UNICODE_WORD_WSEG_SPACE) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB4. Newline exceptions above take precedence. */
  if (unicode_word_is_ignore(right.wb)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  if (context.significant_count == 0u) {
    *out_break = 1;
    return SALTS_UNICODE_OK;
  }

  left = context.significant_left.wb;
  before_left = context.significant_count >= 2u
                    ? context.significant_before_left.wb
                    : SALTS_UNICODE_WORD_OTHER;

  status = unicode_word_next_significant(
      input, right_end, &right_after, &has_right_after);
  if (status != SALTS_UNICODE_OK) return status;

  /* WB5 */
  if (unicode_word_is_ahletter(left) &&
      unicode_word_is_ahletter(right.wb)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB6 */
  if (unicode_word_is_ahletter(left) &&
      unicode_word_is_mid_letter(right.wb) && has_right_after &&
      unicode_word_is_ahletter(right_after.wb)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB7 */
  if (unicode_word_is_ahletter(right.wb) &&
      unicode_word_is_mid_letter(left) &&
      context.significant_count >= 2u &&
      unicode_word_is_ahletter(before_left)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB7a */
  if (left == SALTS_UNICODE_WORD_HEBREW_LETTER &&
      right.wb == SALTS_UNICODE_WORD_SINGLE_QUOTE) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB7b */
  if (left == SALTS_UNICODE_WORD_HEBREW_LETTER &&
      right.wb == SALTS_UNICODE_WORD_DOUBLE_QUOTE && has_right_after &&
      right_after.wb == SALTS_UNICODE_WORD_HEBREW_LETTER) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB7c */
  if (right.wb == SALTS_UNICODE_WORD_HEBREW_LETTER &&
      left == SALTS_UNICODE_WORD_DOUBLE_QUOTE &&
      context.significant_count >= 2u &&
      before_left == SALTS_UNICODE_WORD_HEBREW_LETTER) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB8 */
  if (left == SALTS_UNICODE_WORD_NUMERIC &&
      right.wb == SALTS_UNICODE_WORD_NUMERIC) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB9 */
  if (unicode_word_is_ahletter(left) &&
      right.wb == SALTS_UNICODE_WORD_NUMERIC) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB10 */
  if (left == SALTS_UNICODE_WORD_NUMERIC &&
      unicode_word_is_ahletter(right.wb)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB11 */
  if (right.wb == SALTS_UNICODE_WORD_NUMERIC &&
      unicode_word_is_mid_num(left) &&
      context.significant_count >= 2u &&
      before_left == SALTS_UNICODE_WORD_NUMERIC) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB12 */
  if (left == SALTS_UNICODE_WORD_NUMERIC &&
      unicode_word_is_mid_num(right.wb) && has_right_after &&
      right_after.wb == SALTS_UNICODE_WORD_NUMERIC) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB13 */
  if (left == SALTS_UNICODE_WORD_KATAKANA &&
      right.wb == SALTS_UNICODE_WORD_KATAKANA) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB13a */
  if (unicode_word_is_extend_num_left(left) &&
      right.wb == SALTS_UNICODE_WORD_EXTEND_NUM_LET) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB13b */
  if (left == SALTS_UNICODE_WORD_EXTEND_NUM_LET &&
      unicode_word_is_ah_numeric_katakana(right.wb)) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB15 / WB16 */
  if (left == SALTS_UNICODE_WORD_REGIONAL_INDICATOR &&
      right.wb == SALTS_UNICODE_WORD_REGIONAL_INDICATOR &&
      (context.regional_indicator_run & 1u) != 0u) {
    *out_break = 0;
    return SALTS_UNICODE_OK;
  }

  /* WB999 */
  *out_break = 1;
  return SALTS_UNICODE_OK;
}

static unicode_status unicode_word_next_unchecked(
    vstr input, size_t *cursor, vstr *segment) {
  const size_t start = *cursor;
  size_t scan = start;
  unicode_scalar scalar;
  unicode_status status;

  if (start == input.len) return SALTS_UNICODE_END;

  status = unicode_utf8_next(input, &scan, &scalar);
  if (status != SALTS_UNICODE_OK) return status;

  while (scan < input.len) {
    int is_break = 0;
    size_t next;

    status = unicode_word_break_at_unchecked(input, scan, &is_break);
    if (status != SALTS_UNICODE_OK) return status;
    if (is_break) break;

    next = scan;
    status = unicode_utf8_next(input, &next, &scalar);
    if (status != SALTS_UNICODE_OK) return status;
    scan = next;
  }

  *segment = vstr_from_buf(input.data + start, scan - start);
  *cursor = scan;
  return SALTS_UNICODE_OK;
}

unicode_status unicode_word_next(
    vstr input, size_t *cursor, vstr *segment) {
  unicode_status status;
  size_t candidate;
  vstr result;
  int is_break = 0;

  if (cursor == NULL || segment == NULL)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  candidate = *cursor;
  status = unicode_word_validate_utf8_boundary(input, candidate);
  if (status != SALTS_UNICODE_OK) return status;
  if (candidate == input.len) return SALTS_UNICODE_END;

  status = unicode_word_break_at_unchecked(input, candidate, &is_break);
  if (status != SALTS_UNICODE_OK) return status;
  if (!is_break) return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  result = (vstr){0};
  status = unicode_word_next_unchecked(input, &candidate, &result);
  if (status != SALTS_UNICODE_OK) return status;

  *segment = result;
  *cursor = candidate;
  return SALTS_UNICODE_OK;
}

unicode_status unicode_word_prev(
    vstr input, size_t *cursor, vstr *segment) {
  unicode_status status;
  const size_t target = cursor == NULL ? 0u : *cursor;
  size_t scan = 0u;
  int is_break = 0;

  if (cursor == NULL || segment == NULL)
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  status = unicode_word_validate_utf8_boundary(input, target);
  if (status != SALTS_UNICODE_OK) return status;
  if (target == 0u) return SALTS_UNICODE_END;

  status = unicode_word_break_at_unchecked(input, target, &is_break);
  if (status != SALTS_UNICODE_OK) return status;
  if (!is_break) return SALTS_UNICODE_ERR_INVALID_ARGUMENT;

  while (scan < target) {
    const size_t start = scan;
    size_t next = scan;
    vstr candidate = {0};

    status = unicode_word_next_unchecked(input, &next, &candidate);
    if (status != SALTS_UNICODE_OK) return status;
    if (next > target) return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    if (next == target) {
      *segment = candidate;
      *cursor = start;
      return SALTS_UNICODE_OK;
    }
    scan = next;
  }

  return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
}
