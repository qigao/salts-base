#include "tinytest.h"

static int g_count = 0;

suite("tinytest sugar") {
  group("Basic Types") {
    before_each() {
      g_count = 0;
    }
    after_each() {
      check_equal(g_count, 1);
    }

    it("parses null") {
      g_count = 1;
      check(1);
    }
    context("When parsing arrays") {
      it("parses empty array") {
        g_count = 1;
        check(1);
      }
    }
  }
}

suite("JSON") {
  group("Basic Types") {
    it("parses null again") {
      check(1);
    }
  }
}
