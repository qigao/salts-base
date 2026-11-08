#ifndef JSONPATH_CONTAINS_H
#define JSONPATH_CONTAINS_H

#include <cmeta_simd_scan.h>

#include <stddef.h>
#include <string.h>

/* Byte-exact substring search with explicit lengths. The SIMDe variant scans
 * for the first needle byte with a 16-byte vector compare, then verifies the
 * remaining bytes with memcmp; the scalar variant is the same algorithm and
 * is kept for equivalence tests and benchmarks. */
static inline int jsonpath_contains_scalar(const char *haystack, size_t haystack_len,
                                           const char *needle, size_t needle_len) {
  size_t i;
  if (needle_len == 0) return 1;
  if (needle_len > haystack_len) return 0;
  for (i = 0; i + needle_len <= haystack_len; ++i) {
    if (memcmp(haystack + i, needle, needle_len) == 0) return 1;
  }
  return 0;
}

static inline int jsonpath_contains_simde(const char *haystack, size_t haystack_len,
                                          const char *needle, size_t needle_len) {
  return cmeta_scan_mem(haystack, haystack_len, needle, needle_len) != NULL;
}

#endif
