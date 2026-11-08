#include "idna.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IDNA_TEST_CAPACITY = 16384 };

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') ++s;
  size_t n = strlen(s);
  while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
  return s;
}

/* Preserve ill-formed surrogate escapes as invalid UTF-8, never replace them. */
static size_t unescape(const char *s, char *out) {
  size_t n = 0;
  if (strcmp(s, "\"\"") == 0) return 0;
  while (*s) {
    check(n + 4 < IDNA_TEST_CAPACITY);
    if (*s != '\\') { out[n++] = *s++; continue; }
    uint32_t cp;
    char *end;
    if (s[1] == 'u') {
      check(strlen(s) >= 6);
      char hex[5]; memcpy(hex, s + 2, 4); hex[4] = 0;
      cp = (uint32_t)strtoul(hex, &end, 16);
      check_equal(end, hex + 4);
      s += 6;
    } else {
      check(s[1] == 'x' && s[2] == '{');
      cp = (uint32_t)strtoul(s + 3, &end, 16);
      check_equal(*end, '}');
      s = end + 1;
    }
    check(cp <= 0x10ffffu);
    if (cp < 128) out[n++] = (char)cp;
    else {
      unsigned tail = cp < 0x800u ? 1 : cp < 0x10000u ? 2 : 3;
      out[n++] = (char)((tail == 1 ? 0xc0u : tail == 2 ? 0xe0u : 0xf0u) | (cp >> (6 * tail)));
      while (tail) out[n++] = (char)(0x80u | ((cp >> (6 * --tail)) & 63u));
    }
  }
  return n;
}

spec("UTS46 rev35 nontransitional strict profile") {
  static FILE *file;
  after_each() { if (file) { fclose(file); file = NULL; } }
  it("checks every official nontransitional vector with no skipped cases") {
    file = fopen(SALTS_IDNA_TEST_DATA, "rb");
    check_not_null(file);
    char line[IDNA_TEST_CAPACITY], input[IDNA_TEST_CAPACITY], expected[IDNA_TEST_CAPACITY];
    char mapped[IDNA_TEST_CAPACITY], normalized[IDNA_TEST_CAPACITY], output[254], repeat[254];
    uint32_t scalars[IDNA_TEST_CAPACITY];
    salts_idna_workspace w = {mapped, sizeof mapped, normalized, sizeof normalized, scalars, IDNA_TEST_CAPACITY};
    size_t line_number = 0, vectors = 0, accepted = 0, errors = 0;
    while (fgets(line, sizeof line, file)) {
      ++line_number;
      char *comment = strchr(line, '#');
      if (comment) *comment = 0;
      char *pos = trim(line);
      if (!*pos) continue;
      char *col[7];
      for (size_t i = 0; i < 7; ++i) {
        col[i] = pos;
        char *semi = strchr(pos, ';');
        if (i < 6) check_not_null(semi);
        if (semi) { *semi = 0; pos = semi + 1; }
        col[i] = trim(col[i]);
      }
      if (!*col[1]) col[1] = col[0];
      if (!*col[2]) col[2] = "[]";
      if (!*col[3]) col[3] = col[1];
      if (!*col[4]) col[4] = col[2];
      int official_error = strcmp(col[4], "[]") != 0;
      size_t in_size = unescape(col[0], input), n = 9999;
      memset(output, 0x5a, sizeof output);
      salts_idna_status status = salts_idna_to_ascii(vstr_from_buf(input, in_size),
          SALTS_IDNA_UNICODE17_UTS46_35_STRICT, &w, output, sizeof output, &n);
      info("IdnaTestV2 line %zu status %d expected %s", line_number, (int)status, col[4]);
      check_not_equal(status, SALTS_IDNA_WORKSPACE);
      check_not_equal(status, SALTS_IDNA_OUTPUT_CAPACITY);
      check_not_equal(status, SALTS_IDNA_INVALID_ARGUMENT);
      if (official_error) {
        check_not_equal(status, SALTS_IDNA_OK);
        check_equal(n, (size_t)9999);
        for (size_t i = 0; i < sizeof output; ++i) check_equal(output[i], (char)0x5a);
        ++errors;
      } else {
        size_t expect_size = unescape(col[3], expected), repeat_size = 0;
        check_equal(status, SALTS_IDNA_OK);
        check_equal(n, expect_size);
        check_equal(memcmp(output, expected, n), 0);
        check_equal(output[n], '\0');
        check_equal(salts_idna_to_ascii(vstr_from_buf(output, n), SALTS_IDNA_UNICODE17_UTS46_35_STRICT,
            &w, repeat, sizeof repeat, &repeat_size), SALTS_IDNA_OK);
        check_equal(n, repeat_size);
        check_equal(memcmp(output, repeat, n + 1), 0);
        ++accepted;
      }
      ++vectors;
    }
    check_false(ferror(file));
    /* Counts belong to the SHA-256-pinned Unicode 17 fixture. ContextO has
     * separate positive/negative unit tests; this corpus does not test it. */
    check_equal(vectors, (size_t)6391);
    check_equal(errors, (size_t)5842);
    check_equal(accepted, (size_t)549);
  }
}
