#ifndef CSV_LEXER_SIMDE_H
#define CSV_LEXER_SIMDE_H

#include <simde/x86/sse2.h>

#include <stddef.h>

static inline const char *csv_find_unquoted_field_end_scalar(const char *p, const char *end) {
  while (p < end) {
    char c = *p;
    if (c == '\0' || c == ',' || c == '"' || c == '\r' || c == '\n')
      return p;
    ++p;
  }
  return p;
}

/* Finds the first byte that requires the regular re2c path to decide CSV syntax. */
static inline const char *csv_find_unquoted_field_end_simde(const char *p, const char *end) {
  const simde__m128i nul = simde_mm_setzero_si128();
  const simde__m128i comma = simde_mm_set1_epi8(',');
  const simde__m128i quote = simde_mm_set1_epi8('"');
  const simde__m128i carriage_return = simde_mm_set1_epi8('\r');
  const simde__m128i newline = simde_mm_set1_epi8('\n');

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i special = simde_mm_or_si128(
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, nul),
                           simde_mm_cmpeq_epi8(bytes, comma)),
        simde_mm_or_si128(
            simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, quote),
                               simde_mm_cmpeq_epi8(bytes, carriage_return)),
            simde_mm_cmpeq_epi8(bytes, newline)));
    unsigned int special_mask = (unsigned int)simde_mm_movemask_epi8(special);

    if (special_mask == 0U) {
      p += sizeof(simde__m128i);
      continue;
    }
    while ((special_mask & 1U) == 0U) {
      ++p;
      special_mask >>= 1;
    }
    return p;
  }

  return csv_find_unquoted_field_end_scalar(p, end);
}

#endif
