#include <simd.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "SIMD boundary failure at line %d: %s\n", __LINE__, #expr); \
    return 1; \
} } while (0)

static void store_lane(unsigned char *bytes, unsigned bits, uint64_t value) {
    switch (bits) {
        case 8u: { uint8_t v = (uint8_t)value; memcpy(bytes, &v, sizeof(v)); break; }
        case 16u: { uint16_t v = (uint16_t)value; memcpy(bytes, &v, sizeof(v)); break; }
        case 32u: { uint32_t v = (uint32_t)value; memcpy(bytes, &v, sizeof(v)); break; }
        default: memcpy(bytes, &value, sizeof(value)); break;
    }
}

static uint64_t load_lane(const unsigned char *bytes, unsigned bits) {
    switch (bits) {
        case 8u: { uint8_t v; memcpy(&v, bytes, sizeof(v)); return v; }
        case 16u: { uint16_t v; memcpy(&v, bytes, sizeof(v)); return v; }
        case 32u: { uint32_t v; memcpy(&v, bytes, sizeof(v)); return v; }
        default: { uint64_t v; memcpy(&v, bytes, sizeof(v)); return v; }
    }
}

/* Modular integer instructions preserve low bits, including abs/neg of MIN.
 * This oracle uses only defined unsigned arithmetic, independently of SIMDe. */
static int check_integer_wrap_boundaries(void) {
    const simd_desc *const shapes[] = {
        &simd_i8x16, &simd_i16x8,
        &simd_i32x4, &simd_i64x2
    };
    const simd_binary_op binary_ops[] = {
        SALTS_SIMD_BINARY_ADD, SALTS_SIMD_BINARY_SUB, SALTS_SIMD_BINARY_MUL
    };
    size_t s, pattern, lane, op;
    for (s = 0u; s < sizeof(shapes) / sizeof(shapes[0]); ++s) {
        const simd_desc *shape = shapes[s];
        const unsigned bits = shape->lane_bits;
        const unsigned width = bits / 8u;
        const uint64_t sign = UINT64_C(1) << (bits - 1u);
        const uint64_t mask = bits == 64u ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
        const uint64_t lhs[] = {sign, mask, sign - 1u, 0u, 1u};
        const uint64_t rhs[] = {mask, 1u, 2u, sign, 3u};
        for (pattern = 0u; pattern < sizeof(lhs) / sizeof(lhs[0]); ++pattern) {
            v128 left = {{0}}, right = {{0}}, out = {{0}}, sentinel = {{0}};
            for (lane = 0u; lane < shape->lane_count; ++lane) {
                const size_t i = (lane + pattern) % (sizeof(lhs) / sizeof(lhs[0]));
                store_lane(left.bytes + lane * width, bits, lhs[i]);
                store_lane(right.bytes + lane * width, bits, rhs[i]);
            }
            CHECK(simd_unary(shape, SALTS_SIMD_UNARY_ABS, &out, &left));
            for (lane = 0u; lane < shape->lane_count; ++lane) {
                const uint64_t a = load_lane(left.bytes + lane * width, bits);
                const uint64_t expected = (a & sign) ? (UINT64_C(0) - a) & mask : a;
                CHECK(load_lane(out.bytes + lane * width, bits) == expected);
            }
            CHECK(simd_unary(shape, SALTS_SIMD_UNARY_NEG, &out, &left));
            for (lane = 0u; lane < shape->lane_count; ++lane) {
                const uint64_t a = load_lane(left.bytes + lane * width, bits);
                CHECK(load_lane(out.bytes + lane * width, bits) == ((UINT64_C(0) - a) & mask));
            }
            for (op = 0u; op < sizeof(binary_ops) / sizeof(binary_ops[0]); ++op) {
                if (bits == 8u && binary_ops[op] == SALTS_SIMD_BINARY_MUL) {
                    memset(sentinel.bytes, 0x69, sizeof(sentinel.bytes));
                    out = sentinel;
                    CHECK(!simd_binary(shape, binary_ops[op], &out, &left, &right));
                    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
                    continue;
                }
                CHECK(simd_binary(shape, binary_ops[op], &out, &left, &right));
                for (lane = 0u; lane < shape->lane_count; ++lane) {
                    const uint64_t a = load_lane(left.bytes + lane * width, bits);
                    const uint64_t b = load_lane(right.bytes + lane * width, bits);
                    const uint64_t expected = binary_ops[op] == SALTS_SIMD_BINARY_ADD ? a + b :
                        binary_ops[op] == SALTS_SIMD_BINARY_SUB ? a - b : a * b;
                    CHECK(load_lane(out.bytes + lane * width, bits) == (expected & mask));
                }
            }
        }
    }
    return 0;
}

