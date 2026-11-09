#ifndef SALTS_PLATFORM_RANDOM_H
#define SALTS_PLATFORM_RANDOM_H

#include <salts/api.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fills a buffer from the operating system CSPRNG without a process-local
 * fallback. A zero-length request accepts a null buffer.
 */

SALTS_API int platform_secure_random(void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLATFORM_RANDOM_H */
