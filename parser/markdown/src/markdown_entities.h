#ifndef STUN_MARKDOWN_ENTITIES_INTERNAL_H
#define STUN_MARKDOWN_ENTITIES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

/* Single C11 inline semantic decoder; called ONLY after a physical '&'.
 * CommonMark 0.31.2 requires a terminating semicolon for both named and
 * numeric references. No markdown syntax rescanning follows decoded text:
 * it is emitted as an owned, decoded TEXT node in the existing RichDoc.
 *
 * On success: consumed includes the '&' and final ';'; output_bytes is
 * between 1 and 8 and contains one or two Unicode UTF-8 scalar encodings.
 * On failure: all outputs remain unchanged; caller retains raw punctuation.
 * This file is private to the Stun C11 Markdown frontend (not a public ABI).
 */
bool salts_md_decode_character_reference(const char *source, size_t size,
                                        size_t *consumed,
                                        char utf8[8], size_t *output_bytes);

#endif /* STUN_MARKDOWN_ENTITIES_INTERNAL_H */
