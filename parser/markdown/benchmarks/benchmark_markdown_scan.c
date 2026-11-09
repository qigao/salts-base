#include <markdown.h>
#include <tinytest.h>

#include <string.h>

/* The parser borrows the fixture only during md_parse and owns each result.
 * Input construction and semantic checks stay outside the timed batches. */
static char markdown_input[65536u + 4u];
static volatile size_t markdown_text_bytes;

#define CODE_SPAN_BENCH(title_, size_, multiline_) \
  bench(title_) { \
    const size_t payload_bytes = (size_); \
    const size_t input_bytes = payload_bytes + 2u; \
    markdown_input[0] = '`'; \
    memset(markdown_input + 1u, 'x', payload_bytes); \
    if (multiline_) markdown_input[payload_bytes / 2u] = '\n'; \
    markdown_input[input_bytes - 1u] = '`'; \
    md_document *initial = NULL; \
    check_equal(md_parse(markdown_input, input_bytes, 128u, input_bytes + 1u, &initial), SALTS_MD_OK); \
    const md_rich_document_view expected = md_get_document_view(initial); \
    size_t code_spans = 0u; \
    for (size_t i = 0u; i < expected.document.block_count; ++i) \
      code_spans += expected.document.blocks[i].kind == SALTS_MD_BLOCK_CODE_SPAN; \
    check_equal(code_spans, 1u); \
    check_equal(expected.document.source_bytes, payload_bytes); \
    md_document_destroy(initial); \
    size_t failures = 0u; \
    benchmark_ops("parse owned code span", 64, 8) { \
      for (size_t operation = 0u; operation < 8u; ++operation) { \
        md_document *document = NULL; \
        if (md_parse(markdown_input, input_bytes, 128u, input_bytes + 1u, &document) != SALTS_MD_OK) \
          ++failures; \
        markdown_text_bytes = md_get_document_view(document).document.source_bytes; \
        md_document_destroy(document); \
      } \
    } \
    check_equal(failures, 0u); \
    check_equal(markdown_text_bytes, payload_bytes); \
  }

suite("Markdown bounded scan workloads") {
  CODE_SPAN_BENCH("15-byte code span", 15u, 0)
  CODE_SPAN_BENCH("16-byte code span", 16u, 0)
  CODE_SPAN_BENCH("17-byte code span", 17u, 0)
  CODE_SPAN_BENCH("256-byte code span", 256u, 0)
  CODE_SPAN_BENCH("64-KiB code span", 65536u, 0)
  CODE_SPAN_BENCH("64-KiB multiline code span", 65536u, 1)
}
