#ifndef SALTS_SIMD_TYPES_H
#define SALTS_SIMD_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Numeric values retain the original lane-kind ordering. */
typedef enum salts_simd_lane_kind {
    SALTS_SIMD_LANE_I8 = 0,
    SALTS_SIMD_LANE_U8,
    SALTS_SIMD_LANE_I16,
    SALTS_SIMD_LANE_U16,
    SALTS_SIMD_LANE_I32,
    SALTS_SIMD_LANE_U32,
    SALTS_SIMD_LANE_I64,
    SALTS_SIMD_LANE_U64,
    SALTS_SIMD_LANE_F32,
    SALTS_SIMD_LANE_F64,
    SALTS_SIMD_LANE_BOOL
} salts_simd_lane_kind;

/* Neutral storage only: no native vector type, allocator or retained owner.
 * Lane interpretation is supplied separately by salts_simd_desc. */
typedef struct salts_simd_v128 {
#if defined(__cplusplus)
    alignas(16) unsigned char bytes[16];
#else
    _Alignas(16) unsigned char bytes[16];
#endif
} salts_simd_v128;

/* Pure execution metadata with value semantics; descriptor addresses have no
 * cross-DSO identity contract. This deliberately has no CMeta type pointer and
 * is not ABI-compatible with the old reflected cmeta_vector_desc. Reflection
 * adapters must validate their type separately, then pass a native descriptor.
 * Do not cast an old descriptor or copy its object representation here. */
typedef struct salts_simd_desc {
    salts_simd_lane_kind lane_kind;
    uint16_t lane_bits;
    uint16_t lane_count;
    bool is_mask;
} salts_simd_desc;

extern const salts_simd_desc salts_simd_i8x16;
extern const salts_simd_desc salts_simd_u8x16;
extern const salts_simd_desc salts_simd_i16x8;
extern const salts_simd_desc salts_simd_u16x8;
extern const salts_simd_desc salts_simd_i32x4;
extern const salts_simd_desc salts_simd_u32x4;
extern const salts_simd_desc salts_simd_i64x2;
extern const salts_simd_desc salts_simd_u64x2;
extern const salts_simd_desc salts_simd_f32x4;
extern const salts_simd_desc salts_simd_f64x2;
extern const salts_simd_desc salts_simd_b8x16;
extern const salts_simd_desc salts_simd_b16x8;
extern const salts_simd_desc salts_simd_b32x4;
extern const salts_simd_desc salts_simd_b64x2;

/* Retains the original numeric structural checks: nonzero lane dimensions
 * totaling 128 bits and BOOL/mask agreement. Per-operation support is checked
 * by the execution function. This does not validate a reflection type, infer
 * lane width from lane_kind, or make an unsupported operation available. */
bool salts_simd_desc_valid(const salts_simd_desc *desc);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SIMD_TYPES_H */
