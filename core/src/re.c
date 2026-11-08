/* qigao/salts original utils/src/re.c Git blob 33468540fe02a2adcc7fd7a5303eb990d1521a31.
 * Original parser, matcher, budgets and caller ownership retained; only
 * the internal first-byte accelerator maps to intrinsic SSE2/NEON.
 */
/**
 * @file re.c
 * @brief Bounded byte-oriented regex engine backing the public re.h API.
 *
 * Single implementation owned by Salts (utils/src/re.c); JSONPath and
 * TBE data_bind consume it from Salts::Core.
 *
 * re_match_borrowed() accelerates patterns whose first atom is a mandatory
 * single literal byte: candidate start positions are located with a native platform SIMD
 * 16-byte first-byte scan (re_scan.h) instead of probing every offset. The
 * scalar loop remains for all other patterns, so matching results are
 * unchanged; the only observable difference is that large no-match texts
 * finish with RE_STATUS_NO_MATCH instead of exhausting the step budget first
 * (JSONPath treats both as "no match").
 */

#include "re.h"
#include "re_scan.h"

#include <limits.h>
#include <string.h>

#if !defined(RE_MALLOC) || !defined(RE_FREE)
  #include <stdlib.h>
#endif

#ifndef RE_MALLOC
  #define RE_MALLOC(size) malloc(size)
#endif

#ifndef RE_FREE
  #define RE_FREE(pointer) free(pointer)
#endif

struct regex_t {
  char *pattern;
  size_t pattern_len;
};

typedef struct re_exec {
  const char *pattern;
  const char *text;
  size_t text_len;
  re_limits_t limits;
  uint64_t steps;
  size_t workspace_used;
  uint32_t depth;
  re_status_t status;
} re_exec_t;

static int re_find_char_class_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_find_group_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_find_branch_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_validate_expr(re_exec_t *exec, size_t start, size_t end);
static int re_validate_sequence(re_exec_t *exec, size_t start, size_t end);
static int re_atom_end_at(re_exec_t *exec, size_t start, size_t end, int validate_nested,
                          size_t *out);
static int re_group_prefix_valid(const char *pattern, size_t start, size_t end);
static size_t re_group_content_start(const char *pattern, size_t start, size_t end);
static int re_quantifier_at(re_exec_t *exec, size_t i, size_t end, size_t *out_min,
                            size_t *out_max, int *out_lazy, size_t *out_consumed,
                            int *out_invalid);
static int re_match_expr(re_exec_t *exec, size_t start, size_t end, size_t pos, size_t *out_pos);
static int re_match_sequence(re_exec_t *exec, size_t start, size_t end, size_t pos,
                             size_t *out_pos);
static int re_match_atom_once(re_exec_t *exec, size_t start, size_t atom_end, size_t pos,
                              size_t *out_pos);

static size_t re_cstr_len(const char *text) {
  size_t len = 0;
  if (text == NULL) return 0;
  while (text[len] != '\0')
    ++len;
  return len;
}

static int re_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int re_is_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static int re_is_whitespace(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\f' || c == '\r' || c == '\n' || c == '\v';
}

static int re_is_alphanum(unsigned char c) { return c == '_' || re_is_alpha(c) || re_is_digit(c); }

static int re_match_dot(unsigned char c) {
#if defined(RE_DOT_MATCHES_NEWLINE) && (RE_DOT_MATCHES_NEWLINE == 1)
  (void)c;
  return 1;
#else
  return c != '\n' && c != '\r';
#endif
}

static int re_match_metachar(unsigned char c, unsigned char metachar) {
  switch (metachar) {
  case 'd':
    return re_is_digit(c);
  case 'D':
    return !re_is_digit(c);
  case 'w':
    return re_is_alphanum(c);
  case 'W':
    return !re_is_alphanum(c);
  case 's':
    return re_is_whitespace(c);
  case 'S':
    return !re_is_whitespace(c);
  default:
    return c == metachar;
  }
}

static int re_is_quantifier(char c) { return c == '*' || c == '+' || c == '?'; }

static void re_fail(re_exec_t *exec, re_status_t status) {
  if (exec->status == RE_STATUS_OK) exec->status = status;
}

