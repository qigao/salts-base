#include <simd.h>

#include <simde/wasm/simd128.h>

#include <string.h>

static simde_v128_t cmeta_simd_load_value(const cmeta_v128 *value) {
    return simde_wasm_v128_load(value->bytes);
}

static void cmeta_simd_store_value(cmeta_v128 *out, simde_v128_t value) {
    simde_wasm_v128_store(out->bytes, value);
}

static bool cmeta_simd_desc_valid(const salts_simd_desc *desc) {
    return desc != NULL && salts_simd_desc_valid(desc);
}

void cmeta_simd_v128_load(cmeta_v128 *out, const void *source) {
    if (out == NULL || source == NULL) return;
    cmeta_simd_store_value(out, simde_wasm_v128_load(source));
}

void cmeta_simd_v128_store(void *destination, const cmeta_v128 *value) {
    if (destination == NULL || value == NULL) return;
    simde_wasm_v128_store(destination, cmeta_simd_load_value(value));
}


bool cmeta_simd_load_splat(const salts_simd_desc *desc,
                           cmeta_v128 *out,
                           const void *source) {
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || source == NULL)
        return false;

    switch (desc->lane_bits) {
        case 8u:
            result = simde_wasm_v128_load8_splat(source);
            break;
        case 16u:
            result = simde_wasm_v128_load16_splat(source);
            break;
        case 32u:
            result = simde_wasm_v128_load32_splat(source);
            break;
        case 64u:
            result = simde_wasm_v128_load64_splat(source);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_load_extend(const salts_simd_desc *desc,
                            cmeta_v128 *out,
                            const void *source) {
    simde_v128_t low;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || source == NULL)
        return false;

    low = simde_wasm_v128_load64_zero(source);

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I16:
            result = simde_wasm_i16x8_extend_low_i8x16(low);
            break;
        case SALTS_SIMD_LANE_U16:
            result = simde_wasm_u16x8_extend_low_u8x16(low);
            break;
        case SALTS_SIMD_LANE_I32:
            result = simde_wasm_i32x4_extend_low_i16x8(low);
            break;
        case SALTS_SIMD_LANE_U32:
            result = simde_wasm_u32x4_extend_low_u16x8(low);
            break;
        case SALTS_SIMD_LANE_I64:
            result = simde_wasm_i64x2_extend_low_i32x4(low);
            break;
        case SALTS_SIMD_LANE_U64:
            result = simde_wasm_u64x2_extend_low_u32x4(low);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_load_zero(uint16_t loaded_bits,
                          cmeta_v128 *out,
                          const void *source) {
    simde_v128_t result;

    if (out == NULL || source == NULL)
        return false;

    if (loaded_bits == 32u)
        result = simde_wasm_v128_load32_zero(source);
    else if (loaded_bits == 64u)
        result = simde_wasm_v128_load64_zero(source);
    else
        return false;

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_extract_lane(const salts_simd_desc *desc,
                             const cmeta_v128 *value,
                             uint16_t lane,
                             cmeta_simd_scalar *out) {
    simde_v128_private private_value;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        value == NULL || out == NULL ||
        lane >= desc->lane_count)
        return false;

    private_value = simde_v128_to_private(
        cmeta_simd_load_value(value));

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            out->i8 = private_value.i8[lane];
            return true;
        case SALTS_SIMD_LANE_U8:
            out->u8 = private_value.u8[lane];
            return true;
        case SALTS_SIMD_LANE_I16:
            out->i16 = private_value.i16[lane];
            return true;
        case SALTS_SIMD_LANE_U16:
            out->u16 = private_value.u16[lane];
            return true;
        case SALTS_SIMD_LANE_I32:
            out->i32 = private_value.i32[lane];
            return true;
        case SALTS_SIMD_LANE_U32:
            out->u32 = private_value.u32[lane];
            return true;
        case SALTS_SIMD_LANE_I64:
            out->i64 = private_value.i64[lane];
            return true;
        case SALTS_SIMD_LANE_U64:
            out->u64 = private_value.u64[lane];
            return true;
        case SALTS_SIMD_LANE_F32:
            out->f32 = private_value.f32[lane];
            return true;
        case SALTS_SIMD_LANE_F64:
            out->f64 = private_value.f64[lane];
            return true;
        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }
}

