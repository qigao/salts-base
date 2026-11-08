#ifndef JSONPATH_UTF8_H
#define JSONPATH_UTF8_H

#include <simde/x86/sse2.h>

#include <stddef.h>

/* Number of Unicode code points in a UTF-8 byte slice (RFC 9535 length() on a
 * string counts characters, not bytes): every byte that is not a continuation
 * byte (0x80..0xBF) starts one code point. The SIMDe variant scans 16 bytes
 * per step and keeps a scalar fallback for the tail and for builds where SIMDe
 * lowers to portable C; the scalar variant is kept for equivalence tests. */

static inline unsigned int jsonpath_utf8_popcount16(unsigned int value) {
  value = value - ((value >> 1U) & 0x5555U);
  value = (value & 0x3333U) + ((value >> 2U) & 0x3333U);
  value = (value + (value >> 4U)) & 0x0F0FU;
  return (value + (value >> 8U)) & 0x3FU;
}

static inline size_t jsonpath_utf8_length_scalar(const char *str, size_t len) {
  size_t count = 0;
  size_t i;
  if (!str) return 0;
  for (i = 0; i < len; ++i) {
    if (((unsigned char)str[i] & 0xC0U) != 0x80U) ++count;
  }
  return count;
}

static inline size_t jsonpath_utf8_length_simde(const char *str, size_t len) {
  const simde__m128i cont_test = simde_mm_set1_epi8(-64);  /* 0xC0 */
  const simde__m128i cont_mask = simde_mm_set1_epi8(-128); /* 0x80 */
  size_t count = 0;
  size_t pos = 0;
  if (!str) return 0;
  while (len - pos >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)(str + pos));
    simde__m128i continuation = simde_mm_and_si128(bytes, cont_test);
    continuation = simde_mm_cmpeq_epi8(continuation, cont_mask);
    count += sizeof(simde__m128i) -
             jsonpath_utf8_popcount16((unsigned int)simde_mm_movemask_epi8(continuation));
    pos += sizeof(simde__m128i);
  }
  return count + jsonpath_utf8_length_scalar(str + pos, len - pos);
}

#endif /* JSONPATH_UTF8_H */
