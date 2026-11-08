/**
 * @file test_csv_parser.c
 * @brief CSV Parser Tests - RFC 4180 Compliance
 */

#include "csv_parser.h"
#include "tinytest.h"
#include <fmt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char records[4][128];
  size_t lengths[4];
  size_t count;
  size_t fail_at;
} csv_write_state_t;

static int capture_csv_record(const void *data, size_t len, void *user) {
  csv_write_state_t *state = (csv_write_state_t *)user;
  if (state->count < 4) {
    size_t copy_len = len < sizeof(state->records[0]) - 1 ? len : sizeof(state->records[0]) - 1;
    memcpy(state->records[state->count], data, copy_len);
    state->records[state->count][copy_len] = '\0';
    state->lengths[state->count] = len;
  }
  state->count++;
  return state->fail_at > 0 && state->count >= state->fail_at ? -1 : 0;
}

typedef struct {
  size_t row_count;
  size_t field_count;
  char last_field[256];
} stream_test_ctx_t;

typedef struct {
  size_t count;
  size_t last_row;
  int64_t age_sum;
  char note[32];
} scan_test_ctx_t;

static int capture_scan_match(void *ctx, size_t row_index,
                              const csv_scan_value_t *values, size_t value_count) {
  scan_test_ctx_t *state = (scan_test_ctx_t *)ctx;
  if (!state || !values || value_count != 2 ||
      values[0].type != CSV_SCAN_VALUE_INT64 || values[1].type != CSV_SCAN_VALUE_TEXT)
    return -1;
  state->count++;
  state->last_row = row_index;
  state->age_sum += values[0].value.integer;
  if (values[1].value.text.len >= sizeof(state->note)) return -1;
  memcpy(state->note, values[1].value.text.data, values[1].value.text.len);
  state->note[values[1].value.text.len] = '\0';
  return 0;
}

static int on_row_start(void *ctx, size_t row_index) {
  (void)row_index;
  stream_test_ctx_t *tc = (stream_test_ctx_t *)ctx;
  tc->row_count++;
  return 0;
}

static int on_field(void *ctx, size_t row_index, size_t col_index, const char *value, size_t len) {
  (void)row_index;
  (void)col_index;
  stream_test_ctx_t *tc = (stream_test_ctx_t *)ctx;
  tc->field_count++;
  if (len < sizeof(tc->last_field)) {
    memcpy(tc->last_field, value, len);
    tc->last_field[len] = '\0';
  }
  return 0;
}

static int on_row_end(void *ctx, size_t row_index, size_t field_count) {
  (void)ctx;
  (void)row_index;
  (void)field_count;
  return 0;
}

