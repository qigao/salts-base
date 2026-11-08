#include "str.h"
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
}
