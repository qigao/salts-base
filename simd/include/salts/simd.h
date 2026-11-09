#ifndef SALTS_SIMD_H
#define SALTS_SIMD_H

#include <salts/simd_types.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The original C API names use the sole native 16-byte carrier.
 * SIMDe/native compiler vector types remain private to Salts::SIMD. */
typedef simd_v128 v128;

typedef union simd_scalar {
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
} simd_scalar;

typedef enum simd_unary_op {
    SALTS_SIMD_UNARY_NOT = 0,
    SALTS_SIMD_UNARY_ABS,
    SALTS_SIMD_UNARY_NEG,
    SALTS_SIMD_UNARY_SQRT,
    SALTS_SIMD_UNARY_POPCOUNT,
    SALTS_SIMD_UNARY_CEIL,
    SALTS_SIMD_UNARY_FLOOR,
    SALTS_SIMD_UNARY_TRUNC,
    SALTS_SIMD_UNARY_NEAREST
} simd_unary_op;

typedef enum simd_binary_op {
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
} simd_binary_op;

typedef enum simd_compare_op {
    SALTS_SIMD_COMPARE_EQ = 0,
    SALTS_SIMD_COMPARE_NE,
    SALTS_SIMD_COMPARE_LT,
    SALTS_SIMD_COMPARE_LE,
    SALTS_SIMD_COMPARE_GT,
    SALTS_SIMD_COMPARE_GE
} simd_compare_op;

typedef enum simd_shift_op {
    SALTS_SIMD_SHIFT_LEFT = 0,
    SALTS_SIMD_SHIFT_RIGHT
} simd_shift_op;


typedef enum simd_saturating_op {
    SALTS_SIMD_SATURATING_ADD = 0,
    SALTS_SIMD_SATURATING_SUB
} simd_saturating_op;

typedef enum simd_reduce_op {
    SALTS_SIMD_REDUCE_ANY_TRUE = 0,
    SALTS_SIMD_REDUCE_ALL_TRUE,
    SALTS_SIMD_REDUCE_BITMASK
} simd_reduce_op;

typedef enum simd_half {
    SALTS_SIMD_HALF_LOW = 0,
    SALTS_SIMD_HALF_HIGH
} simd_half;

typedef enum simd_convert_op {
    SALTS_SIMD_CONVERT_TRUNC_SAT = 0,
    SALTS_SIMD_CONVERT_NUMERIC,
    SALTS_SIMD_CONVERT_DEMOTE,
    SALTS_SIMD_CONVERT_PROMOTE
} simd_convert_op;

typedef enum simd_lane_policy {
    SALTS_SIMD_LANES_FULL = 0,
    SALTS_SIMD_LANES_LOW,
    SALTS_SIMD_LANES_LOW_ZERO
} simd_lane_policy;

void simd_v128_load(v128 *out, const void *source);
void simd_v128_store(void *destination, const v128 *value);

bool simd_splat(const simd_desc *desc,
                      v128 *out,
                      simd_scalar scalar);


bool simd_load_splat(const simd_desc *desc,
                           v128 *out,
                           const void *source);

bool simd_load_extend(const simd_desc *desc,
                            v128 *out,
                            const void *source);

bool simd_load_zero(uint16_t loaded_bits,
                          v128 *out,
                          const void *source);

bool simd_extract_lane(const simd_desc *desc,
                             const v128 *value,
                             uint16_t lane,
                             simd_scalar *out);

bool simd_replace_lane(const simd_desc *desc,
                             v128 *out,
                             const v128 *value,
                             uint16_t lane,
                             simd_scalar scalar);

bool simd_shuffle_bytes(v128 *out,
                              const v128 *left,
                              const v128 *right,
                              const uint8_t lanes[16]);

bool simd_swizzle_bytes(v128 *out,
                              const v128 *value,
                              const v128 *indices);


bool simd_unary(const simd_desc *desc,
                      simd_unary_op op,
                      v128 *out,
                      const v128 *value);

bool simd_binary(const simd_desc *desc,
                       simd_binary_op op,
                       v128 *out,
                       const v128 *left,
                       const v128 *right);

bool simd_compare(const simd_desc *desc,
                        simd_compare_op op,
                        v128 *out_mask,
                        const v128 *left,
                        const v128 *right);

bool simd_shift(const simd_desc *desc,
                      simd_shift_op op,
                      v128 *out,
                      const v128 *value,
                      uint32_t count);


bool simd_saturating_binary(
    const simd_desc *desc,
    simd_saturating_op op,
    v128 *out,
    const v128 *left,
    const v128 *right);

bool simd_reduce(const simd_desc *desc,
                       simd_reduce_op op,
                       const v128 *value,
                       uint32_t *out);

bool simd_narrow(const simd_desc *dst_desc,
                       v128 *out,
                       const v128 *low,
                       const v128 *high);

bool simd_extend_half(const simd_desc *dst_desc,
                            simd_half half,
                            v128 *out,
                            const v128 *value);

bool simd_extmul_half(const simd_desc *dst_desc,
                            simd_half half,
                            v128 *out,
                            const v128 *left,
                            const v128 *right);

bool simd_extadd_pairwise(const simd_desc *dst_desc,
                                v128 *out,
                                const v128 *value);

bool simd_q15mulr_sat(const simd_desc *desc,
                            v128 *out,
                            const v128 *left,
                            const v128 *right);

bool simd_dot_pairwise(const simd_desc *dst_desc,
                             v128 *out,
                             const v128 *left,
                             const v128 *right);

bool simd_convert(const simd_desc *dst_desc,
                        const simd_desc *src_desc,
                        simd_convert_op op,
                        simd_lane_policy lane_policy,
                        v128 *out,
                        const v128 *value);

bool simd_select(const simd_desc *desc,
                       v128 *out,
                       const v128 *when_set,
                       const v128 *when_unset,
                       const v128 *mask);

/* Compatibility helpers. New consumers should prefer the descriptor-driven
 * generic API above. */
void simd_i32x4_splat(v128 *out, int32_t value);
void simd_i32x4_add(v128 *out,
                          const v128 *left,
                          const v128 *right);
void simd_i32x4_sub(v128 *out,
                          const v128 *left,
                          const v128 *right);
void simd_i32x4_mul(v128 *out,
                          const v128 *left,
                          const v128 *right);
void simd_i32x4_eq(v128 *out_mask,
                         const v128 *left,
                         const v128 *right);

void simd_f32x4_add(v128 *out,
                          const v128 *left,
                          const v128 *right);
void simd_f32x4_mul(v128 *out,
                          const v128 *left,
                          const v128 *right);

void simd_v128_bitselect(v128 *out,
                               const v128 *when_set,
                               const v128 *when_unset,
                               const v128 *mask);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SIMD_H */
