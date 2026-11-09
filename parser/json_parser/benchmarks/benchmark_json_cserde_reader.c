#include <json_cserde_reader.h>
#include <core/tstr.h>
#include <tinytest.h>

enum { NUMBER_COUNT = 128 };
static volatile size_t reader_token_count;

#define NUMBER_READER_BENCH(title_, text_) \
  bench(title_) { \
    tstr source = tstr_new(); \
    check_not_null(source); \
    check_true(tstr_append(&source, VSTR_LIT("["))); \
    for (size_t i = 0u; i < NUMBER_COUNT; ++i) { \
      if (i != 0u) check_true(tstr_append(&source, VSTR_LIT(","))); \
      check_true(tstr_append(&source, VSTR_LIT(text_))); \
    } \
    check_true(tstr_append(&source, VSTR_LIT("]"))); \
    json_value_t *root = json_parse(source, tstr_len(source)); \
    check_not_null(root); \
    size_t failures = 0u; \
    benchmark_ops("read 128 numeric tokens", 64, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        cserde_reader *reader = json_cserde_reader_create(root, 1u); \
        cserde_token token; \
        cserde_status status = CSERDE_SOURCE_ERROR; \
        size_t count = 0u; \
        if (reader != NULL) { \
          while ((status = cserde_reader_next(reader, &token)) == CSERDE_OK) ++count; \
        } \
        if (status != CSERDE_DONE || count != NUMBER_COUNT + 2u) ++failures; \
        reader_token_count = count; \
        json_cserde_reader_destroy(reader); \
      } \
    } \
    check_equal(failures, 0u); \
    check_equal(reader_token_count, NUMBER_COUNT + 2u); \
    json_free(root); \
    tstr_free(source); \
  }

suite("JSON numeric token scans") {
  NUMBER_READER_BENCH("short integers", "7")
  NUMBER_READER_BENCH("19-digit integers", "1234567890123456789")
  NUMBER_READER_BENCH("decimal after 19 digits", "1234567890123456789.0")
  NUMBER_READER_BENCH("exponent after 19 digits", "1234567890123456789e0")
  NUMBER_READER_BENCH("short uppercase exponent", "1E+10")
}
