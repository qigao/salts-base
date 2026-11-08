#ifndef SALTS_MARKDOWN_HTML_H
#define SALTS_MARKDOWN_HTML_H

#include <salts/markdown.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Render an already-parsed native C11 RichDoc into HTML.
 *
 * The caller retains the view (including source and metadata). The renderer
 * parses nothing, stores no runtime state, and does not touch the Tango or
 * gCanvas backends. It is an explicit supported subset, not full CommonMark.
 *
 * This API is transactional:
 *   buffer == NULL, capacity == 0: count output bytes without writing;
 *   buffer != NULL: emit only after fully validating output and capacity.
 *   required receives exact output byte count, excluding terminating NUL.
 *   on failure, supplied buffer bytes are unchanged.
 *
 * Unsupported semantic kinds and invalid/unsafe link targets fail closed with
 * SALTS_MD_INVALID_ARGUMENT, never silently fall back to raw HTML output.
 * max_nodes caps validation/traversal effort and recursion depth is bounded.
 */
/* Explicit opt-in SPEC_CONFORMANCE emits literal typed RAW_HTML tokens.
 * It is intended for normative test output only; never use it with
 * untrusted markup in a WebView. Production defaults to SAFE_APPLICATION. */
typedef enum salts_md_html_policy {
    SALTS_MD_HTML_SAFE_APPLICATION = 0,
    SALTS_MD_HTML_SPEC_CONFORMANCE = 1
} salts_md_html_policy;

/* Share SAFE_APPLICATION destination checks with native RichDoc widgets.
 * Input is already decoded C11 metadata; this does not parse Markdown.
 * Reject decoded controls and non-allowlisted URI schemes. IMAGE rejects
 * mailto, while link/image titles allow LF and TAB only. */
bool salts_md_html_safe_href(const char *decoded, size_t bytes);
bool salts_md_html_safe_image_src(const char *decoded, size_t bytes);
bool salts_md_html_safe_title(const char *decoded, size_t bytes);

salts_md_status salts_md_render_html_with_policy(
    const salts_md_rich_document_view *view,size_t max_nodes,
    salts_md_html_policy policy,char *buffer,size_t capacity,
    size_t *required);

salts_md_status salts_md_render_html(const salts_md_rich_document_view *view,
                                   size_t max_nodes,
                                   char *buffer, size_t capacity,
                                   size_t *required);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_MARKDOWN_HTML_H */
