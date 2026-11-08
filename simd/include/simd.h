#ifndef SALTS_SIMD_H
#define SALTS_SIMD_H

#include <simd_types.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The original C API names use the sole native 16-byte carrier.
 * SIMDe/native compiler vector types remain private to Salts::SIMD. */
typedef salts_simd_v128 cmeta_v128;

typedef union cmeta_simd_scalar {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    float f32;
    double f64;
} cmeta_simd_scalar;

typedef enum cmeta_simd_unary_op {
    SALTS_SIMD_UNARY_NOT = 0,
    SALTS_SIMD_UNARY_ABS,
    SALTS_SIMD_UNARY_NEG,
    SALTS_SIMD_UNARY_SQRT,
    SALTS_SIMD_UNARY_POPCOUNT,
    SALTS_SIMD_UNARY_CEIL,
    SALTS_SIMD_UNARY_FLOOR,
    SALTS_SIMD_UNARY_TRUNC,
    SALTS_SIMD_UNARY_NEAREST
} cmeta_simd_unary_op;

typedef enum cmeta_simd_binary_op {
    SALTS_SIMD_BINARY_ADD = 0,
    SALTS_SIMD_BINARY_SUB,
    SALTS_SIMD_BINARY_MUL,
    SALTS_SIMD_BINARY_DIV,
    SALTS_SIMD_BINARY_MIN,
    SALTS_SIMD_BINARY_MAX,
    SALTS_SIMD_BINARY_PSEUDO_MIN,
    SALTS_SIMD_BINARY_PSEUDO_MAX,
    SALTS_SIMD_BINARY_AVERAGE_ROUND_UNSIGNED,
    SALTS_SIMD_BINARY_AND,
    SALTS_SIMD_BINARY_AND_NOT,
    SALTS_SIMD_BINARY_OR,
    SALTS_SIMD_BINARY_XOR
} cmeta_simd_binary_op;

typedef enum cmeta_simd_compare_op {
    SALTS_SIMD_COMPARE_EQ = 0,
    SALTS_SIMD_COMPARE_NE,
    SALTS_SIMD_COMPARE_LT,
    SALTS_SIMD_COMPARE_LE,
    SALTS_SIMD_COMPARE_GT,
    SALTS_SIMD_COMPARE_GE
} cmeta_simd_compare_op;

typedef enum cmeta_simd_shift_op {
    SALTS_SIMD_SHIFT_LEFT = 0,
    SALTS_SIMD_SHIFT_RIGHT
} cmeta_simd_shift_op;


typedef enum cmeta_simd_saturating_op {
    SALTS_SIMD_SATURATING_ADD = 0,
    SALTS_SIMD_SATURATING_SUB
} cmeta_simd_saturating_op;

typedef enum cmeta_simd_reduce_op {
    SALTS_SIMD_REDUCE_ANY_TRUE = 0,
    SALTS_SIMD_REDUCE_ALL_TRUE,
    SALTS_SIMD_REDUCE_BITMASK
} cmeta_simd_reduce_op;

typedef enum cmeta_simd_half {
    SALTS_SIMD_HALF_LOW = 0,
    SALTS_SIMD_HALF_HIGH
} cmeta_simd_half;

typedef enum cmeta_simd_convert_op {
    SALTS_SIMD_CONVERT_TRUNC_SAT = 0,
    SALTS_SIMD_CONVERT_NUMERIC,
    SALTS_SIMD_CONVERT_DEMOTE,
    SALTS_SIMD_CONVERT_PROMOTE
} cmeta_simd_convert_op;

typedef enum cmeta_simd_lane_policy {
    SALTS_SIMD_LANES_FULL = 0,
    SALTS_SIMD_LANES_LOW,
    SALTS_SIMD_LANES_LOW_ZERO
} cmeta_simd_lane_policy;

void cmeta_simd_v128_load(cmeta_v128 *out, const void *source);
void cmeta_simd_v128_store(void *destination, const cmeta_v128 *value);

bool cmeta_simd_splat(const salts_simd_desc *desc,
                      cmeta_v128 *out,
                      cmeta_simd_scalar scalar);


bool cmeta_simd_load_splat(const salts_simd_desc *desc,
                           cmeta_v128 *out,
                           const void *source);

bool cmeta_simd_load_extend(const salts_simd_desc *desc,
                            cmeta_v128 *out,
                            const void *source);

bool cmeta_simd_load_zero(uint16_t loaded_bits,
                          cmeta_v128 *out,
                          const void *source);

