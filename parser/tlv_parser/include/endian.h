#ifndef SALTS_ENDIAN_H
#define SALTS_ENDIAN_H

#include <stddef.h>
#include <stdint.h>

/*
 * Detect byte order using compiler-defined macros (GCC/Clang)
 */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #define SALTS_IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#elif defined(_WIN32) || defined(_MSC_VER)
  #define SALTS_IS_LITTLE_ENDIAN 1
#elif defined(__LITTLE_ENDIAN__)
  #define SALTS_IS_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__)
  #define SALTS_IS_LITTLE_ENDIAN 0
#else
  /* Default to little-endian as it's most common today */
  #define SALTS_IS_LITTLE_ENDIAN 1
#endif

/*
 * Byte swap intrinsics
 */
#if defined(_MSC_VER) && !defined(__clang__)
  #include <intrin.h>
  #define SALTS_BSWAP16(x) _byteswap_ushort(x)
  #define SALTS_BSWAP32(x) _byteswap_ulong(x)
  #define SALTS_BSWAP64(x) _byteswap_uint64(x)
#else
  #define SALTS_BSWAP16(x) __builtin_bswap16(x)
  #define SALTS_BSWAP32(x) __builtin_bswap32(x)
  #define SALTS_BSWAP64(x) __builtin_bswap64(x)
#endif

/*
 * Conversion functions - defined as macros to ensure expansion and avoid linking issues
 */

/* Little Endian <-> Host */
#if SALTS_IS_LITTLE_ENDIAN
  #define salts_le16toh(x) (uint16_t)(x)
  #define salts_le32toh(x) (uint32_t)(x)
  #define salts_le64toh(x) (uint64_t)(x)
  #define salts_htole16(x) (uint16_t)(x)
  #define salts_htole32(x) (uint32_t)(x)
  #define salts_htole64(x) (uint64_t)(x)
#else
  #define salts_le16toh(x) SALTS_BSWAP16((uint16_t)(x))
  #define salts_le32toh(x) SALTS_BSWAP32((uint32_t)(x))
  #define salts_le64toh(x) SALTS_BSWAP64((uint64_t)(x))
  #define salts_htole16(x) SALTS_BSWAP16((uint16_t)(x))
  #define salts_htole32(x) SALTS_BSWAP32((uint32_t)(x))
  #define salts_htole64(x) SALTS_BSWAP64((uint64_t)(x))
#endif

/* Big Endian <-> Host */
#if SALTS_IS_LITTLE_ENDIAN
  #define salts_be16toh(x) SALTS_BSWAP16((uint16_t)(x))
  #define salts_be32toh(x) SALTS_BSWAP32((uint32_t)(x))
  #define salts_be64toh(x) SALTS_BSWAP64((uint64_t)(x))
  #define salts_htobe16(x) SALTS_BSWAP16((uint16_t)(x))
  #define salts_htobe32(x) SALTS_BSWAP32((uint32_t)(x))
  #define salts_htobe64(x) SALTS_BSWAP64((uint64_t)(x))
#else
  #define salts_be16toh(x) (uint16_t)(x)
  #define salts_be32toh(x) (uint32_t)(x)
  #define salts_be64toh(x) (uint64_t)(x)
  #define salts_htobe16(x) (uint16_t)(x)
  #define salts_htobe32(x) (uint32_t)(x)
  #define salts_htobe64(x) (uint64_t)(x)
#endif

/* Compatibility mapping to standard names (le32toh, htole32, etc.) */

#ifndef le16toh
#define le16toh(x) salts_le16toh(x)
#endif
#ifndef le32toh
#define le32toh(x) salts_le32toh(x)
#endif
#ifndef le64toh
#define le64toh(x) salts_le64toh(x)
#endif

#ifndef htole16
#define htole16(x) salts_htole16(x)
#endif
#ifndef htole32
#define htole32(x) salts_htole32(x)
#endif
#ifndef htole64
#define htole64(x) salts_htole64(x)
#endif

#ifndef be16toh
#define be16toh(x) salts_be16toh(x)
#endif
#ifndef be32toh
#define be32toh(x) salts_be32toh(x)
#endif
#ifndef be64toh
#define be64toh(x) salts_be64toh(x)
#endif

#ifndef htobe16
#define htobe16(x) salts_htobe16(x)
#endif
#ifndef htobe32
#define htobe32(x) salts_htobe32(x)
#endif
#ifndef htobe64
#define htobe64(x) salts_htobe64(x)
#endif

#endif /* SALTS_ENDIAN_H */
