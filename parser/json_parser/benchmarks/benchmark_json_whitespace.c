#include "json_parser.h"
#include "json_lexer_whitespace.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

enum { WHITESPACE_BYTES = 64 * 1024 };

static char *make_whitespace_prefixed_json(void) {
  static const char whitespace[] = {' ', '\t', '\r', '\n'};
  const char suffix[] = "{\"value\":7}";
  char *json = (char *)malloc(WHITESPACE_BYTES + sizeof(suffix));
  size_t i;

  if (!json)
    return NULL;
  for (i = 0; i < WHITESPACE_BYTES; ++i)
    json[i] = whitespace[i % sizeof(whitespace)];
  memcpy(json + WHITESPACE_BYTES, suffix, sizeof(suffix));
  return json;
}

static char *make_plain_ascii_string_json(void) {
  char *json = (char *)malloc(WHITESPACE_BYTES + 3);

  if (!json)
    return NULL;
  json[0] = '"';
  memset(json + 1, 'x', WHITESPACE_BYTES);
  json[WHITESPACE_BYTES + 1] = '"';
  json[WHITESPACE_BYTES + 2] = '\0';
  return json;
}

suite("JSON whitespace benchmark") {
  static char *json;
  static char *plain_string;

  before_all() {
    json = make_whitespace_prefixed_json();
    check_not_null(json);
    plain_string = make_plain_ascii_string_json();
    check_not_null(plain_string);
  }

  after_all() {
    free(plain_string);
    free(json);
  }

  bench("SIMDe whitespace scanning") {
    benchmark_bytes("scalar 64KiB scan", 1000, WHITESPACE_BYTES) {
      check_true(json_skip_rfc_whitespace_scalar(json, json + WHITESPACE_BYTES) ==
                 json + WHITESPACE_BYTES);
    }

    benchmark_bytes("SIMDe 64KiB scan", 1000, WHITESPACE_BYTES) {
      check_true(json_skip_rfc_whitespace_simde(json, json + WHITESPACE_BYTES) ==
                 json + WHITESPACE_BYTES);
    }

    benchmark_bytes("64KiB RFC whitespace prefix", 1000, WHITESPACE_BYTES) {
      json_value_t *value = json_parse(json, WHITESPACE_BYTES + sizeof("{\"value\":7}") - 1);
      check_not_null(value);
      check_equal(json_get_int(value, "value", 0), 7);
      json_free(value);
    }

    benchmark_bytes("64KiB plain ASCII string", 1000, WHITESPACE_BYTES) {
      json_value_t *value = json_parse(plain_string, WHITESPACE_BYTES + 2);
      check_not_null(value);
      check_equal(json_string_len(value), WHITESPACE_BYTES);
      json_free(value);
    }
  }
}
