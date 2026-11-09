#include <core/scan.h>
#include <core/tstr.h>
#include "scan_internal.h"
#include <tinytest.h>
#include <string.h>

/* A volatile input pointer is reread for every sample, so the compiler cannot
 * reuse one invariant scan result. Output stores are observable; assertions
 * and input construction stay outside the timed regions. */
static char input_storage[64 * 1024 + 16];
static const char *volatile scan_input;
static const char *volatile scan_result;
static volatile size_t string_scan_result;

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

/* Exercise the string entry points with the same counted, unaligned input
 * before and after changing their scan backend. Owners and views are prepared
 * outside the timed regions; each batch records actual operations performed. */
#define STRING_SCAN_BENCH(title_, size_, hit_) \
  bench(title_) { \
    const size_t length = (size_); \
    const size_t hit = (hit_); \
    const size_t expected = hit < length ? hit : VSTR_NPOS; \
    char *input = input_storage + 1; \
    memset(input, 'x', length); \
    if (hit < length) memcpy(input + hit, "END", 3u); \
    scan_input = input; \
    tstr owned = tstr_dup_len(input, length); \
    check_not_null(owned); \
    benchmark_ops("vstr substring", 16, 8) { \
      for (size_t sample = 0; sample < 8u; ++sample) \
        string_scan_result = vstr_find(vstr_from_buf(scan_input, length), VSTR_LIT("END")); \
    } \
    check_equal(string_scan_result, expected); \
    benchmark_ops("tstr substring", 16, 8) { \
      for (size_t sample = 0; sample < 8u; ++sample) \
        string_scan_result = tstr_find_v(owned, VSTR_LIT("END")); \
    } \
    check_equal(string_scan_result, expected); \
    benchmark_ops("vstr byte", 16, 8) { \
      for (size_t sample = 0; sample < 8u; ++sample) \
        string_scan_result = vstr_find_char(vstr_from_buf(scan_input, length), 'E'); \
    } \
    check_equal(string_scan_result, expected); \
    const char delimiters[] = {'E', ';', '\r', '\n', '\t', '\0'}; \
    const char *labels[] = {"vstr any2", "vstr any3", "vstr any4", "vstr any5", "vstr any6"}; \
    for (size_t count = 2u; count <= sizeof(delimiters); ++count) { \
      benchmark_ops(labels[count - 2u], 16, 8) { \
        for (size_t sample = 0; sample < 8u; ++sample) \
          string_scan_result = vstr_find_any(vstr_from_buf(scan_input, length), \
                                             vstr_from_buf(delimiters, count)); \
      } \
      check_equal(string_scan_result, expected); \
    } \
    tstr_free(owned); \
  }

suite("String scan integration") {
  STRING_SCAN_BENCH("15-byte string tail", 15u, 12u)
  STRING_SCAN_BENCH("16-byte string", 16u, 13u)
  STRING_SCAN_BENCH("256-byte string", 256u, 253u)
  STRING_SCAN_BENCH("64-KiB string match at end", 65536u, 65533u)
  STRING_SCAN_BENCH("64-KiB string without match", 65536u, 65536u)
  STRING_SCAN_BENCH("64-KiB string match at start", 65536u, 0u)
}
