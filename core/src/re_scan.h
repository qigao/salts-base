/* First-party C11-native first-byte matcher for the original regex engine.
 * Donor behavior: qigao/salts utils/src/re_scan.h
 * Git blob 616b381ecae509b54b9f96f90c1309a189462981.
 *
 * Only the acceleration implementation changes: intrinsic SSE2 on x86,
 * NEON on ARM and strictly bounded C11 scalar scanning on other targets and
 * the tail. This helper is PRIVATE to the sole Salts::Core regex engine and
 * does not create a second regex parser, provider, or dynamically selected
 * runtime fallback. It always yields the first matching byte or NULL.
 */
#ifndef SALTS_RE_SCAN_H
#define SALTS_RE_SCAN_H

#include <stddef.h>
#include <stdint.h>

#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define SALTS_RE_NATIVE_SSE2 1
#  include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || \
      defined(__aarch64__) || defined(_M_ARM64)
#  define SALTS_RE_NATIVE_NEON 1
#  include <arm_neon.h>
#endif

static inline const char *re_scan_first_byte_scalar(
    const char *p, const char *end, unsigned char needle)
{
    while (p < end) {
        if ((unsigned char)*p == needle) return p;
        ++p;
    }
    return NULL;
}

static inline const char *re_scan_first_byte_native(
    const char *p, const char *end, unsigned char needle)
{
#if defined(SALTS_RE_NATIVE_SSE2)
    const __m128i target = _mm_set1_epi8((char)needle);
    while ((size_t)(end - p) >= 16u) {
        const __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)p);
        unsigned int mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, target));
        if (mask != 0u) {
            unsigned int offset = 0u;
            while ((mask & 1u) == 0u) {
                ++offset;
                mask >>= 1u;
            }
            return p + offset;
        }
        p += 16u;
    }
#elif defined(SALTS_RE_NATIVE_NEON)
    const uint8x16_t target = vdupq_n_u8(needle);
    while ((size_t)(end - p) >= 16u) {
        const uint8x16_t data = vld1q_u8((const uint8_t *)(const void *)p);
        const uint8x16_t match = vceqq_u8(data, target);
        uint8_t byte_matches[16];
        unsigned int i;
        vst1q_u8(byte_matches, match);
        for (i = 0u; i < 16u; ++i) {
            if (byte_matches[i] != 0u) return p + i;
        }
        p += 16u;
    }
#else
    /* Scalar IS the native implementation on non-SIMD architectures,
     * not an optional external regex fallback.
     */
#endif
    return re_scan_first_byte_scalar(p, end, needle);
}

#endif /* SALTS_RE_SCAN_H */
