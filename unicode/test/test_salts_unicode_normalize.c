#include "unicode_normalize.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NFC_TEST_BUFFER = 4096 };

static size_t encode(uint32_t cp, char *out) {
  if (cp < 128) { out[0] = (char)cp; return 1; }
  unsigned n = cp < 0x800u ? 2 : cp < 0x10000u ? 3 : 4;
  out[0] = (char)((n == 2 ? 0xc0u : n == 3 ? 0xe0u : 0xf0u) | (cp >> (6 * (n - 1))));
  for (unsigned i = 1; i < n; ++i) out[i] = (char)(0x80u | ((cp >> (6 * (n - i - 1))) & 63u));
  return n;
}

static size_t parse_sequence(char *s, char *out) {
  size_t n = 0;
  while (*s) {
    char *end;
    unsigned long cp = strtoul(s, &end, 16);
    if (end == s) break;
    check(cp <= 0x10fffful && n + 4 < NFC_TEST_BUFFER);
    n += encode((uint32_t)cp, out + n);
    s = end;
  }
  return n;
}

spec("Unicode 17 NFC") {
  static FILE *file;
  after_each() { if (file) { fclose(file); file = NULL; } }

  it("passes all official NFC invariants and Part 1 unlisted scalars") {
    file = fopen(SALTS_NFC_TEST_DATA, "rb");
    check_not_null(file);
    static unsigned char listed[0x110000];
    memset(listed, 0, sizeof listed);
    char line[16384], columns[5][NFC_TEST_BUFFER], output[NFC_TEST_BUFFER];
    uint32_t workspace[NFC_TEST_BUFFER];
    size_t vectors = 0, line_number = 0;
    int part1 = 0;
    while (fgets(line, sizeof line, file)) {
      ++line_number;
      if (line[0] == '@') { part1 = strncmp(line, "@Part1", 6) == 0; continue; }
      if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
      size_t sizes[5];
      char *pos = line;
      for (size_t i = 0; i < 5; ++i) {
        char *end = strchr(pos, ';');
        check_not_null(end);
        *end = '\0';
        sizes[i] = parse_sequence(pos, columns[i]);
        pos = end + 1;
      }
      if (part1) {
        size_t cursor = 0;
        salts_unicode_scalar s;
        check_equal(salts_unicode_utf8_next(vstr_from_buf(columns[0], sizes[0]), &cursor, &s), SALTS_UNICODE_OK);
        check_equal(cursor, sizes[0]);
        listed[s.value] = 1;
      }
      for (size_t i = 0; i < 5; ++i) {
        size_t n = SIZE_MAX, expected = i < 3 ? 1 : 3;
        info("NormalizationTest line %zu column %zu", line_number, i + 1);
        check_equal(salts_unicode_nfc(vstr_from_buf(columns[i], sizes[i]), SALTS_UNICODE_NFC_17_0_0,
            workspace, NFC_TEST_BUFFER, output, sizeof output, &n), SALTS_UNICODE_NFC_OK);
        check_equal(n, sizes[expected]);
        check_equal(memcmp(output, columns[expected], n), 0);
      }
      ++vectors;
    }
    check_false(ferror(file));
    check_equal(vectors, (size_t)20034);
    for (uint32_t cp = 0; cp <= 0x10ffffu; ++cp) {
      if (listed[cp] || (cp >= 0xd800u && cp <= 0xdfffu)) continue;
      char input[4];
      size_t n, bytes = encode(cp, input);
      check_equal(salts_unicode_nfc(vstr_from_buf(input, bytes), SALTS_UNICODE_NFC_17_0_0,
          workspace, NFC_TEST_BUFFER, output, sizeof output, &n), SALTS_UNICODE_NFC_OK);
      check_equal(n, bytes);
      check_equal(memcmp(input, output, n), 0);
    }
  }

  it("preserves NUL and does canonical rather than compatibility folding") {
    uint32_t w[16];
    char out[32];
    size_t n;
    const char input[] = "e\xcc\x81\0\xef\xbc\xa1";
    const char expected[] = "\xc3\xa9\0\xef\xbc\xa1";
    check_equal(salts_unicode_nfc(vstr_from_buf(input, sizeof input - 1), SALTS_UNICODE_NFC_17_0_0,
        w, 16, out, sizeof out, &n), SALTS_UNICODE_NFC_OK);
    check_equal(n, sizeof expected - 1);
    check_equal(memcmp(out, expected, n), 0);
    check_equal(out[n], '\0');
  }

  it("commits only after version UTF8 workspace output and alias validation") {
    uint32_t w[8];
    char out[16] = "unchanged";
    size_t n = 91;
    const vstr input = vstr_from_cstr("e\xcc\x81");
    check_equal(salts_unicode_nfc(input, 0, w, 8, out, sizeof out, &n), SALTS_UNICODE_NFC_UNSUPPORTED_VERSION);
    check_equal(salts_unicode_nfc(vstr_from_cstr("a\xc0\xaf"), SALTS_UNICODE_NFC_17_0_0,
        w, 8, out, sizeof out, &n), SALTS_UNICODE_NFC_INVALID_UTF8);
    check_equal(salts_unicode_nfc(input, SALTS_UNICODE_NFC_17_0_0, w, 1, out, sizeof out, &n), SALTS_UNICODE_NFC_WORKSPACE);
    check_equal(salts_unicode_nfc(input, SALTS_UNICODE_NFC_17_0_0, w, 8, out, 2, &n), SALTS_UNICODE_NFC_OUTPUT_CAPACITY);
    check_equal(salts_unicode_nfc(vstr_from_cstr(out), SALTS_UNICODE_NFC_17_0_0,
        w, 8, out, sizeof out, &n), SALTS_UNICODE_NFC_INVALID_ARGUMENT);
    check_equal(salts_unicode_nfc(input, SALTS_UNICODE_NFC_17_0_0, w, SIZE_MAX, out, sizeof out, &n), SALTS_UNICODE_NFC_OVERFLOW);
    check_equal(out, "unchanged");
    check_equal(n, (size_t)91);
    check_equal(salts_unicode_nfc(input, SALTS_UNICODE_NFC_17_0_0, w, 2, out, 3, &n), SALTS_UNICODE_NFC_OK);
    check_equal(out, "\xc3\xa9");
    check_equal(n, (size_t)2);
    check_equal(salts_unicode_nfc(vstr_from_buf(NULL, 0), SALTS_UNICODE_NFC_17_0_0,
        NULL, 0, out, 1, &n), SALTS_UNICODE_NFC_OK);
    check_equal(n, (size_t)0);
  }
}
