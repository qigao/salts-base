/**
 * @file tstr.c
 * @brief tstr implementation wrapping SDS
 *
 * API uses snake_case: tstr_len, tstr_cat, tstr_cpy, etc.
 */

#include "tstr.h"
#include "sds.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_MSC_VER)
#include <strings.h>
#endif

static int tstr_append_checked(tstr *s, const void *data, size_t len) {
  tstr next;

  if (!s || !*s) return 0;
  if (len == 0) return 1;
  if (!data) return 0;

  next = sdscatlen(*s, data, len);
  if (!next) return 0;
  *s = next;
  return 1;
}

static int tstr_reserve_checked(tstr *s, size_t addlen) {
  tstr next;

  if (!s || !*s) return 0;
  if (addlen == 0) return 1;

  next = sdsMakeRoomFor(*s, addlen);
  if (!next) return 0;
  *s = next;
  return 1;
}

static size_t tstr_ssize_max_value(void) {
  return (size_t)((~(size_t)0) >> 1);
}

static size_t tstr_utf8_encode_one(uint32_t codepoint, char out[4]) {
  size_t len = tstr_utf8_codepoint_size(codepoint);

  if (len == 1) {
    out[0] = (char)codepoint;
  } else if (len == 2) {
    out[0] = (char)(0xC0u | (codepoint >> 6));
    out[1] = (char)(0x80u | (codepoint & 0x3Fu));
  } else if (len == 3) {
    out[0] = (char)(0xE0u | (codepoint >> 12));
    out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (codepoint & 0x3Fu));
  } else if (len == 4) {
    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
  }
  return len;
}

/* ============================================================================
 * tstr <-> vstr conversion
 * ========================================================================= */

tstr tstr_from_v(vstr v) {
  if (!vstr_is_valid(v) || v.len == SIZE_MAX) return NULL;
  if (v.len == 0) return sdsempty();
  return sdsnewlen(v.data, v.len);
}

vstr tstr_to_v(tstr s) {
  vstr v;
  v.data = s;
  v.len = s ? sdslen(s) : 0;
  return v;
}

/* ============================================================================
 * Creation / Destruction
 * ========================================================================= */

tstr tstr_new(void) {
  return sdsempty();
}

tstr tstr_dup(const char *s) {
  if (!s) return sdsempty();
  return sdsnew(s);
}

tstr tstr_clone(tstr s) {
  if (!s) return NULL;
  return sdsdup(s);
}

tstr tstr_dup_len(const char *s, size_t n) {
  if (n == SIZE_MAX) return NULL;
  if (!s || n == 0) return sdsempty();
  return sdsnewlen(s, n);
}

tstr tstr_new_len(const void *init, size_t n) {
  if (n == SIZE_MAX) return NULL;
  return sdsnewlen(init, n);
}

void tstr_free(tstr s) {
  sdsfree(s);
}

void tstr_freep(tstr *s) {
  if (!s) return;
  tstr_free(*s);
  *s = NULL;
}

tstr tstr_move(tstr *s) {
  tstr moved;
  if (!s) return NULL;
  moved = *s;
  *s = NULL;
  return moved;
}

/* ============================================================================
 * Properties
 * ========================================================================= */

size_t tstr_len(tstr s) {
  if (!s) return 0;
  return sdslen(s);
}

size_t tstr_avail(tstr s) {
  if (!s) return 0;
  return sdsavail(s);
}

int tstr_empty(tstr s) {
  return !s || sdslen(s) == 0;
}

void tstr_set_len(tstr s, size_t n) {
  (void)tstr_set_len_checked(s, n);
}

int tstr_set_len_checked(tstr s, size_t n) {
  if (!s || n > sdsalloc(s)) return 0;
  sdssetlen(s, n);
  s[n] = '\0';
  return 1;
}

/* ============================================================================
 * Concatenation
 * ========================================================================= */

tstr tstr_cat(tstr s, const char *t) {
  if (!s) s = sdsempty();
  if (!t) return s;
  return sdscat(s, t);
}

tstr tstr_cat_len(tstr s, const char *t, size_t n) {
  if (!s) s = sdsempty();
  if (!t || n == 0) return s;
  return sdscatlen(s, t, n);
}

tstr tstr_cat_str(tstr s, tstr t) {
  if (!s) s = sdsempty();
  if (!t) return s;
  return sdscatsds(s, t);
}

tstr tstr_cat_v(tstr s, vstr v) {
  if (!vstr_is_valid(v)) return s;
  if (!s) s = sdsempty();
  if (v.len == 0) return s;
  return sdscatlen(s, v.data, v.len);
}

