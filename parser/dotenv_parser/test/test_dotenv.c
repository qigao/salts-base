#include <tinytest.h>
#include <dotenv.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define setenv(name,val,overwrite) _putenv_s(name, val)
#define unsetenv(name) _putenv_s(name, "")
#endif

spec("dotenv") {
    before_each() {
        // Clean up environment variables used in tests
        unsetenv("TEST_KEY");
        unsetenv("NESTED_KEY");
        unsetenv("OVERWRITE_KEY");
        unsetenv("EXISTING_VAR");
    }

    describe("Simple Load") {
        it("should load simple environment variables") {
            const char *env_content = "TEST_KEY=test_value\n";
            FILE *f = fopen(".env.test.simple", "w");
            if (f) {
                fputs(env_content, f);
                fclose(f);
            }

            int res = dotenv_load(".env.test.simple", true);
            check_equal(res, 0);

            const char *val = getenv("TEST_KEY");
            check_not_null(val);
            check_equal(val, "test_value");

            remove(".env.test.simple");
        }
    }

    describe("Nested Variables") {
        it("should handle nested variable substitution") {
            setenv("EXISTING_VAR", "base", 1);
            const char *env_content = "NESTED_KEY=${EXISTING_VAR}/extra\n";
            FILE *f = fopen(".env.test.nested", "w");
            if (f) {
                fputs(env_content, f);
                fclose(f);
            }

            int res = dotenv_load(".env.test.nested", true);
            check_equal(res, 0);

            const char *val = getenv("NESTED_KEY");
            check_not_null(val);
            check_equal(val, "base/extra");

            remove(".env.test.nested");
        }
    }

    describe("Overwrite Behavior") {
        it("should respect overwrite flag") {
            setenv("OVERWRITE_KEY", "original", 1);
            const char *env_content = "OVERWRITE_KEY=new_value\n";
            FILE *f = fopen(".env.test.overwrite", "w");
            if (f) {
                fputs(env_content, f);
                fclose(f);
            }

            // Test without overwrite
            dotenv_load(".env.test.overwrite", false);
            check_equal(getenv("OVERWRITE_KEY"), "original");

            // Test with overwrite
            dotenv_load(".env.test.overwrite", true);
            check_equal(getenv("OVERWRITE_KEY"), "new_value");

            remove(".env.test.overwrite");
        }
    }

    describe("Error Handling") {
        it("should return error for missing file") {
            int res = dotenv_load("non_existent_file", true);
            check(res < 0);
        }
    }

    it("should preserve a long unquoted value while skipping comments") {
        enum { VALUE_BYTES = 4096 };
        const char prefix[] = "  # ignored comment\nSIMD_VALUE=";
        const char suffix[] = "   # ignored trailing comment\n";
        char *env_content = (char *)malloc(sizeof(prefix) + VALUE_BYTES + sizeof(suffix));
        size_t offset = 0;

        check_not_null(env_content);
        memcpy(env_content + offset, prefix, sizeof(prefix) - 1);
        offset += sizeof(prefix) - 1;
        memset(env_content + offset, 'x', VALUE_BYTES);
        offset += VALUE_BYTES;
        memcpy(env_content + offset, suffix, sizeof(suffix));

        FILE *f = fopen(".env.test.simd", "w");
        check_not_null(f);
        fputs(env_content, f);
        fclose(f);

        check_equal(dotenv_load(".env.test.simd", true), 0);
        check_equal(strlen(getenv("SIMD_VALUE")), VALUE_BYTES);
        check_equal(getenv("SIMD_VALUE"), env_content + sizeof(prefix) - 1, VALUE_BYTES);

        remove(".env.test.simd");
        unsetenv("SIMD_VALUE");
        free(env_content);
    }
}
