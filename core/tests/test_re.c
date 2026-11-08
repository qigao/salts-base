/* Original tests from utils/tests/test_re.c blob a4fef94edb32f4225c001550a6d15f86bb455225.
 * Only internal first-byte accelerator identifier changed, no tests removed.
 */
#include "re.h"
#include "re_scan.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

#ifndef SALTS_API
  #error "re.h must use the Salts export contract"
#endif

#ifdef RE_API
  #error "re.h must not expose the legacy RE_API macro"
#endif

static void check_match_at(const char *pattern, const char *text, int expected_idx,
                           int expected_len) {
  int len = -1;
  int idx = re_match(pattern, text, &len);
  check_equal(idx, expected_idx);
  check_equal(len, expected_len);
}

static void check_no_match(const char *pattern, const char *text) {
  int len = -1;
  int idx = re_match(pattern, text, &len);
  check_equal(idx, -1);
  check_equal(len, 0);
}

suite("bounded regex") {
  group("matching") {
    it("matches inverted character classes") {
      check_match_at("^[^abc]+$", "xyz", 0, 3);
      check_no_match("^[^abc]+$", "xay");
      check_match_at("^[^0-9]+$", "abc", 0, 3);
      check_no_match("^[^0-9]+$", "a7c");
      check_match_at("^[^\\d]+$", "abc", 0, 3);
      check_no_match("^[^\\d]+$", "abc7");
    }

    it("matches branches without leaking group scope") {
      check_match_at("foo|bar", "xxbarzz", 2, 3);
      check_match_at("foo|bar", "foo", 0, 3);
      check_match_at("^(cat|dog)$", "dog", 0, 3);
      check_no_match("^(cat|dog)$", "cow");
    }

    it("matches quantified groups and backtracks for a suffix") {
      check_match_at("^(a|b)+$", "abba", 0, 4);
      check_no_match("^(a|b)+$", "abbc");
      check_match_at("^(a|b)+c$", "abbc", 0, 4);
    }

    it("returns zero-length matches") {
      check_match_at("$", "a", 1, 0);
      check_match_at("a*", "", 0, 0);
      check_match_at("", "", 0, 0);
    }

    it("matches byte slices containing NUL") {
      static const char text[] = {'a', '\0', 'b'};
      re_match_result_t match = {0};
      check_equal(re_match_n("a.b", 3, text, sizeof(text), NULL, &match), RE_STATUS_OK);
      check_equal(match.index, 0);
      check_equal(match.length, 3);
    }
  }

  group("compiled handles") {
    it("keeps separately compiled patterns independent") {
      re_t first = NULL;
      re_t second = NULL;
      re_match_result_t match = {0};

      check_equal(re_compile_n("foo", 3, NULL, &first), RE_STATUS_OK);
      check_equal(re_compile_n("bar", 3, NULL, &second), RE_STATUS_OK);
      check_not_null(first);
      check_not_null(second);
      check_equal(re_matchn(first, "xxfoo", 5, NULL, &match), RE_STATUS_OK);
      check_equal(match.index, 2);
      check_equal(re_matchn(second, "xxbar", 5, NULL, &match), RE_STATUS_OK);
      check_equal(match.index, 2);
      check_equal(re_matchn(first, "bar", 3, NULL, &match), RE_STATUS_NO_MATCH);

      re_destroy(second);
      re_destroy(first);
    }

    it("rejects malformed patterns without returning a handle") {
      re_t pattern = (re_t)(uintptr_t)1;
      check_equal(re_compile_n("(a|b", 4, NULL, &pattern), RE_STATUS_INVALID_PATTERN);
      check_null(pattern);
      check_null(re_compile("*a"));
      check_null(re_compile("a**"));
    }
  }

  group("resource limits") {
    it("reports pattern and text limits separately") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_pattern_bytes = 2;
      check_equal(re_validate_n("abc", 3, &limits), RE_STATUS_PATTERN_LIMIT);

      limits = re_limits_default();
      limits.max_text_bytes = 2;
      check_equal(re_match_n("a", 1, "abc", 3, &limits, &match), RE_STATUS_TEXT_LIMIT);
      check_equal(match.index, RE_NPOS);
    }

    it("stops when the recursion depth budget is exhausted") {
      re_limits_t limits = re_limits_default();
      limits.max_depth = 2;
      check_equal(re_validate_n("((a))", 5, &limits), RE_STATUS_DEPTH_LIMIT);
    }

    it("stops when the execution step budget is exhausted") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_steps = 1;
      check_equal(re_match_n("a", 1, "a", 1, &limits, &match), RE_STATUS_STEP_LIMIT);
      check_equal(match.index, RE_NPOS);
    }

    it("stops before allocating beyond the workspace budget") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_workspace_bytes = (uint32_t)(sizeof(size_t) * 2);
      check_equal(re_match_n("(a*)*", 5, "a", 1, &limits, &match), RE_STATUS_WORKSPACE_LIMIT);
      check_equal(match.index, RE_NPOS);
    }

    it("rejects malformed limit structures") {
      re_limits_t limits = re_limits_default();
      limits.max_steps = 0;
      check_equal(re_validate_n("a", 1, &limits), RE_STATUS_INVALID_ARGUMENT);
      limits = re_limits_default();
      limits.struct_size = sizeof(limits) - 1;
      check_equal(re_validate_n("a", 1, &limits), RE_STATUS_INVALID_ARGUMENT);
    }
  }

  group("status strings") {
    it("returns stable diagnostics") {
      check_equal(re_status_string(RE_STATUS_STEP_LIMIT), "step limit exceeded");
      check_equal(re_status_string((re_status_t)99), "unknown regex status");
    }
  }

  group("prefix scanning") {
    it("matches mandatory literal prefixes at the correct offset") {
      check_match_at("alpha", "xxalpha-yy", 2, 5);
      check_match_at("alpha$", "xxalpha", 2, 5);
      check_no_match("alpha$", "xxalphas");
      check_match_at("abc[0-9]+", "zzabc123", 2, 6);
      check_no_match("abc[0-9]+", "zzabcd123");
    }

    it("probes past failed candidates before a real match") {
      check_match_at("alpha", "xxalxalpha", 5, 5);
      check_no_match("alpha", "zzalzz");
      check_match_at("needle", "xnxxneedle", 4, 6);
    }

    it("keeps branches and quantified first atoms working") {
      check_match_at("foo|bar", "xxbarzz", 2, 3);
      check_match_at("a*", "", 0, 0);
      check_match_at("a*", "bb", 0, 0);
      check_match_at("a+b", "xaab", 1, 3);
      check_match_at("^alpha", "alpha-1", 0, 5);
      check_no_match("^alpha", "xalpha");
      check_match_at("(a|b)+c", "zzbac", 2, 3);
    }

    it("finds first bytes identically with native SIMD and scalar scans") {
      static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      enum { BUF_LEN = 4096 };
      char buf[BUF_LEN + 1];
      size_t i;
      for (i = 0; i < BUF_LEN; ++i) buf[i] = alphabet[i % (sizeof(alphabet) - 1)];
      buf[BUF_LEN] = '\0';
      for (i = 1; i < BUF_LEN; i = i * 3 + 7) {
        const char *scalar = re_scan_first_byte_scalar(buf, buf + i, (unsigned char)'q');
        const char *native = re_scan_first_byte_native(buf, buf + i, (unsigned char)'q');
        check_equal((const void *)(scalar), (const void *)(native));
      }
      check_null(re_scan_first_byte_scalar(buf, buf + BUF_LEN, (unsigned char)'~'));
      check_null(re_scan_first_byte_native(buf, buf + BUF_LEN, (unsigned char)'~'));
      check_null(re_scan_first_byte_native(buf, buf, (unsigned char)'a'));

      check_equal((const void *)(re_scan_first_byte_native(buf, buf + 1, (unsigned char)'a')), (const void *)(buf));
    }
  }

  group("extended syntax") {
    it("matches non-capturing groups without retaining captures") {
      check_match_at("^(?:ab)+$", "abab", 0, 4);
      check_no_match("^(?:ab)+$", "abac");
      check_match_at("(?:a|b)+c", "zbbac", 1, 4);
      check_match_at("(?:)", "", 0, 0);
    }

    it("matches word boundaries") {
      check_match_at("\\bcat\\b", "a cat", 2, 3);
      check_no_match("\\bcat\\b", "scat");
      check_match_at("\\Bcat\\B", "scatter", 1, 3);
      check_no_match("\\Bcat\\B", "cat");
      check_match_at("\\bword$", "word", 0, 4);
      check_match_at("^\\bword\\b$", "word", 0, 4);
    }

    it("matches interval quantifiers") {
      check_match_at("^a{2,4}$", "aaa", 0, 3);
      check_no_match("^a{2,4}$", "a");
      check_no_match("^a{2,4}$", "aaaaa");
      check_match_at("a{3}", "aaaa", 0, 3);
      check_match_at("a{2,}", "aaaaa", 0, 5);
      check_match_at("a{0}", "aaa", 0, 0);
      check_match_at("a{1,3}b", "aab", 0, 3);
      check_no_match("a{2}", "a");
    }

    it("keeps braces without a valid interval as literals") {
      check_match_at("a{b}", "a{b}", 0, 4);
      check_match_at("x{", "x{", 0, 2);
    }

    it("rejects malformed interval quantifiers") {
      re_limits_t limits = re_limits_default();
      check_equal(re_validate_n("a{2,1}", 6, &limits), RE_STATUS_INVALID_PATTERN);
      check_equal(re_validate_n("a{2", 3, &limits), RE_STATUS_INVALID_PATTERN);
      check_equal(re_validate_n("a{1,2,3}", 8, &limits), RE_STATUS_INVALID_PATTERN);
      check_null(re_compile("a{2,1}"));
    }

    it("matches lazy quantifiers") {
      check_match_at("a.*?b", "axxbyyb", 0, 4);
      check_match_at("a.*b", "axxbyyb", 0, 7);
      check_match_at("a+?b", "aab", 0, 3);
      check_match_at("a??", "a", 0, 0);
      check_match_at("a?", "a", 0, 1);
      check_match_at("a??b", "b", 0, 1);
      check_match_at("a{2,4}?", "aaaa", 0, 2);
    }

    it("matches positive and negative lookahead without consuming") {
      check_match_at("foo(?=bar)", "foobar", 0, 3);
      check_no_match("foo(?=bar)", "foobaz");
      check_match_at("foo(?!bar)", "foobaz", 0, 3);
      check_no_match("foo(?!bar)", "foobar");
      check_match_at("(?=abc)abc", "abc", 0, 3);
      check_match_at("(?=abc)ab", "abc", 0, 2);
      check_match_at("^(?![0-9])[a-z]+$", "abc", 0, 3);
      check_no_match("^(?![0-9])[a-z]+$", "1abc");
    }

    it("rejects unsupported group prefixes") {
      check_null(re_compile("(?i)"));
      check_null(re_compile("(?<name>a)"));
    }
  }
}
