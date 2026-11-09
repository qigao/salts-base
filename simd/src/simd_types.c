#include <simd_types.h>

/* These immutable execution descriptors are the only native shape objects;
 * reflection/type identity remains in the upper-layer CMeta owner. */
#define SALTS_DEFINE_SIMD_DESC(symbol, lane, bits, count, mask_value) \
    const simd_desc simd_##symbol = { \
        lane, bits, count, mask_value \
    }

SALTS_DEFINE_SIMD_DESC(i8x16, SALTS_SIMD_LANE_I8, 8u, 16u, false);
SALTS_DEFINE_SIMD_DESC(u8x16, SALTS_SIMD_LANE_U8, 8u, 16u, false);
SALTS_DEFINE_SIMD_DESC(i16x8, SALTS_SIMD_LANE_I16, 16u, 8u, false);
SALTS_DEFINE_SIMD_DESC(u16x8, SALTS_SIMD_LANE_U16, 16u, 8u, false);
SALTS_DEFINE_SIMD_DESC(i32x4, SALTS_SIMD_LANE_I32, 32u, 4u, false);
SALTS_DEFINE_SIMD_DESC(u32x4, SALTS_SIMD_LANE_U32, 32u, 4u, false);
SALTS_DEFINE_SIMD_DESC(i64x2, SALTS_SIMD_LANE_I64, 64u, 2u, false);
SALTS_DEFINE_SIMD_DESC(u64x2, SALTS_SIMD_LANE_U64, 64u, 2u, false);
SALTS_DEFINE_SIMD_DESC(f32x4, SALTS_SIMD_LANE_F32, 32u, 4u, false);
SALTS_DEFINE_SIMD_DESC(f64x2, SALTS_SIMD_LANE_F64, 64u, 2u, false);
SALTS_DEFINE_SIMD_DESC(b8x16, SALTS_SIMD_LANE_BOOL, 8u, 16u, true);
SALTS_DEFINE_SIMD_DESC(b16x8, SALTS_SIMD_LANE_BOOL, 16u, 8u, true);
SALTS_DEFINE_SIMD_DESC(b32x4, SALTS_SIMD_LANE_BOOL, 32u, 4u, true);
SALTS_DEFINE_SIMD_DESC(b64x2, SALTS_SIMD_LANE_BOOL, 64u, 2u, true);

#undef SALTS_DEFINE_SIMD_DESC

bool simd_desc_valid(const simd_desc *desc) {
    if (desc == NULL || desc->lane_bits == 0u || desc->lane_count == 0u ||
        (size_t)desc->lane_bits * (size_t)desc->lane_count != 128u)
        return false;
    if (desc->is_mask)
        return desc->lane_kind == SALTS_SIMD_LANE_BOOL;
    return desc->lane_kind != SALTS_SIMD_LANE_BOOL;
}
