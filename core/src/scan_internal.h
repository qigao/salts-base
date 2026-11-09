#ifndef SALTS_SCAN_INTERNAL_H
#define SALTS_SCAN_INTERNAL_H

#include <simde/x86/sse2.h>

#include <stddef.h>

/* Private Core kernels: borrowed [p, end) within one array, O(n) time and
 * O(1) space. Check null/reversed ranges before pointer subtraction.
 * Vector loads require 16 remaining bytes; scalar kernels handle the tail. */

static inline const char *scan_scalar_skip_sp_tab(const char *p,
                                                                   const char *end) {
  if (!p || !end || p > end) return end;
  while (p < end && (*p == ' ' || *p == '\t'))
    ++p;
  return p;
}

static inline const char *scan_simde_skip_sp_tab(const char *p, const char *end) {
  if (!p || !end || p > end) return end;
  if ((size_t)(end - p) < sizeof(simde__m128i))
    return scan_scalar_skip_sp_tab(p, end);
  const simde__m128i space = simde_mm_set1_epi8(' ');
  const simde__m128i tab = simde_mm_set1_epi8('\t');

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i whitespace = simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, space),
                                                 simde_mm_cmpeq_epi8(bytes, tab));
    unsigned int whitespace_mask = (unsigned int)simde_mm_movemask_epi8(whitespace);

    if (whitespace_mask == 0xFFFFU) {
      p += sizeof(simde__m128i);
      continue;
    }
    while ((whitespace_mask & 1U) != 0U) {
      ++p;
      whitespace_mask >>= 1;
    }
    return p;
  }

  return scan_scalar_skip_sp_tab(p, end);
}

static inline const char *scan_scalar_to_any4(const char *p, const char *end,
                                                  char first, char second, char third,
                                                  char fourth) {
  if (!p || !end || p > end) return end;
  while (p < end && *p != first && *p != second && *p != third && *p != fourth)
    ++p;
  return p;
}

static inline const char *scan_simde_to_any4(const char *p, const char *end,
                                                char first, char second, char third,
                                                char fourth) {
  if (!p || !end || p > end) return end;
  if ((size_t)(end - p) < sizeof(simde__m128i))
    return scan_scalar_to_any4(p, end, first, second, third, fourth);
  const simde__m128i first_byte = simde_mm_set1_epi8(first);
  const simde__m128i second_byte = simde_mm_set1_epi8(second);
  const simde__m128i third_byte = simde_mm_set1_epi8(third);
  const simde__m128i fourth_byte = simde_mm_set1_epi8(fourth);

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i matches = simde_mm_or_si128(
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, first_byte),
                           simde_mm_cmpeq_epi8(bytes, second_byte)),
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, third_byte),
                           simde_mm_cmpeq_epi8(bytes, fourth_byte)));
    unsigned int match_mask = (unsigned int)simde_mm_movemask_epi8(matches);

    if (match_mask == 0U) {
      p += sizeof(simde__m128i);
      continue;
    }
    while ((match_mask & 1U) == 0U) {
      ++p;
      match_mask >>= 1;
    }
    return p;
  }

  return scan_scalar_to_any4(p, end, first, second, third, fourth);
}

static inline const char *scan_scalar_to_any5(const char *p, const char *end,
                                                  char first, char second, char third,
                                                  char fourth, char fifth) {
  if (!p || !end || p > end) return end;
  while (p < end && *p != first && *p != second && *p != third && *p != fourth &&
         *p != fifth)
    ++p;
  return p;
}

static inline const char *scan_simde_to_any5(const char *p, const char *end,
                                                char first, char second, char third,
                                                char fourth, char fifth) {
  if (!p || !end || p > end) return end;
  if ((size_t)(end - p) < sizeof(simde__m128i))
    return scan_scalar_to_any5(p, end, first, second, third, fourth, fifth);
  const simde__m128i first_byte = simde_mm_set1_epi8(first);
  const simde__m128i second_byte = simde_mm_set1_epi8(second);
  const simde__m128i third_byte = simde_mm_set1_epi8(third);
  const simde__m128i fourth_byte = simde_mm_set1_epi8(fourth);
  const simde__m128i fifth_byte = simde_mm_set1_epi8(fifth);

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i matches = simde_mm_or_si128(
        simde_mm_or_si128(
            simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, first_byte),
                               simde_mm_cmpeq_epi8(bytes, second_byte)),
            simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, third_byte),
                               simde_mm_cmpeq_epi8(bytes, fourth_byte))),
        simde_mm_cmpeq_epi8(bytes, fifth_byte));
    unsigned int match_mask = (unsigned int)simde_mm_movemask_epi8(matches);

    if (match_mask == 0U) {
      p += sizeof(simde__m128i);
      continue;
    }
    while ((match_mask & 1U) == 0U) {
      ++p;
      match_mask >>= 1;
    }
    return p;
  }

  return scan_scalar_to_any5(p, end, first, second, third, fourth, fifth);
}

#endif
