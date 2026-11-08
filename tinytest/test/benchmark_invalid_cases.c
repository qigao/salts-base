#include "tinytest.h"

spec("benchmark invalid input") {
  it("rejects zero iterations") {
    benchmark_batch("zero samples", 0) {
      check_true(1);
    }
  }
}
