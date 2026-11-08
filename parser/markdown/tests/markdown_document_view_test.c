/* Markdown-owned RichDoc contract migrated from Stun ui_core_test.c.
 * Stun retains its UI Runtime, rect layout and CMeta tests.
 */
#include <salts/markdown.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct visitor_state {
    size_t visited;
    size_t text_bytes;
    bool stop_early;
} visitor_state;

static salts_md_status record_block(const salts_md_block *block, const char *text,
                                    size_t bytes, void *user_data) {
    visitor_state *state = (visitor_state *)user_data;
    if (!block || (!text && bytes)) return SALTS_MD_INVALID_ARGUMENT;
    ++state->visited;
    if (block->kind == SALTS_MD_BLOCK_HEADING ||
        block->kind == SALTS_MD_BLOCK_CODE_FENCE)
        state->text_bytes += bytes;
    return (state->stop_early && state->visited == 2u)
        ? SALTS_MD_INVALID_ARGUMENT : SALTS_MD_OK;
}

static int test_document_visitor(void) {
    static const char source[] = "# Hello\n\n~~~c\nint x;\n~~~\n";
    const char *heading = strstr(source, "Hello");
    const char *code = strstr(source, "int x;");
    CHECK(heading && code);
    size_t h0 = (size_t)(heading - source), c0 = (size_t)(code - source);
    const salts_md_block blocks[] = {
        {1u, SALTS_MD_PARENT_NONE, SALTS_MD_BLOCK_DOCUMENT, 0u, 0u, sizeof(source)-1u},
        {2u, 0u, SALTS_MD_BLOCK_HEADING, 1u, h0, h0+5u},
        {3u, 0u, SALTS_MD_BLOCK_CODE_FENCE, 0u, c0, c0+6u}
    };
    salts_md_document_view doc = {source, sizeof(source)-1u, blocks, 3u};
    CHECK(salts_md_document_validate(&doc, 3u) == SALTS_MD_OK);
    CHECK(salts_md_document_validate(&doc, 2u) == SALTS_MD_CAPACITY_EXCEEDED);
    visitor_state state = {0};
    CHECK(salts_md_document_visit(&doc, 3u, record_block, &state) == SALTS_MD_OK);
    CHECK(state.visited == 3u && state.text_bytes == 11u);
    state = (visitor_state){.stop_early = true};
    CHECK(salts_md_document_visit(&doc, 3u, record_block, &state) ==
          SALTS_MD_INVALID_ARGUMENT);
    CHECK(state.visited == 2u);
    const salts_md_block invalid[] = {
        {1u, SALTS_MD_PARENT_NONE, SALTS_MD_BLOCK_DOCUMENT, 0u, 0u, 2u},
        {2u, 0u, SALTS_MD_BLOCK_HEADING, 1u, 0u, 999u}
    };
    doc = (salts_md_document_view){source, sizeof(source)-1u, invalid, 2u};
    CHECK(salts_md_document_validate(&doc, 2u) == SALTS_MD_INVALID_ARGUMENT);
    const salts_md_block duplicate[] = {
        {1u, SALTS_MD_PARENT_NONE, SALTS_MD_BLOCK_DOCUMENT, 0u, 0u, 2u},
        {1u, 0u, SALTS_MD_BLOCK_PARAGRAPH, 0u, 0u, 1u}
    };
    doc = (salts_md_document_view){source, sizeof(source)-1u, duplicate, 2u};
    CHECK(salts_md_document_validate(&doc, 2u) == SALTS_MD_INVALID_ARGUMENT);
    return 0;
}

static int test_metadata(void) {
    static const char text[] = "Heading", raw[] = "# Heading\n";
    const salts_md_block blocks[] = {
        {1u, SALTS_MD_PARENT_NONE, SALTS_MD_BLOCK_DOCUMENT, 0u, 0u, 7u},
        {2u, 0u, SALTS_MD_BLOCK_HEADING, 1u, 0u, 7u}
    };
    const salts_md_block_metadata positions[] = {{0u,0u}, {0u,3u}};
    salts_md_rich_document_view rich = {
        .document = {text, sizeof(text)-1u, blocks, 2u},
        .metadata = "tag", .metadata_bytes = 3u,
        .block_metadata = positions, .block_metadata_count = 2u,
        .raw_markdown = raw, .raw_markdown_bytes = sizeof(raw)-1u
    };
    CHECK(salts_md_rich_document_validate(&rich, 2u) == SALTS_MD_OK);
    const char *data = NULL;
    size_t count = 0u;
    CHECK(salts_md_rich_document_metadata(&rich, 1u, &data, &count) ==
          SALTS_MD_OK);
    CHECK(count == 3u && data && memcmp(data, "tag", 3u) == 0);
    rich.block_metadata_count = 1u;
    CHECK(salts_md_rich_document_validate(&rich, 2u) ==
          SALTS_MD_INVALID_ARGUMENT);
    rich.block_metadata_count = 2u;
    rich.metadata_bytes = 2u;
    CHECK(salts_md_rich_document_validate(&rich, 2u) ==
          SALTS_MD_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    if (test_document_visitor()) return 1;
    if (test_metadata()) return 1;
    puts("Markdown C11 RichDoc validation/visit: PASS");
    return 0;
}