bool cmeta_simd_replace_lane(const salts_simd_desc *desc,
                             cmeta_v128 *out,
                             const cmeta_v128 *value,
                             uint16_t lane,
                             cmeta_simd_scalar scalar) {
    simde_v128_private private_value;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || value == NULL ||
        lane >= desc->lane_count)
        return false;

    private_value = simde_v128_to_private(
        cmeta_simd_load_value(value));

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            private_value.i8[lane] = scalar.i8;
            break;
        case SALTS_SIMD_LANE_U8:
            private_value.u8[lane] = scalar.u8;
            break;
        case SALTS_SIMD_LANE_I16:
            private_value.i16[lane] = scalar.i16;
            break;
        case SALTS_SIMD_LANE_U16:
            private_value.u16[lane] = scalar.u16;
            break;
        case SALTS_SIMD_LANE_I32:
            private_value.i32[lane] = scalar.i32;
            break;
        case SALTS_SIMD_LANE_U32:
            private_value.u32[lane] = scalar.u32;
            break;
        case SALTS_SIMD_LANE_I64:
            private_value.i64[lane] = scalar.i64;
            break;
        case SALTS_SIMD_LANE_U64:
            private_value.u64[lane] = scalar.u64;
            break;
        case SALTS_SIMD_LANE_F32:
            private_value.f32[lane] = scalar.f32;
            break;
        case SALTS_SIMD_LANE_F64:
            private_value.f64[lane] = scalar.f64;
            break;
        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }

    cmeta_simd_store_value(
        out, simde_v128_from_private(private_value));
    return true;
}

bool cmeta_simd_shuffle_bytes(cmeta_v128 *out,
                              const cmeta_v128 *left,
                              const cmeta_v128 *right,
                              const uint8_t lanes[16]) {
    simde_v128_private left_private;
    simde_v128_private right_private;
    simde_v128_private result_private;
    uint16_t index;

    if (out == NULL || left == NULL ||
        right == NULL || lanes == NULL)
        return false;

    left_private = simde_v128_to_private(
        cmeta_simd_load_value(left));
    right_private = simde_v128_to_private(
        cmeta_simd_load_value(right));

    for (index = 0u; index < 16u; ++index) {
        uint8_t lane = lanes[index];
        if (lane >= 32u)
            return false;
        result_private.u8[index] =
            lane < 16u
                ? left_private.u8[lane]
                : right_private.u8[lane - 16u];
    }

    cmeta_simd_store_value(
        out, simde_v128_from_private(result_private));
    return true;
}

bool cmeta_simd_swizzle_bytes(cmeta_v128 *out,
                              const cmeta_v128 *value,
                              const cmeta_v128 *indices) {
    simde_v128_private value_private;
    simde_v128_private index_private;
    simde_v128_private result_private;
    uint16_t index;

    if (out == NULL || value == NULL || indices == NULL)
        return false;

    value_private = simde_v128_to_private(
        cmeta_simd_load_value(value));
    index_private = simde_v128_to_private(
        cmeta_simd_load_value(indices));

    for (index = 0u; index < 16u; ++index) {
        uint8_t lane = index_private.u8[index];
        result_private.u8[index] =
            lane < 16u ? value_private.u8[lane] : 0u;
    }

    cmeta_simd_store_value(
        out, simde_v128_from_private(result_private));
    return true;
}

