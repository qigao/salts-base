#include "json_cserde_reader.h"
#include "json_parser.h"
#include "tinytest.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static void check_next_kind(cserde_reader *reader, cserde_token_kind kind, cserde_token *token) {
  check_equal(cserde_reader_next(reader, token), CSERDE_OK);
  check_equal(token->kind, kind);
}

spec("JSON CSerde reader") {
  it("emits a canonical token stream with stable DOM slices") {
    static const char json[] = "{\"name\":\"Ada\",\"active\":true,\"values\":[null,-7,"
                               "18446744073709551615,1.5]}";
    static const cserde_token_kind expected[] = {
        CSERDE_MAP_BEGIN, CSERDE_STRING,      CSERDE_STRING, CSERDE_STRING, CSERDE_BOOL,
        CSERDE_STRING,    CSERDE_ARRAY_BEGIN, CSERDE_NULL,   CSERDE_SINT,   CSERDE_UINT,
        CSERDE_FLOAT,     CSERDE_ARRAY_END,   CSERDE_MAP_END};
    json_value_t *root = json_parse(json, sizeof(json) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 8u);
    cserde_token token = {0};
    size_t i;

    check_not_null(root);
    check_not_null(reader);
    for (i = 0u; reader != NULL && i < sizeof(expected) / sizeof(expected[0]); ++i) {
      check_next_kind(reader, expected[i], &token);
      if (token.kind == CSERDE_STRING) check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
      if (i == 2u) {
        check_equal(token.value.slice.size, (size_t)3u);
        check_equal(token.value.slice.data, (const unsigned char *)"Ada", 3u);
      } else if (i == 8u) {
        check_equal(token.value.sint, (int64_t)-7);
      } else if (i == 9u) {
        check_equal(token.value.uint, UINT64_MAX);
      } else if (i == 10u) {
        check_within(token.value.floating, 1.5, 0.0);
      }
    }
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("rejects integers outside the canonical 64-bit domains") {
    static const char positive[] = "18446744073709551616";
    static const char negative[] = "-9223372036854775809";
    json_value_t *root = json_parse(positive, sizeof(positive) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 1u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);

    root = json_parse(negative, sizeof(negative) - 1u);
    reader = json_cserde_reader_create(root, 1u);
    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("preserves signed minimum and embedded NUL string slices") {
    static const char json[] = "{\"\\u0000\":-9223372036854775808,\"text\":\"a\\u0000b\"}";
    json_value_t *root = json_parse(json, sizeof(json) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 1u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_MAP_BEGIN, &token);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_equal(token.value.slice.size, (size_t)1u);
      check_equal(token.value.slice.data[0], (unsigned char)0u);
      check_next_kind(reader, CSERDE_SINT, &token);
      check_equal(token.value.sint, INT64_MIN);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_equal(token.value.slice.size, (size_t)3u);
      check_equal(token.value.slice.data, (const unsigned char *)"a\0b", 3u);
      check_next_kind(reader, CSERDE_MAP_END, &token);
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("rejects non-finite floating values from the JSON DOM") {
    static const char json[] = "1e400";
    json_value_t *root = json_parse(json, sizeof(json) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 0u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("rejects non-finite values consistently across double builders") {
    json_value_t *root = json_create_number(INFINITY);
    cserde_reader *reader = json_cserde_reader_create(root, 0u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    root = json_create_object();
    if (root != NULL) json_object_set_number(root, "value", NAN);
    reader = json_cserde_reader_create(root, 1u);
    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_MAP_BEGIN, &token);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
      check_equal(cserde_reader_next(reader, &token), CSERDE_VALUE_OUT_OF_RANGE);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("emits builder numbers without retained text as floating tokens") {
    json_value_t *root = json_create_object();
    cserde_reader *reader;
    cserde_token token = {0};

    check_not_null(root);
    if (root != NULL) json_object_set_number(root, "value", 2.5);
    reader = json_cserde_reader_create(root, 1u);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_MAP_BEGIN, &token);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_next_kind(reader, CSERDE_FLOAT, &token);
      check_within(token.value.floating, 2.5, 0.0);
      check_next_kind(reader, CSERDE_MAP_END, &token);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("classifies equivalent double builders consistently") {
    json_value_t *root = json_create_number(2.0);
    cserde_reader *reader = json_cserde_reader_create(root, 0u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_UINT, &token);
      check_equal(token.value.uint, UINT64_C(2));
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    root = json_create_object();
    if (root != NULL) json_object_set_number(root, "value", 2.0);
    reader = json_cserde_reader_create(root, 1u);
    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_MAP_BEGIN, &token);
      check_next_kind(reader, CSERDE_STRING, &token);
      check_next_kind(reader, CSERDE_UINT, &token);
      check_equal(token.value.uint, UINT64_C(2));
      check_next_kind(reader, CSERDE_MAP_END, &token);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("preserves integer builder boundaries") {
    json_value_t *root = json_create_int64(INT64_MIN);
    cserde_reader *reader = json_cserde_reader_create(root, 0u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_SINT, &token);
      check_equal(token.value.sint, INT64_MIN);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);

    root = json_create_uint64(UINT64_MAX);
    reader = json_cserde_reader_create(root, 0u);
    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_UINT, &token);
      check_equal(token.value.uint, UINT64_MAX);
    }
    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("enforces the configured container depth") {
    static const char json[] = "[[0]]";
    json_value_t *root = json_parse(json, sizeof(json) - 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 1u);
    cserde_token token = {0};

    check_not_null(root);
    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_ARRAY_BEGIN, &token);
      check_equal(cserde_reader_next(reader, &token), CSERDE_LIMIT_EXCEEDED);
    }

    json_cserde_reader_destroy(reader);
    json_free(root);
  }

  it("rejects invalid creation arguments") {
    json_value_t *root = json_parse("0", 1u);
    cserde_reader *reader = json_cserde_reader_create(root, 0u);
    cserde_token token = {0};

    check_not_null(reader);
    if (reader != NULL) {
      check_next_kind(reader, CSERDE_UINT, &token);
      check_equal(token.value.uint, UINT64_C(0));
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
    }
    check_null(json_cserde_reader_create(NULL, 1u));
    check_null(json_cserde_reader_create(root, SIZE_MAX));
    json_cserde_reader_destroy(NULL);
    json_cserde_reader_destroy(reader);
    json_free(root);
  }
}