static int re_step(re_exec_t *exec) {
  if (exec->status != RE_STATUS_OK) return 0;
  if (exec->steps >= exec->limits.max_steps) {
    re_fail(exec, RE_STATUS_STEP_LIMIT);
    return 0;
  }
  ++exec->steps;
  return 1;
}

static int re_enter(re_exec_t *exec) {
  if (!re_step(exec)) return 0;
  if (exec->depth >= exec->limits.max_depth) {
    re_fail(exec, RE_STATUS_DEPTH_LIMIT);
    return 0;
  }
  ++exec->depth;
  return 1;
}

static void re_leave(re_exec_t *exec) {
  if (exec->depth != 0) --exec->depth;
}

static void *re_workspace_alloc(re_exec_t *exec, size_t size) {
  void *allocation;
  if (size > exec->limits.max_workspace_bytes ||
      exec->workspace_used > exec->limits.max_workspace_bytes - size) {
    re_fail(exec, RE_STATUS_WORKSPACE_LIMIT);
    return NULL;
  }
  allocation = RE_MALLOC(size);
  if (allocation == NULL) {
    re_fail(exec, RE_STATUS_NO_MEMORY);
    return NULL;
  }
  exec->workspace_used += size;
  return allocation;
}

static void re_workspace_free(re_exec_t *exec, void *allocation, size_t size) {
  if (allocation == NULL) return;
  RE_FREE(allocation);
  exec->workspace_used -= size;
}

re_limits_t re_limits_default(void) {
  const re_limits_t limits = RE_LIMITS_INIT;
  return limits;
}

static re_status_t re_normalize_limits(const re_limits_t *input, re_limits_t *out) {
  if (out == NULL) return RE_STATUS_INVALID_ARGUMENT;
  if (input == NULL) {
    *out = re_limits_default();
    return RE_STATUS_OK;
  }
  if (input->struct_size < sizeof(*input) || input->max_pattern_bytes == 0 ||
      input->max_text_bytes == 0 || input->max_depth == 0 || input->max_workspace_bytes == 0 ||
      input->max_steps == 0)
    return RE_STATUS_INVALID_ARGUMENT;
  *out = *input;
  return RE_STATUS_OK;
}

static int re_find_char_class_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t i = start + 1;
  if (i < end && exec->pattern[i] == '^') ++i;
  while (i < end) {
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      i += 2;
      continue;
    }
    if (exec->pattern[i] == ']') {
      *out = i;
      return 1;
    }
    ++i;
  }
  return 0;
}

static int re_find_group_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t depth = 1;
  size_t i;
  for (i = start + 1; i < end; ++i) {
    size_t class_end;
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      ++i;
      continue;
    }
    if (exec->pattern[i] == '[') {
      if (!re_find_char_class_end(exec, i, end, &class_end)) return 0;
      i = class_end;
      continue;
    }
    if (exec->pattern[i] == '(') {
      ++depth;
    } else if (exec->pattern[i] == ')') {
      --depth;
      if (depth == 0) {
        *out = i;
        return 1;
      }
    }
  }
  return 0;
}

static int re_find_branch_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t depth = 0;
  size_t i;
  for (i = start; i < end; ++i) {
    size_t class_end;
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      ++i;
      continue;
    }
    if (exec->pattern[i] == '[') {
      if (!re_find_char_class_end(exec, i, end, &class_end)) return 0;
      i = class_end;
      continue;
    }
    if (exec->pattern[i] == '(') {
      ++depth;
    } else if (exec->pattern[i] == ')') {
      if (depth == 0) return 0;
      --depth;
    } else if (exec->pattern[i] == '|' && depth == 0) {
      *out = i;
      return 1;
    }
  }
  *out = end;
  return 1;
}

static int re_atom_end_at(re_exec_t *exec, size_t start, size_t end, int validate_nested,
                          size_t *out) {
  size_t nested_end;
  if (!re_step(exec) || start >= end) return 0;
  if (exec->pattern[start] == '[') {
    if (!re_find_char_class_end(exec, start, end, &nested_end)) return 0;
    *out = nested_end + 1;
    return 1;
  }
  if (exec->pattern[start] == '(') {
    if (!re_group_prefix_valid(exec->pattern, start, end)) return 0;
    if (!re_find_group_end(exec, start, end, &nested_end)) return 0;
    if (validate_nested &&
        !re_validate_expr(exec, re_group_content_start(exec->pattern, start, end),
                          nested_end))
      return 0;
    *out = nested_end + 1;
    return 1;
  }
  if (exec->pattern[start] == '\\') {
    if (start + 1 >= end) return 0;
    *out = start + 2;
    return 1;
  }
  if (exec->pattern[start] == '|' || exec->pattern[start] == ')' ||
      re_is_quantifier(exec->pattern[start]))
    return 0;
  *out = start + 1;
  return 1;
}

