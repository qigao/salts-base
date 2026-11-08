#include "csv_lexer_simde.h"
#include "csv_parser.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

enum { FIELD_BYTES = 64 * 1024 };

static char *make_long_unquoted_csv(void) {
  char *csv = (char *)malloc(FIELD_BYTES + sizeof(",end\n"));
  size_t i;

  if (!csv)
    return NULL;
  for (i = 0; i < FIELD_BYTES; ++i)
    csv[i] = i % 31U == 0U ? ' ' : 'x';
  memcpy(csv + FIELD_BYTES, ",end\n", sizeof(",end\n"));
  return csv;
}

suite("CSV SIMD lexer benchmark") {
  static char *csv;

  before_all() {
    csv = make_long_unquoted_csv();
    check_not_null(csv);
  }

  after_all() {
    free(csv);
  }

  bench("unquoted field scanning") {
    benchmark_bytes("scalar 64KiB scan", 1000, FIELD_BYTES) {
      check_true(csv_find_unquoted_field_end_scalar(csv, csv + FIELD_BYTES + 1) ==
                 csv + FIELD_BYTES);
    }

    benchmark_bytes("SIMDe 64KiB scan", 1000, FIELD_BYTES) {
      check_true(csv_find_unquoted_field_end_simde(csv, csv + FIELD_BYTES + 1) ==
                 csv + FIELD_BYTES);
    }

    benchmark_bytes("64KiB unquoted field parse", 1000, FIELD_BYTES) {
      csv_doc_t *doc = csv_parse(csv, FIELD_BYTES + sizeof(",end\n") - 1);
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);
      check_equal(strlen(csv_get(doc, 0, 0)), FIELD_BYTES);
      check_equal(csv_get(doc, 0, 1), "end");
      csv_free(doc);
    }
  }
}
