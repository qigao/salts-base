#include <core/tstr.h>
#include "tinytest.h"

spec("tstr/vstr v2 contract") {
  describe("literal views") {
    it("captures literal length without strlen") {
      vstr v = VSTR_LIT("hello");
      check_equal(v.data, "hello");
      check_equal(v.len, 5);
    }
  }

  describe("owned/view interop") {
    it("creates an O(1) borrowed view of tstr storage") {
      tstr s = tstr_dup("hello");
      vstr v = tstr_view(s);

      check_equal(v.data, s);
      check_equal(v.len, 5);
      tstr_free(s);
    }
  }

  describe("checked mutation") {
    it("appends through the owner handle") {
      tstr s = tstr_dup("hello");

      check_equal(tstr_append(&s, VSTR_LIT(" world")), 1);
      check_equal(s, "hello world");
      check_equal(tstr_len(s), 11);
      tstr_free(s);
    }

    it("can initialize a null owner during append") {
      tstr s = NULL;

      check_equal(tstr_append(&s, VSTR_LIT("hello")), 1);
      check_equal(s, "hello");
      tstr_free(s);
    }

    it("reserves absolute capacity through the owner handle") {
      tstr s = tstr_dup("hello");

      check_equal(tstr_reserve_capacity(&s, 128), 1);
      check(tstr_capacity(s) >= 128);
      check_equal(s, "hello");
      tstr_free(s);
    }
  }

  describe("counted scans through borrowed views") {
    it("preserves binary search, partition and split semantics") {
      const char bytes[] = {'a', '\0', 'b', ':', ':', 'a', '\0', 'b'};
      const vstr needle = vstr_from_buf(bytes, 3u);
      tstr s = tstr_dup_len(bytes, sizeof(bytes));
      check_not_null(s);
      check_equal(tstr_find_v(s, needle), 0u);
      check_equal(tstr_find_char(s, '\0'), 1u);
      check_equal(tstr_count_v(s, needle), 2u);
      check_true(tstr_contains_v(s, needle));

      vstr before, match, after;
      check_true(vstr_partition(tstr_to_v(s), VSTR_LIT("::"), &before, &match, &after));
      check_equal(before.data, s);
      check_equal(before.len, 3u);
      check_equal(after.data, s + 5u);
      check_equal(after.len, 3u);
      vstr rest = tstr_to_v(s);
      check_true(vstr_eq(vstr_split_next(&rest, VSTR_LIT("::")), needle));
      check_true(vstr_eq(vstr_split_next(&rest, VSTR_LIT("::")), needle));
      check_equal(rest.len, 0u);
      tstr_free(s);
    }

    it("keeps source aliases live until replacement has completed") {
      const char bytes[] = {'a', '\0', 'b', ':', ':', 'a', '\0', 'b'};
      const char expected[] = {'\0', 'b', ':', ':', '\0', 'b'};
      tstr s = tstr_dup_len(bytes, sizeof(bytes));
      check_not_null(s);
      const vstr needle = vstr_from_buf(s, 3u);
      const vstr replacement = vstr_from_buf(s + 1u, 2u);

      s = tstr_replace_v(s, needle, replacement, SIZE_MAX);
      check_not_null(s);
      check_equal(tstr_len(s), sizeof(expected));
      check_equal(s, expected, sizeof(expected));
      tstr_free(s);
    }
  }
}
