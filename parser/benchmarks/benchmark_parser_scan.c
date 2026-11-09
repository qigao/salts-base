#include <csv_parser.h>
#include <cyaml.h>
#include <core/tstr.h>
#include <tinytest.h>
#include <toml.h>
#include <xml_parser/xml_sax.h>

#include <stdlib.h>
#include <string.h>

/* Every workload owns its input until all parser instances have been freed.
 * Fixture construction, validation and assertions are outside timed batches. */
static tstr scan_fixture(const char *prefix, const char *fill, size_t bytes,
                         const char *suffix) {
  tstr payload = tstr_repeat(fill, bytes);
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

#define XML_SCAN_BENCH(title_, prefix_, fill_, bytes_, suffix_) \
  bench(title_) { \
    tstr source = scan_fixture(prefix_, fill_, bytes_, suffix_); \
    check_not_null(source); \
    const xml_sax_handler_t handler = {0}; \
    const size_t length = tstr_len(source); \
    size_t failures = 0u; \
    benchmark_ops("parse XML SAX document", 32, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        xml_sax_parser_t *parser = xml_sax_parser_create(&handler, NULL, length); \
        if (!parser) { ++failures; continue; } \
        if (xml_sax_parser_feed(parser, source, length) != 0 || \
            xml_sax_parser_finish(parser) != 0) ++failures; \
        xml_sax_parser_destroy(parser); \
      } \
    } \
    check_equal(failures, 0u); \
    tstr_free(source); \
  }

suite("XML bounded searches") {
  XML_SCAN_BENCH("16-byte XML comment", "<r><!--", "x", 16u, "--></r>")
  XML_SCAN_BENCH("64-KiB XML comment", "<r><!--", "x", 65536u, "--></r>")
  XML_SCAN_BENCH("64-KiB XML CDATA", "<r><![CDATA[", "x", 65536u, "]]></r>")
  XML_SCAN_BENCH("64-KiB XML attribute", "<r a='", "x", 65536u, "'/>")
  XML_SCAN_BENCH("64-KiB XML whitespace", "<r ", " ", 65536u, "a='v'/>")
  XML_SCAN_BENCH("64-KiB quoted XML DOCTYPE", "<!DOCTYPE r [<!ENTITY v '\x22", "x", 65536u, "\x22'>]><r/>")
}

suite("YAML bounded searches") {
  bench("64-KiB YAML comment") {
    tstr source = scan_fixture("#", "x", 65536u, "\r\nkey: value\n");
    check_not_null(source);
    size_t failures = 0u;
    benchmark_ops("parse YAML document", 32, 8) {
      for (size_t operation = 0u; operation < 8u; ++operation) {
        cyaml_doc_t *document = cyaml_parse(source, tstr_len(source), NULL, NULL);
        if (!document || !cyaml_is_map(cyaml_root(document))) ++failures;
        cyaml_free(document);
      }
    }
    check_equal(failures, 0u);
    tstr_free(source);
  }

  bench("64-KiB multiline YAML scalar events") {
    tstr source = scan_fixture("key: '", "x", 32768u, "\n  ");
    tstr payload = scan_fixture("", "x", 32768u, "'\n");
    check_not_null(source);
    check_not_null(payload);
    check_true(tstr_append(&source, tstr_to_v(payload)));
    tstr_free(payload);
    cyaml_doc_t *document = cyaml_parse(source, tstr_len(source), NULL, NULL);
    check_not_null(document);
    size_t failures = 0u;
    benchmark_ops("resolve YAML scalar events", 32, 8) {
      for (size_t operation = 0u; operation < 8u; ++operation) {
        size_t length = 0u;
        char *events = cyaml_events(document, false, &length);
        if (!events || length < 65536u) ++failures;
        free(events);
      }
    }
    check_equal(failures, 0u);
    cyaml_free(document);
    tstr_free(source);
  }
}

suite("TOML bounded searches") {
  bench("64-KiB TOML literal string") {
    tstr source = scan_fixture("key = '", "x", 65536u, "'\n");
    check_not_null(source);
    size_t failures = 0u;
    benchmark_ops("parse TOML document", 32, 8) {
      for (size_t operation = 0u; operation < 8u; ++operation) {
        char error[256];
        toml_table_t *table = toml_parse(source, error, sizeof(error));
        if (!table) ++failures;
        toml_free(table);
      }
    }
    check_equal(failures, 0u);
    tstr_free(source);
  }
}

#define CSV_SCAN_BENCH(title_, bytes_) \
  bench(title_) { \
    tstr source = scan_fixture("\x22", "x", bytes_, "\x22,end\r\n"); \
    check_not_null(source); \
    size_t failures = 0u; \
    benchmark_ops("parse quoted CSV record", 32, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        csv_doc_t *document = csv_parse(source, tstr_len(source)); \
        if (!document || csv_row_count(document) != 1u) ++failures; \
        csv_free(document); \
      } \
    } \
    check_equal(failures, 0u); \
    tstr_free(source); \
  }

suite("CSV bounded searches") {
  CSV_SCAN_BENCH("16-byte CSV quoted field", 16u)
  CSV_SCAN_BENCH("64-KiB CSV quoted field", 65536u)
}
