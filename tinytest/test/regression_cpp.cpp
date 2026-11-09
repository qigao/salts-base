#include "tinytest.hpp"

#include <limits>
#include <stdexcept>

static void throw_boom() { throw std::runtime_error("boom"); }

suite("tinytest cpp regression") {
  it("check_equal_warn should evaluate operands once") {
    int value = 1;
    check_equal_warn(value++, 1);
    check_equal(value, 2);
  }

  it("check_not_equal_warn should evaluate operands once") {
    int value = 1;
    check_not_equal_warn(value++, 2);
    check_equal(value, 2);
  }

  it("check_greater_warn should evaluate operands once") {
    int value = 1;
    check_greater_warn(value++, 0);
    check_equal(value, 2);
  }

  it("check_less_warn should evaluate operands once") {
    int value = 1;
    check_less_warn(value++, 2);
    check_equal(value, 2);
  }

  it("check_warn should compose with else in C++") {
    if (true)
      check_warn(true);
    else
      check_warn(false);
    check_true(true);
  }

  it("generic string checks compare contents") {
    char first[] = "tinytest";
    char same[] = "tinytest";
    char different[] = "other";

    check_equal(first, same);
    check_not_equal(first, different);
    check_contains(first, "test");
    check_starts_with(first, "tiny");
    check_ends_with(first, "test");
  }

  it("three-argument equality compares memory") {
    const unsigned char same_a[] = {0u, 1u, 2u, 3u};
    const unsigned char same_b[] = {0u, 1u, 2u, 3u};
    const unsigned char different[] = {0u, 1u, 9u, 3u};

    check_equal(same_a, same_b, sizeof(same_a));
    check_equal_warn(same_a, same_b, sizeof(same_a));
    check_not_equal(same_a, different, sizeof(same_a));
    check_not_equal_warn(same_a, different, sizeof(same_a));
  }

  it("generic equality handles floating and mixed-sign values without warning suppression") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const unsigned long long positive = 3u;

    check_equal(-0.0, 0.0);
    check_not_equal(nan, nan);
    check_equal(positive, 3);
    check_not_equal(positive, -1);
  }

  it_should_fail("should convert an uncaught C++ exception into a test failure") {
    throw_boom();
  }

  it("should continue after an uncaught C++ exception") { check_true(true); }

  bench("explicit benchmark units compile in C++") {
    benchmark_io("C++ batched I/O", 1, 2, 8) {
      volatile int value = 1;
      value += 1;
    }
  }
}
namespace {
/* Destructor probe ... */
struct unwind_probe {
  static int count;
  unwind_probe() {}
  ~unwind_probe() { ++count; }
};
int unwind_probe::count = 0;
} /* namespace */

suite("tinytest cpp unwind") {
  it_should_fail("runs destructors when an assertion fails") {
    unwind_probe p;
    check_equal(1, 2);
  }
  it("observed the destructor running during unwind") {
    check_equal(unwind_probe::count, 1);
  }

  it_should_fail("propagates a sub-assertion failure out of check_throws") {
    check_throws([] { check_equal(1, 2); });
  }
}