int main(void) {
    const simd_desc *const shapes[] = {
        &simd_i8x16, &simd_u8x16,
        &simd_i16x8, &simd_u16x8,
        &simd_i32x4, &simd_u32x4,
        &simd_i64x2, &simd_u64x2,
        &simd_f32x4, &simd_f64x2,
        &simd_b8x16, &simd_b16x8,
        &simd_b32x4, &simd_b64x2
    };
    const int32_t left_data[4] = {1, -2, 3, -4};
    const int32_t right_data[4] = {10, 20, 30, 40};
    const int32_t expected_sum[4] = {11, 18, 33, 36};
    int32_t result[4] = {0};
    simd_desc copy = simd_i32x4;
    simd_desc invalid = copy;
    v128 left = {{0}}, right = {{0}}, out = {{0}}, sentinel = {{0}};
    simd_scalar scalar = {0};
    unsigned char unaligned[18], stored[18];
    uint8_t selectors[16] = {0};
    size_t i;

    CHECK(sizeof(v128) == 16u);
    CHECK(_Alignof(v128) >= 16u);
    CHECK(check_integer_wrap_boundaries() == 0);
    for (i = 0u; i < sizeof(shapes) / sizeof(shapes[0]); ++i) {
        CHECK(simd_desc_valid(shapes[i]));
        CHECK((size_t)shapes[i]->lane_bits * shapes[i]->lane_count == 128u);
        CHECK(shapes[i]->is_mask == (shapes[i]->lane_kind == SALTS_SIMD_LANE_BOOL));
    }

    /* Plain value descriptors work independently of global pointer identity. */
    simd_v128_load(&left, left_data);
    simd_v128_load(&right, right_data);
    CHECK(simd_binary(&copy, SALTS_SIMD_BINARY_ADD, &left, &left, &right));
    simd_v128_store(result, &left);
    CHECK(memcmp(result, expected_sum, sizeof(result)) == 0);

    /* The implementation owns no caller storage; unaligned byte ranges need
     * only the documented readable/writable 16 bytes. Guards stay untouched. */
    memset(unaligned, 0xa5, sizeof(unaligned));
    memset(stored, 0x5a, sizeof(stored));
    memcpy(unaligned + 1u, left_data, sizeof(left_data));
    simd_v128_load(&out, unaligned + 1u);
    simd_v128_store(stored + 1u, &out);
    CHECK(memcmp(stored + 1u, left_data, sizeof(left_data)) == 0);
    CHECK(stored[0] == 0x5au && stored[17] == 0x5au);

    memset(sentinel.bytes, 0x6b, sizeof(sentinel.bytes));
    out = sentinel;
    CHECK(!simd_desc_valid(NULL));
    CHECK(!simd_binary(NULL, SALTS_SIMD_BINARY_ADD, &out, &left, &right));
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
    invalid.lane_count = 3u;
    CHECK(!simd_desc_valid(&invalid));
    CHECK(!simd_binary(&invalid, SALTS_SIMD_BINARY_ADD, &out, &left, &right));
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
    CHECK(!simd_binary(&copy, SALTS_SIMD_BINARY_DIV, &out, &left, &right));
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
    CHECK(!simd_binary(&copy, SALTS_SIMD_BINARY_ADD, NULL, &left, &right));
    CHECK(!simd_binary(&copy, SALTS_SIMD_BINARY_ADD, &out, NULL, &right));
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);

    scalar.i32 = 12345;
    CHECK(!simd_extract_lane(&copy, &left, 4u, &scalar));
    CHECK(scalar.i32 == 12345);
    selectors[15] = 32u;
    CHECK(!simd_shuffle_bytes(&out, &left, &right, selectors));
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
    simd_v128_load(&out, NULL);
    CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
    simd_v128_load(NULL, left_data);
    simd_v128_store(NULL, &left);
    simd_v128_store(stored + 1u, NULL);
    CHECK(memcmp(stored + 1u, left_data, sizeof(left_data)) == 0);
    return 0;
}
