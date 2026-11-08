// re2c --lang c
/**
 * @file vstr.h
 * @brief Non-owning string view type for Salts (C string_view)
 *
 * Memory model:
 * - vstr does NOT own memory
 * - Underlying buffer must outlive the view
 * - Use *_to_* helpers to copy into arena/pool/heap when needed
 */

#ifndef VSTR_H
#define VSTR_H

#include "cmeta_api.h"
#include "memory_pool.h"
#include "cmeta_buffer.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Borrowed immutable byte view; no CMeta schema/runtime owner. */
typedef struct vstr {
  const char *data;
  size_t len;
} vstr;

/* ============================================================================
 * Construction
 * ========================================================================= */

static inline vstr vstr_from_buf(const char *s, size_t n) {
  vstr v;
  v.data = s;
  v.len = n;
  return v;
}

static inline vstr vstr_from_cstr(const char *s) {
  vstr v;
  v.data = s;
  v.len = s ? strlen(s) : 0;
  return v;
}

/* ============================================================================
 * Properties
 * ========================================================================= */

static inline size_t vstr_len(vstr v) { return v.len; }

static inline int vstr_empty(vstr v) { return v.len == 0; }

/** Return nonzero when the view is empty or data is non-NULL. */
static inline int vstr_is_valid(vstr v) { return v.data != NULL || v.len == 0; }

/* ============================================================================
 * Comparison
 * ========================================================================= */

SALTS_C_API int vstr_eq(vstr a, vstr b);
SALTS_C_API int vstr_ieq(vstr a, vstr b);

/* ============================================================================
 * Predicates
 * ========================================================================= */

SALTS_C_API int vstr_starts_with(vstr s, vstr prefix);
SALTS_C_API int vstr_ends_with(vstr s, vstr suffix);
SALTS_C_API int vstr_contains(vstr s, vstr needle);

/* ============================================================================
 * Search
 * ========================================================================= */

#define VSTR_NPOS SIZE_MAX

SALTS_C_API size_t vstr_find(vstr s, vstr needle);
SALTS_C_API size_t vstr_rfind(vstr s, vstr needle);
SALTS_C_API size_t vstr_find_char(vstr s, char c);
SALTS_C_API size_t vstr_rfind_char(vstr s, char c);
/** Find the first byte that equals any byte in delimiters. */
SALTS_C_API size_t vstr_find_any(vstr s, vstr delimiters);
SALTS_C_API size_t vstr_count(vstr s, vstr needle);

/** Split once from the left. Returns 1 when delim is present. */
SALTS_C_API int vstr_partition(vstr s, vstr delim, vstr *before, vstr *match,
                               vstr *after);

/** Split once from the right. Returns 1 when delim is present. */
SALTS_C_API int vstr_rpartition(vstr s, vstr delim, vstr *before, vstr *match,
                                vstr *after);

/* ============================================================================
 * Slicing
 * ========================================================================= */

SALTS_C_API vstr vstr_sub(vstr s, size_t pos, size_t n);
SALTS_C_API vstr vstr_trim(vstr s, const char *cset);
SALTS_C_API vstr vstr_trim_left(vstr s, const char *cset);
SALTS_C_API vstr vstr_trim_right(vstr s, const char *cset);

/* ============================================================================
 * UTF-8 helpers
 * ========================================================================= */

/** Strict UTF-8 validation */
SALTS_C_API int vstr_utf8_valid(vstr s);

/** Invalid byte offset, or VSTR_NPOS when the view is valid UTF-8 */
SALTS_C_API size_t vstr_utf8_invalid_offset(vstr s);

/** Count Unicode code points; returns VSTR_NPOS when input is invalid UTF-8 */
SALTS_C_API size_t vstr_utf8_len(vstr s);

/** Count Unicode code points in at most n bytes; invalid/truncated input returns VSTR_NPOS */
SALTS_C_API size_t vstr_utf8_nlen(vstr s, size_t n);

/** Number of bytes, equivalent to utf8size_lazy() for a bounded view */
SALTS_C_API size_t vstr_utf8_size_lazy(vstr s);

/** Convert code-point index to byte offset; index at end returns s.len */
SALTS_C_API size_t vstr_utf8_byte_offset(vstr s, size_t char_index);

/** Return a view sliced by Unicode code-point indexes */
SALTS_C_API vstr vstr_utf8_sub(vstr s, size_t char_pos, size_t char_count);

/** Decode and consume one Unicode code point from rest */
SALTS_C_API int vstr_utf8_next(vstr *rest, uint32_t *codepoint);

/** Find first/last byte offset of a Unicode code point */
SALTS_C_API size_t vstr_utf8_find_cp(vstr s, uint32_t codepoint);
SALTS_C_API size_t vstr_utf8_rfind_cp(vstr s, uint32_t codepoint);

/** Find a UTF-8 needle only at code-point boundaries */
SALTS_C_API size_t vstr_utf8_find(vstr haystack, vstr needle);

/** Encoded byte size of one Unicode code point, or 0 when invalid */
SALTS_C_API size_t tstr_utf8_codepoint_size(uint32_t codepoint);

/* ============================================================================
 * Split (zero-allocation iterator)
 * ========================================================================= */

SALTS_C_API vstr vstr_split_next(vstr *rest, vstr delim);

/** Return the next field from the right and shorten rest from the right. */
SALTS_C_API vstr vstr_rsplit_next(vstr *rest, vstr delim);

/* ============================================================================
 * Conversion (copies)
 * ========================================================================= */

SALTS_C_API char *vstr_to_cstr(vstr v);
SALTS_C_API char *vstr_to_pool(vstr v, MemoryPool *pool);
SALTS_C_API char *vstr_to_arena(vstr v, mem_pool_t *arena);

/* ============================================================================
 * Arena/Network interop (zero-copy)
 * ========================================================================= */

struct mem_slice_s;

/** Create view from arena slice (zero-copy) */
static inline vstr vstr_from_slice(const struct mem_slice_s *slice) {
  vstr v;
  if (slice) {
    v.data = slice->data;
    v.len = slice->length;
  } else {
    v.data = NULL;
    v.len = 0;
  }
  return v;
}

#ifdef __cplusplus
}
#endif

#endif /* VSTR_H */
