#ifndef SALTS_XML_SAX_H
#define SALTS_XML_SAX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xml_sax_parser_s xml_sax_parser_t;

typedef struct xml_sax_handler_s {
  int (*on_start_document)(void *ctx);
  int (*on_end_document)(void *ctx);
  int (*on_element_start)(void *ctx, const char *name, size_t name_len);
  int (*on_attribute)(void *ctx, const char *name, size_t name_len, const char *value,
                      size_t value_len);
  int (*on_element_end)(void *ctx, const char *name, size_t name_len);
  int (*on_text)(void *ctx, const char *text, size_t text_len);
  int (*on_comment)(void *ctx, const char *text, size_t text_len);
  int (*on_cdata)(void *ctx, const char *text, size_t text_len);
  int (*on_processing_instruction)(void *ctx, const char *target, size_t target_len,
                                   const char *data, size_t data_len);
  int (*on_doctype)(void *ctx, const char *text, size_t text_len);
} xml_sax_handler_t;

/**
 * Incremental lexical XML events. Callback spans are borrowed until callback
 * return. Text/attribute slices retain XML entities; consumers that need a DOM
 * or decoded values use xml_parse. No external entities are expanded.
 * Returning nonzero from any callback stops parsing and makes failure sticky.
 * Events may be delivered during feed, before the whole document is complete.
 * Instances are independent, single-owner, and must not be reentered.
 *
 * max_buffer_bytes bounds pending token/chunk storage (excluding its NUL).
 * It must be in [1, SIZE_MAX-1]. Open element names are bounded by that limit
 * and at most 256 nested elements. Returns NULL for invalid arguments or OOM.
 */
xml_sax_parser_t *xml_sax_parser_create(
    const xml_sax_handler_t *handler, void *ctx, size_t max_buffer_bytes);

/** Change the storage bound before the first nonempty feed. 0 on success. */
int xml_sax_parser_set_buffer_limit(xml_sax_parser_t *parser, size_t limit);

/** Feed raw bytes, then finish once at EOF. Return 0 on success, -1 on failure. */
int xml_sax_parser_feed(xml_sax_parser_t *parser, const char *data, size_t len);
int xml_sax_parser_finish(xml_sax_parser_t *parser);
/** Instance-owned first error message; NULL when this instance has no error. */
const char *xml_sax_parser_error(const xml_sax_parser_t *parser);
void xml_sax_parser_destroy(xml_sax_parser_t *parser);

#ifdef __cplusplus
}
#endif
#endif