spec("csv_parser") {
  describe("Basic Parsing") {
    it("should parse a simple CSV string correctly") {
      const char *csv = "a,b,c\n1,2,3\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 2);
      check_equal(csv_column_count(doc), 3);

      check_equal(csv_get(doc, 0, 0), "a");
      check_equal(csv_get(doc, 0, 1), "b");
      check_equal(csv_get(doc, 0, 2), "c");
      check_equal(csv_get(doc, 1, 0), "1");
      check_equal(csv_get(doc, 1, 1), "2");
      check_equal(csv_get(doc, 1, 2), "3");

      csv_free(doc);
    }

    it("should handle CSV strings without a trailing newline") {
      const char *csv = "a,b,c\n1,2,3";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 2);

      check_equal(csv_get(doc, 0, 0), "a");
      check_equal(csv_get(doc, 1, 2), "3");

      csv_free(doc);
    }

    it("should handle CRLF line endings") {
      const char *csv = "a,b,c\r\n1,2,3\r\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 2);

      check_equal(csv_get(doc, 0, 0), "a");
      check_equal(csv_get(doc, 1, 2), "3");

      csv_free(doc);
    }
  }

  describe("Empty Field Handling (RFC 4180 Section 2.5)") {
    it("should handle empty fields in the middle") {
      const char *csv = "a,,c\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);
      check_equal(csv_column_count(doc), 3);

      check_equal(csv_get(doc, 0, 0), "a");
      check_equal(csv_get(doc, 0, 1), "");
      check_equal(csv_get(doc, 0, 2), "c");

      csv_free(doc);
    }

    it("should handle empty fields at the start") {
      const char *csv = ",b,c\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "");
      check_equal(csv_get(doc, 0, 1), "b");
      check_equal(csv_get(doc, 0, 2), "c");

      csv_free(doc);
    }

    it("should handle empty fields at the end") {
      const char *csv = "a,b,\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "a");
      check_equal(csv_get(doc, 0, 1), "b");
      check_equal(csv_get(doc, 0, 2), "");

      csv_free(doc);
    }

    it("should handle all empty fields") {
      const char *csv = ",,\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_column_count(doc), 3);

      check_equal(csv_get(doc, 0, 0), "");
      check_equal(csv_get(doc, 0, 1), "");
      check_equal(csv_get(doc, 0, 2), "");

      csv_free(doc);
    }
  }

  describe("Quoted Fields (RFC 4180 Section 2.6)") {
    it("should parse quoted fields correctly") {
      const char *csv = "\"hello\",world\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "hello");
      check_equal(csv_get(doc, 0, 1), "world");

      csv_free(doc);
    }

    it("should handle commas within quoted fields") {
      const char *csv = "\"hello, world\",test\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_column_count(doc), 2);

      check_equal(csv_get(doc, 0, 0), "hello, world");
      check_equal(csv_get(doc, 0, 1), "test");

      csv_free(doc);
    }

    it("should handle newlines within quoted fields") {
      const char *csv = "\"line1\nline2\",test\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);

      check_equal(csv_get(doc, 0, 0), "line1\nline2");
      check_equal(csv_get(doc, 0, 1), "test");

      csv_free(doc);
    }

    it("should handle CRLF within quoted fields") {
      const char *csv = "\"line1\r\nline2\",test\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);

      check_equal(csv_get(doc, 0, 0), "line1\r\nline2");

      csv_free(doc);
    }
  }

  describe("Escaped Quotes (RFC 4180 Section 2.7)") {
    it("should handle escaped double quotes correctly") {
      const char *csv = "\"say \"\"hello\"\"\",test\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "say \"hello\"");
      check_equal(csv_get(doc, 0, 1), "test");

      csv_free(doc);
    }

    it("should handle multiple instances of escaped quotes") {
      const char *csv = "\"\"\"a\"\"\",\"\"\"\"\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "\"a\"");
      check_equal(csv_get(doc, 0, 1), "\"");

      csv_free(doc);
    }

    it("should handle empty quoted fields") {
      const char *csv = "\"\",b,c\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get(doc, 0, 0), "");
      check_equal(csv_get(doc, 0, 1), "b");

      csv_free(doc);
    }
  }

  describe("Header Support") {
    it("should parse CSV correctly when a header is present") {
      const char *csv = "name,age,city\nAlice,30,NYC\nBob,25,LA\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.has_header = true;

      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);
      check(csv_has_header(doc));
      check_equal(csv_row_count(doc), 2);

      check_equal(csv_header_get(doc, 0), "name");
      check_equal(csv_header_get(doc, 1), "age");
      check_equal(csv_header_get(doc, 2), "city");

      check_equal(csv_get(doc, 0, 0), "Alice");
      check_equal(csv_get(doc, 0, 1), "30");
      check_equal(csv_get(doc, 1, 0), "Bob");

      csv_free(doc);
    }

    it("should allow data retrieval by column name") {
      const char *csv = "name,age,city\nAlice,30,NYC\nBob,25,LA\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.has_header = true;

      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      check_equal(csv_get_by_name(doc, 0, "name"), "Alice");
      check_equal(csv_get_by_name(doc, 0, "age"), "30");
      check_equal(csv_get_by_name(doc, 0, "city"), "NYC");
      check_equal(csv_get_by_name(doc, 1, "name"), "Bob");

      check_null(csv_get_by_name(doc, 0, "nonexistent"));

      csv_free(doc);
    }

    it("should find the index of a column by its name") {
      const char *csv = "name,age,city\nAlice,30,NYC\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.has_header = true;

      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      check_equal((int)csv_find_column(doc, "name"), 0);
      check_equal((int)csv_find_column(doc, "age"), 1);
      check_equal((int)csv_find_column(doc, "city"), 2);
      check_equal((int)csv_find_column(doc, "nonexistent"), -1);

      csv_free(doc);
    }
  }

  describe("Parser Options") {
    it("should parse custom delimiters and quote characters") {
      const char *csv = "'name';'note'\n'Ada';'hello; world'\n'Bob';'it''s ok'\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.delimiter = ';';
      opts.quote = '\'';

      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);
      check_equal(csv_row_count(doc), 3);
      check_equal(csv_column_count(doc), 2);
      check_equal(csv_get(doc, 1, 1), "hello; world");
      check_equal(csv_get(doc, 2, 1), "it's ok");

      csv_free(doc);
    }

    it("should preserve empty rows when requested") {
      const char *csv = "a\n\nb\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.skip_empty_rows = false;

      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);
      check_equal(csv_row_count(doc), 3);
      check_equal(csv_get(doc, 0, 0), "a");
      check_null(csv_get(doc, 1, 0));
      check_equal(csv_get(doc, 2, 0), "b");

      csv_free(doc);
    }
  }

  describe("Type Conversion") {
    it("should convert fields to integers correctly") {
      const char *csv = "10,20,abc\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_equal(csv_get_int(doc, 0, 0, -1), 10);
      check_equal(csv_get_int(doc, 0, 1, -1), 20);
      check_equal(csv_get_int(doc, 0, 2, -1), 0);   // "abc" -> 0
      check_equal(csv_get_int(doc, 0, 99, -1), -1); // Out of bounds

      csv_free(doc);
    }

    it("should convert fields to doubles correctly") {
      const char *csv = "1.5,2.7,3\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check_within(csv_get_double(doc, 0, 0, 0.0), 1.5, 0.01);
      check_within(csv_get_double(doc, 0, 1, 0.0), 2.7, 0.01);
      check_within(csv_get_double(doc, 0, 2, 0.0), 3.0, 0.01);

      csv_free(doc);
    }

    it("should convert fields to booleans correctly") {
      const char *csv = "true,false,1,0,yes,no,T,F\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      check(csv_get_bool(doc, 0, 0, false));
      check(!csv_get_bool(doc, 0, 1, true));
      check(csv_get_bool(doc, 0, 2, false));
      check(!csv_get_bool(doc, 0, 3, true));
      check(csv_get_bool(doc, 0, 4, false));
      check(!csv_get_bool(doc, 0, 5, true));
      check(csv_get_bool(doc, 0, 6, false));
      check(!csv_get_bool(doc, 0, 7, true));

      csv_free(doc);
    }
  }

  describe("Streaming API") {
    it("should support streaming parsing of simple CSV") {
      const char *csv = "a,b,c\n1,2,3\n";

      csv_stream_handler_t handler = {
          .on_row_start = on_row_start, .on_field = on_field, .on_row_end = on_row_end};

      stream_test_ctx_t ctx = {0};
      int ret = csv_parse_stream(csv, strlen(csv), &handler, &ctx);

      check_equal(ret, 0);
      check_equal(ctx.row_count, 2);
      check_equal(ctx.field_count, 6);
      check_equal(ctx.last_field, "3");
    }

    it("should support streaming parsing of quoted data") {
      const char *csv = "\"hello, world\",\"say \"\"hi\"\"\"\n";

      csv_stream_handler_t handler = {.on_field = on_field};

      stream_test_ctx_t ctx = {0};
      int ret = csv_parse_stream(csv, strlen(csv), &handler, &ctx);

      check_equal(ret, 0);
      check_equal(ctx.field_count, 2);
      check_equal(ctx.last_field, "say \"hi\"");
    }

    it("should honor streaming parser options") {
      const char *csv = "'a'\t'b'\n'1'\t'2'\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.delimiter = '\t';
      opts.quote = '\'';

      csv_stream_handler_t handler = {
          .on_row_start = on_row_start, .on_field = on_field, .on_row_end = on_row_end};

      stream_test_ctx_t ctx = {0};
      int ret = csv_parse_stream_opts(csv, strlen(csv), &handler, &ctx, &opts);

      check_equal(ret, 0);
      check_equal(ctx.row_count, 2);
      check_equal(ctx.field_count, 4);
      check_equal(ctx.last_field, "2");
    }
  }

  describe("Iterator API") {
    it("should support iterating over rows and fields") {
      const char *csv = "a,b,c\n1,2,3\n4,5,6\n";

      csv_iter_t *iter = csv_iter_new(csv, strlen(csv));
      check_not_null(iter);

      check(csv_iter_next(iter));
      check_equal(csv_iter_field_count(iter), 3);
      check_equal(csv_iter_field(iter, 0), "a");
      check_equal(csv_iter_field(iter, 1), "b");
      check_equal(csv_iter_field(iter, 2), "c");
      check_equal(csv_iter_row_index(iter), 0);

      check(csv_iter_next(iter));
      check_equal(csv_iter_field(iter, 0), "1");
      check_equal(csv_iter_row_index(iter), 1);

      check(csv_iter_next(iter));
      check_equal(csv_iter_field(iter, 0), "4");
      check_equal(csv_iter_row_index(iter), 2);

      check(!csv_iter_next(iter));

      csv_iter_free(iter);
    }

    it("should support iterating over quoted data") {
      const char *csv = "\"hello, world\",\"line1\nline2\"\n";

      csv_iter_t *iter = csv_iter_new(csv, strlen(csv));
      check_not_null(iter);

      check(csv_iter_next(iter));
      check_equal(csv_iter_field_count(iter), 2);
      check_equal(csv_iter_field(iter, 0), "hello, world");
      check_equal(csv_iter_field(iter, 1), "line1\nline2");

      csv_iter_free(iter);
    }

    it("should honor iterator parser options") {
      const char *csv = "'x';'y;y'\n'1';'2'\n";
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      opts.delimiter = ';';
      opts.quote = '\'';

      csv_iter_t *iter = csv_iter_new_opts(csv, strlen(csv), &opts);
      check_not_null(iter);

      check(csv_iter_next(iter));
      check_equal(csv_iter_field_count(iter), 2);
      check_equal(csv_iter_field(iter, 1), "y;y");

      check(csv_iter_next(iter));
      check_equal(csv_iter_field(iter, 0), "1");
      check_equal(csv_iter_field(iter, 1), "2");

      check(!csv_iter_next(iter));
      csv_iter_free(iter);
    }
  }

  describe("Edge Cases") {
    it("should handle single-field CSV data") {
      const char *csv = "hello\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);
      check_equal(csv_column_count(doc), 1);
      check_equal(csv_get(doc, 0, 0), "hello");
      csv_free(doc);
    }

    it("should handle single-field data without a trailing newline") {
      const char *csv = "hello";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 1);
      check_equal(csv_get(doc, 0, 0), "hello");
      csv_free(doc);
    }

    it("should preserve leading and trailing whitespace within fields") {
      const char *csv = " a , b , c \n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);

      // RFC 4180: spaces are part of the field
      check_equal(csv_get(doc, 0, 0), " a ");
      check_equal(csv_get(doc, 0, 1), " b ");
      check_equal(csv_get(doc, 0, 2), " c ");

      csv_free(doc);
    }

    it("should preserve a long unquoted field including spaces") {
      enum { FIELD_BYTES = 4096 };
      char *csv = (char *)malloc(FIELD_BYTES + sizeof(",end\n"));
      size_t i;

      check_not_null(csv);
      for (i = 0; i < FIELD_BYTES; ++i)
        csv[i] = i % 31U == 0U ? ' ' : 'x';
      memcpy(csv + FIELD_BYTES, ",end\n", sizeof(",end\n"));

      csv_doc_t *doc = csv_parse(csv, FIELD_BYTES + sizeof(",end\n") - 1);
      check_not_null(doc);
      check_equal(strlen(csv_get(doc, 0, 0)), FIELD_BYTES);
      check_equal(csv_get(doc, 0, 0), csv, FIELD_BYTES);
      check_equal(csv_get(doc, 0, 1), "end");

      csv_free(doc);
      free(csv);
    }

    it("should handle a large number of rows correctly") {
      char csv[10000];
      int offset = 0;
      for (int i = 0; i < 100; i++) {
        offset += fmt(csv + offset, sizeof(csv) - (size_t)offset, "{},{},{}\n", i, i * 2, i * 3);
      }

      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_row_count(doc), 100);
      check_equal(csv_get_int(doc, 99, 0, -1), 99);
      check_equal(csv_get_int(doc, 99, 1, -1), 198);

      csv_free(doc);
    }

    it("should provide indexed access to distant data rows after a header") {
      enum { ROWS = 4096 };
      const size_t capacity = (size_t)ROWS * 24 + 16;
      char *csv = (char *)malloc(capacity);
      size_t offset = 0;
      csv_options_t opts = CSV_OPTIONS_DEFAULT;

      check_not_null(csv);
      offset += (size_t)fmt_text(csv + offset, capacity - offset, "id,value\n");
      for (int i = 0; i < ROWS; ++i) {
        offset += (size_t)fmt(csv + offset, capacity - offset, "{},{}\n", i, i * 3);
      }
      opts.has_header = true;

      csv_doc_t *doc = csv_parse_opts(csv, offset, &opts);
      check_not_null(doc);
      check_equal(csv_row_count(doc), ROWS);
      check_equal(csv_header_get(doc, 0), "id");
      check_equal(csv_get(doc, 0, 1), "0");
      check_equal(csv_get(doc, 2048, 1), "6144");
      check_equal(csv_get(doc, ROWS - 1, 1), "12285");

      csv_free(doc);
      free(csv);
    }

    it("should handle NULL pointer and empty strings gracefully") {
      check_null(csv_parse(NULL, 0));
      check_null(csv_parse("", 0));
      check_null(csv_get(NULL, 0, 0));
    }
  }

  describe("csv_to_string serialization") {
    it("should roundtrip simple CSV") {
      const char *csv = "a,b,c\n1,2,3\n4,5,6\n";
      csv_options_t opts = {true, ',', '"', true};
      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      char *out = csv_to_string(doc);
      check_not_null(out);

      csv_doc_t *doc2 = csv_parse_opts(out, strlen(out), &opts);
      check_not_null(doc2);
      check_equal(csv_row_count(doc2), 2);
      check_equal(csv_header_get(doc2, 0), "a");
      check_equal(csv_get(doc2, 0, 0), "1");
      check_equal(csv_get(doc2, 1, 2), "6");

      csv_free(doc2);
      free(out);
      csv_free(doc);
    }

    it("should escape fields with commas and quotes") {
      const char *csv = "name,value\n\"hello, world\",42\n\"say \"\"hi\"\"\",99\n";
      csv_options_t opts = {true, ',', '"', true};
      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      char *out = csv_to_string(doc);
      check_not_null(out);

      csv_doc_t *doc2 = csv_parse_opts(out, strlen(out), &opts);
      check_not_null(doc2);
      check_equal(csv_get(doc2, 0, 0), "hello, world");
      check_equal(csv_get(doc2, 1, 0), "say \"hi\"");

      csv_free(doc2);
      free(out);
      csv_free(doc);
    }

    it("should escape fields with newlines") {
      const char *csv = "a,b\n\"line1\nline2\",ok\n";
      csv_options_t opts = {true, ',', '"', true};
      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      char *out = csv_to_string(doc);
      check_not_null(out);

      csv_doc_t *doc2 = csv_parse_opts(out, strlen(out), &opts);
      check_not_null(doc2);
      check_equal(csv_get(doc2, 0, 0), "line1\nline2");
      check_equal(csv_get(doc2, 0, 1), "ok");

      csv_free(doc2);
      free(out);
      csv_free(doc);
    }

    it("should return NULL for NULL doc") {
      check_null(csv_to_string(NULL));
    }

    it("should preserve header in roundtrip") {
      const char *csv = "x,y,z\n10,20,30\n";
      csv_options_t opts = {true, ',', '"', true};
      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      char *out = csv_to_string(doc);
      check_not_null(out);

      csv_doc_t *doc2 = csv_parse_opts(out, strlen(out), &opts);
      check_not_null(doc2);
      check_equal(csv_has_header(doc2), 1);
      check_equal(csv_header_get(doc2, 0), "x");
      check_equal(csv_header_get(doc2, 1), "y");
      check_equal(csv_header_get(doc2, 2), "z");

      csv_free(doc2);
      free(out);
      csv_free(doc);
    }

    it("should write one complete logical record per callback") {
      const char *csv = "name,value\n\"line1\nline2\",\"say \"\"hi\"\"\"\ntail,end\n";
      csv_options_t opts = {true, ',', '"', true};
      csv_write_state_t state = {0};
      csv_doc_t *doc = csv_parse_opts(csv, strlen(csv), &opts);
      check_not_null(doc);

      check_equal(csv_write_records(doc, capture_csv_record, &state), 0);
      check_equal(state.count, 3);
      check_equal(state.records[0], "name,value\n");
      check_equal(state.records[1], "\"line1\nline2\",\"say \"\"hi\"\"\"\n");
      check_equal(state.records[2], "tail,end\n");
      csv_free(doc);
    }

    it("should stop writing when a record callback fails") {
      const char *csv = "a,b\n1,2\n3,4\n";
      csv_write_state_t state = {.fail_at = 2};
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      check_not_null(doc);
      check_equal(csv_write_records(doc, capture_csv_record, &state), -1);
      check_equal(state.count, 2);
      csv_free(doc);
    }

    it("should iterate rows with reusable cursor field views") {
      const char *csv = "a,b\n1,2\n3,4\n";
      csv_doc_t *doc = csv_parse(csv, strlen(csv));
      csv_cursor_t *cursor;
      size_t field_count = 0;
      const vstr *fields;

      check_not_null(doc);
      cursor = csv_cursor_new(doc, 1);
      check_not_null(cursor);

      check_equal(csv_cursor_next(cursor), 1);
      check_equal(csv_cursor_row_index(cursor), 1);
      fields = csv_cursor_fields(cursor, &field_count);
      check_equal(field_count, 2);
      check_equal(fields[0].data, "1");
      check_equal(csv_cursor_field_v(cursor, 1).data, "2");

      check_equal(csv_cursor_next(cursor), 1);
      check_equal(csv_cursor_row_index(cursor), 2);
      fields = csv_cursor_fields(cursor, &field_count);
      check_equal(field_count, 2);
      check_equal(fields[0].data, "3");
      check_equal(csv_cursor_next(cursor), 0);
      check_null(csv_cursor_fields(cursor, &field_count));
      check_equal(field_count, 0);

      csv_cursor_free(cursor);
      csv_free(doc);
    }

    it("should scan selected fields without building a document") {
      const char *csv = "id,age,country,score,note\n"
                        "1,21,CN,91,\"say \"\"hi\"\"\"\n"
                        "2,22,US,99,skip\n"
                        "3,23,CN,90,skip\n"
                        "4,24,CN,92,ok\n";
      const csv_scan_predicate_t predicates[] = {
          {.column = 2, .type = CSV_SCAN_VALUE_TEXT, .op = CSV_SCAN_OP_EQ,
           .text = {.data = "CN", .len = 2}},
          {.column = 3, .type = CSV_SCAN_VALUE_INT64, .op = CSV_SCAN_OP_GT,
           .integer = 90},
          {.column = 3, .type = CSV_SCAN_VALUE_INT64, .op = CSV_SCAN_OP_LE,
           .integer = 92},
      };
      const csv_scan_projection_t projections[] = {
          {.column = 1, .type = CSV_SCAN_VALUE_INT64},
          {.column = 4, .type = CSV_SCAN_VALUE_TEXT},
      };
      csv_scan_plan_t plan = {
          .predicates = predicates,
          .predicate_count = sizeof(predicates) / sizeof(predicates[0]),
          .projections = projections,
          .projection_count = sizeof(projections) / sizeof(projections[0]),
          .on_match = capture_scan_match,
      };
      csv_options_t opts = CSV_OPTIONS_DEFAULT;
      scan_test_ctx_t state = {0};
      size_t matches = 0;

      opts.has_header = true;
      plan.ctx = &state;
      check_equal(csv_filter_scan_opts(csv, strlen(csv), &opts, &plan, &matches), 0);
      check_equal(matches, 2);
      check_equal(state.count, 2);
      check_equal(state.last_row, 3);
      check_equal(state.age_sum, 45);
      check_equal(state.note, "ok");
    }
  }
}
