#include "tinytest.h"

spec("tinytest benchmark validation") {
  bench("invalid operation count") {
    benchmark_ops("zero operations", 1, 0) { check_true(1); }
  }
}