bool cmeta_simd_extract_lane(const salts_simd_desc *desc,
                             const cmeta_v128 *value,
                             uint16_t lane,
                             cmeta_simd_scalar *out);

bool cmeta_simd_replace_lane(const salts_simd_desc *desc,
                             cmeta_v128 *out,
                             const cmeta_v128 *value,
                             uint16_t lane,
                             cmeta_simd_scalar scalar);

bool cmeta_simd_shuffle_bytes(cmeta_v128 *out,
                              const cmeta_v128 *left,
                              const cmeta_v128 *right,
                              const uint8_t lanes[16]);

bool cmeta_simd_swizzle_bytes(cmeta_v128 *out,
                              const cmeta_v128 *value,
                              const cmeta_v128 *indices);


bool cmeta_simd_unary(const salts_simd_desc *desc,
                      cmeta_simd_unary_op op,
                      cmeta_v128 *out,
                      const cmeta_v128 *value);

bool cmeta_simd_binary(const salts_simd_desc *desc,
                       cmeta_simd_binary_op op,
                       cmeta_v128 *out,
                       const cmeta_v128 *left,
                       const cmeta_v128 *right);

bool cmeta_simd_compare(const salts_simd_desc *desc,
                        cmeta_simd_compare_op op,
                        cmeta_v128 *out_mask,
                        const cmeta_v128 *left,
                        const cmeta_v128 *right);

bool cmeta_simd_shift(const salts_simd_desc *desc,
                      cmeta_simd_shift_op op,
                      cmeta_v128 *out,
                      const cmeta_v128 *value,
                      uint32_t count);


bool cmeta_simd_saturating_binary(
    const salts_simd_desc *desc,
    cmeta_simd_saturating_op op,
    cmeta_v128 *out,
    const cmeta_v128 *left,
    const cmeta_v128 *right);

bool cmeta_simd_reduce(const salts_simd_desc *desc,
                       cmeta_simd_reduce_op op,
                       const cmeta_v128 *value,
                       uint32_t *out);

bool cmeta_simd_narrow(const salts_simd_desc *dst_desc,
                       cmeta_v128 *out,
                       const cmeta_v128 *low,
                       const cmeta_v128 *high);

bool cmeta_simd_extend_half(const salts_simd_desc *dst_desc,
                            cmeta_simd_half half,
                            cmeta_v128 *out,
                            const cmeta_v128 *value);

bool cmeta_simd_extmul_half(const salts_simd_desc *dst_desc,
                            cmeta_simd_half half,
                            cmeta_v128 *out,
                            const cmeta_v128 *left,
                            const cmeta_v128 *right);

bool cmeta_simd_extadd_pairwise(const salts_simd_desc *dst_desc,
                                cmeta_v128 *out,
                                const cmeta_v128 *value);

bool cmeta_simd_q15mulr_sat(const salts_simd_desc *desc,
                            cmeta_v128 *out,
                            const cmeta_v128 *left,
                            const cmeta_v128 *right);

bool cmeta_simd_dot_pairwise(const salts_simd_desc *dst_desc,
                             cmeta_v128 *out,
                             const cmeta_v128 *left,
                             const cmeta_v128 *right);

bool cmeta_simd_convert(const salts_simd_desc *dst_desc,
                        const salts_simd_desc *src_desc,
                        cmeta_simd_convert_op op,
                        cmeta_simd_lane_policy lane_policy,
                        cmeta_v128 *out,
                        const cmeta_v128 *value);

bool cmeta_simd_select(const salts_simd_desc *desc,
                       cmeta_v128 *out,
                       const cmeta_v128 *when_set,
                       const cmeta_v128 *when_unset,
                       const cmeta_v128 *mask);

/* Compatibility helpers. New consumers should prefer the descriptor-driven
 * generic API above. */
void cmeta_simd_i32x4_splat(cmeta_v128 *out, int32_t value);
void cmeta_simd_i32x4_add(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right);
void cmeta_simd_i32x4_sub(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right);
void cmeta_simd_i32x4_mul(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right);
void cmeta_simd_i32x4_eq(cmeta_v128 *out_mask,
                         const cmeta_v128 *left,
                         const cmeta_v128 *right);

void cmeta_simd_f32x4_add(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right);
void cmeta_simd_f32x4_mul(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right);

void cmeta_simd_v128_bitselect(cmeta_v128 *out,
                               const cmeta_v128 *when_set,
                               const cmeta_v128 *when_unset,
                               const cmeta_v128 *mask);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SIMD_H */
