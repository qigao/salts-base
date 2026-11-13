/**
 * @file crypto_random.h
 * @brief Cryptographically Secure Random Number Generator
 *
 * Cross-platform CSPRNG for cryptographic key generation.
 */

#ifndef CRYPTO_RANDOM_H
#define CRYPTO_RANDOM_H


#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill buffer with cryptographically secure random bytes
 * @param buf Output buffer
 * @param len Number of bytes to generate
 * @return 0 on success, -1 on error
 */
int crypto_random_bytes(uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // CRYPTO_RANDOM_H
