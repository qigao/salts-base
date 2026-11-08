#include "simd_scan.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

enum { SCAN_BYTES = 64 * 1024 };

static char *make_input(void) {
  char *input = (char *)malloc(SCAN_BYTES + 1);

  if (!input)
    return NULL;
  memset(input, 'x', SCAN_BYTES);
  input[SCAN_BYTES] = '\n';
  return input;
}

suite("shared parser SIMD scan benchmark") {
  static char *input;

  before_all() {
    input = make_input();
    check_not_null(input);
  }

  after_all() {
    free(input);
  }

  bench("comment and value boundary scan") {
    benchmark_bytes("scalar 64KiB scan", 1000, SCAN_BYTES) {
      check_true(salts_scalar_find_any4(input, input + SCAN_BYTES + 1,
                                        '\r', '\n', '#', '\0') ==
                 input + SCAN_BYTES);
    }

    benchmark_bytes("SIMDe 64KiB scan", 1000, SCAN_BYTES) {
      check_true(salts_simd_find_any4(input, input + SCAN_BYTES + 1,
                                      '\r', '\n', '#', '\0') ==
                 input + SCAN_BYTES);
    }
  }
}