bool cmeta_simd_splat(const salts_simd_desc *desc,
                      cmeta_v128 *out,
                      cmeta_simd_scalar scalar) {
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask || out == NULL)
        return false;

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            result = simde_wasm_i8x16_splat(scalar.i8);
            break;
        case SALTS_SIMD_LANE_U8: {
            int8_t value;
            memcpy(&value, &scalar.u8, sizeof(value));
            result = simde_wasm_i8x16_splat(value);
            break;
        }
        case SALTS_SIMD_LANE_I16:
            result = simde_wasm_i16x8_splat(scalar.i16);
            break;
        case SALTS_SIMD_LANE_U16: {
            int16_t value;
            memcpy(&value, &scalar.u16, sizeof(value));
            result = simde_wasm_i16x8_splat(value);
            break;
        }
        case SALTS_SIMD_LANE_I32:
            result = simde_wasm_i32x4_splat(scalar.i32);
            break;
        case SALTS_SIMD_LANE_U32: {
            int32_t value;
            memcpy(&value, &scalar.u32, sizeof(value));
            result = simde_wasm_i32x4_splat(value);
            break;
        }
        case SALTS_SIMD_LANE_I64:
            result = simde_wasm_i64x2_splat(scalar.i64);
            break;
        case SALTS_SIMD_LANE_U64: {
            int64_t value;
            memcpy(&value, &scalar.u64, sizeof(value));
            result = simde_wasm_i64x2_splat(value);
            break;
        }
        case SALTS_SIMD_LANE_F32:
            result = simde_wasm_f32x4_splat(scalar.f32);
            break;
        case SALTS_SIMD_LANE_F64:
            result = simde_wasm_f64x2_splat(scalar.f64);
            break;
        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_unary(const salts_simd_desc *desc,
                      cmeta_simd_unary_op op,
                      cmeta_v128 *out,
                      const cmeta_v128 *value) {
    simde_v128_t input;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || out == NULL || value == NULL)
        return false;

    input = cmeta_simd_load_value(value);

    if (op == SALTS_SIMD_UNARY_NOT) {
        cmeta_simd_store_value(out, simde_wasm_v128_not(input));
        return true;
    }
    if (desc->is_mask)
        return false;

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_i8x16_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_i8x16_neg(input);
            else if (op == SALTS_SIMD_UNARY_POPCOUNT)
                result = simde_wasm_i8x16_popcnt(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_U8:
            if (op == SALTS_SIMD_UNARY_POPCOUNT)
                result = simde_wasm_i8x16_popcnt(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_I16:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_i16x8_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_i16x8_neg(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_I32:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_i32x4_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_i32x4_neg(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_I64:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_i64x2_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_i64x2_neg(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_F32:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_f32x4_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_f32x4_neg(input);
            else if (op == SALTS_SIMD_UNARY_SQRT)
                result = simde_wasm_f32x4_sqrt(input);
            else if (op == SALTS_SIMD_UNARY_CEIL)
                result = simde_wasm_f32x4_ceil(input);
            else if (op == SALTS_SIMD_UNARY_FLOOR)
                result = simde_wasm_f32x4_floor(input);
            else if (op == SALTS_SIMD_UNARY_TRUNC)
                result = simde_wasm_f32x4_trunc(input);
            else if (op == SALTS_SIMD_UNARY_NEAREST)
                result = simde_wasm_f32x4_nearest(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_F64:
            if (op == SALTS_SIMD_UNARY_ABS)
                result = simde_wasm_f64x2_abs(input);
            else if (op == SALTS_SIMD_UNARY_NEG)
                result = simde_wasm_f64x2_neg(input);
            else if (op == SALTS_SIMD_UNARY_SQRT)
                result = simde_wasm_f64x2_sqrt(input);
            else if (op == SALTS_SIMD_UNARY_CEIL)
                result = simde_wasm_f64x2_ceil(input);
            else if (op == SALTS_SIMD_UNARY_FLOOR)
                result = simde_wasm_f64x2_floor(input);
            else if (op == SALTS_SIMD_UNARY_TRUNC)
                result = simde_wasm_f64x2_trunc(input);
            else if (op == SALTS_SIMD_UNARY_NEAREST)
                result = simde_wasm_f64x2_nearest(input);
            else
                return false;
            break;
        case SALTS_SIMD_LANE_U16:
        case SALTS_SIMD_LANE_U32:
        case SALTS_SIMD_LANE_U64:
        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_binary(const salts_simd_desc *desc,
                       cmeta_simd_binary_op op,
                       cmeta_v128 *out,
                       const cmeta_v128 *left,
                       const cmeta_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) ||
        out == NULL || left == NULL || right == NULL)
        return false;

    a = cmeta_simd_load_value(left);
    b = cmeta_simd_load_value(right);

    switch (op) {
        case SALTS_SIMD_BINARY_AND:
            cmeta_simd_store_value(out, simde_wasm_v128_and(a, b));
            return true;
        case SALTS_SIMD_BINARY_AND_NOT:
            cmeta_simd_store_value(out, simde_wasm_v128_andnot(a, b));
            return true;
        case SALTS_SIMD_BINARY_OR:
            cmeta_simd_store_value(out, simde_wasm_v128_or(a, b));
            return true;
        case SALTS_SIMD_BINARY_XOR:
            cmeta_simd_store_value(out, simde_wasm_v128_xor(a, b));
            return true;
        default:
            break;
    }

    if (desc->is_mask)
        return false;

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
        case SALTS_SIMD_LANE_U8:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i8x16_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i8x16_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_AVERAGE_ROUND_UNSIGNED &&
                     desc->lane_kind == SALTS_SIMD_LANE_U8)
                result = simde_wasm_u8x16_avgr(a, b);
            else if (op == SALTS_SIMD_BINARY_MIN)
                result = desc->lane_kind == SALTS_SIMD_LANE_I8
                    ? simde_wasm_i8x16_min(a, b)
                    : simde_wasm_u8x16_min(a, b);
            else if (op == SALTS_SIMD_BINARY_MAX)
                result = desc->lane_kind == SALTS_SIMD_LANE_I8
                    ? simde_wasm_i8x16_max(a, b)
                    : simde_wasm_u8x16_max(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_I16:
        case SALTS_SIMD_LANE_U16:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i16x8_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i16x8_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i16x8_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_AVERAGE_ROUND_UNSIGNED &&
                     desc->lane_kind == SALTS_SIMD_LANE_U16)
                result = simde_wasm_u16x8_avgr(a, b);
            else if (op == SALTS_SIMD_BINARY_MIN)
                result = desc->lane_kind == SALTS_SIMD_LANE_I16
                    ? simde_wasm_i16x8_min(a, b)
                    : simde_wasm_u16x8_min(a, b);
            else if (op == SALTS_SIMD_BINARY_MAX)
                result = desc->lane_kind == SALTS_SIMD_LANE_I16
                    ? simde_wasm_i16x8_max(a, b)
                    : simde_wasm_u16x8_max(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_I32:
        case SALTS_SIMD_LANE_U32:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i32x4_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i32x4_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i32x4_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_MIN)
                result = desc->lane_kind == SALTS_SIMD_LANE_I32
                    ? simde_wasm_i32x4_min(a, b)
                    : simde_wasm_u32x4_min(a, b);
            else if (op == SALTS_SIMD_BINARY_MAX)
                result = desc->lane_kind == SALTS_SIMD_LANE_I32
                    ? simde_wasm_i32x4_max(a, b)
                    : simde_wasm_u32x4_max(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_I64:
        case SALTS_SIMD_LANE_U64:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i64x2_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i64x2_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i64x2_mul(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_F32:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_f32x4_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_f32x4_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_f32x4_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_DIV)
                result = simde_wasm_f32x4_div(a, b);
            else if (op == SALTS_SIMD_BINARY_MIN)
                result = simde_wasm_f32x4_min(a, b);
            else if (op == SALTS_SIMD_BINARY_MAX)
                result = simde_wasm_f32x4_max(a, b);
            else if (op == SALTS_SIMD_BINARY_PSEUDO_MIN)
                result = simde_wasm_f32x4_pmin(a, b);
            else if (op == SALTS_SIMD_BINARY_PSEUDO_MAX)
                result = simde_wasm_f32x4_pmax(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_F64:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_f64x2_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_f64x2_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_f64x2_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_DIV)
                result = simde_wasm_f64x2_div(a, b);
            else if (op == SALTS_SIMD_BINARY_MIN)
                result = simde_wasm_f64x2_min(a, b);
            else if (op == SALTS_SIMD_BINARY_MAX)
                result = simde_wasm_f64x2_max(a, b);
            else if (op == SALTS_SIMD_BINARY_PSEUDO_MIN)
                result = simde_wasm_f64x2_pmin(a, b);
            else if (op == SALTS_SIMD_BINARY_PSEUDO_MAX)
                result = simde_wasm_f64x2_pmax(a, b);
            else
                return false;
            break;

        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

static bool cmeta_simd_compare_i8(salts_simd_lane_kind lane_kind,
                                  cmeta_simd_compare_op op,
                                  simde_v128_t a,
                                  simde_v128_t b,
                                  simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i8x16_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i8x16_ne(a, b);
        return true;
    }
    if (lane_kind == SALTS_SIMD_LANE_I8) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i8x16_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i8x16_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i8x16_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i8x16_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u8x16_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u8x16_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u8x16_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u8x16_ge(a, b); return true;
        default: return false;
    }
}

static bool cmeta_simd_compare_i16(salts_simd_lane_kind lane_kind,
                                   cmeta_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i16x8_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i16x8_ne(a, b);
        return true;
    }
    if (lane_kind == SALTS_SIMD_LANE_I16) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i16x8_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i16x8_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i16x8_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i16x8_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u16x8_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u16x8_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u16x8_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u16x8_ge(a, b); return true;
        default: return false;
    }
}

static bool cmeta_simd_compare_i32(salts_simd_lane_kind lane_kind,
                                   cmeta_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i32x4_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i32x4_ne(a, b);
        return true;
    }
    if (lane_kind == SALTS_SIMD_LANE_I32) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i32x4_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i32x4_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i32x4_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i32x4_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u32x4_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u32x4_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u32x4_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u32x4_ge(a, b); return true;
        default: return false;
    }
}