static int re_validate_sequence(re_exec_t *exec, size_t start, size_t end) {
  size_t i = start;
  int valid = 0;
  if (!re_enter(exec)) return 0;
  while (i < end) {
    size_t atom_end;
    size_t q_min;
    size_t q_max;
    size_t q_consumed;
    int q_lazy;
    int q_invalid = 0;
    if (!re_atom_end_at(exec, i, end, 1, &atom_end)) goto cleanup;
    if (atom_end < end &&
        re_quantifier_at(exec, atom_end, end, &q_min, &q_max, &q_lazy, &q_consumed,
                         &q_invalid)) {
      if (exec->pattern[i] == '^' || exec->pattern[i] == '$') goto cleanup;
      atom_end += q_consumed;
    } else if (q_invalid) {
      goto cleanup;
    }
    i = atom_end;
  }
  valid = 1;

cleanup:
  re_leave(exec);
  return valid;
}

static int re_validate_expr(re_exec_t *exec, size_t start, size_t end) {
  size_t branch_start = start;
  int valid = 0;
  if (!re_enter(exec)) return 0;
  while (branch_start <= end) {
    size_t branch_end;
    if (!re_find_branch_end(exec, branch_start, end, &branch_end)) goto cleanup;
    if (!re_validate_sequence(exec, branch_start, branch_end)) goto cleanup;
    if (branch_end == end) {
      valid = 1;
      goto cleanup;
    }
    branch_start = branch_end + 1;
  }
  valid = 1;

cleanup:
  re_leave(exec);
  return valid;
}

static re_status_t re_validate_with_limits(const char *pattern, size_t pattern_len,
                                           const re_limits_t *limits) {
  re_exec_t exec;
  if (pattern == NULL && pattern_len != 0) return RE_STATUS_INVALID_ARGUMENT;
  if (pattern_len > limits->max_pattern_bytes) return RE_STATUS_PATTERN_LIMIT;
  exec.pattern = pattern;
  exec.text = NULL;
  exec.text_len = 0;
  exec.limits = *limits;
  exec.steps = 0;
  exec.workspace_used = 0;
  exec.depth = 0;
  exec.status = RE_STATUS_OK;
  if (!re_validate_expr(&exec, 0, pattern_len))
    return exec.status == RE_STATUS_OK ? RE_STATUS_INVALID_PATTERN : exec.status;
  return exec.status;
}

re_status_t re_validate_n(const char *pattern, size_t pattern_len, const re_limits_t *limits) {
  re_limits_t normalized;
  re_status_t status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_validate_with_limits(pattern, pattern_len, &normalized);
}

re_status_t re_compile_n(const char *pattern, size_t pattern_len, const re_limits_t *limits,
                         re_t *out_pattern) {
  struct regex_t *compiled = NULL;
  re_limits_t normalized;
  re_status_t status;
  size_t i;
  if (out_pattern == NULL) return RE_STATUS_INVALID_ARGUMENT;
  *out_pattern = NULL;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  status = re_validate_with_limits(pattern, pattern_len, &normalized);
  if (status != RE_STATUS_OK) return status;
  compiled = (struct regex_t *)RE_MALLOC(sizeof(*compiled));
  if (compiled == NULL) return RE_STATUS_NO_MEMORY;
  compiled->pattern = (char *)RE_MALLOC(pattern_len + 1);
  if (compiled->pattern == NULL) {
    RE_FREE(compiled);
    return RE_STATUS_NO_MEMORY;
  }
  for (i = 0; i < pattern_len; ++i)
    compiled->pattern[i] = pattern[i];
  compiled->pattern[pattern_len] = '\0';
  compiled->pattern_len = pattern_len;
  *out_pattern = compiled;
  return RE_STATUS_OK;
}

