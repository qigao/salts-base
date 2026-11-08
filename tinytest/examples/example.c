#include "tinytest.h"
#include <string.h>
#include <math.h>

static void verify_even(int n) {
    it("should be even") {
        check(n % 2 == 0, "expected %d to be even", n);
    }
}

static void verify_in_range(int val, int min, int max) {
    /* No 'it' block here, just a reusable assertion called inside a test */
    check(val >= min && val <= max, "expected %d to be in [%d, %d]", val, min, max);
}

suite("tinytest C Example") {

    static int a, b;

    before_all() {
        a = 3;
        b = 3;
    }

    group("Typed assertions") {

        it("should compare integers") {
            check_equal(a + b, 6);
            check_not_equal(a, b + 1);
            check_greater(a + b, 5);
            check_greater_equal(a, 3);
            check_less(a, 4);
            check_less_equal(b, 3);
        }

        it("should compare unsigned and size_t") {
            unsigned count = 42;
            check_equal(count, 42);
            check_not_equal(count, 0);

            size_t len = strlen("hello");
            check_equal(len, 5);
            check_not_equal(len, 0);
        }

        it("should compare 64-bit integers") {
            long long big = 1LL << 40;
            check_equal(big, 1LL << 40);
        }

        it("should compare floats with epsilon") {
            double pi = 3.14159;
            check_within(pi, 3.14159, 0.00001);
            check_not_equal(pi, 3.0);
            check_greater(pi, 3.0);
            check_less(pi, 4.0);
        }

        it("should compare hex values") {
            unsigned flags = 0xFF01;
            check_equal(flags, 0xFF01);
            check_equal(0xDEADBEEFULL, 0xDEADBEEFULL);
        }
    }

    group("String assertions") {

        it("should compare strings") {
            const char *greeting = "hello world";
            check_equal(greeting, "hello world");
            check_not_equal(greeting, "goodbye");
        }

        context("when working with URLs") {
            it("should check substrings and prefixes") {
                const char *url = "https://example.com/api/v2";
                check_contains(url, "example.com");
                check_starts_with(url, "https://");
            }
        }

        it("should handle NULL safely") {
            const char *empty = NULL;
            check_null(empty);
            check_not_equal(empty, "something");
        }
    }

    group("Pointer and memory") {

        it("should check pointers") {
            int x = 42;
            int *p = &x;
            check_not_null(p);

            int *q = NULL;
            check_null(q);
        }

        it("should compare memory blocks") {
            unsigned char buf_a[] = {0x01, 0x02, 0x03, 0x04};
            unsigned char buf_b[] = {0x01, 0x02, 0x03, 0x04};
            check_equal(buf_a, buf_b, 4);
        }
    }

    group("Non-fatal warnings") {

        it("should continue after check_warn") {
            check_warn(1 == 1);
            check_warn(2 + 2 == 4);
            check(1 == 1);
        }
    }

    group("File helpers") {

        it("should write and read a temp file") {
            const char *msg = "hello file";
            char *path = tt_make_temp_file("tt", ".txt");
            check_not_null(path);
            check_equal(tt_write_file(path, msg, strlen(msg)), 0);

            size_t n = 0;
            char *data = tt_read_file(path, &n);
            check_not_null(data);
            check_equal(n, strlen(msg));
            check_equal(data, msg);
            free(data);

            check_equal(tt_remove_file(path), 0);
            free(path);
        }

        it("should create and remove a temp directory tree") {
            char *dir = tt_make_temp_dir("ttd");
            check_not_null(dir);

            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s/%s", dir, "a.txt");
            check_equal(tt_write_file(file_path, "x", 1), 0);

            check_equal(tt_remove_tree(dir), 0);
            free(dir);
        }
    }

    group("Function organization") {
        /* Helpers containing 'it' or 'describe' should be called at group/suite level */
        verify_even(42);
        verify_even(100);

        it("should allow calling functions WITH NO 'it' inside tests") {
            int score = 85;
            verify_in_range(score, 0, 100);
        }
    }

    group("Info context") {

        it("should attach context on failure") {
            int values[] = {10, 20, 30, 40, 50};
            for (int i = 0; i < 5; i++) {
                info("checking index %d, value=%d", i, values[i]);
                check(values[i] > 0);
            }
        }

        it("should capture variables") {
            int x = 42;
            capture(x, "%d");
            check_equal(x, 42);
        }
    }

    group("BDD-style syntax") {
        given("a balance") {
        static int balance;

        before_each() {
            balance = 100;
        }

        when("depositing money") {
            then("should increase balance") {
                balance += 50;
                check_equal(balance, 150);
            }
        }

        when("withdrawing money") {
            then("should decrease balance") {
                balance -= 30;
                check_equal(balance, 70);
            }
        }
        }
    }

    group("Focus and skip") {

        xit("this test is skipped") {
            check(0);
        }

        it_should_fail("known issue: off-by-one") {
            check_equal(a + b, 7);
        }
    }

    group("Line number reporting") {
        it_should_fail("should report line 235 for check_not_null failure") {
            void* ptr = NULL;
            check_not_null(ptr);  // Line 235 - should report THIS line
        }

        it_should_fail("should report line 240 for check_equal failure") {
            int x = 42;
            check_equal(x, 99);  // Line 240 - should report THIS line
        }

        it_should_fail("should report line 245 for check with message failure") {
            int value = 10;
            check(value == 20, "expected 20 but got %d", value);  // Line 245 - should report THIS line
        }
    }
}
