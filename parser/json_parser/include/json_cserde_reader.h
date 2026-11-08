/**
 * @file json_cserde_reader.h
 * @brief Bounded CSerde reader adapter for an existing JSON DOM.
 */

#ifndef JSON_CSERDE_READER_H
#define JSON_CSERDE_READER_H

#include "json_parser.h"

#include <cserde/reader.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a pull reader over an existing JSON DOM.
 *
 * The adapter borrows root and never modifies or frees it. The caller must keep
 * the complete DOM alive and unmodified while reading. STRING tokens, including
 * object keys, use CSERDE_VIEW_STABLE and remain valid until that DOM is modified
 * or freed; destroying the reader does not invalidate those slices.
 *
 * JSON numbers without a decimal point or exponent emit CSERDE_SINT when
 * negative and CSERDE_UINT otherwise. Numbers with either marker emit
 * CSERDE_FLOAT. Builder values without retained source text are first rendered
 * with the same %.17g rule as json_create_number(), so equivalent double
 * builder paths have the same token kind.
 *
 * max_depth bounds the number of simultaneously open JSON arrays and objects.
 * A scalar root therefore accepts zero. Exceeding the bound causes
 * cserde_reader_next() to return CSERDE_LIMIT_EXCEEDED and latch that failure.
 * Integers outside int64_t/uint64_t or non-finite floating values return
 * CSERDE_VALUE_OUT_OF_RANGE; invalid DOM state or number formatting failure
 * returns CSERDE_SOURCE_ERROR. Provider failures latch through the CSerde reader
 * core. The reader is single-threaded and emits exactly one document in
 * insertion order.
 *
 * @param root Borrowed JSON root; must not be NULL.
 * @param max_depth Maximum simultaneously open container count.
 * @return Owned reader released by json_cserde_reader_destroy(), or NULL for an
 * invalid root, an allocation-size overflow, or allocation failure.
 *
 * @code
 * json_value_t *root = json_parse("{\"id\":7}", 8);
 * cserde_reader *reader = json_cserde_reader_create(root, 16);
 * cserde_token token;
 * while (cserde_reader_next(reader, &token) == CSERDE_OK) {
 *   consume_token(&token);
 * }
 * json_cserde_reader_destroy(reader);
 * json_free(root);
 * @endcode
 */
cserde_reader *json_cserde_reader_create(const json_value_t *root, size_t max_depth);

/**
 * Destroy a JSON CSerde reader without freeing its borrowed DOM.
 *
 * @param reader Reader returned by json_cserde_reader_create(); NULL is accepted.
 */
void json_cserde_reader_destroy(cserde_reader *reader);

#ifdef __cplusplus
}
#endif

#endif /* JSON_CSERDE_READER_H */
