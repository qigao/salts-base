/**
 * @file crypto_random.c
 * @brief Compatibility wrapper over Salts platform CSPRNG
 */

#include <asn1/crypto_random.h>
#include <core/platform.h>

int crypto_random_bytes(uint8_t *buf, size_t len) {
    if (!buf || len == 0u) return -1;
    return secure_random(buf, len) == 0 ? 0 : -1;
}