void re_destroy(re_t pattern) {
  if (pattern == NULL) return;
  RE_FREE(pattern->pattern);
  pattern->pattern = NULL;
  pattern->pattern_len = 0;
  RE_FREE(pattern);
}

static int re_match_char_class(re_exec_t *exec, unsigned char c, size_t start, size_t len) {
  size_t i = 0;
  while (i < len) {
    unsigned char item;
    if (!re_step(exec)) return 0;
    item = (unsigned char)exec->pattern[start + i];
    if (i + 2 < len && item != '-' && exec->pattern[start + i + 1] == '-' &&
        (unsigned char)exec->pattern[start + i + 2] != '-' && c >= item &&
        c <= (unsigned char)exec->pattern[start + i + 2])
      return 1;
    if (item == '\\') {
      if (i + 1 < len && re_match_metachar(c, (unsigned char)exec->pattern[start + i + 1]))
        return 1;
      i += 2;
      continue;
    }
    if (c == item && (c != '-' || i == 0 || i == len - 1)) return 1;
    ++i;
  }
  return 0;
}

/* Group prefixes: plain ( ... ), non-capturing (?: ... ), positive lookahead
 * (?= ... ) and negative lookahead (?! ... ). A '(' immediately followed by
 * '?' with any other third byte is invalid. */
static int re_group_prefix_valid(const char *pattern, size_t start, size_t end) {
  if (start + 1 < end && pattern[start + 1] == '?')
    return start + 2 < end &&
           (pattern[start + 2] == ':' || pattern[start + 2] == '=' ||
            pattern[start + 2] == '!');
  return 1;
}

static size_t re_group_content_start(const char *pattern, size_t start, size_t end) {
  if (start + 1 < end && pattern[start + 1] == '?') return start + 3;
  return start + 1;
}

/* Parses the quantifier starting at pattern[i]. Fills min/max (max == SIZE_MAX
 * means unbounded, bounded by the text length at match time), the lazy flag,
 * and the number of bytes consumed. Returns 1 for a real quantifier
 * (* + ? {m} {m,} {m,n}, each optionally followed by '?'); returns 0 when
 * pattern[i] is not a quantifier (a literal '{' that is not a valid interval
 * is treated as a literal and returns 0 with *out_invalid unchanged).
 * An interval that starts with a digit but is structurally invalid (unclosed,
 * overflowing, or m > n) sets *out_invalid. */
static int re_quantifier_at(re_exec_t *exec, size_t i, size_t end, size_t *out_min,
                            size_t *out_max, int *out_lazy, size_t *out_consumed,
                            int *out_invalid) {
  char c;
  if (i >= end) return 0;
  c = exec->pattern[i];
  if (c == '*' || c == '+' || c == '?') {
    *out_min = (c == '+') ? 1 : 0;
    *out_max = (size_t)-1;
    *out_lazy = 0;
    *out_consumed = 1;
    if (i + 1 < end && exec->pattern[i + 1] == '?') {
      *out_lazy = 1;
      *out_consumed = 2;
    }
    return 1;
  }
  if (c == '{') {
    size_t min = 0;
    size_t max = 0;
    int has_max = 0;
    int max_specified = 0;
    size_t p = i + 1;
    if (p >= end || !re_is_digit((unsigned char)exec->pattern[p])) return 0;
    while (p < end && re_is_digit((unsigned char)exec->pattern[p])) {
      if (!re_step(exec)) return 0;
      if (min > (((size_t)-1) - 9U) / 10U) {
        *out_invalid = 1;
        return 0;
      }
      min = min * 10U + (size_t)(exec->pattern[p] - '0');
      ++p;
    }
    if (p < end && exec->pattern[p] == ',') {
      ++p;
      has_max = 1;
      while (p < end && re_is_digit((unsigned char)exec->pattern[p])) {
        if (!re_step(exec)) return 0;
        if (max > (((size_t)-1) - 9U) / 10U) {
          *out_invalid = 1;
          return 0;
        }
        max = max * 10U + (size_t)(exec->pattern[p] - '0');
        ++p;
        max_specified = 1;
      }
    }
    if (p >= end || exec->pattern[p] != '}') {
      *out_invalid = 1;
      return 0;
    }
    ++p;
    if (has_max && max_specified && max < min) {
      *out_invalid = 1;
      return 0;
    }
    *out_min = min;
    /* {m} is exact; {m,} is unbounded; {m,n} is bounded. */
    *out_max = has_max ? (max_specified ? max : (size_t)-1) : min;
    *out_lazy = 0;
    *out_consumed = p - i;
    if (p < end && exec->pattern[p] == '?') {
      *out_lazy = 1;
      ++*out_consumed;
    }
    return 1;
  }
  return 0;
}

