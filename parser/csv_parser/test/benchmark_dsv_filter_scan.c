#include "dsv_filter.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  DSV_SCAN_ROWS = 32768,
  DSV_SCAN_COLUMNS = 32,
  DSV_SCAN_CORE_COLUMNS = 4,
  DSV_SCAN_BUFFER_CAPACITY = 8 * 1024 * 1024,
  DSV_SCAN_SAMPLES = 8,
  DSV_SCAN_COL_AGE = 1,
  DSV_SCAN_MIN_SCORE = 90,
};

typedef struct {
  int64_t age_sum;
} dsv_scan_result_t;

static char *g_content;
static size_t g_content_length;
static size_t g_expected_count;
static int64_t g_expected_sum;
static csv_doc_t *g_header;
static dsv_filter_t *g_filter;
static volatile int64_t g_sink;
static size_t g_failures;

static int dsv_scan_accumulate(void *ctx, size_t row_index,
                               const csv_scan_value_t *values, size_t value_count) {
  dsv_scan_result_t *result = (dsv_scan_result_t *)ctx;
  (void)row_index;
  if (!result || !values || value_count != 1 || values[0].type != CSV_SCAN_VALUE_INT64)
    return -1;
  result->age_sum += values[0].value.integer;
  return 0;
}

static int dsv_scan_generate_data(void) {
  size_t used = 0;
  int row;

  g_content = (char *)malloc(DSV_SCAN_BUFFER_CAPACITY);
  if (!g_content) return -1;
  for (row = 0; row < DSV_SCAN_ROWS; ++row) {
    const char *country = (row & 1) == 0 ? "CN" : "US";
    int age = 18 + row % 50;
    int score = 80 + row % 21;
    int written = snprintf(g_content + used, DSV_SCAN_BUFFER_CAPACITY - used,
                           "%d,%d,%s,%d", row + 1, age, country, score);
    int column;

    if (written < 0 || (size_t)written >= DSV_SCAN_BUFFER_CAPACITY - used) return -1;
    used += (size_t)written;
    for (column = DSV_SCAN_CORE_COLUMNS; column < DSV_SCAN_COLUMNS; ++column) {
      if (DSV_SCAN_BUFFER_CAPACITY - used < 2U) return -1;
      g_content[used++] = ',';
      g_content[used++] = 'x';
    }
    if (DSV_SCAN_BUFFER_CAPACITY - used < 1U) return -1;
    g_content[used++] = '\n';
    if (strcmp(country, "CN") == 0 && score > DSV_SCAN_MIN_SCORE) {
      ++g_expected_count;
      g_expected_sum += age;
    }
  }
  g_content_length = used;
  return 0;
}

static int dsv_scan_execute(size_t *row_count, int64_t *age_sum) {
  const csv_scan_projection_t projection = {
      .column = DSV_SCAN_COL_AGE,
      .type = CSV_SCAN_VALUE_INT64,
  };
  dsv_scan_result_t result = {0};
  size_t matches = 0;
  int rc = dsv_filter_scan(g_filter, g_content, g_content_length, NULL, &projection, 1,
                           dsv_scan_accumulate, &result, &matches);
  if (row_count) *row_count = matches;
  if (age_sum) *age_sum = result.age_sum;
  return rc;
}

static int dsv_scan_rewind_and_execute(void) {
  size_t count = 0;
  int64_t sum = 0;
  int rc = dsv_scan_execute(&count, &sum);
  if (rc != 0 || count != g_expected_count || sum != g_expected_sum) {
    ++g_failures;
    return -1;
  }
  g_sink ^= sum;
  return 0;
}

spec("direct DSV filter scan") {
  before_all() {
    check_equal(dsv_scan_generate_data(), 0);
    check_equal(g_content_length, 2316982U);
    g_header = csv_parse("id_n,age_n,country_s,score_n\n",
                         strlen("id_n,age_n,country_s,score_n\n"));
    check_not_null(g_header);
    g_filter = dsv_filter_create(g_header, 0);
    check_not_null(g_filter);
    check(dsv_filter_compile(g_filter, "score > 90 and country == \"CN\""));
  }

  after_all() {
    dsv_filter_destroy(g_filter);
    csv_free(g_header);
    free(g_content);
  }

  it("filters and projects the VDBE reference workload") {
    size_t count = 0;
    int64_t sum = 0;
    check_equal(dsv_scan_execute(&count, &sum), 0);
    check_equal(count, g_expected_count);
    check_equal(sum, g_expected_sum);
  }

  bench("compiled expression predicate pushdown") {
    size_t failures_before = g_failures;
    benchmark_bytes("direct DSV filter country/score", DSV_SCAN_SAMPLES, g_content_length) {
      (void)dsv_scan_rewind_and_execute();
    }
    check_equal(g_failures, failures_before);
  }
}
