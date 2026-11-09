#include "tinytest.h"

static volatile size_t benchmark_sink;

spec("tinytest benchmark output") {
  bench("explicit work units") {
    benchmark_batch("batch", 2) { benchmark_sink += 1; }

    benchmark_ops("operations", 2, 4) {
      for (size_t i = 0; i < 4; ++i) benchmark_sink += i;
    }

    benchmark_bytes("bytes", 2, 16) {
      for (size_t i = 0; i < 16; ++i) benchmark_sink += i;
    }

    benchmark_io("io", 2, 4, 16) {
      for (size_t i = 0; i < 4; ++i) benchmark_sink += i;
    }

    benchmark("legacy alias", 2, 4) {
      for (size_t i = 0; i < 4; ++i) benchmark_sink += i;
    }
  }
}
