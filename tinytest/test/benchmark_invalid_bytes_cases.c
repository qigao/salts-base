#include "tinytest.h"

spec("tinytest benchmark validation") {
  bench("invalid byte count") {
    benchmark_bytes("zero bytes", 1, 0) { check_true(1); }
  }
}
