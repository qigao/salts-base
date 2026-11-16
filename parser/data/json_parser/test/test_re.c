#include "jsonpath_contains.h"
#include "jsonpath_utf8.h"
#include "tinytest.h"

#include <string.h>

suite("jsonpath helpers") {
  group("contains helpers") {
    it("matches substrings like strstr over edge cases") {
      static const char *pairs[][2] = {
          {"", ""},        {"abc", ""},   {"", "a"},     {"a", "a"},
          {"a", "b"},      {"abc", "abc"}, {"abc", "b"}, {"abc", "bc"},
          {"abc", "abcd"}, {"aaaa", "aa"}, {"banana", "nan"}, {"x", "xyz"},
      };
      size_t i;
      for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        const char *h = pairs[i][0];
        const char *n = pairs[i][1];
        size_t hlen = strlen(h);
        size_t nlen = strlen(n);
        int expected = strstr(h, n) != NULL;
        check_equal(jsonpath_contains_scalar(h, hlen, n, nlen), expected);
        check_equal(jsonpath_contains_simde(h, hlen, n, nlen), expected);
      }
    }

    it("agrees with strstr over a generated buffer") {
      static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
      enum { BUF_LEN = 2048 };
      char buf[BUF_LEN + 1];
      size_t i;
      for (i = 0; i < BUF_LEN; ++i) buf[i] = alphabet[i % (sizeof(alphabet) - 1)];
      buf[BUF_LEN] = '\0';
      for (i = 0; i < sizeof(alphabet) - 1; ++i) {
        char needle[3] = {alphabet[i], alphabet[(i * 7 + 3) % (sizeof(alphabet) - 1)], '\0'};
        int expected = strstr(buf, needle) != NULL;
        check_equal(jsonpath_contains_simde(buf, BUF_LEN, needle, 2), expected);
      }
      check_equal(jsonpath_contains_simde(buf, BUF_LEN, "zzzz", 4), 0);
      check_equal(jsonpath_contains_simde(buf, BUF_LEN, "a", 1), 1);
    }
  }

  group("utf8 length") {
    it("counts UTF-8 code points identically with SIMDe and scalar scans") {
      const unsigned char mixed[] = {
          0x61, 0x62, 0xC3, 0xA9, 0xE4, 0xB8, 0xAD, 0xF0, 0x9F, 0x98, 0x80,
          0x80, 0xBF, 0xC2, 0x80, 0xE1, 0x80, 0x80, 0x00, 0x7F, 0x80,
      };
      const size_t mixed_len = sizeof(mixed);
      for (size_t start = 0; start < mixed_len; ++start) {
        for (size_t len = 0; len <= mixed_len - start; ++len) {
          int scalar = (int)jsonpath_utf8_length_scalar((const char *)mixed + start, len);
          int simde = (int)jsonpath_utf8_length_simde((const char *)mixed + start, len);
          check_equal(scalar, simde);
        }
      }

      unsigned char wide[64];
      for (size_t i = 0; i < sizeof(wide); ++i)
        wide[i] = (i % 4U == 0U) ? 0x61U : 0x80U; /* one ASCII per 4 bytes */
      for (size_t start = 0; start < sizeof(wide); ++start) {
        int scalar = (int)jsonpath_utf8_length_scalar((const char *)wide + start, sizeof(wide) - start);
        int simde = (int)jsonpath_utf8_length_simde((const char *)wide + start, sizeof(wide) - start);
        check_equal(scalar, simde);
        {
          int expected = 0;
          for (size_t k = start; k < sizeof(wide); ++k)
            if (k % 4U == 0U) ++expected;
          check_equal(scalar, expected);
        }
      }

      check_equal((int)jsonpath_utf8_length_scalar("", 0), 0);
      check_equal((int)jsonpath_utf8_length_simde("", 0), 0);
      check_equal((int)jsonpath_utf8_length_simde("\xe4\xb8\xad\xe6\x96\x87", 6), 2);
    }
  }
}