static int re_match_atom_once(re_exec_t *exec, size_t start, size_t atom_end, size_t pos,
                              size_t *out_pos) {
  if (!re_step(exec)) return 0;
  if (exec->pattern[start] == '^') {
    *out_pos = pos;
    return pos == 0;
  }
  if (exec->pattern[start] == '$') {
    *out_pos = pos;
    return pos == exec->text_len;
  }
  if (exec->pattern[start] == '(') {
    size_t group_end;
    size_t content_start;
    if (!re_group_prefix_valid(exec->pattern, start, atom_end)) return 0;
    if (!re_find_group_end(exec, start, atom_end, &group_end)) return 0;
    content_start = re_group_content_start(exec->pattern, start, atom_end);
    if (start + 1 < atom_end && exec->pattern[start + 1] == '?') {
      if (start + 2 < atom_end && exec->pattern[start + 2] == '=') {
        size_t probe = pos;
        int ok = re_match_expr(exec, content_start, group_end, pos, &probe);
        if (exec->status != RE_STATUS_OK) return 0;
        if (!ok) return 0;
        *out_pos = pos;
        return 1;
      }
      if (start + 2 < atom_end && exec->pattern[start + 2] == '!') {
        size_t probe = pos;
        int ok = re_match_expr(exec, content_start, group_end, pos, &probe);
        if (exec->status != RE_STATUS_OK) return 0;
        if (ok) return 0;
        *out_pos = pos;
        return 1;
      }
    }
    return re_match_expr(exec, content_start, group_end, pos, out_pos);
  }
  if (exec->pattern[start] == '\\' && start + 1 < atom_end &&
      (exec->pattern[start + 1] == 'b' || exec->pattern[start + 1] == 'B')) {
    int left_word;
    int right_word;
    int boundary;
    if (!re_step(exec)) return 0;
    left_word = pos > 0 && re_is_alphanum((unsigned char)exec->text[pos - 1]);
    right_word = pos < exec->text_len && re_is_alphanum((unsigned char)exec->text[pos]);
    boundary = left_word != right_word;
    if (exec->pattern[start + 1] == 'B') boundary = !boundary;
    *out_pos = pos;
    return boundary;
  }
  if (pos >= exec->text_len) return 0;
  if (exec->pattern[start] == '.') {
    if (!re_match_dot((unsigned char)exec->text[pos])) return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if (exec->pattern[start] == '[') {
    size_t class_end;
    size_t class_start;
    int inverted;
    int in_class;
    if (!re_find_char_class_end(exec, start, atom_end, &class_end)) return 0;
    inverted = start + 1 < class_end && exec->pattern[start + 1] == '^';
    class_start = inverted ? start + 2 : start + 1;
    in_class = re_match_char_class(exec, (unsigned char)exec->text[pos], class_start,
                                   class_end - class_start);
    if (exec->status != RE_STATUS_OK || (inverted ? in_class : !in_class)) return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if (exec->pattern[start] == '\\') {
    if (!re_match_metachar((unsigned char)exec->text[pos], (unsigned char)exec->pattern[start + 1]))
      return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if ((unsigned char)exec->text[pos] != (unsigned char)exec->pattern[start]) return 0;
  *out_pos = pos + 1;
  return 1;
}

/* Greedy repetition of [start, atom_end) between min and max times (max is
 * SIZE_MAX when unbounded). Matches as many occurrences as fit, then backtracks
 * through recorded positions until the tail succeeds. Mirrors the original
 * * / + behavior and extends it to {m,n}. */
static int re_match_repeat_greedy(re_exec_t *exec, size_t start, size_t atom_end,
                                  size_t rest_start, size_t end, size_t pos,
                                  size_t min, size_t max, size_t *out_pos) {
  size_t repeat_count = 0;
  size_t capacity = exec->text_len - pos + 1;
  size_t *positions;
  size_t positions_size;
  size_t current_pos = pos;
  size_t max_repeat;
  int matched = 0;
  if (capacity > ((size_t)-1) / sizeof(*positions)) {
    re_fail(exec, RE_STATUS_WORKSPACE_LIMIT);
    return 0;
  }
  positions_size = capacity * sizeof(*positions);
  positions = (size_t *)re_workspace_alloc(exec, positions_size);
  if (positions == NULL) return 0;
  positions[0] = pos;
  max_repeat = max == (size_t)-1 ? capacity - 1 : (max < capacity - 1 ? max : capacity - 1);
  while (repeat_count + 1 < capacity && repeat_count < max_repeat) {
    size_t next_pos;
    if (!re_match_atom_once(exec, start, atom_end, current_pos, &next_pos) ||
        next_pos == current_pos)
      break;
    current_pos = next_pos;
    positions[++repeat_count] = current_pos;
  }
  if (exec->status == RE_STATUS_OK) {
    while (repeat_count >= min) {
      if (re_match_sequence(exec, rest_start, end, positions[repeat_count], out_pos)) {
        matched = 1;
        break;
      }
      if (exec->status != RE_STATUS_OK || repeat_count == 0) break;
      --repeat_count;
    }
  }
  re_workspace_free(exec, positions, positions_size);
  return matched;
}

/* Lazy repetition between min and max times: consumes the minimum first, then
 * tries the tail after every extra occurrence without backtracking. */
static int re_match_repeat_lazy(re_exec_t *exec, size_t start, size_t atom_end,
                                size_t rest_start, size_t end, size_t pos,
                                size_t min, size_t max, size_t *out_pos) {
  size_t count = 0;
  size_t current_pos = pos;
  int matched = 0;
  while (count < min) {
    size_t next_pos;
    if (!re_match_atom_once(exec, start, atom_end, current_pos, &next_pos) ||
        next_pos == current_pos)
      return 0;
    current_pos = next_pos;
    ++count;
  }
  while (exec->status == RE_STATUS_OK) {
    if (re_match_sequence(exec, rest_start, end, current_pos, out_pos)) {
      matched = 1;
      break;
    }
    if (exec->status != RE_STATUS_OK || count >= max) break;
    {
      size_t next_pos;
      if (!re_match_atom_once(exec, start, atom_end, current_pos, &next_pos) ||
          next_pos == current_pos)
        break;
      current_pos = next_pos;
      ++count;
    }
  }
  return matched;
}

static int re_match_sequence(re_exec_t *exec, size_t start, size_t end, size_t pos,
                             size_t *out_pos) {
  size_t atom_end;
  size_t rest_start;
  int matched = 0;
  if (!re_enter(exec)) return 0;
  if (start >= end) {
    *out_pos = pos;
    matched = 1;
    goto cleanup;
  }
  if (!re_atom_end_at(exec, start, end, 0, &atom_end)) goto cleanup;
  rest_start = atom_end;
  if (rest_start < end) {
    size_t q_min;
    size_t q_max;
    size_t q_consumed;
    int q_lazy;
    int q_invalid = 0;
    if (re_quantifier_at(exec, rest_start, end, &q_min, &q_max, &q_lazy, &q_consumed,
                         &q_invalid)) {
      rest_start += q_consumed;
      if (q_max == 0) {
        /* {0} / {0,0}: skip the atom entirely. */
        matched = re_match_sequence(exec, rest_start, end, pos, out_pos);
        goto cleanup;
      }
      if (q_min == 0 && q_max == 1) {
        /* ? is greedy (one then zero); ?? is lazy (zero then one). */
        if (q_lazy) {
          if (re_match_sequence(exec, rest_start, end, pos, out_pos)) {
            matched = 1;
            goto cleanup;
          }
          if (exec->status != RE_STATUS_OK) goto cleanup;
          {
            size_t next_pos;
            matched = re_match_atom_once(exec, start, atom_end, pos, &next_pos) &&
                      re_match_sequence(exec, rest_start, end, next_pos, out_pos);
          }
        } else {
          {
            size_t next_pos;
            if (re_match_atom_once(exec, start, atom_end, pos, &next_pos) &&
                re_match_sequence(exec, rest_start, end, next_pos, out_pos)) {
              matched = 1;
              goto cleanup;
            }
          }
          if (exec->status != RE_STATUS_OK) goto cleanup;
          matched = re_match_sequence(exec, rest_start, end, pos, out_pos);
        }
        goto cleanup;
      }
      if (q_lazy)
        matched = re_match_repeat_lazy(exec, start, atom_end, rest_start, end, pos, q_min,
                                       q_max, out_pos);
      else
        matched = re_match_repeat_greedy(exec, start, atom_end, rest_start, end, pos, q_min,
                                         q_max, out_pos);
      goto cleanup;
    }
    if (q_invalid) {
      re_fail(exec, RE_STATUS_INVALID_PATTERN);
      goto cleanup;
    }
  }
  {
    size_t next_pos;
    matched = re_match_atom_once(exec, start, atom_end, pos, &next_pos) &&
              re_match_sequence(exec, rest_start, end, next_pos, out_pos);
  }

cleanup:
  re_leave(exec);
  return matched;
}

static int re_match_expr(re_exec_t *exec, size_t start, size_t end, size_t pos, size_t *out_pos) {
  size_t branch_start = start;
  int matched = 0;
  if (!re_enter(exec)) return 0;
  while (branch_start <= end) {
    size_t branch_end;
    if (!re_find_branch_end(exec, branch_start, end, &branch_end)) goto cleanup;
    if (re_match_sequence(exec, branch_start, branch_end, pos, out_pos)) {
      matched = 1;
      goto cleanup;
    }
    if (exec->status != RE_STATUS_OK || branch_end == end) goto cleanup;
    branch_start = branch_end + 1;
  }

cleanup:
  re_leave(exec);
  return matched;
}

/* Returns 1 when the pattern contains a top-level alternation (a '|' outside
 * groups and character classes); such a pattern cannot rely on its first
 * literal byte being mandatory. */
static int re_pattern_top_level_alternation(const char *pattern, size_t len) {
  size_t i = 0;
  int group_depth = 0;
  while (i < len) {
    unsigned char c = (unsigned char)pattern[i];
    if (c == '\\') {
      i += 2;
      continue;
    }
    if (c == '[') {
      ++i;
      while (i < len) {
        if (pattern[i] == '\\') {
          i += 2;
          continue;
        }
        if (pattern[i] == ']') break;
        ++i;
      }
      continue;
    }
    if (c == '(') {
      ++group_depth;
      ++i;
      continue;
    }
    if (c == ')') {
      if (group_depth > 0) --group_depth;
      ++i;
      continue;
    }
    if (c == '|' && group_depth == 0) return 1;
    ++i;
  }
  return 0;
}

/* When the first atom is a single mandatory literal byte (the pattern starts
 * with a plain byte, the byte is not quantified, and there is no top-level
 * alternation), a match can only start at a text position holding that byte.
 * Returns 1 and stores the byte, otherwise returns 0. */
static int re_match_prefix_first_byte(const char *pattern, size_t pattern_len,
                                      unsigned char *out_byte) {
  const char *metachar = ".^$[()|*+?{\\";
  if (pattern_len == 0 || strchr(metachar, pattern[0]) != NULL) return 0;
  if (pattern_len > 1 && re_is_quantifier((unsigned char)pattern[1])) return 0;
  if (re_pattern_top_level_alternation(pattern, pattern_len)) return 0;
  *out_byte = (unsigned char)pattern[0];
  return 1;
}

static re_status_t re_match_borrowed(const char *pattern, size_t pattern_len, const char *text,
                                     size_t text_len, const re_limits_t *limits,
                                     re_match_result_t *out_match) {
  re_exec_t exec;
  size_t index;
  unsigned char prefix_byte = 0;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  if (pattern_len > limits->max_pattern_bytes) return RE_STATUS_PATTERN_LIMIT;
  if (text == NULL && text_len != 0) return RE_STATUS_INVALID_ARGUMENT;
  if (text_len > limits->max_text_bytes) return RE_STATUS_TEXT_LIMIT;
  exec.pattern = pattern;
  exec.text = text;
  exec.text_len = text_len;
  exec.limits = *limits;
  exec.steps = 0;
  exec.workspace_used = 0;
  exec.depth = 0;
  exec.status = RE_STATUS_OK;
  /* When the first atom is a mandatory literal byte, probe only text
   * positions holding that byte (found with a SIMDe byte scan instead of
   * testing every offset). Skipped offsets provably cannot start a match, so
   * the first match found is identical to the scalar loop below. */
  if (re_match_prefix_first_byte(pattern, pattern_len, &prefix_byte)) {
    const char *cursor = text;
    const char *end = text_len == 0 ? text : text + text_len;
    while (cursor != NULL) {
      const char *candidate = re_scan_first_byte_native(cursor, end, prefix_byte);
      if (candidate == NULL) break;
      index = (size_t)(candidate - text);
      {
        size_t out_pos = index;
        if (!re_step(&exec)) return exec.status;
        if (re_match_expr(&exec, 0, pattern_len, index, &out_pos)) {
          out_match->index = index;
          out_match->length = out_pos - index;
          return RE_STATUS_OK;
        }
        if (exec.status != RE_STATUS_OK) return exec.status;
      }
      cursor = candidate + 1;
    }
    return RE_STATUS_NO_MATCH;
  }

  for (index = 0; index <= text_len; ++index) {
    size_t out_pos = index;
    if (!re_step(&exec)) return exec.status;
    if (re_match_expr(&exec, 0, pattern_len, index, &out_pos)) {
      out_match->index = index;
      out_match->length = out_pos - index;
      return RE_STATUS_OK;
    }
    if (exec.status != RE_STATUS_OK) return exec.status;
    if (pattern_len != 0 && pattern[0] == '^') break;
  }
  return RE_STATUS_NO_MATCH;
}

re_status_t re_matchn(re_t pattern, const char *text, size_t text_len, const re_limits_t *limits,
                      re_match_result_t *out_match) {
  re_limits_t normalized;
  re_status_t status;
  if (out_match == NULL || pattern == NULL) return RE_STATUS_INVALID_ARGUMENT;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_match_borrowed(pattern->pattern, pattern->pattern_len, text, text_len, &normalized,
                           out_match);
}

re_status_t re_match_n(const char *pattern, size_t pattern_len, const char *text, size_t text_len,
                       const re_limits_t *limits, re_match_result_t *out_match) {
  re_limits_t normalized;
  re_status_t status;
  if (out_match == NULL) return RE_STATUS_INVALID_ARGUMENT;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  status = re_validate_with_limits(pattern, pattern_len, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_match_borrowed(pattern, pattern_len, text, text_len, &normalized, out_match);
}

const char *re_status_string(re_status_t status) {
  switch (status) {
  case RE_STATUS_OK:
    return "ok";
  case RE_STATUS_NO_MATCH:
    return "no match";
  case RE_STATUS_INVALID_ARGUMENT:
    return "invalid argument";
  case RE_STATUS_INVALID_PATTERN:
    return "invalid pattern";
  case RE_STATUS_PATTERN_LIMIT:
    return "pattern limit exceeded";
  case RE_STATUS_TEXT_LIMIT:
    return "text limit exceeded";
  case RE_STATUS_DEPTH_LIMIT:
    return "depth limit exceeded";
  case RE_STATUS_STEP_LIMIT:
    return "step limit exceeded";
  case RE_STATUS_WORKSPACE_LIMIT:
    return "workspace limit exceeded";
  case RE_STATUS_NO_MEMORY:
    return "out of memory";
  default:
    return "unknown regex status";
  }
}

re_t re_compile(const char *pattern) {
  re_t compiled = NULL;
  if (pattern == NULL ||
      re_compile_n(pattern, re_cstr_len(pattern), NULL, &compiled) != RE_STATUS_OK)
    return NULL;
  return compiled;
}

int re_matchp(re_t pattern, const char *text, int *matchlength) {
  re_match_result_t match;
  re_status_t status;
  if (matchlength == NULL) return -1;
  *matchlength = 0;
  if (text == NULL) return -1;
  status = re_matchn(pattern, text, re_cstr_len(text), NULL, &match);
  if (status != RE_STATUS_OK || match.index > INT_MAX || match.length > INT_MAX) return -1;
  *matchlength = (int)match.length;
  return (int)match.index;
}

int re_match(const char *pattern, const char *text, int *matchlength) {
  re_t compiled;
  int index;
  if (matchlength == NULL) return -1;
  *matchlength = 0;
  compiled = re_compile(pattern);
  if (compiled == NULL) return -1;
  index = re_matchp(compiled, text, matchlength);
  re_destroy(compiled);
  return index;
}
