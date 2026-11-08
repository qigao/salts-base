#include "dsv_filter.h"
#include "dsv_index.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  INDEX_ROWS = 32768,
  INDEX_COLUMNS = 32,
  INDEX_CORE_COLUMNS = 4,
  INDEX_BUFFER_CAPACITY = 8 * 1024 * 1024,
  INDEX_BENCHMARK_SAMPLES = 100,
};

static char *g_content;
static size_t g_content_length;
static size_t g_expected_count;
static int64_t g_expected_sum;
static size_t g_or_expected_count;
static int64_t g_or_expected_sum;
static char *g_index_path;
static csv_doc_t *g_header;
static dsv_filter_t *g_filter;
static dsv_filter_t *g_or_filter;
static dsv_index_t *g_index;
static volatile int64_t g_sink;
static size_t g_failures;

static int index_generate_data(void) {
  size_t used = 0;
  int row;
  g_content = (char *)malloc(INDEX_BUFFER_CAPACITY);
  if (!g_content) return -1;
  for (row = 0; row < INDEX_ROWS; ++row) {
    const char *country = (row & 1) == 0 ? "CN" : "US";
    int age = 18 + row % 50;
    int score = 80 + row % 21;
    int written = snprintf(g_content + used, INDEX_BUFFER_CAPACITY - used,
                           "%d,%d,%s,%d", row + 1, age, country, score);
    int column;
    if (written < 0 || (size_t)written >= INDEX_BUFFER_CAPACITY - used) return -1;
    used += (size_t)written;
    for (column = INDEX_CORE_COLUMNS; column < INDEX_COLUMNS; ++column) {
      if (INDEX_BUFFER_CAPACITY - used < 2U) return -1;
      g_content[used++] = ',';
      g_content[used++] = 'x';
    }
    if (INDEX_BUFFER_CAPACITY - used < 1U) return -1;
    g_content[used++] = '\n';
    if (strcmp(country, "CN") == 0 && score > 90) {
      ++g_expected_count;
      g_expected_sum += age;
    }
    if (score > 90) {
      ++g_or_expected_count;
      g_or_expected_sum += age;
    }
  }
  g_content_length = used;
  return 0;
}

static int index_execute(dsv_filter_t *filter, size_t expected_count,
                         int64_t expected_sum) {
  dsv_index_cursor_t cursor;
  dsv_index_row_t row;
  size_t count = 0;
  int64_t sum = 0;
  int rc;
  if (dsv_filter_index_seek(filter, g_index, &cursor) != 0) return -1;
  while ((rc = dsv_index_cursor_next(g_index, &cursor, &row)) > 0) {
    if (!row.has_covering_int64) return -1;
    sum += row.covering_int64;
    ++count;
  }
  if (rc != 0 || count != expected_count || sum != expected_sum) return -1;
  g_sink ^= sum;
  return 0;
}

spec("static DSV sidecar index") {
  before_all() {
    dsv_index_config_t config = {
        .text_column = 2,
        .number_column = 3,
        .covering_int64_column = 1,
        .has_header = false,
    };
    check_equal(index_generate_data(), 0);
    check_equal(g_content_length, 2316982U);
    g_index_path = tt_make_temp_file("benchmark-dsv-index", ".idx");
    check_not_null(g_index_path);
    g_index = dsv_index_create();
    check_not_null(g_index);
    check_equal(dsv_index_build_memory(g_index, g_index_path, g_content,
                                        g_content_length, &config), 0);
    check_equal(dsv_index_open_memory(g_index, g_index_path, g_content,
                                       g_content_length), 0);
    g_header = csv_parse("id_n,age_n,country_s,score_n\n",
                         strlen("id_n,age_n,country_s,score_n\n"));
    check_not_null(g_header);
    g_filter = dsv_filter_create(g_header, 0);
    check_not_null(g_filter);
    check(dsv_filter_compile(g_filter, "score > 90 and country == \"CN\""));
    g_or_filter = dsv_filter_create(g_header, 0);
    check_not_null(g_or_filter);
    check(dsv_filter_compile(
        g_or_filter,
        "country == \"CN\" or country == \"US\" and score > 90"));
  }

  after_all() {
    dsv_filter_destroy(g_filter);
    dsv_filter_destroy(g_or_filter);
    csv_free(g_header);
    dsv_index_destroy(g_index);
    if (g_index_path) {
      (void)tt_remove_file(g_index_path);
      free(g_index_path);
    }
    free(g_content);
  }

  it("returns the covering projection without reading CSV rows") {
    check_equal(index_execute(g_filter, g_expected_count, g_expected_sum), 0);
    check_equal(index_execute(g_or_filter, g_or_expected_count, g_or_expected_sum), 0);
  }

  bench("composite range seek") {
    size_t failures_before = g_failures;
    benchmark_ops("indexed DSV country/score matches", INDEX_BENCHMARK_SAMPLES,
                  g_expected_count) {
      if (index_execute(g_filter, g_expected_count, g_expected_sum) != 0) ++g_failures;
    }
    check_equal(g_failures, failures_before);
  }

  bench("OR union seek") {
    size_t failures_before = g_failures;
    benchmark_ops("indexed DSV OR country ranges", INDEX_BENCHMARK_SAMPLES,
                  g_or_expected_count) {
      if (index_execute(g_or_filter, g_or_expected_count, g_or_expected_sum) != 0)
        ++g_failures;
    }
    check_equal(g_failures, failures_before);
  }
}