tstr tstr_cat_fmt(tstr s, const char *fmt, ...) {
  if (!s) s = sdsempty();
  if (!fmt) return s;

  va_list ap;
  va_start(ap, fmt);
  s = sdscatvprintf(s, fmt, ap);
  va_end(ap);
  return s;
}

tstr tstr_cat_vfmt(tstr s, const char *fmt, va_list ap) {
  if (!s) s = sdsempty();
  if (!fmt) return s;
  return sdscatvprintf(s, fmt, ap);
}

/* ============================================================================
 * Copy
 * ========================================================================= */

tstr tstr_cpy(tstr s, const char *t) {
  if (!s) return sdsnew(t);
  if (!t) {
    sdsclear(s);
    return s;
  }
  return sdscpy(s, t);
}

tstr tstr_cpy_len(tstr s, const char *t, size_t n) {
  if (!s) return sdsnewlen(t, n);
  if (!t || n == 0) {
    sdsclear(s);
    return s;
  }
  return sdscpylen(s, t, n);
}

tstr tstr_cpy_v(tstr s, vstr v) {
  if (!vstr_is_valid(v)) return s;
  return tstr_cpy_len(s, v.data, v.len);
}

void tstr_clear(tstr s) {
  if (s) sdsclear(s);
}

/* ============================================================================
 * Comparison
 * ========================================================================= */

int tstr_cmp(tstr s1, tstr s2) {
  if (!s1 && !s2) return 0;
  if (!s1) return -1;
  if (!s2) return 1;
  return sdscmp(s1, s2);
}

int tstr_cmp_v(tstr s, vstr v) {
  vstr sv = tstr_to_v(s);
  if (!vstr_is_valid(v)) return 1;
  if (sv.len < v.len) return -1;
  if (sv.len > v.len) return 1;
  if (sv.len == 0) return 0;
  return memcmp(sv.data, v.data, sv.len);
}

int tstr_casecmp(const char *s1, const char *s2) {
  return sdscasecmp(s1, s2);
}

int tstr_ncasecmp(const char *s1, const char *s2, size_t n) {
#ifdef _MSC_VER
  return _strnicmp(s1, s2, n);
#else
  return strncasecmp(s1, s2, n);
#endif
}

int tstr_eq_v(tstr s, vstr v) {
  return vstr_eq(tstr_to_v(s), v);
}

int tstr_ieq_v(tstr s, vstr v) {
  return vstr_ieq(tstr_to_v(s), v);
}

int tstr_starts_with(const char *s, const char *prefix) {
  return sdsstartswith(s, prefix);
}

int tstr_starts_with_v(tstr s, vstr prefix) {
  return vstr_starts_with(tstr_to_v(s), prefix);
}

int tstr_istarts_with(const char *s, const char *prefix) {
  return sdsistartswith(s, prefix);
}

int tstr_ends_with(const char *s, const char *suffix) {
  return sdsendswith(s, suffix);
}

int tstr_ends_with_v(tstr s, vstr suffix) {
  return vstr_ends_with(tstr_to_v(s), suffix);
}

int tstr_contains(const char *s, const char *substr) {
  return sdscontains(s, substr);
}

int tstr_contains_v(tstr s, vstr needle) {
  return vstr_contains(tstr_to_v(s), needle);
}

size_t tstr_count_v(tstr s, vstr needle) {
  return vstr_count(tstr_to_v(s), needle);
}

/* ============================================================================
 * Search
 * ========================================================================= */

size_t tstr_find_v(tstr s, vstr needle) {
  return vstr_find(tstr_to_v(s), needle);
}

size_t tstr_find_char(tstr s, char c) {
  return vstr_find_char(tstr_to_v(s), c);
}

size_t tstr_rfind_v(tstr s, vstr needle) {
  return vstr_rfind(tstr_to_v(s), needle);
}

size_t tstr_rfind_char(tstr s, char c) {
  return vstr_rfind_char(tstr_to_v(s), c);
}

/* ============================================================================
 * Transformation
 * ========================================================================= */

tstr tstr_trim(tstr s, const char *cset) {
  if (!s || !cset) return s;
  return tstr_rtrim(tstr_ltrim(s, cset), cset);
}

tstr tstr_ltrim(tstr s, const char *cset) {
  size_t len;
  size_t start = 0;

  if (!s || !cset) return s;

  len = sdslen(s);
  start = (size_t)(vstr_trim_left(tstr_to_v(s), cset).data - s);

  if (start > 0) {
    len -= start;
    memmove(s, s + start, len);
    s[len] = '\0';
    sdssetlen(s, len);
  }
  return s;
}

tstr tstr_rtrim(tstr s, const char *cset) {
  size_t end;

  if (!s || !cset) return s;

  end = vstr_trim_right(tstr_to_v(s), cset).len;

  s[end] = '\0';
  sdssetlen(s, end);
  return s;
}

