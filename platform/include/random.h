#ifndef SALTS_PLATFORM_RANDOM_H
#define SALTS_PLATFORM_RANDOM_H

#include <platform_api.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fills a buffer from the operating system CSPRNG without a process-local
 * fallback. A zero-length request accepts a null buffer.
 */

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_secure_random(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLATFORM_RANDOM_H */
