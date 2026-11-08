#ifndef SALTS_UUID_H
#define SALTS_UUID_H

/**
 * @file cmeta_uuid.h
 * @brief Installed UUID value type and generation/conversion API.
 *
 * @code
 * cmeta_uuid_t id;
 * char text[SALTS_UUID_STRING_SIZE];
 * if (cmeta_uuid_v7_generate(&id) == SALTS_OK &&
 *     cmeta_uuid_format(&id, text, sizeof(text)) == SALTS_OK) {
 *   use_uuid(text);
 * }
 * @endcode
 */

#include "cmeta_api.h"
#include <error_codes.h>
#include <stddef.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_UUID_SIZE 16U
#define SALTS_UUID_STRING_LENGTH 36U
#define SALTS_UUID_STRING_SIZE (SALTS_UUID_STRING_LENGTH + 1U)

/** Fixed-size UUID value with no heap ownership. */
typedef struct cmeta_uuid_s {
  uint8_t bytes[SALTS_UUID_SIZE];
} cmeta_uuid_t;

/**
 * Generate an RFC 4122/RFC 9562 UUID version 4 using the system CSPRNG.
 * The output remains unchanged if entropy acquisition fails.
 *
 * @param out Destination UUID
 * @return 0 on success, negative error code on failure
 */
SALTS_C_API int cmeta_uuid_v4_generate(cmeta_uuid_t *out);

/**
 * Generate an RFC 9562 UUID version 7 using the current Unix millisecond
 * timestamp and the system CSPRNG. The output remains unchanged on failure.
 * Values generated within the same millisecond have random rather than
 * monotonic ordering.
 *
 * @param out Destination UUID
 * @return 0 on success, negative error code on failure
 */
SALTS_C_API int cmeta_uuid_v7_generate(cmeta_uuid_t *out);

/**
 * Parse a canonical lowercase or uppercase 8-4-4-4-12 UUID string.
 * The output remains unchanged on failure.
 *
 * @param text NUL-terminated canonical UUID string
 * @param out Destination UUID
 * @return 0 on success, SALTS_EINVAL on invalid input
 */
SALTS_C_API int cmeta_uuid_parse(const char *text, cmeta_uuid_t *out);

/**
 * Format a UUID as a lowercase canonical 8-4-4-4-12 string.
 *
 * @param uuid UUID value
 * @param out Destination string buffer
 * @param out_size Destination capacity, at least SALTS_UUID_STRING_SIZE
 * @return 0 on success, SALTS_EINVAL or SALTS_ENOSPC on failure
 */
SALTS_C_API int cmeta_uuid_format(const cmeta_uuid_t *uuid, char *out, size_t out_size);

/** Return true when both UUID values contain the same 16 bytes. */
SALTS_C_API bool cmeta_uuid_equal(const cmeta_uuid_t *left, const cmeta_uuid_t *right);

#ifdef __cplusplus
}

namespace Salts {
  using UUID = ::cmeta_uuid_t;
}
#endif

#endif /* SALTS_UUID_H */
