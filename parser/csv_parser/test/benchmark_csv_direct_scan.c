#include "csv_parser.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  DIRECT_SCAN_ROWS = 32768,
  DIRECT_SCAN_COLUMNS = 32,
  DIRECT_SCAN_PROJECTED_COLUMNS = 4,
  DIRECT_SCAN_BUFFER_CAPACITY = 8 * 1024 * 1024,
  DIRECT_SCAN_BENCHMARK_SAMPLES = 8,
  DIRECT_SCAN_COL_AGE = 1,
  DIRECT_SCAN_COL_COUNTRY = 2,
  DIRECT_SCAN_COL_SCORE = 3,
  DIRECT_SCAN_MIN_SCORE = 90,
};

typedef struct {
  int64_t age_sum;
} direct_scan_result_t;

static char *g_content;
static size_t g_content_length;
static size_t g_expected_count;
static int64_t g_expected_sum;
static volatile int64_t g_sink;
static size_t g_failures;

static int direct_scan_accumulate(void *ctx, size_t row_index,
                                  const csv_scan_value_t *values, size_t value_count) {
  direct_scan_result_t *result = (direct_scan_result_t *)ctx;
  (void)row_index;
  if (!result || !values || value_count != 1 || values[0].type != CSV_SCAN_VALUE_INT64)
    return -1;
  result->age_sum += values[0].value.integer;
  return 0;
}

static int direct_scan_generate_data(void) {
  size_t used = 0;
  int row;

  g_content = (char *)malloc(DIRECT_SCAN_BUFFER_CAPACITY);
  if (!g_content) return -1;
  for (row = 0; row < DIRECT_SCAN_ROWS; ++row) {
    const char *country = (row & 1) == 0 ? "CN" : "US";
    int age = 18 + row % 50;
    int score = 80 + row % 21;
    int written;
    int column;

    written = snprintf(g_content + used, DIRECT_SCAN_BUFFER_CAPACITY - used,
                       "%d,%d,%s,%d", row + 1, age, country, score);
    if (written < 0 || (size_t)written >= DIRECT_SCAN_BUFFER_CAPACITY - used) return -1;
    used += (size_t)written;
    for (column = DIRECT_SCAN_PROJECTED_COLUMNS; column < DIRECT_SCAN_COLUMNS; ++column) {
      if (DIRECT_SCAN_BUFFER_CAPACITY - used < 2U) return -1;
      g_content[used++] = ',';
      g_content[used++] = 'x';
    }
    if (DIRECT_SCAN_BUFFER_CAPACITY - used < 1U) return -1;
    g_content[used++] = '\n';
    if (strcmp(country, "CN") == 0 && score > DIRECT_SCAN_MIN_SCORE) {
      ++g_expected_count;
      g_expected_sum += age;
    }
  }
  g_content_length = used;
  return 0;
}

static int direct_scan_execute(size_t *row_count, int64_t *age_sum) {
  const csv_scan_predicate_t predicates[] = {
      {.column = DIRECT_SCAN_COL_SCORE, .type = CSV_SCAN_VALUE_INT64,
       .op = CSV_SCAN_OP_GT, .integer = DIRECT_SCAN_MIN_SCORE},
      {.column = DIRECT_SCAN_COL_COUNTRY, .type = CSV_SCAN_VALUE_TEXT,
       .op = CSV_SCAN_OP_EQ, .text = {.data = "CN", .len = 2}},
  };
  const csv_scan_projection_t projections[] = {
      {.column = DIRECT_SCAN_COL_AGE, .type = CSV_SCAN_VALUE_INT64},
  };
  direct_scan_result_t result = {0};
  csv_scan_plan_t plan = {
      .predicates = predicates,
      .predicate_count = sizeof(predicates) / sizeof(predicates[0]),
      .projections = projections,
      .projection_count = sizeof(projections) / sizeof(projections[0]),
      .on_match = direct_scan_accumulate,
      .ctx = &result,
  };
  size_t matches = 0;
  int rc = csv_filter_scan_opts(g_content, g_content_length, NULL, &plan, &matches);

  if (row_count) *row_count = matches;
  if (age_sum) *age_sum = result.age_sum;
  return rc;
}

static int direct_scan_rewind_and_execute(void) {
  size_t count = 0;
  int64_t sum = 0;
  int rc = direct_scan_execute(&count, &sum);
  if (rc != 0 || count != g_expected_count || sum != g_expected_sum) {
    ++g_failures;
    return -1;
  }
  g_sink ^= sum;
  return 0;
}

spec("direct CSV parser scan") {
  before_all() {
    check_equal(direct_scan_generate_data(), 0);
    check_equal(g_content_length, 2316982U);
  }

  after_all() {
    free(g_content);
    g_content = NULL;
  }

  it("filters and projects the VDBE reference workload") {
    size_t count = 0;
    int64_t sum = 0;
    check_equal(direct_scan_execute(&count, &sum), 0);
    check_equal(count, g_expected_count);
    check_equal(sum, g_expected_sum);
  }

  bench("streaming predicate pushdown") {
    size_t failures_before = g_failures;
    benchmark_bytes("direct CSV parser country/score filter", DIRECT_SCAN_BENCHMARK_SAMPLES,
                    g_content_length) {
      (void)direct_scan_rewind_and_execute();
    }
    check_equal(g_failures, failures_before);
  }
}
