#ifndef SALTS_MARKDOWN_H
#define SALTS_MARKDOWN_H

/* Canonical, UI-independent C11 Markdown grammar and owned semantic document.
 * Ported from qigao/stun master 78995ecf8c5365c7f001b4501cb29a109971ba9c.
 * No UI Runtime, CMeta reflection, SQLite, Graph or HTML frontend included.
 * NOTE: RichDoc node byte spans refer to the decoded semantic text arena,
 * NOT raw source byte positions. Historical source offset mapping is a
 * separately required acceptance before use as TurboAgent evidence.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_MD_PARENT_NONE UINT32_MAX

typedef enum salts_md_status {
  SALTS_MD_OK = 0,
  SALTS_MD_INVALID_ARGUMENT = -1,
  SALTS_MD_CAPACITY_EXCEEDED = -2,
  SALTS_MD_OUT_OF_MEMORY = -3
} salts_md_status;

typedef enum salts_md_block_kind {
    SALTS_MD_BLOCK_DOCUMENT = 1,
    SALTS_MD_BLOCK_PARAGRAPH,
    SALTS_MD_BLOCK_HEADING,
    SALTS_MD_BLOCK_TEXT,
    SALTS_MD_BLOCK_CODE_FENCE,
    SALTS_MD_BLOCK_CODE_SPAN,
    SALTS_MD_BLOCK_LIST,
    SALTS_MD_BLOCK_LIST_ITEM,
    SALTS_MD_BLOCK_QUOTE,
    SALTS_MD_BLOCK_TABLE,
    SALTS_MD_BLOCK_TABLE_ROW,
    SALTS_MD_BLOCK_TABLE_CELL,
    SALTS_MD_BLOCK_ORDERED_LIST,
    SALTS_MD_BLOCK_EMPHASIS,
    SALTS_MD_BLOCK_STRONG,
    SALTS_MD_BLOCK_LINK,
    SALTS_MD_BLOCK_IMAGE,
    SALTS_MD_BLOCK_STRIKETHROUGH,
    SALTS_MD_BLOCK_UNDERLINE,
    SALTS_MD_BLOCK_RULE,
    SALTS_MD_BLOCK_ENTITY,
    SALTS_MD_BLOCK_LINE_BREAK,
    SALTS_MD_BLOCK_TASK_ITEM,
    SALTS_MD_BLOCK_CHECKBOX,
    SALTS_MD_BLOCK_MATH,
    SALTS_MD_BLOCK_MATH_BLOCK,
    SALTS_MD_BLOCK_HIGHLIGHT,
    SALTS_MD_BLOCK_SUPERSCRIPT,
    SALTS_MD_BLOCK_SUBSCRIPT,
    SALTS_MD_BLOCK_FOOTNOTE,
    SALTS_MD_BLOCK_FOOTNOTE_DEF,
    SALTS_MD_BLOCK_DEFINITION_LIST,
    SALTS_MD_BLOCK_DEFINITION_TERM,
    SALTS_MD_BLOCK_DEFINITION_DESC,
    SALTS_MD_BLOCK_DIAGRAM,
    /* Appended: keep all existing public enum values stable. */
    SALTS_MD_BLOCK_CODE_INDENTED,
    /* Distinct from the existing SOFT SALTS_MD_BLOCK_LINE_BREAK node. */
    SALTS_MD_BLOCK_HARD_LINE_BREAK,
    /* Optional decoded title child of SALTS_MD_BLOCK_LINK; metadata-owned. */
    SALTS_MD_BLOCK_LINK_TITLE,
    /* Decoded IMAGE title, always the first direct IMAGE child. */
    SALTS_MD_BLOCK_IMAGE_TITLE,
    /* Parser-owned literal HTML tokens; never implicit unsafe passthrough. */
    SALTS_MD_BLOCK_RAW_HTML_INLINE,
    SALTS_MD_BLOCK_RAW_HTML_BLOCK,
    /* A CommonMark URI autolink outside the production scheme allowlist.
     * Leaf with an owned visible-URI span plus matching metadata; the
     * default SAFE HTML writer always escapes its enclosing angle token.
     * Only explicit trusted SPEC_CONFORMANCE emits an anchor element. */
    SALTS_MD_BLOCK_URI_AUTOLINK
} salts_md_block_kind;

typedef struct salts_md_block {
    uint64_t stable_id;
    uint32_t parent_index;
    salts_md_block_kind kind;
    uint32_t heading_level;
    size_t byte_begin;
    size_t byte_end;
} salts_md_block;