tstr tstr_slice(tstr s, size_t pos, size_t n) {
  return tstr_from_v(vstr_sub(tstr_to_v(s), pos, n));
}

int tstr_utf8_valid(tstr s) {
  return vstr_utf8_valid(tstr_to_v(s));
}

size_t tstr_utf8_invalid_offset(tstr s) {
  return vstr_utf8_invalid_offset(tstr_to_v(s));
}

size_t tstr_utf8_len(tstr s) {
  return vstr_utf8_len(tstr_to_v(s));
}

size_t tstr_utf8_nlen(tstr s, size_t n) {
  return vstr_utf8_nlen(tstr_to_v(s), n);
}

size_t tstr_utf8_size(tstr s) {
  size_t len = tstr_len(s);
  if (!s || len == SIZE_MAX) return 0;
  return len + 1;
}

size_t tstr_utf8_size_lazy(tstr s) {
  return s ? tstr_len(s) : 0;
}

tstr tstr_utf8_slice(tstr s, size_t char_pos, size_t char_count) {
  return tstr_from_v(vstr_utf8_sub(tstr_to_v(s), char_pos, char_count));
}

tstr tstr_utf8_append_cp(tstr s, uint32_t codepoint) {
  char buf[4];
  size_t len = tstr_utf8_encode_one(codepoint, buf);

  if (!s) s = sdsempty();
  if (!s) return NULL;
  if (len == 0) return s;
  (void)tstr_append_checked(&s, buf, len);
  return s;
}

tstr tstr_utf8_from_cp(uint32_t codepoint) {
  char buf[4];
  size_t len = tstr_utf8_encode_one(codepoint, buf);

  if (len == 0) return NULL;
  return tstr_dup_len(buf, len);
}

size_t tstr_utf8_find_cp(tstr s, uint32_t codepoint) {
  return vstr_utf8_find_cp(tstr_to_v(s), codepoint);
}

size_t tstr_utf8_rfind_cp(tstr s, uint32_t codepoint) {
  return vstr_utf8_rfind_cp(tstr_to_v(s), codepoint);
}

size_t tstr_utf8_find(tstr haystack, vstr needle) {
  return vstr_utf8_find(tstr_to_v(haystack), needle);
}

tstr tstr_repeat(const char *s, size_t count) {
  return tstr_repeat_v(vstr_from_cstr(s), count);
}

tstr tstr_repeat_v(vstr v, size_t count) {
  tstr out;
  size_t total;

  if (!vstr_is_valid(v)) return NULL;
  if (v.len == 0 || count == 0) return sdsempty();
  if (count > SIZE_MAX / v.len) return NULL;

  total = v.len * count;
  out = sdsempty();
  if (!out) return NULL;
  if (!tstr_reserve_checked(&out, total)) {
    tstr_free(out);
    return NULL;
  }

  if (!tstr_append_checked(&out, v.data, v.len)) {
    tstr_free(out);
    return NULL;
  }
  while (sdslen(out) < total) {
    size_t used = sdslen(out);
    size_t copy_len = total - used < used ? total - used : used;

    memcpy(out + used, out, copy_len);
    sdssetlen(out, used + copy_len);
    out[used + copy_len] = '\0';
  }
  return out;
}

tstr tstr_replace(tstr s, const char *needle, const char *replacement, size_t max_count) {
  return tstr_replace_v(s, vstr_from_cstr(needle), vstr_from_cstr(replacement), max_count);
}

tstr tstr_replace_v(tstr s, vstr needle, vstr replacement, size_t max_count) {
  vstr src;
  tstr out;
  size_t offset = 0;
  size_t replaced = 0;

  if (!s) s = sdsempty();
  if (!s || max_count == 0 || needle.len == 0) return s;
  if (!vstr_is_valid(needle) || !vstr_is_valid(replacement)) return s;

  src = tstr_to_v(s);
  out = sdsempty();
  if (!out) return s;

  while (offset < src.len) {
    vstr rest = vstr_from_buf(src.data + offset, src.len - offset);
    size_t pos = (replaced < max_count) ? vstr_find(rest, needle) : VSTR_NPOS;

    if (pos == VSTR_NPOS) {
      if (!tstr_append_checked(&out, rest.data, rest.len)) {
        tstr_free(out);
        return s;
      }
      break;
    }

    if (pos > 0) {
      if (!tstr_append_checked(&out, rest.data, pos)) {
        tstr_free(out);
        return s;
      }
    }
    if (replacement.len > 0) {
      if (!tstr_append_checked(&out, replacement.data, replacement.len)) {
        tstr_free(out);
        return s;
      }
    }
    offset += pos + needle.len;
    ++replaced;
  }

  tstr_free(s);
  return out;
}

