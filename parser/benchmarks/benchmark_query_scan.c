#include <cmd_arger_internal.h>
#include <cyaml_ypath_lexer.h>
#include <dsv_filter.h>
#include <core/tstr.h>
#include <selector.h>
#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

static volatile size_t query_scan_result;

static tstr query_fixture(const char *prefix, const char *fill, size_t count,
                           const char *suffix) {
  tstr payload = tstr_repeat(fill, count);
  if (!payload) return NULL;
  tstr source = tstr_dup(prefix);
  if (!source || !tstr_append(&source, tstr_to_v(payload)) ||
      !tstr_append(&source, vstr_from_cstr(suffix))) {
    tstr_free(source);
    source = NULL;
  }
  tstr_free(payload);
  return source;
}

#define SELECTOR_SCAN_BENCH(title_, prefix_, fill_, count_, suffix_) \
  bench(title_) { \
    tstr source = query_fixture(prefix_, fill_, count_, suffix_); \
    check_not_null(source); \
    const char *fields[] = {"region"}; \
    const selector_schema_v1_t schema = { \
        .size = SALTS_SELECTOR_SCHEMA_V1_SIZE, .allowed_fields = fields, .allowed_field_count = 1u}; \
    for (size_t warmup = 0u; warmup < 64u; ++warmup) { \
      selector_program_t *program = NULL; \
      selector_diagnostic_v1_t diagnostic = {.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE}; \
      check_equal(selector_compile_v1(source, tstr_len(source), &schema, &program, &diagnostic), SALTS_SELECTOR_OK); \
      selector_program_destroy(program); \
    } \
    size_t failures = 0u; \
    benchmark_ops("compile selector", 64, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        selector_program_t *program = NULL; \
        selector_diagnostic_v1_t diagnostic = {.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE}; \
        if (selector_compile_v1(source, tstr_len(source), &schema, &program, &diagnostic) != SALTS_SELECTOR_OK) \
          ++failures; \
        selector_program_destroy(program); \
      } \
    } \
    check_equal(failures, 0u); \
    tstr_free(source); \
  }

suite("Selector scan workloads") {
  SELECTOR_SCAN_BENCH("short selector", "region == \"", "x", 8u, "\"")
  SELECTOR_SCAN_BENCH("256-byte selector literal", "region == \"", "x", 256u, "\"")
  SELECTOR_SCAN_BENCH("3-KiB selector whitespace", "", " \t\r\n", 768u, "region == \"east\"")
}

#define YPATH_SCAN_BENCH(title_, prefix_, fill_, count_, suffix_, type_, length_) \
  bench(title_) { \
    tstr source = query_fixture(prefix_, fill_, count_, suffix_); \
    check_not_null(source); \
    size_t failures = 0u; \
    benchmark_ops("read YPath token", 64, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        ypath_lexer_t lexer; \
        ypath_lex_init(&lexer, source); \
        ypath_lex_next(&lexer); \
        if (lexer.tok.type != (type_) || lexer.tok.len != (length_)) ++failures; \
        query_scan_result = lexer.tok.len; \
      } \
    } \
    check_equal(failures, 0u); \
    tstr_free(source); \
  }

suite("YPath scan workloads") {
  YPATH_SCAN_BENCH("64-KiB YPath whitespace", "", " \t", 32768u, "field", YPATH_TOK_IDENT, 5u)
  YPATH_SCAN_BENCH("64-KiB YPath string", "\"", "x", 65536u, "\"", YPATH_TOK_STRING, 65536u)
}

#define RESPONSE_SCAN_BENCH(title_, prefix_, fill_, count_, suffix_, tokens_, bytes_) \
  bench(title_) { \
    tstr source = query_fixture(prefix_, fill_, count_, suffix_); \
    check_not_null(source); \
    size_t failures = 0u; \
    benchmark_ops("scan response arguments", 64, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        const char *cursor = source, *start; \
        size_t length, tokens = 0u, bytes = 0u; \
        while ((length = lex_next_arg(&cursor, &start)) != 0u) { ++tokens; bytes += length; } \
        if (tokens != (tokens_) || bytes != (bytes_)) ++failures; \
        query_scan_result = bytes; \
      } \
    } \
    check_equal(failures, 0u); \
    tstr_free(source); \
  }

suite("Command argument scan workloads") {
  RESPONSE_SCAN_BENCH("64-KiB response argument", "", "x", 65536u, "", 1u, 65536u)
  RESPONSE_SCAN_BENCH("64-KiB quoted response argument", "\"", "x", 65536u, "\"", 1u, 65536u)
  RESPONSE_SCAN_BENCH("128 short response arguments", "", "arg ", 128u, "", 128u, 384u)

  bench("64-KiB command help text") {
    tstr source = query_fixture("{{green}}", "x", 65536u, "{{reset}}");
    check_not_null(source);
    char *output = malloc(tstr_len(source) + 1u);
    check_not_null(output);
    benchmark_ops("strip help colors", 64, 8) {
      for (size_t operation = 0u; operation < 8u; ++operation)
        mustache_colorize(output, source, cmd_arger_false);
    }
    check_equal(strlen(output), 65536u);
    check_equal(output, source + sizeof("{{green}}") - 1u, 65536u);
    free(output);
    tstr_free(source);
  }
}

suite("DSV expression scans") {
  bench("3-KiB arithmetic expression whitespace") {
    const char csv[] = "age_n\n1\n";
    csv_doc_t *document = csv_parse(csv, sizeof(csv) - 1u);
    check_not_null(document);
    tstr expression = query_fixture("age", " \t\r\n", 768u, "+1 > 0");
    check_not_null(expression);
    size_t failures = 0u;
    benchmark_ops("compile DSV expression", 64, 8) {
      for (size_t operation = 0u; operation < 8u; ++operation) {
        dsv_filter_t *filter = dsv_filter_create(document, 0u);
        if (!filter || !dsv_filter_compile(filter, expression)) ++failures;
        dsv_filter_destroy(filter);
      }
    }
    check_equal(failures, 0u);
    tstr_free(expression);
    csv_free(document);
  }
}
