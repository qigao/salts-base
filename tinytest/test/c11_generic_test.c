#include "tinytest.h"
#include <stdint.h>
#include <stddef.h>

#if !defined(TTEST_HAS_C11_GENERIC__)
#error "c11_generic_test requires C11 _Generic support in tinytest.h"
#endif

struct Foo {
    int value;
};

enum c11_generic_signed_enum {
    C11_GENERIC_ENUM_NEGATIVE = -7,
    C11_GENERIC_ENUM_POSITIVE = 9
};

static int c11_generic_enum_evaluations;

static enum c11_generic_signed_enum c11_generic_next_enum(void) {
    ++c11_generic_enum_evaluations;
    return C11_GENERIC_ENUM_POSITIVE;
}

spec("C11 _Generic assertions") {
    it("should route check_equal for diverse integral and floating-point types") {
        char c1 = 12;
        char c2 = 12;
        signed char sc1 = -12;
        signed char sc2 = -12;
        unsigned char uc1 = 200u;
        unsigned char uc2 = 200u;
        short short1 = -42;
        short short2 = -42;
        unsigned short us1 = 42u;
        unsigned short us2 = 42u;
        int i1 = -12;
        int i2 = -12;
        unsigned int ui1 = 100u;
        unsigned int ui2 = 100u;
        bool b1 = true;
        bool b2 = true;
        int64_t i64a = INT64_C(0x1234567890ABCDEF);
        int64_t i64b = INT64_C(0x1234567890ABCDEF);
        uint64_t u64a = UINT64_C(0xFEDCBA9876543210);
        uint64_t u64b = UINT64_C(0xFEDCBA9876543210);
        size_t s1 = (size_t)12345;
        size_t s2 = (size_t)12345;
        float f1 = 2.5f;
        float f2 = 2.5f;
        double d1 = 3.141592653589793;
        double d2 = 3.141592653589793;
        long double ld1 = 2.718281828459045L;
        long double ld2 = 2.718281828459045L;

        check_equal(c1, c2);
        check_equal(sc1, sc2);
        check_equal(uc1, uc2);
        check_equal(short1, short2);
        check_equal(us1, us2);
        check_equal(i1, i2);
        check_equal(b1, b2);
        check_equal(ui1, ui2);
        check_equal(i64a, i64b);
        check_equal(u64a, u64b);
        check_equal(s1, s2);
        check_equal(f1, f2);
        check_equal(d1, d2);
        check_equal(ld1, ld2);
        check_equal_warn(i1, i2);
        check_equal_warn(b1, b2);
        check_equal_warn(ui1, ui2);
        check_equal_warn(i64a, i64b);
        check_equal_warn(u64a, u64b);
        check_equal_warn(s1, s2);
        check_equal_warn(f1, f2);
        check_equal_warn(d1, d2);
        check_equal_warn(ld1, ld2);
    }

    it("should route signed enums through integer comparisons once") {
        c11_generic_enum_evaluations = 0;

        check_equal(C11_GENERIC_ENUM_NEGATIVE, C11_GENERIC_ENUM_NEGATIVE);
        check_not_equal(C11_GENERIC_ENUM_NEGATIVE, C11_GENERIC_ENUM_POSITIVE);
        check_less(C11_GENERIC_ENUM_NEGATIVE, C11_GENERIC_ENUM_POSITIVE);
        check_in_range(C11_GENERIC_ENUM_NEGATIVE,
                       C11_GENERIC_ENUM_NEGATIVE,
                       C11_GENERIC_ENUM_POSITIVE);
        check_equal(c11_generic_next_enum(), C11_GENERIC_ENUM_POSITIVE);
        check_equal(c11_generic_enum_evaluations, 1);
    }

    it("should route string helpers for check_equal and check_not_equal") {
        const char *cs1 = "hello tinytest";
        const char *cs2 = "hello tinytest";
        char mutable_str1[] = "hello tinytest";
        char mutable_str2[] = "hello tinytest";

        check_equal(cs1, cs2);
        check_equal(mutable_str1, mutable_str2);
        check_equal_warn(cs1, cs2);
        check_equal_warn(mutable_str1, mutable_str2);

        check_not_equal(cs1, "different");
        check_not_equal(mutable_str1, "different");
        check_not_equal_warn(cs1, "different");
        check_not_equal_warn(mutable_str1, "different");
    }

    it("should route three-argument equality to memory comparison") {
        const unsigned char same_a[] = {0u, 1u, 2u, 3u};
        const unsigned char same_b[] = {0u, 1u, 2u, 3u};
        const unsigned char different[] = {0u, 1u, 9u, 3u};

        check_equal(same_a, same_b, sizeof(same_a));
        check_equal_warn(same_a, same_b, sizeof(same_a));
        check_not_equal(same_a, different, sizeof(same_a));
        check_not_equal_warn(same_a, different, sizeof(same_a));
    }

    it("should route pointer helpers for check_equal and check_not_equal") {
        struct Foo foo1 = {10};
        struct Foo foo2 = {20};
        struct Foo *foo_ptr1 = &foo1;
        struct Foo *foo_ptr2 = &foo2;
        void *vptr1 = &foo1;
        void *vptr2 = &foo2;
        const void *cptr1 = &foo1;
        const void *cptr2 = &foo2;

        check_equal((const void *)foo_ptr1, (const void *)foo_ptr1);
        check_equal(vptr1, vptr1);
        check_equal(cptr1, cptr1);
        check_equal_warn((const void *)foo_ptr1, (const void *)foo_ptr1);
        check_equal_warn(vptr1, vptr1);
        check_equal_warn(cptr1, cptr1);

        check_not_equal((const void *)foo_ptr1, (const void *)foo_ptr2);
        check_not_equal(vptr1, vptr2);
        check_not_equal(cptr1, cptr2);
        check_not_equal_warn((const void *)foo_ptr1, (const void *)foo_ptr2);
        check_not_equal_warn(vptr1, vptr2);
        check_not_equal_warn(cptr1, cptr2);
    }

    it("should route check_not_equal for integer and pointer families") {
        int i1 = 7;
        int i2 = 8;
        bool b1 = true;
        bool b2 = false;
        unsigned int ui1 = 100u;
        unsigned int ui2 = 200u;
        int64_t i64a = INT64_C(10);
        int64_t i64b = INT64_C(20);
        uint64_t u64a = UINT64_C(100);
        uint64_t u64b = UINT64_C(120);
        size_t s1 = (size_t)16;
        size_t s2 = (size_t)24;
        float f1 = 1.5f;
        float f2 = 2.5f;
        double d1 = 0.25;
        double d2 = 0.5;
        long double ld1 = 0.125L;
        long double ld2 = 0.25L;

        check_not_equal(i1, i2);
        check_not_equal(b1, b2);
        check_not_equal(ui1, ui2);
        check_not_equal(i64a, i64b);
        check_not_equal(u64a, u64b);
        check_not_equal(s1, s2);
        check_not_equal(f1, f2);
        check_not_equal(d1, d2);
        check_not_equal(ld1, ld2);
        check_not_equal_warn(i1, i2);
        check_not_equal_warn(b1, b2);
        check_not_equal_warn(ui1, ui2);
        check_not_equal_warn(i64a, i64b);
        check_not_equal_warn(u64a, u64b);
        check_not_equal_warn(s1, s2);
        check_not_equal_warn(f1, f2);
        check_not_equal_warn(d1, d2);
        check_not_equal_warn(ld1, ld2);
    }

    it("should route ordered comparisons across numeric families") {
        int i_small = 1;
        int i_big = 10;
        unsigned int ui_small = 1u;
        unsigned int ui_big = 10u;
        bool b_small = false;
        bool b_big = true;
        int64_t i64_small = INT64_C(5);
        int64_t i64_big = INT64_C(8);
        uint64_t u64_small = UINT64_C(5);
        uint64_t u64_big = UINT64_C(11);
        size_t s_small = (size_t)16;
        size_t s_big = (size_t)32;
        float f_small = 2.71f;
        float f_big = 3.14f;
        double d_small = 2.0;
        double d_big = 4.0;
        long double ld_small = 1.0L;
        long double ld_big = 2.0L;

        check_greater(i_big, i_small);
        check_greater(b_big, b_small);
        check_greater(ui_big, ui_small);
        check_greater(i64_big, i64_small);
        check_greater(u64_big, u64_small);
        check_greater(s_big, s_small);
        check_greater(f_big, f_small);
        check_greater(d_big, d_small);
        check_greater(ld_big, ld_small);
        check_less(i_small, i_big);
        check_less(b_small, b_big);
        check_less(ui_small, ui_big);
        check_less(i64_small, i64_big);
        check_less(u64_small, u64_big);
        check_less(s_small, s_big);
        check_less(f_small, f_big);
        check_less(d_small, d_big);
        check_less(ld_small, ld_big);
        check_greater_warn(i_big, i_small);
        check_greater_warn(b_big, b_small);
        check_greater_warn(ui_big, ui_small);
        check_greater_warn(i64_big, i64_small);
        check_greater_warn(u64_big, u64_small);
        check_greater_warn(s_big, s_small);
        check_greater_warn(f_big, f_small);
        check_greater_warn(d_big, d_small);
        check_greater_warn(ld_big, ld_small);
        check_less_warn(i_small, i_big);
        check_less_warn(b_small, b_big);
        check_less_warn(ui_small, ui_big);
        check_less_warn(i64_small, i64_big);
        check_less_warn(u64_small, u64_big);
        check_less_warn(s_small, s_big);
        check_less_warn(f_small, f_big);
        check_less_warn(d_small, d_big);
        check_less_warn(ld_small, ld_big);
        check_greater_equal(i_big, i_big);
        check_greater_equal(ui_big, ui_big);
        check_greater_equal(f_big, f_big);
        check_greater_equal(ld_big, ld_big);
        check_less_equal(i_small, i_small);
        check_less_equal(ui_small, ui_small);
        check_less_equal(d_small, d_small);
        check_less_equal(ld_small, ld_small);
        check_greater_equal_warn(i_big, i_big);
        check_greater_equal_warn(ui_big, ui_big);
        check_greater_equal_warn(f_big, f_big);
        check_greater_equal_warn(ld_big, ld_big);
        check_less_equal_warn(i_small, i_small);
        check_less_equal_warn(ui_small, ui_small);
        check_less_equal_warn(d_small, d_small);
        check_less_equal_warn(ld_small, ld_small);
    }

    it("should support close and almost-equal assertions") {
        float f1 = 10.0f;
        float f2 = 10.000001f;
        double d1 = 1000.0;
        double d2 = 1000.0005;
        long double ld1 = 2.0L;
        long double ld2 = 2.0000000000001L;

        check_within(f1, f2, 0.001f);
        check_within_warn(f1, f2, 0.001f);
        check_within(d1, d2, 0.001);
        check_within_warn(d1, d2, 0.001);
        check_within(ld1, ld2, 0.001L);
        check_within_warn(ld1, ld2, 0.001L);
    }

    it("should support in_range and between assertions") {
        int i = 7;
        unsigned int ui = 15;
        float f = 3.5f;
        double d = 10.0;
        long double ld = 1.5L;

        check_in_range(i, 0, 10);
        check_in_range(ui, 10u, 20u);
        check_in_range(f, 2.0f, 5.0f);
        check_in_range(d, 0.0, 11.0);
        check_in_range(ld, 1.0L, 2.0L);
        check_in_range_warn(i, 0, 10);
        check_in_range_warn(ui, 10u, 20u);
        check_in_range_warn(f, 2.0f, 5.0f);
        check_in_range_warn(d, 0.0, 11.0);
        check_in_range_warn(ld, 1.0L, 2.0L);

        check_between(i, 0, 10);
        check_between_warn(i, 0, 10);
        check_between(ui, 10u, 20u);
        check_between_warn(ui, 10u, 20u);
        check_between(f, 2.0f, 5.0f);
        check_between_warn(f, 2.0f, 5.0f);
    }

    it("should support null/is-null aliases") {
        int x = 1;
        int *ptr = &x;
        int *null_ptr = NULL;

        check_not_null(ptr);
        check_not_null_warn(ptr);
        check_is_null(null_ptr);
        check_is_null_warn(null_ptr);
    }

    it("should support string contains using check_contains in C mode") {
        const char *haystack = "hello tinytest c11";

        check_contains(haystack, "tinytest");
        check_contains_warn(haystack, "tinytest");
    }
}
