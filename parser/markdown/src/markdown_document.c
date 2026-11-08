#include <salts/markdown.h>
#include <limits.h>
#include <stdint.h>

static bool valid_block_kind(salts_md_block_kind kind) {
    return kind >= SALTS_MD_BLOCK_DOCUMENT &&
           kind <= SALTS_MD_BLOCK_URI_AUTOLINK;
}

salts_md_status salts_md_document_validate(
    const salts_md_document_view *view, size_t max_blocks) {
    if (!view || !view->blocks || !view->block_count ||
        (!view->source && view->source_bytes))
        return SALTS_MD_INVALID_ARGUMENT;
    if (!max_blocks || view->block_count > max_blocks ||
        view->block_count > UINT32_MAX)
        return SALTS_MD_CAPACITY_EXCEEDED;
    for (size_t i = 0; i < view->block_count; ++i) {
        const salts_md_block *block = &view->blocks[i];
        if (!block->stable_id || !valid_block_kind(block->kind) ||
            (i == 0 ? block->parent_index != SALTS_MD_PARENT_NONE
                    : block->parent_index >= i) ||
            block->byte_begin > block->byte_end ||
            block->byte_end > view->source_bytes ||
            (block->kind == SALTS_MD_BLOCK_HEADING &&
             (block->heading_level < 1 || block->heading_level > 6)) ||
            (block->kind != SALTS_MD_BLOCK_HEADING &&
             block->heading_level != 0))
            return SALTS_MD_INVALID_ARGUMENT;
        /* IMAGE_TITLE is metadata only, never visible content. */
        if (block->kind == SALTS_MD_BLOCK_IMAGE_TITLE &&
            (i == 0 || block->parent_index != i-1u ||
             view->blocks[i-1u].kind != SALTS_MD_BLOCK_IMAGE ||
             (i+1u < view->block_count &&
              view->blocks[i+1u].parent_index == i)))
            return SALTS_MD_INVALID_ARGUMENT;
        if ((block->kind==SALTS_MD_BLOCK_RAW_HTML_INLINE ||
             block->kind==SALTS_MD_BLOCK_RAW_HTML_BLOCK ||
             block->kind==SALTS_MD_BLOCK_URI_AUTOLINK) &&
            i+1u<view->block_count &&
            view->blocks[i+1u].parent_index==i)
            return SALTS_MD_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j) {
            if (view->blocks[j].stable_id == block->stable_id)
                return SALTS_MD_INVALID_ARGUMENT;
        }
    }
    if (view->blocks[0].kind != SALTS_MD_BLOCK_DOCUMENT)
        return SALTS_MD_INVALID_ARGUMENT;
    return SALTS_MD_OK;
}

salts_md_status salts_md_document_visit(
    const salts_md_document_view *view, size_t max_blocks,
    salts_md_document_visit_fn visit, void *context) {
    if (!visit) return SALTS_MD_INVALID_ARGUMENT;
    const salts_md_status status = salts_md_document_validate(view, max_blocks);
    if (status != SALTS_MD_OK) return status;
    for (size_t i = 0; i < view->block_count; ++i) {
        const salts_md_block *block = &view->blocks[i];
        const char *span = view->source
            ? view->source + block->byte_begin : NULL;
        const salts_md_status result = visit(
            block, span, block->byte_end - block->byte_begin, context);
        if (result != SALTS_MD_OK) return result;
    }
    return SALTS_MD_OK;
}

/* A RichDoc contains exact semantic text ranges and a separate borrowed
 * metadata byte arena. Both must remain valid throughout a traversal. */
salts_md_status salts_md_rich_document_validate(
    const salts_md_rich_document_view *view, size_t max_blocks) {
    if (!view || view->block_metadata_count != view->document.block_count ||
        !view->block_metadata ||
        (!view->metadata && view->metadata_bytes) ||
        (!view->raw_markdown && view->raw_markdown_bytes))
        return SALTS_MD_INVALID_ARGUMENT;
    salts_md_status status =
        salts_md_document_validate(&view->document, max_blocks);
    if (status != SALTS_MD_OK) return status;
    for (size_t i=0; i<view->block_metadata_count; ++i) {
        const salts_md_block_metadata *m = &view->block_metadata[i];
        if (m->byte_begin > m->byte_end ||
            m->byte_end > view->metadata_bytes)
            return SALTS_MD_INVALID_ARGUMENT;
    }
    return SALTS_MD_OK;
}

salts_md_status salts_md_rich_document_metadata(
    const salts_md_rich_document_view *view, size_t node_index,
    const char **out_bytes, size_t *out_length) {
    if (!view || !out_bytes || !out_length ||
        node_index >= view->block_metadata_count ||
        !view->block_metadata)
        return SALTS_MD_INVALID_ARGUMENT;
    const salts_md_block_metadata range = view->block_metadata[node_index];
    if (range.byte_begin > range.byte_end ||
        range.byte_end > view->metadata_bytes ||
        (!view->metadata && view->metadata_bytes))
        return SALTS_MD_INVALID_ARGUMENT;
    *out_bytes = view->metadata
        ? view->metadata + range.byte_begin : NULL;
    *out_length = range.byte_end - range.byte_begin;
    return SALTS_MD_OK;
}
