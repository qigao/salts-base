/**
 * @file benchmark_regex.c
 * @brief Benchmarks for SIMDe byte scanning, bounded-regex prefix skipping,
 *        and JSONPath contains / regex filters.
 */

#include "json_parser.h"
#include "jsonpath_contains.h"
#include "re.h"
#include "tinytest.h"
#include <cmeta_simd_scan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCAN_BYTES = 64 * 1024 };

static char *make_no_a_buffer(void) {
  char *buf = (char *)malloc(SCAN_BYTES + 1);
  size_t i;
  if (!buf) return NULL;
  for (i = 0; i < SCAN_BYTES; ++i) buf[i] = (char)('b' + (i % 23));
  buf[SCAN_BYTES] = '\0';
  return buf;
}

static char *make_regex_doc(size_t items, size_t name_len, size_t *out_len) {
  size_t cap = items * (name_len + 24) + 32;
  char *buf = (char *)malloc(cap);
  size_t pos = 0;
  size_t i;
  if (!buf) return NULL;
  pos += (size_t)snprintf(buf + pos, cap - pos, "{\"items\":[");
  for (i = 0; i < items; ++i) {
    size_t j;
    if (i > 0) pos += (size_t)snprintf(buf + pos, cap - pos, ",");
    pos += (size_t)snprintf(buf + pos, cap - pos, "{\"name\":\"");
    for (j = 0; j < name_len; ++j) buf[pos++] = (char)('b' + (j % 23));
    pos += (size_t)snprintf(buf + pos, cap - pos, "\"}");
  }
  pos += (size_t)snprintf(buf + pos, cap - pos, "]}");
  *out_len = pos;
  return buf;
}

/* Raise the step budget so no-match scans complete instead of clipping. */
static re_limits_t big_limits(void) {
  re_limits_t limits = re_limits_default();
  limits.max_steps = 100000000ull;
  return limits;
}

suite("regex and contains benchmarks") {
  static char *buf;
  static char *doc;
  static size_t doc_len;
  static json_value_t *root;
  static json_path_program_t *regex_program;
  static json_path_program_t *contains_program;
  static const char absent[] = "zzzz";

  before_all() {
    buf = make_no_a_buffer();
    check_not_null(buf);
    doc = make_regex_doc(2000, 64, &doc_len);
    check_not_null(doc);
    root = json_parse(doc, doc_len);
    check_not_null(root);
    regex_program = json_path_compile("$.items[@.name ~ 'alpha'].name");
    contains_program = json_path_compile("$.items[contains(@.name, 'alpha')].name");
    check_not_null(regex_program);
    check_not_null(contains_program);
  }

  after_all() {
    json_path_program_free(contains_program);
    json_path_program_free(regex_program);
    json_free(root);
    free(doc);
    free(buf);
  }

  bench("first-byte scanning") {
    benchmark_bytes("libc 64KiB first-byte scan", 1000, SCAN_BYTES) {
      check_null(memchr(buf, 'z', SCAN_BYTES));
    }
    benchmark_bytes("Salts 64KiB first-byte scan", 1000, SCAN_BYTES) {
      check_null(cmeta_scan_char(buf, buf + SCAN_BYTES, 'z'));
    }
  }

  bench("bounded regex no-match scan") {
    const re_limits_t limits = big_limits();
    benchmark_bytes("re ~ [a-z]lpha scalar-loop 64KiB", 200, SCAN_BYTES) {
      re_match_result_t match = {0};
      check_equal(re_match_n("[a-z]lpha", 8, buf, SCAN_BYTES, &limits, &match),
                   RE_STATUS_NO_MATCH);
    }
    benchmark_bytes("re ~ alpha prefix-skip 64KiB", 200, SCAN_BYTES) {
      re_match_result_t match = {0};
      check_equal(re_match_n("alpha", 5, buf, SCAN_BYTES, &limits, &match),
                   RE_STATUS_NO_MATCH);
    }
  }

  bench("JSONPath contains") {
    benchmark_bytes("contains strstr 64KiB", 200, SCAN_BYTES) {
      check_null(strstr(buf, absent));
    }
    benchmark_bytes("contains SIMDe 64KiB", 200, SCAN_BYTES) {
      check_equal(jsonpath_contains_simde(buf, SCAN_BYTES, absent, sizeof(absent) - 1), 0);
    }
  }

  bench("JSONPath filter end-to-end") {
    benchmark_ops("items[~ 'alpha'] no-match", 200, 2000) {
      json_path_result_t *result = json_path_query_compiled(root, regex_program);
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);
      json_path_result_free(result);
    }
    benchmark_ops("items[contains 'alpha'] no-match", 200, 2000) {
      json_path_result_t *result = json_path_query_compiled(root, contains_program);
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);
      json_path_result_free(result);
    }
  }
}
