#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct Point {
    int x;
    int y;
} Point;

static int point_comparator_calls;

static bool point_equal(const Point *actual, const Point *expected) {
    ++point_comparator_calls;
    return actual->x == expected->x && actual->y == expected->y;
}

#define TTEST_USER_EQUAL_TRAIT_LIST \
    , (POINT, Point, point_equal)
#include "tinytest.h"

static Point make_point(int *calls, int x, int y) {
    Point point = {x, y};
    ++*calls;
    return point;
}

spec("TinyTest custom equality") {
    it("compares registered values through check_equal") {
        Point actual = {3, 5};
        Point expected = {3, 5};

        point_comparator_calls = 0;
        check_equal(actual, expected);
        check_equal_warn(actual, expected);
        check_equal(point_comparator_calls, 2);
    }

    it_should_fail("fails when registered values are unequal") {
        Point actual = {3, 5};
        Point expected = {3, 8};

        check_equal(actual, expected);
    }

    it("evaluates each check_equal argument once") {
        int actual_calls = 0;
        int expected_calls = 0;

        point_comparator_calls = 0;
        check_equal(make_point(&actual_calls, 7, 11),
                    make_point(&expected_calls, 7, 11));

        check_equal(actual_calls, 1);
        check_equal(expected_calls, 1);
        check_equal(point_comparator_calls, 1);
    }

    it("preserves TinyTest builtin comparison semantics") {
        double actual = 1.0;
        double expected = 1.0 + 5e-10;
        const char *left = "same text";
        char right[] = "same text";

        check_equal(actual, expected);
        check_equal(left, right);
    }

    it("preserves three-argument memory equality") {
        const unsigned char actual[] = {1u, 2u, 3u};
        const unsigned char expected[] = {1u, 2u, 3u};

        check_equal(actual, expected, sizeof(actual));
        check_equal_warn(actual, expected, sizeof(actual));
    }
}
