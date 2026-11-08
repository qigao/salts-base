#include "unicode.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA
#error SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA must identify Unicode 17 P2/P3 corpus
#endif

#define TEST_LINE_CAPACITY 4096u
#define TEST_UTF8_CAPACITY 4096u
#define EXPECTED_VECTOR_COUNT 28u

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

static int run_vector(char *payload) {
  unsigned char utf8[TEST_UTF8_CAPACITY];
  size_t utf8_length = 0u;
  char *semi = strchr(payload, ';');
  char *token;
  char *end = NULL;
  unsigned long expected;
  uint8_t level = 255u;

  if (semi == NULL) return 0;
  *semi = '\0';
  expected = strtoul(semi + 1, &end, 10);
  if (end == semi + 1 || (*end != '\0' && *end != '\r' && *end != '\n') ||
      expected > 1ul)
    return 0;

  token = strtok(payload, " \t\r\n");
  while (token != NULL) {
    unsigned long parsed;
    unsigned char encoded[4];
    size_t encoded_length;

    end = NULL;
    parsed = strtoul(token, &end, 16);
    if (end == token || *end != '\0' || parsed > 0x10fffful ||
        (parsed >= 0xd800ul && parsed <= 0xdffful))
      return 0;

    encoded_length = encode_scalar((uint32_t)parsed, encoded);
    if (encoded_length > sizeof(utf8) - utf8_length) return 0;
    memcpy(utf8 + utf8_length, encoded, encoded_length);
    utf8_length += encoded_length;
    token = strtok(NULL, " \t\r\n");
  }

  if (salts_unicode_bidi_paragraph_level(
          vstr_from_buf((const char *)utf8, utf8_length), &level) !=
      SALTS_UNICODE_OK)
    return 0;

  return level == (uint8_t)expected;
}

spec("salts_unicode Unicode 17 bidi P2/P3 conformance") {
  it("passes every official auto-direction paragraph vector") {
    FILE *file = fopen(SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA, "rb");
    char line[TEST_LINE_CAPACITY];
    size_t vector_count = 0u;

    check_not_null(file);
    if (file == NULL) return;

    while (fgets(line, sizeof(line), file) != NULL) {
      char *payload = line;
      while (*payload == ' ' || *payload == '\t' ||
             *payload == '\r' || *payload == '\n')
        ++payload;
      if (*payload == '\0' || *payload == '#') continue;

      check_true(run_vector(payload));
      ++vector_count;
    }

    check_equal(ferror(file), 0);
    fclose(file);
    check_equal(vector_count, (size_t)EXPECTED_VECTOR_COUNT);
  }
}