static bool cmeta_simd_compare_i64(salts_simd_lane_kind lane_kind,
                                   cmeta_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i64x2_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i64x2_ne(a, b);
        return true;
    }
    if (lane_kind != SALTS_SIMD_LANE_I64)
        return false;
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i64x2_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i64x2_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i64x2_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i64x2_ge(a, b); return true;
        default: return false;
    }
}

static bool cmeta_simd_compare_f32(cmeta_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    switch (op) {
        case SALTS_SIMD_COMPARE_EQ: *out = simde_wasm_f32x4_eq(a, b); return true;
        case SALTS_SIMD_COMPARE_NE: *out = simde_wasm_f32x4_ne(a, b); return true;
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_f32x4_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_f32x4_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_f32x4_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_f32x4_ge(a, b); return true;
        default: return false;
    }
}

static bool cmeta_simd_compare_f64(cmeta_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    switch (op) {
        case SALTS_SIMD_COMPARE_EQ: *out = simde_wasm_f64x2_eq(a, b); return true;
        case SALTS_SIMD_COMPARE_NE: *out = simde_wasm_f64x2_ne(a, b); return true;
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_f64x2_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_f64x2_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_f64x2_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_f64x2_ge(a, b); return true;
        default: return false;
    }
}

bool cmeta_simd_compare(const salts_simd_desc *desc,
                        cmeta_simd_compare_op op,
                        cmeta_v128 *out_mask,
                        const cmeta_v128 *left,
                        const cmeta_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;
    bool supported;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out_mask == NULL || left == NULL || right == NULL)
        return false;

    a = cmeta_simd_load_value(left);
    b = cmeta_simd_load_value(right);

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
        case SALTS_SIMD_LANE_U8:
            supported = cmeta_simd_compare_i8(desc->lane_kind, op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_I16:
        case SALTS_SIMD_LANE_U16:
            supported = cmeta_simd_compare_i16(desc->lane_kind, op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_I32:
        case SALTS_SIMD_LANE_U32:
            supported = cmeta_simd_compare_i32(desc->lane_kind, op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_I64:
        case SALTS_SIMD_LANE_U64:
            supported = cmeta_simd_compare_i64(desc->lane_kind, op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_F32:
            supported = cmeta_simd_compare_f32(op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_F64:
            supported = cmeta_simd_compare_f64(op, a, b, &result);
            break;
        case SALTS_SIMD_LANE_BOOL:
        default:
            supported = false;
            break;
    }

    if (!supported)
        return false;

    cmeta_simd_store_value(out_mask, result);
    return true;
}

bool cmeta_simd_saturating_binary(
    const salts_simd_desc *desc,
    cmeta_simd_saturating_op op,
    cmeta_v128 *out,
    const cmeta_v128 *left,
    const cmeta_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || left == NULL || right == NULL)
        return false;
    if (op != SALTS_SIMD_SATURATING_ADD &&
        op != SALTS_SIMD_SATURATING_SUB)
        return false;

    a = cmeta_simd_load_value(left);
    b = cmeta_simd_load_value(right);

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            result = op == SALTS_SIMD_SATURATING_ADD
                ? simde_wasm_i8x16_add_sat(a, b)
                : simde_wasm_i8x16_sub_sat(a, b);
            break;
        case SALTS_SIMD_LANE_U8:
            result = op == SALTS_SIMD_SATURATING_ADD
                ? simde_wasm_u8x16_add_sat(a, b)
                : simde_wasm_u8x16_sub_sat(a, b);
            break;
        case SALTS_SIMD_LANE_I16:
            result = op == SALTS_SIMD_SATURATING_ADD
                ? simde_wasm_i16x8_add_sat(a, b)
                : simde_wasm_i16x8_sub_sat(a, b);
            break;
        case SALTS_SIMD_LANE_U16:
            result = op == SALTS_SIMD_SATURATING_ADD
                ? simde_wasm_u16x8_add_sat(a, b)
                : simde_wasm_u16x8_sub_sat(a, b);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_reduce(const salts_simd_desc *desc,
                       cmeta_simd_reduce_op op,
                       const cmeta_v128 *value,
                       uint32_t *out) {
    simde_v128_t input;

    if (!cmeta_simd_desc_valid(desc) || value == NULL || out == NULL)
        return false;

    input = cmeta_simd_load_value(value);

    if (op == SALTS_SIMD_REDUCE_ANY_TRUE) {
        *out = simde_wasm_v128_any_true(input) ? 1u : 0u;
        return true;
    }

    if (desc->is_mask)
        return false;

    if (desc->lane_kind == SALTS_SIMD_LANE_F32 ||
        desc->lane_kind == SALTS_SIMD_LANE_F64 ||
        desc->lane_kind == SALTS_SIMD_LANE_BOOL)
        return false;

    if (op == SALTS_SIMD_REDUCE_ALL_TRUE) {
        switch (desc->lane_bits) {
            case 8u: *out = simde_wasm_i8x16_all_true(input) ? 1u : 0u; return true;
            case 16u: *out = simde_wasm_i16x8_all_true(input) ? 1u : 0u; return true;
            case 32u: *out = simde_wasm_i32x4_all_true(input) ? 1u : 0u; return true;
            case 64u: *out = simde_wasm_i64x2_all_true(input) ? 1u : 0u; return true;
            default: return false;
        }
    }

    if (op == SALTS_SIMD_REDUCE_BITMASK) {
        switch (desc->lane_bits) {
            case 8u: *out = simde_wasm_i8x16_bitmask(input); return true;
            case 16u: *out = simde_wasm_i16x8_bitmask(input); return true;
            case 32u: *out = simde_wasm_i32x4_bitmask(input); return true;
            case 64u: *out = simde_wasm_i64x2_bitmask(input); return true;
            default: return false;
        }
    }

    return false;
}


bool cmeta_simd_narrow(const salts_simd_desc *dst_desc,
                       cmeta_v128 *out,
                       const cmeta_v128 *low,
                       const cmeta_v128 *high) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(dst_desc) || dst_desc->is_mask ||
        out == NULL || low == NULL || high == NULL)
        return false;

    a = cmeta_simd_load_value(low);
    b = cmeta_simd_load_value(high);

    switch (dst_desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            result = simde_wasm_i8x16_narrow_i16x8(a, b);
            break;
        case SALTS_SIMD_LANE_U8:
            result = simde_wasm_u8x16_narrow_i16x8(a, b);
            break;
        case SALTS_SIMD_LANE_I16:
            result = simde_wasm_i16x8_narrow_i32x4(a, b);
            break;
        case SALTS_SIMD_LANE_U16:
            result = simde_wasm_u16x8_narrow_i32x4(a, b);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_extend_half(const salts_simd_desc *dst_desc,
                            cmeta_simd_half half,
                            cmeta_v128 *out,
                            const cmeta_v128 *value) {
    simde_v128_t input;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(dst_desc) || dst_desc->is_mask ||
        out == NULL || value == NULL ||
        (half != SALTS_SIMD_HALF_LOW && half != SALTS_SIMD_HALF_HIGH))
        return false;

    input = cmeta_simd_load_value(value);

    switch (dst_desc->lane_kind) {
        case SALTS_SIMD_LANE_I16:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i16x8_extend_low_i8x16(input)
                : simde_wasm_i16x8_extend_high_i8x16(input);
            break;
        case SALTS_SIMD_LANE_U16:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u16x8_extend_low_u8x16(input)
                : simde_wasm_u16x8_extend_high_u8x16(input);
            break;
        case SALTS_SIMD_LANE_I32:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i32x4_extend_low_i16x8(input)
                : simde_wasm_i32x4_extend_high_i16x8(input);
            break;
        case SALTS_SIMD_LANE_U32:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u32x4_extend_low_u16x8(input)
                : simde_wasm_u32x4_extend_high_u16x8(input);
            break;
        case SALTS_SIMD_LANE_I64:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i64x2_extend_low_i32x4(input)
                : simde_wasm_i64x2_extend_high_i32x4(input);
            break;
        case SALTS_SIMD_LANE_U64:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u64x2_extend_low_u32x4(input)
                : simde_wasm_u64x2_extend_high_u32x4(input);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_extmul_half(const salts_simd_desc *dst_desc,
                            cmeta_simd_half half,
                            cmeta_v128 *out,
                            const cmeta_v128 *left,
                            const cmeta_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(dst_desc) || dst_desc->is_mask ||
        out == NULL || left == NULL || right == NULL ||
        (half != SALTS_SIMD_HALF_LOW && half != SALTS_SIMD_HALF_HIGH))
        return false;

    a = cmeta_simd_load_value(left);
    b = cmeta_simd_load_value(right);

    switch (dst_desc->lane_kind) {
        case SALTS_SIMD_LANE_I16:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i16x8_extmul_low_i8x16(a, b)
                : simde_wasm_i16x8_extmul_high_i8x16(a, b);
            break;
        case SALTS_SIMD_LANE_U16:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u16x8_extmul_low_u8x16(a, b)
                : simde_wasm_u16x8_extmul_high_u8x16(a, b);
            break;
        case SALTS_SIMD_LANE_I32:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i32x4_extmul_low_i16x8(a, b)
                : simde_wasm_i32x4_extmul_high_i16x8(a, b);
            break;
        case SALTS_SIMD_LANE_U32:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u32x4_extmul_low_u16x8(a, b)
                : simde_wasm_u32x4_extmul_high_u16x8(a, b);
            break;
        case SALTS_SIMD_LANE_I64:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_i64x2_extmul_low_i32x4(a, b)
                : simde_wasm_i64x2_extmul_high_i32x4(a, b);
            break;
        case SALTS_SIMD_LANE_U64:
            result = half == SALTS_SIMD_HALF_LOW
                ? simde_wasm_u64x2_extmul_low_u32x4(a, b)
                : simde_wasm_u64x2_extmul_high_u32x4(a, b);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_extadd_pairwise(const salts_simd_desc *dst_desc,
                                cmeta_v128 *out,
                                const cmeta_v128 *value) {
    simde_v128_t input;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(dst_desc) || dst_desc->is_mask ||
        out == NULL || value == NULL)
        return false;

    input = cmeta_simd_load_value(value);

    switch (dst_desc->lane_kind) {
        case SALTS_SIMD_LANE_I16:
            result = simde_wasm_i16x8_extadd_pairwise_i8x16(input);
            break;
        case SALTS_SIMD_LANE_U16:
            result = simde_wasm_u16x8_extadd_pairwise_u8x16(input);
            break;
        case SALTS_SIMD_LANE_I32:
            result = simde_wasm_i32x4_extadd_pairwise_i16x8(input);
            break;
        case SALTS_SIMD_LANE_U32:
            result = simde_wasm_u32x4_extadd_pairwise_u16x8(input);
            break;
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}


bool cmeta_simd_q15mulr_sat(const salts_simd_desc *desc,
                            cmeta_v128 *out,
                            const cmeta_v128 *left,
                            const cmeta_v128 *right) {
    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        desc->lane_kind != SALTS_SIMD_LANE_I16 ||
        desc->lane_count != 8u ||
        out == NULL || left == NULL || right == NULL)
        return false;

    cmeta_simd_store_value(
        out,
        simde_wasm_i16x8_q15mulr_sat(
            cmeta_simd_load_value(left),
            cmeta_simd_load_value(right)));
    return true;
}

bool cmeta_simd_dot_pairwise(const salts_simd_desc *dst_desc,
                             cmeta_v128 *out,
                             const cmeta_v128 *left,
                             const cmeta_v128 *right) {
    if (!cmeta_simd_desc_valid(dst_desc) || dst_desc->is_mask ||
        dst_desc->lane_kind != SALTS_SIMD_LANE_I32 ||
        dst_desc->lane_count != 4u ||
        out == NULL || left == NULL || right == NULL)
        return false;

    cmeta_simd_store_value(
        out,
        simde_wasm_i32x4_dot_i16x8(
            cmeta_simd_load_value(left),
            cmeta_simd_load_value(right)));
    return true;
}

bool cmeta_simd_convert(const salts_simd_desc *dst_desc,
                        const salts_simd_desc *src_desc,
                        cmeta_simd_convert_op op,
                        cmeta_simd_lane_policy lane_policy,
                        cmeta_v128 *out,
                        const cmeta_v128 *value) {
    simde_v128_t input;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(dst_desc) ||
        !cmeta_simd_desc_valid(src_desc) ||
        dst_desc->is_mask || src_desc->is_mask ||
        out == NULL || value == NULL)
        return false;

    input = cmeta_simd_load_value(value);

    if (op == SALTS_SIMD_CONVERT_TRUNC_SAT &&
        src_desc->lane_kind == SALTS_SIMD_LANE_F32 &&
        src_desc->lane_count == 4u &&
        lane_policy == SALTS_SIMD_LANES_FULL) {
        if (dst_desc->lane_kind == SALTS_SIMD_LANE_I32 &&
            dst_desc->lane_count == 4u)
            result = simde_wasm_i32x4_trunc_sat_f32x4(input);
        else if (dst_desc->lane_kind == SALTS_SIMD_LANE_U32 &&
                 dst_desc->lane_count == 4u)
            result = simde_wasm_u32x4_trunc_sat_f32x4(input);
        else
            return false;
        cmeta_simd_store_value(out, result);
        return true;
    }

    if (op == SALTS_SIMD_CONVERT_TRUNC_SAT &&
        src_desc->lane_kind == SALTS_SIMD_LANE_F64 &&
        src_desc->lane_count == 2u &&
        lane_policy == SALTS_SIMD_LANES_LOW_ZERO) {
        if (dst_desc->lane_kind == SALTS_SIMD_LANE_I32 &&
            dst_desc->lane_count == 4u)
            result = simde_wasm_i32x4_trunc_sat_f64x2_zero(input);
        else if (dst_desc->lane_kind == SALTS_SIMD_LANE_U32 &&
                 dst_desc->lane_count == 4u)
            result = simde_wasm_u32x4_trunc_sat_f64x2_zero(input);
        else
            return false;
        cmeta_simd_store_value(out, result);
        return true;
    }

    if (op == SALTS_SIMD_CONVERT_NUMERIC &&
        dst_desc->lane_kind == SALTS_SIMD_LANE_F32 &&
        dst_desc->lane_count == 4u &&
        lane_policy == SALTS_SIMD_LANES_FULL) {
        if (src_desc->lane_kind == SALTS_SIMD_LANE_I32 &&
            src_desc->lane_count == 4u)
            result = simde_wasm_f32x4_convert_i32x4(input);
        else if (src_desc->lane_kind == SALTS_SIMD_LANE_U32 &&
                 src_desc->lane_count == 4u)
            result = simde_wasm_f32x4_convert_u32x4(input);
        else
            return false;
        cmeta_simd_store_value(out, result);
        return true;
    }

    if (op == SALTS_SIMD_CONVERT_NUMERIC &&
        dst_desc->lane_kind == SALTS_SIMD_LANE_F64 &&
        dst_desc->lane_count == 2u &&
        lane_policy == SALTS_SIMD_LANES_LOW) {
        if (src_desc->lane_kind == SALTS_SIMD_LANE_I32 &&
            src_desc->lane_count == 4u)
            result = simde_wasm_f64x2_convert_low_i32x4(input);
        else if (src_desc->lane_kind == SALTS_SIMD_LANE_U32 &&
                 src_desc->lane_count == 4u)
            result = simde_wasm_f64x2_convert_low_u32x4(input);
        else
            return false;
        cmeta_simd_store_value(out, result);
        return true;
    }

    if (op == SALTS_SIMD_CONVERT_DEMOTE &&
        dst_desc->lane_kind == SALTS_SIMD_LANE_F32 &&
        dst_desc->lane_count == 4u &&
        src_desc->lane_kind == SALTS_SIMD_LANE_F64 &&
        src_desc->lane_count == 2u &&
        lane_policy == SALTS_SIMD_LANES_LOW_ZERO) {
        cmeta_simd_store_value(
            out, simde_wasm_f32x4_demote_f64x2_zero(input));
        return true;
    }

    if (op == SALTS_SIMD_CONVERT_PROMOTE &&
        dst_desc->lane_kind == SALTS_SIMD_LANE_F64 &&
        dst_desc->lane_count == 2u &&
        src_desc->lane_kind == SALTS_SIMD_LANE_F32 &&
        src_desc->lane_count == 4u &&
        lane_policy == SALTS_SIMD_LANES_LOW) {
        cmeta_simd_store_value(
            out, simde_wasm_f64x2_promote_low_f32x4(input));
        return true;
    }

    return false;
}

bool cmeta_simd_shift(const salts_simd_desc *desc,
                      cmeta_simd_shift_op op,
                      cmeta_v128 *out,
                      const cmeta_v128 *value,
                      uint32_t count) {
    simde_v128_t input;
    simde_v128_t result;

    if (!cmeta_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || value == NULL)
        return false;

    input = cmeta_simd_load_value(value);

    switch (desc->lane_kind) {
        case SALTS_SIMD_LANE_I8:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i8x16_shl(input, count)
                : simde_wasm_i8x16_shr(input, count);
            break;
        case SALTS_SIMD_LANE_U8:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i8x16_shl(input, count)
                : simde_wasm_u8x16_shr(input, count);
            break;
        case SALTS_SIMD_LANE_I16:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i16x8_shl(input, count)
                : simde_wasm_i16x8_shr(input, count);
            break;
        case SALTS_SIMD_LANE_U16:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i16x8_shl(input, count)
                : simde_wasm_u16x8_shr(input, count);
            break;
        case SALTS_SIMD_LANE_I32:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i32x4_shl(input, count)
                : simde_wasm_i32x4_shr(input, count);
            break;
        case SALTS_SIMD_LANE_U32:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i32x4_shl(input, count)
                : simde_wasm_u32x4_shr(input, count);
            break;
        case SALTS_SIMD_LANE_I64:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i64x2_shl(input, count)
                : simde_wasm_i64x2_shr(input, count);
            break;
        case SALTS_SIMD_LANE_U64:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i64x2_shl(input, count)
                : simde_wasm_u64x2_shr(input, count);
            break;
        case SALTS_SIMD_LANE_F32:
        case SALTS_SIMD_LANE_F64:
        case SALTS_SIMD_LANE_BOOL:
        default:
            return false;
    }

    cmeta_simd_store_value(out, result);
    return true;
}

bool cmeta_simd_select(const salts_simd_desc *desc,
                       cmeta_v128 *out,
                       const cmeta_v128 *when_set,
                       const cmeta_v128 *when_unset,
                       const cmeta_v128 *mask) {
    if (!cmeta_simd_desc_valid(desc) ||
        out == NULL || when_set == NULL ||
        when_unset == NULL || mask == NULL)
        return false;

    cmeta_simd_store_value(
        out,
        simde_wasm_v128_bitselect(
            cmeta_simd_load_value(when_set),
            cmeta_simd_load_value(when_unset),
            cmeta_simd_load_value(mask)));
    return true;
}

void cmeta_simd_i32x4_splat(cmeta_v128 *out, int32_t value) {
    cmeta_simd_scalar scalar;
    scalar.i32 = value;
    (void)cmeta_simd_splat(&salts_simd_i32x4, out, scalar);
}

void cmeta_simd_i32x4_add(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right) {
    (void)cmeta_simd_binary(
        &salts_simd_i32x4, SALTS_SIMD_BINARY_ADD,
        out, left, right);
}

void cmeta_simd_i32x4_sub(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right) {
    (void)cmeta_simd_binary(
        &salts_simd_i32x4, SALTS_SIMD_BINARY_SUB,
        out, left, right);
}

void cmeta_simd_i32x4_mul(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right) {
    (void)cmeta_simd_binary(
        &salts_simd_i32x4, SALTS_SIMD_BINARY_MUL,
        out, left, right);
}

void cmeta_simd_i32x4_eq(cmeta_v128 *out_mask,
                         const cmeta_v128 *left,
                         const cmeta_v128 *right) {
    (void)cmeta_simd_compare(
        &salts_simd_i32x4, SALTS_SIMD_COMPARE_EQ,
        out_mask, left, right);
}

void cmeta_simd_f32x4_add(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right) {
    (void)cmeta_simd_binary(
        &salts_simd_f32x4, SALTS_SIMD_BINARY_ADD,
        out, left, right);
}

void cmeta_simd_f32x4_mul(cmeta_v128 *out,
                          const cmeta_v128 *left,
                          const cmeta_v128 *right) {
    (void)cmeta_simd_binary(
        &salts_simd_f32x4, SALTS_SIMD_BINARY_MUL,
        out, left, right);
}

void cmeta_simd_v128_bitselect(cmeta_v128 *out,
                               const cmeta_v128 *when_set,
                               const cmeta_v128 *when_unset,
                               const cmeta_v128 *mask) {
    (void)cmeta_simd_select(
        &salts_simd_i8x16,
        out, when_set, when_unset, mask);
}
