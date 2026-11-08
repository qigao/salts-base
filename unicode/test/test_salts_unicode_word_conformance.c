#include "unicode.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SALTS_UNICODE_WORD_TEST_DATA
#error SALTS_UNICODE_WORD_TEST_DATA must identify WordBreakTest-17.0.0.txt
#endif

#define TEST_LINE_CAPACITY 16384u
#define TEST_UTF8_CAPACITY 8192u
#define TEST_BOUNDARY_CAPACITY 2048u
#define EXPECTED_VECTOR_COUNT 1944u

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

static int run_vector(char *payload) {
  unsigned char utf8[TEST_UTF8_CAPACITY];
  size_t boundaries[TEST_BOUNDARY_CAPACITY];
  size_t utf8_length = 0u;
  size_t boundary_count = 0u;
  char *token = strtok(payload, " \t\r\n");

  if (!is_break_token(token)) return 0;
  boundaries[boundary_count++] = 0u;

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

  if (boundary_count < 2u || boundaries[boundary_count - 1u] != utf8_length)
    return 0;

  {
    const vstr input = vstr_from_buf((const char *)utf8, utf8_length);
    size_t cursor = 0u;
    size_t boundary = 1u;
    vstr segment = {0};

    while (cursor < input.len) {
      if (boundary >= boundary_count) return 0;
      if (salts_unicode_word_next(input, &cursor, &segment) != SALTS_UNICODE_OK)
        return 0;
      if (cursor != boundaries[boundary]) return 0;
      ++boundary;
    }
    if (boundary != boundary_count) return 0;
    if (salts_unicode_word_next(input, &cursor, &segment) != SALTS_UNICODE_END)
      return 0;
  }

  {
    const vstr input = vstr_from_buf((const char *)utf8, utf8_length);
    size_t cursor = input.len;
    size_t boundary = boundary_count - 1u;
    vstr segment = {0};

    while (cursor != 0u) {
      if (boundary == 0u) return 0;
      if (salts_unicode_word_prev(input, &cursor, &segment) != SALTS_UNICODE_OK)
        return 0;
      --boundary;
      if (cursor != boundaries[boundary]) return 0;
    }
    if (boundary != 0u) return 0;
    if (salts_unicode_word_prev(input, &cursor, &segment) != SALTS_UNICODE_END)
      return 0;
  }

  return 1;
}

spec("salts_unicode Unicode 17 word boundary conformance") {
  it("passes every official WordBreakTest vector forward and backward") {
    FILE *file = fopen(SALTS_UNICODE_WORD_TEST_DATA, "rb");
    char line[TEST_LINE_CAPACITY];
    size_t vector_count = 0u;

    check_not_null(file);
    if (file == NULL) return;

    while (fgets(line, sizeof(line), file) != NULL) {
      char *hash;
      char *payload;

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

      check_true(run_vector(payload));
      ++vector_count;
    }

    check_equal(ferror(file), 0);
    fclose(file);
    check_equal(vector_count, (size_t)EXPECTED_VECTOR_COUNT);
  }
}
