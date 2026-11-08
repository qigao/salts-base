#include "unicode.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SALTS_UNICODE_LINE_BREAK_TEST_DATA
#error SALTS_UNICODE_LINE_BREAK_TEST_DATA must identify LineBreakTest-17.0.0.txt
#endif

#define TEST_LINE_CAPACITY 16384u
#define TEST_UTF8_CAPACITY 4096u
#define TEST_BOUNDARY_CAPACITY 256u
#define EXPECTED_VECTOR_COUNT 19338u

static size_t encode_scalar(uint32_t scalar, unsigned char output[4]) {
  if (scalar <= 0x7fu) {
    output[0] = (unsigned char)scalar;
    return 1u;
  }
  if (scalar <= 0x7ffu) {
    output[0] = (unsigned char)(0xc0u | (scalar >> 6u));
    output[1] = (unsigned char)(0x80u | (scalar & 0x3fu));
    return 2u;
  }
  if (scalar <= 0xffffu) {
    output[0] = (unsigned char)(0xe0u | (scalar >> 12u));
    output[1] = (unsigned char)(0x80u | ((scalar >> 6u) & 0x3fu));
    output[2] = (unsigned char)(0x80u | (scalar & 0x3fu));
    return 3u;
  }
  output[0] = (unsigned char)(0xf0u | (scalar >> 18u));
  output[1] = (unsigned char)(0x80u | ((scalar >> 12u) & 0x3fu));
  output[2] = (unsigned char)(0x80u | ((scalar >> 6u) & 0x3fu));
  output[3] = (unsigned char)(0x80u | (scalar & 0x3fu));
  return 4u;
}

static int is_break_token(const char *token) {
  return token != NULL && strcmp(token, "\xC3\xB7") == 0;
}

static int is_no_break_token(const char *token) {
  return token != NULL && strcmp(token, "\xC3\x97") == 0;
}

static int run_vector(char *payload, size_t line_number) {
  unsigned char utf8[TEST_UTF8_CAPACITY];
  size_t boundaries[TEST_BOUNDARY_CAPACITY];
  size_t utf8_length = 0u;
  size_t boundary_count = 0u;
  char *token = strtok(payload, " \t\r\n");
  (void)line_number;

  if (!is_no_break_token(token)) return 0;

  while ((token = strtok(NULL, " \t\r\n")) != NULL) {
    char *end = NULL;
    unsigned long parsed;
    unsigned char encoded[4];
    size_t encoded_length;
    char *marker;

    parsed = strtoul(token, &end, 16);
    if (end == token || *end != '\0' || parsed > 0x10fffful ||
        (parsed >= 0xd800ul && parsed <= 0xdffful))
      return 0;

    encoded_length = encode_scalar((uint32_t)parsed, encoded);
    if (encoded_length > sizeof(utf8) - utf8_length) return 0;
    memcpy(utf8 + utf8_length, encoded, encoded_length);
    utf8_length += encoded_length;

    marker = strtok(NULL, " \t\r\n");
    if (is_break_token(marker)) {
      if (boundary_count >= TEST_BOUNDARY_CAPACITY) return 0;
      boundaries[boundary_count++] = utf8_length;
    } else if (!is_no_break_token(marker)) {
      return 0;
    }
  }

  if (boundary_count == 0u ||
      boundaries[boundary_count - 1u] != utf8_length)
    return 0;

  {
    const vstr input = vstr_from_buf((const char *)utf8, utf8_length);
    size_t cursor = 0u;
    size_t offset = 0u;
    salts_unicode_line_break_opportunity opportunity =
        SALTS_UNICODE_LINE_BREAK_ALLOWED;

    for (size_t i = 0u; i < boundary_count; ++i) {
      if (salts_unicode_line_break_next(
              input, &cursor, &offset, &opportunity) != SALTS_UNICODE_OK)
        return 0;
      if (cursor != boundaries[i] || offset != boundaries[i])
        return 0;
    }
    if (salts_unicode_line_break_next(
            input, &cursor, &offset, &opportunity) != SALTS_UNICODE_END)
      return 0;
  }
  return 1;
}

spec("salts_unicode Unicode 17 line-break conformance") {
  it("passes every official LineBreakTest vector") {
    FILE *file = fopen(SALTS_UNICODE_LINE_BREAK_TEST_DATA, "rb");
    char line[TEST_LINE_CAPACITY];
    size_t line_number = 0u;
    size_t vector_count = 0u;

    check_not_null(file);
    if (file == NULL) return;

    while (fgets(line, sizeof(line), file) != NULL) {
      char *hash;
      char *payload;
      ++line_number;

      if (strchr(line, '\n') == NULL && !feof(file)) {
        check_true(0);
        fclose(file);
        return;
      }

      hash = strchr(line, '#');
      if (hash != NULL) *hash = '\0';
      payload = line;
      while (*payload == ' ' || *payload == '\t' ||
             *payload == '\r' || *payload == '\n')
        ++payload;
      if (*payload == '\0') continue;

      check_true(run_vector(payload, line_number));
      ++vector_count;
    }

    check_equal(ferror(file), 0);
    fclose(file);
    check_equal(vector_count, (size_t)EXPECTED_VECTOR_COUNT);
  }
}
