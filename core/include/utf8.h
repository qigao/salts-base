#ifndef SALTS_CORE_UTF8_H
#define SALTS_CORE_UTF8_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Strict Unicode scalar UTF-8 in a caller-bounded byte span.
 * Returns 1 for valid data (including an empty span), 0 otherwise.
 * Validates overlong encodings, surrogate code points, >U+10FFFF,
 * invalid continuation bytes, truncated sequences and illegal leads.
 * Does not allocate, normalize, or inspect bytes outside [data,data+len).
 * NULL is valid only when len == 0. Embedded U+0000 is a valid scalar;
 * path callers must separately enforce their NUL termination/length limits.
 * This is an encoding primitive, NOT an authority to admit filesystem paths.
 */
int salts_core_utf8_valid(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* SALTS_CORE_UTF8_H */
