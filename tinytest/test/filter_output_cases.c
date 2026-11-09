#include "tinytest.h"

spec("filter output") {
  static int fixture_count;

  before_each() { ++fixture_count; }

  it_skip("explicit skip") {
    check_true(0);
  }

  it("keep test") {
    check_equal(fixture_count, 1);
  }

  it("drop test") {
    check_true(1);
  }
}