typedef struct salts_md_document_view {
    const char *source;
    size_t source_bytes;
    const salts_md_block *blocks;
    size_t block_count;
} salts_md_document_view;

salts_md_status salts_md_document_validate(
    const salts_md_document_view *view, size_t max_blocks);

typedef salts_md_status (*salts_md_document_visit_fn)(
    const salts_md_block *block, const char *source_span,
    size_t span_bytes, void *context);

salts_md_status salts_md_document_visit(
    const salts_md_document_view *view, size_t max_blocks,
    salts_md_document_visit_fn visit, void *context);

/* RichDoc bridge: the existing Markdown AST does not record raw source offsets.
 * Therefore text.source is an OWNED-BY-PROJECTOR concatenation of decoded
 * semantic text nodes; byte_begin/end do NOT index into raw Markdown. Block
 * kinds retain structural semantics. Node metadata (fence language, link or
 * image target, footnote label) is borrowed separately from the projector.
 * The raw Markdown is preserved for explicit diagnostics only.
 *
 * Lifetime: all pointers are borrowed for the call; the producer must keep
 * source, metadata and both arrays alive until the last consumer finishes.
 * A projector re-build, move or release invalidates all these views. Stable
 * node IDs are deterministic only within a snapshot; streaming reconciliation
 * must use a separate identity policy rather than relying on ordinal IDs.
 */
/* Owned eight-byte metadata payload for list semantics. */
typedef struct salts_md_list_semantics {
    uint32_t start; /* 0 for bullet, 0..999999999 for ordered */
    uint8_t marker; /* '-', '+', '*', '.', ')' */
    uint8_t loose; /* 0 tight; 1 loose */
    uint8_t reserved[2];
} salts_md_list_semantics;

/* #186: a TABLE owns exactly one alignment byte per column in RichDoc
 * metadata. The delimiter row supplies these values; header and body cells
 * consume the same immutable vector. No renderer-side Markdown reparse. */
typedef enum salts_md_table_alignment {
    SALTS_MD_TABLE_ALIGN_NONE = 0,
    SALTS_MD_TABLE_ALIGN_LEFT = 1,
    SALTS_MD_TABLE_ALIGN_CENTER = 2,
    SALTS_MD_TABLE_ALIGN_RIGHT = 3
} salts_md_table_alignment;

/* Hard-break source origin is owned as four metadata bytes on a
 * SALTS_MD_BLOCK_HARD_LINE_BREAK node, never re-read from decoded text or
 * inferred by an HTML consumer. Soft breaks carry no origin metadata.
 * Additional origins require explicit admission and validation. */
typedef enum salts_md_line_break_origin {
    SALTS_MD_LINE_BREAK_TRAILING_SPACES = 1,
    SALTS_MD_LINE_BREAK_BACKSLASH = 2
} salts_md_line_break_origin;
typedef struct salts_md_line_break_semantics {
    uint8_t origin; /* salts_md_line_break_origin */
    uint8_t reserved[3];
} salts_md_line_break_semantics;

typedef struct salts_md_block_metadata {
    size_t byte_begin;
    size_t byte_end;
} salts_md_block_metadata;

typedef struct salts_md_rich_document_view {
    salts_md_document_view document;
    const char *metadata;
    size_t metadata_bytes;
    const salts_md_block_metadata *block_metadata;
    size_t block_metadata_count;
    const char *raw_markdown;
    size_t raw_markdown_bytes;
} salts_md_rich_document_view;

salts_md_status salts_md_rich_document_validate(
    const salts_md_rich_document_view *view, size_t max_blocks);

salts_md_status salts_md_rich_document_metadata(
    const salts_md_rich_document_view *view, size_t node_index,
    const char **out_bytes, size_t *out_length);


typedef struct salts_md_document salts_md_document;

/* Bounded node and source admission; zero budgets are invalid.
 * Input is an exact-length byte sequence, not a NUL-terminated string.
 * ASCII syntax delimiters are recognized without modifying UTF-8 payloads.
 * Strict Unicode validation remains at the Salts::Unicode admission boundary.
 */
salts_md_status salts_md_parse(const char *input, size_t length,
                            size_t max_nodes, size_t max_source_bytes,
                            salts_md_document **out);

/* No-fail cleanup; NULL is accepted. */
void salts_md_document_destroy(salts_md_document *doc);

/* Borrowed immutable view. Empty view returned for NULL. */
salts_md_rich_document_view salts_md_get_document_view(const salts_md_document *doc);


#ifdef __cplusplus
}
#endif
#endif /* SALTS_MARKDOWN_H */
