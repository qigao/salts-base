#include <salts/scan.h>
#include "scan_internal.h"
#include <tinytest.h>
#include <string.h>

/* A volatile input pointer is reread for every sample, so the compiler cannot
 * reuse one invariant scan result. Output stores are observable; assertions
 * and input construction stay outside the timed regions. */
static char input_storage[64 * 1024 + 16];
static const char *volatile scan_input;
static const char *volatile scan_result;

enum { SCAN_SAMPLES = 64, SCAN_BATCH = 32 };

#define SCAN_DELIMITER_BENCH(title_, size_, hit_) \
  bench(title_) { \
    const size_t length = (size_); \
    const size_t hit = (hit_); \
    char *input = input_storage + 1; \
    const char *end = input + length; \
    const char *expected = hit < length ? input + hit : end; \
    memset(input, 'x', length); \
    if (hit < length) input[hit] = '\n'; \
    scan_input = input; \
    benchmark_io("scalar any4", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_scalar_to_any4(scan_input, end, '\r', '\n', '#', '\0'); \
    } \
    check(scan_result == expected); \
    benchmark_io("SIMDe any4", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_simde_to_any4(scan_input, end, '\r', '\n', '#', '\0'); \
    } \
    check(scan_result == expected); \
    benchmark_io("public any4", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_to_any4(scan_input, end, '\r', '\n', '#', '\0'); \
    } \
    check(scan_result == expected); \
    benchmark_io("scalar any5", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_scalar_to_any5(scan_input, end, '\r', '\n', '#', '\0', ':'); \
    } \
    check(scan_result == expected); \
    benchmark_io("SIMDe any5", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_simde_to_any5(scan_input, end, '\r', '\n', '#', '\0', ':'); \
    } \
    check(scan_result == expected); \
    benchmark_io("public any5", SCAN_SAMPLES, SCAN_BATCH, \
                     SCAN_BATCH * (hit < length ? hit + 1u : length)) { \
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample) \
        scan_result = scan_to_any5(scan_input, end, '\r', '\n', '#', '\0', ':'); \
    } \
    check(scan_result == expected); \
  }

suite("Core scalar, SIMDe and public scan comparison") {
  SCAN_DELIMITER_BENCH("15-byte scalar tail", 15u, 14u)
  SCAN_DELIMITER_BENCH("16-byte vector", 16u, 15u)
  SCAN_DELIMITER_BENCH("17-byte vector and tail", 17u, 16u)
  SCAN_DELIMITER_BENCH("256-byte parser field", 256u, 255u)
  SCAN_DELIMITER_BENCH("64-KiB delimiter at end", 65536u, 65535u)
  SCAN_DELIMITER_BENCH("64-KiB no delimiter", 65536u, 65536u)
  SCAN_DELIMITER_BENCH("64-KiB delimiter at start", 65536u, 0u)

  bench("64-KiB horizontal whitespace") {
    char *input = input_storage + 1;
    const size_t length = 65536u;
    const char *end = input + length;
    memset(input, ' ', length);
    input[length - 1u] = 'x';
    scan_input = input;
    benchmark_io("scalar whitespace", SCAN_SAMPLES, SCAN_BATCH, SCAN_BATCH * length) {
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample)
        scan_result = scan_scalar_skip_sp_tab(scan_input, end);
    }
    check(scan_result == end - 1);
    benchmark_io("SIMDe whitespace", SCAN_SAMPLES, SCAN_BATCH, SCAN_BATCH * length) {
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample)
        scan_result = scan_simde_skip_sp_tab(scan_input, end);
    }
    check(scan_result == end - 1);
    benchmark_io("public whitespace", SCAN_SAMPLES, SCAN_BATCH, SCAN_BATCH * length) {
      for (size_t sample = 0; sample < SCAN_BATCH; ++sample)
        scan_result = scan_skip_sp_tab(scan_input, end);
    }
    check(scan_result == end - 1);
  }
}
