#include <simd.h>
#include <tinytest.hpp>

#include <type_traits>

static_assert(sizeof(v128) == 16u, "SIMD carrier must remain 128 bits");
static_assert(alignof(v128) >= 16u, "SIMD carrier must remain 16-byte aligned");
static_assert(std::is_standard_layout<v128>::value,
              "SIMD carrier must remain a standard-layout ABI type");

suite("SIMD C++ header") {
    group("vector operations") {
        it("splats an integer into every lane") {
            constexpr int32_t scalar = 7;
            v128 value{};
            int32_t lanes[sizeof(v128) / sizeof(int32_t)]{};

            simd_i32x4_splat(&value, scalar);
            simd_v128_store(lanes, &value);
            for (const auto lane : lanes) {
                check_equal(lane, scalar);
            }
        }
    }
}
