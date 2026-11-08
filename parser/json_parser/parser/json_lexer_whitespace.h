#ifndef JSON_LEXER_WHITESPACE_H
#define JSON_LEXER_WHITESPACE_H

#include <simde/x86/sse2.h>

#include <stddef.h>

static inline const char *json_skip_rfc_whitespace_scalar(const char *p, const char *end) {
  while (p < end) {
    char c = *p;
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      return p;
    ++p;
  }
  return p;
}

static inline const char *json_skip_rfc_whitespace_simde(const char *p, const char *end) {
  const simde__m128i space = simde_mm_set1_epi8(' ');
  const simde__m128i tab = simde_mm_set1_epi8('\t');
  const simde__m128i newline = simde_mm_set1_epi8('\n');
  const simde__m128i carriage_return = simde_mm_set1_epi8('\r');

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i whitespace = simde_mm_or_si128(
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, space),
                           simde_mm_cmpeq_epi8(bytes, tab)),
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, newline),
                           simde_mm_cmpeq_epi8(bytes, carriage_return)));
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

  return json_skip_rfc_whitespace_scalar(p, end);
}

/* Returns the closing quote only when the payload is plain ASCII JSON string data. */
static inline const char *json_find_plain_ascii_string_end(const char *p, const char *end) {
  const simde__m128i quote = simde_mm_set1_epi8('"');
  const simde__m128i escape = simde_mm_set1_epi8('\\');
  const simde__m128i control_limit = simde_mm_set1_epi8(0x20);

  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    simde__m128i special = simde_mm_or_si128(
        simde_mm_or_si128(simde_mm_cmpeq_epi8(bytes, quote),
                           simde_mm_cmpeq_epi8(bytes, escape)),
        simde_mm_cmplt_epi8(bytes, control_limit));
    unsigned int special_mask = (unsigned int)simde_mm_movemask_epi8(special);

    if (special_mask == 0U) {
      p += sizeof(simde__m128i);
      continue;
    }
    while ((special_mask & 1U) == 0U) {
      ++p;
      special_mask >>= 1;
    }
    return *p == '"' ? p : NULL;
  }

  while (p < end) {
    unsigned char c = (unsigned char)*p;
    if (c == '"')
      return p;
    if (c == '\\' || c < 0x20U || c >= 0x80U)
      return NULL;
    ++p;
  }
  return NULL;
}

#endif