tstr tstr_replace_all(tstr s, const char *needle, const char *replacement) {
  return tstr_replace(s, needle, replacement, SIZE_MAX);
}


void tstr_lower(tstr s) {
  if (!s) return;
  const size_t n = sdslen(s);
  for (size_t i=0; i<n; ++i) {
    if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] | 0x20);
  }
}


void tstr_upper(tstr s) {
  if (!s) return;
  const size_t n = sdslen(s);
  for (size_t i=0; i<n; ++i) {
    if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] & (char)~0x20);
  }
}

tstr tstr_pad_left(tstr s, size_t width, char fill) {
  size_t len;
  size_t padding;
  tstr next;

  if (!s) s = sdsempty();
  if (!s) return NULL;
  len = sdslen(s);
  if (len >= width) return s;
  padding = width - len;
  next = sdsMakeRoomFor(s, padding);
  if (!next) return s;
  memmove(next + padding, next, len);
  memset(next, (unsigned char)fill, padding);
  sdssetlen(next, width);
  next[width] = '\0';
  return next;
}

tstr tstr_pad_right(tstr s, size_t width, char fill) {
  size_t len;
  size_t padding;
  tstr next;

  if (!s) s = sdsempty();
  if (!s) return NULL;
  len = sdslen(s);
  if (len >= width) return s;
  padding = width - len;
  next = sdsMakeRoomFor(s, padding);
  if (!next) return s;
  memset(next + len, (unsigned char)fill, padding);
  sdssetlen(next, width);
  next[width] = '\0';
  return next;
}

/* ============================================================================
 * Memory Management
 * ========================================================================= */

tstr tstr_reserve(tstr s, size_t addlen) {
  if (!s) s = sdsempty();
  return sdsMakeRoomFor(s, addlen);
}

tstr tstr_shrink(tstr s) {
  if (!s) return NULL;
  return sdsRemoveFreeSpace(s);
}

/* ============================================================================
 * Conversion
 * ========================================================================= */

char *tstr_to_cstr(tstr s) {
  if (!s) return NULL;

  size_t len = sdslen(s);
  if (len == SIZE_MAX) return NULL;
  char *result = (char *)malloc(len + 1);
  if (!result) return NULL;

  memcpy(result, s, len);
  result[len] = '\0';
  return result;
}

tstr tstr_from_ll(long long value) {
  return sdsfromlonglong(value);
}

/* ============================================================================
 * Split / Join
 * ========================================================================= */

tstr *tstr_split(tstr s, const char *sep, int *count) {
  size_t sep_len;
  tstr *tokens;

  if (!s || !sep || !count) {
    if (count) *count = 0;
    return NULL;
  }

  sep_len = strlen(sep);
  if (sep_len == 0) {
    tokens = (tstr *)malloc(sizeof(*tokens));
    if (!tokens) {
      *count = 0;
      return NULL;
    }
    tokens[0] = tstr_dup_len(s, sdslen(s));
    if (!tokens[0]) {
      free(tokens);
      *count = 0;
      return NULL;
    }
    *count = 1;
    return tokens;
  }

  if (sep_len > (size_t)INT_MAX || sdslen(s) > tstr_ssize_max_value()) {
    *count = 0;
    return NULL;
  }
  return sdssplitlen(s, (ssize_t)sdslen(s), sep, (int)sep_len, count);
}

void tstr_free_split(tstr *tokens, int count) {
  sdsfreesplitres(tokens, count);
}

tstr tstr_join(char **argv, int argc, const char *sep) {
  tstr out;
  size_t sep_len;
  size_t total = 0;

  if (!argv || argc <= 0) return sdsempty();

  sep = sep ? sep : "";
  sep_len = strlen(sep);
  for (int i = 0; i < argc; ++i) {
    size_t part_len = argv[i] ? strlen(argv[i]) : 0;
    if (part_len > SIZE_MAX - total) return NULL;
    total += part_len;
  }
  if (sep_len > 0 && argc > 1) {
    size_t separator_total = (size_t)(argc - 1) * sep_len;
    if (separator_total / sep_len != (size_t)(argc - 1) || separator_total > SIZE_MAX - total)
      return NULL;
    total += separator_total;
  }
  out = sdsempty();
  if (!out) return NULL;
  if (!tstr_reserve_checked(&out, total)) {
    tstr_free(out);
    return NULL;
  }

  for (int i = 0; i < argc; ++i) {
    const char *part = argv[i] ? argv[i] : "";
    if (i > 0 && sep_len > 0) {
      if (!tstr_append_checked(&out, sep, sep_len)) {
        tstr_free(out);
        return NULL;
      }
    }
    if (!tstr_append_checked(&out, part, strlen(part))) {
      tstr_free(out);
      return NULL;
    }
  }
  return out;
}
