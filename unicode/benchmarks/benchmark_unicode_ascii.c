#include <unicode.h>
#include <tinytest.h>

#include <string.h>

/* Counted, unaligned fixtures include embedded NUL and non-ASCII tails.
 * Buffers are prepared and validated outside timed batches. */
static char unicode_input[65536u + 32u];
static char unicode_output[65536u + 32u];
static volatile size_t unicode_result;

#define FOLD_BENCH(title_, bytes_, tail_) \
  bench(title_) { \
    char *input = unicode_input + 1u; \
    const size_t ascii = (bytes_); \
    const size_t tail = sizeof(tail_) - 1u; \
    memset(input, 'A', ascii); \
    memcpy(input + ascii, tail_, tail); \
    const vstr view = vstr_from_buf(input, ascii + tail); \
    size_t required = 0u; \
    check_equal(unicode_case_fold(view, NULL, 0u, &required), SALTS_UNICODE_OK); \
    size_t failures = 0u; \
    const size_t operations = ascii < 256u ? 1024u : 4u; \
    benchmark_ops("counted full case fold", 32, operations) { \
      for (size_t operation = 0u; operation < operations; ++operation) { \
        size_t written = 0u; \
        if (unicode_case_fold(view, unicode_output, sizeof(unicode_output), &written) != SALTS_UNICODE_OK || \
            written != required) ++failures; \
        unicode_result = written; \
      } \
    } \
    check_equal(failures, 0u); \
    for (size_t i = 0u; i < ascii; ++i) check_equal(unicode_output[i], 'a'); \
    check_equal(unicode_output + ascii, tail_, tail); \
  }

suite("Unicode ASCII case folding") {
  FOLD_BENCH("8-byte ASCII fold", 8u, "")
  FOLD_BENCH("15-byte ASCII fold", 15u, "")
  FOLD_BENCH("16-byte ASCII fold", 16u, "")
  FOLD_BENCH("17-byte ASCII fold", 17u, "")
  FOLD_BENCH("256-byte ASCII fold", 256u, "")
  FOLD_BENCH("64-KiB ASCII fold", 65536u, "")
  FOLD_BENCH("64-KiB prefix plus unchanged UTF-8 tail", 65536u, "\xc3\xa9")
  FOLD_BENCH("ASCII fold including NUL", 32u, "\0end")

  bench("32-KiB non-ASCII fold") {
    char *input = unicode_input + 1u;
    for (size_t i = 0u; i < 32768u; i += 2u) {
      input[i] = (char)0xc3;
      input[i + 1u] = (char)0xa9;
    }
    const vstr view = vstr_from_buf(input, 32768u);
    size_t failures = 0u;
    benchmark_ops("non-ASCII full case fold", 32, 4) {
      for (size_t operation = 0u; operation < 4u; ++operation) {
        size_t written = 0u;
        if (unicode_case_fold(view, unicode_output, sizeof(unicode_output), &written) != SALTS_UNICODE_OK ||
            written != view.len) ++failures;
        unicode_result = written;
      }
    }
    check_equal(failures, 0u);
    check_equal(unicode_output, input, view.len);
  }
}

suite("Unicode counted ASCII queries") {
  bench("64-KiB Unicode whitespace trim") {
    char *input = unicode_input + 1u;
    const size_t length = 65536u;
    memset(input, 'x', length);
    memset(input, ' ', 32u);
    memset(input + length - 32u, '\t', 32u);
    vstr result = {0};
    size_t failures = 0u;
    benchmark_ops("Unicode whitespace trim", 32, 4) {
      for (size_t operation = 0u; operation < 4u; ++operation) {
        if (unicode_trim_whitespace(vstr_from_buf(input, length), &result) != SALTS_UNICODE_OK) ++failures;
        unicode_result = result.len;
      }
    }
    check_equal(failures, 0u);
    check_true(result.data == input + 32u);
    check_equal(result.len, length - 64u);
  }

  bench("64-KiB XID identifier") {
    char *input = unicode_input + 1u;
    const size_t length = 65536u;
    memset(input, 'A', length);
    int result = 0;
    size_t failures = 0u;
    benchmark_ops("Unicode XID validation", 32, 4) {
      for (size_t operation = 0u; operation < 4u; ++operation) {
        if (unicode_is_xid(vstr_from_buf(input, length), &result) != SALTS_UNICODE_OK) ++failures;
        unicode_result = (size_t)result;
      }
    }
    check_equal(failures, 0u);
    check_equal(result, 1);
  }
}
