#ifndef TOON_JSON_ADAPTER_H
#define TOON_JSON_ADAPTER_H

#include "json_parser.h"
#include "toonc.h"
#include <cmeta_error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOON_JSON_ADAPTER_MAX_DEPTH 256U

/**
 * Convert a TOON node into an independently owned JSON DOM.
 *
 * The conversion preserves array/object order and length-delimited string
 * values. It rejects malformed/shared TOON graphs, duplicate object keys,
 * invalid UTF-8, non-finite numbers, and unsupported node types.
 *
 * @param root Source TOON node; borrowed for the duration of the call.
 * @param out_value Receives an owned JSON value released with json_free().
 * @return SALTS_OK on success; SALTS_EINVAL for invalid arguments;
 *         SALTS_EPROTO for a malformed, shared, cyclic, or duplicate-key
 *         TOON tree; SALTS_ECHARSET for invalid UTF-8; SALTS_ERANGE for a
 *         non-finite number or excessive depth; SALTS_ENOTSUP for an
 *         unsupported node type; or SALTS_ENOMEM. On failure, *out_value is
 *         NULL.
 */
int toon_json_to_value(const toonObject *root, json_value_t **out_value);

/**
 * Convert a JSON DOM into an independently owned TOON tree.
 *
 * JSON strings are copied with their byte length. Object keys containing NUL
 * and integer tokens outside the exact IEEE-754 integer range are rejected
 * because the current TOON model cannot represent them without loss.
 * The adapter has no shared mutable state and is safe for concurrent calls
 * with distinct inputs; the caller must not mutate either source tree during
 * conversion.
 *
 * @param value Source JSON value; borrowed for the duration of the call.
 * @param out_root Receives an owned TOON tree released with TOONc_free().
 * @return SALTS_OK on success; SALTS_EINVAL for invalid arguments;
 *         SALTS_EPROTO for a malformed JSON DOM; SALTS_ECHARSET for invalid
 *         UTF-8; SALTS_ERANGE for a non-finite/lossy number or excessive
 *         depth; SALTS_ENOTSUP for an embedded-NUL object key or unsupported
 *         JSON type; or SALTS_ENOMEM. On failure, *out_root is NULL.
 */
int toon_json_from_value(const json_value_t *value, toonObject **out_root);

#ifdef __cplusplus
}
#endif

#endif /* TOON_JSON_ADAPTER_H */
