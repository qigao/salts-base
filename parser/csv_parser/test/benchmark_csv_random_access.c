#include "csv_parser.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  BENCHMARK_ROWS = 100000,
  LOOKUPS_PER_RUN = 100000,
};

static volatile uint64_t g_sink;

static char *make_csv(size_t rows) {
  const size_t capacity = rows * 24 + 16;
  char *csv = (char *)malloc(capacity);
  size_t offset = 0;
  size_t row;

  if (!csv)
    return NULL;
  offset += (size_t)snprintf(csv + offset, capacity - offset, "id,value\n");
  for (row = 0; row < rows; ++row) {
    offset += (size_t)snprintf(csv + offset, capacity - offset, "%zu,%zu\n", row, row * 3);
  }
  return csv;
}

static uint64_t read_random_rows(const csv_doc_t *doc, size_t rows, size_t lookups) {
  unsigned int state = 0x9e3779b9U;
  uint64_t sum = 0;
  size_t i;

  for (i = 0; i < lookups; ++i) {
    state = state * 1664525U + 1013904223U;
    sum += (uint64_t)csv_get_int(doc, state % rows, 1, 0);
  }
  return sum;
}

static uint64_t read_sequential_rows_indexed(const csv_doc_t *doc, size_t rows) {
  uint64_t sum = 0;
  size_t row;
  for (row = 0; row < rows; ++row) {
    sum += (uint64_t)csv_get_int(doc, row, 1, 0);
  }
  return sum;
}

static uint64_t read_sequential_rows_cursor(csv_cursor_t *cursor, size_t rows) {
  uint64_t sum = 0;
  size_t seen = 0;
  if (csv_cursor_rewind(cursor, 0) != 0) return 0;
  while (seen < rows && csv_cursor_next(cursor) == 1) {
    vstr value = csv_cursor_field_v(cursor, 1);
    sum += (uint64_t)strtol(value.data ? value.data : "0", NULL, 10);
    ++seen;
  }
  return seen == rows ? sum : 0;
}

suite("CSV random access benchmark") {
  static char *csv;
  static csv_doc_t *doc;
  static csv_cursor_t *cursor;

  before_all() {
    csv_options_t options = CSV_OPTIONS_DEFAULT;
    csv = make_csv(BENCHMARK_ROWS);
    check_not_null(csv);
    options.has_header = true;
    doc = csv_parse_opts(csv, strlen(csv), &options);
    check_not_null(doc);
    check_equal(csv_row_count(doc), BENCHMARK_ROWS);
    cursor = csv_cursor_new(doc, 0);
    check_not_null(cursor);
  }

  after_all() {
    csv_cursor_free(cursor);
    csv_free(doc);
    free(csv);
  }

  bench("indexed row lookup") {
    benchmark_ops("100K pseudo-random rows", 3, LOOKUPS_PER_RUN) {
      g_sink = read_random_rows(doc, BENCHMARK_ROWS, LOOKUPS_PER_RUN);
    }
  }

  bench("sequential row cursor") {
    benchmark_ops("100K sequential rows via indexed access", 3, BENCHMARK_ROWS) {
      g_sink = read_sequential_rows_indexed(doc, BENCHMARK_ROWS);
    }
    benchmark_ops("100K sequential rows via cursor", 3, BENCHMARK_ROWS) {
      g_sink = read_sequential_rows_cursor(cursor, BENCHMARK_ROWS);
    }
  }
}
