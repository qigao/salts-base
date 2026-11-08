#ifndef SALTS_STR_H
#define SALTS_STR_H

/**
 * @file str.h
 * @brief Ergonomic tstr/vstr facade over the SDS-backed string storage.
 *
 * Ownership model:
 * - tstr owns mutable storage.
 * - vstr borrows immutable bytes.
 * - read-only algorithms remain on vstr.
 * - mutating helpers take tstr * so reallocations update the owner handle.
 */

#include "tstr.h"
#include "vstr.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create a view over a string literal without calling strlen(). */
#define VSTR_LIT(literal) vstr_from_buf((literal), sizeof(literal) - 1u)

/** Borrow the current tstr storage in O(1). The view is invalidated by mutation. */
static inline vstr tstr_view(tstr s) { return tstr_to_v(s); }

/** Return total SDS storage capacity in bytes, excluding the trailing NUL. */
static inline size_t tstr_capacity(tstr s) {
  const size_t len = tstr_len(s);
  const size_t avail = tstr_avail(s);
  return SIZE_MAX - len < avail ? SIZE_MAX : len + avail;
}

/**
 * Append a borrowed view and update the owning handle if SDS reallocates.
 * Returns nonzero on success. A NULL owner handle is invalid; a NULL tstr value
 * is initialized as an empty owned string by the existing tstr semantics.
 */
static inline int tstr_append(tstr *s, vstr value) {
  tstr next;

  if (!s || !vstr_is_valid(value)) return 0;
  next = tstr_cat_v(*s, value);
  if (!next) return 0;
  *s = next;
  return 1;
}

/**
 * Ensure at least `capacity` bytes of total storage and update the owner handle
 * if SDS reallocates. Unlike tstr_reserve(), this uses absolute capacity.
 */
static inline int tstr_reserve_capacity(tstr *s, size_t capacity) {
  tstr next;
  size_t len;

  if (!s) return 0;
  if (!*s) {
    *s = tstr_new();
    if (!*s) return 0;
  }

  if (capacity <= tstr_capacity(*s)) return 1;

  len = tstr_len(*s);
  if (capacity < len) return 1;

  /* tstr_reserve follows SDS make-room semantics: argument is free bytes
   * required after the current logical length. */
  next = tstr_reserve(*s, capacity - len);
  if (!next) return 0;
  *s = next;
  return tstr_capacity(*s) >= capacity;
}

#ifdef __cplusplus
}
#endif

#endif /* SALTS_STR_H */
