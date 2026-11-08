#include "ini_parser.h"
#include "tinytest.h"
#include <string.h>

spec("ini_parser") {
  describe("Basic Parsing") {
    it("should return NULL for NULL input") {
        ini_t* ini = ini_parse(NULL, 0);
        check_null(ini);
    }

    it("should parse an empty string correctly") {
        ini_t* ini = ini_parse("", 0);
        check_not_null(ini);
        check_equal(ini_section_count(ini), 0);
        ini_free(ini);
    }

    it("should parse a single section with one key correctly") {
        const char* content = "[section1]\nkey1 = value1\n";
        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_null(ini_get(ini, "section1", "key2"));
        check_null(ini_get(ini, "section2", "key1"));

        ini_free(ini);
    }

    it("should parse multiple sections and keys correctly") {
        const char* content =
            "[section1]\n"
            "key1 = value1\n"
            "key2 = value2\n"
            "\n"
            "[section2]\n"
            "key3 = value3\n"
            "key4 = value4\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_equal(ini_get(ini, "section1", "key2"), "value2");
        check_equal(ini_get(ini, "section2", "key3"), "value3");
        check_equal(ini_get(ini, "section2", "key4"), "value4");
        check_null(ini_get(ini, "section1", "key3"));

        ini_free(ini);
    }
  }

  describe("Formatting and Whitespace") {
    it("should ignore comments starting with ; or #") {
        const char* content =
            "; This is a comment\n"
            "[section1]\n"
            "key1 = value1 ; inline comment\n"
            "# Another comment\n"
            "key2 = value2\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_equal(ini_get(ini, "section1", "key2"), "value2");

        ini_free(ini);
    }

    it("should handle leading and trailing whitespace correctly") {
        const char* content =
            "  [section1]  \n"
            "  key1  =  value1  \n"
            "key2=value2\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_equal(ini_get(ini, "section1", "key2"), "value2");

        ini_free(ini);
    }

    it("should handle values containing spaces") {
        const char* content =
            "[section1]\n"
            "key1 = value with spaces\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value with spaces");

        ini_free(ini);
    }

    it("should correctly handle CRLF line endings") {
        const char* content =
            "[section1]\r\n"
            "key1 = value1\r\n"
            "key2 = value2\r\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_equal(ini_get(ini, "section1", "key2"), "value2");

        ini_free(ini);
    }
  }

  describe("Redundancy Handling") {
    it("should use the last value for duplicate keys") {
        const char* content =
            "[section1]\n"
            "key1 = value1\n"
            "key1 = value2\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value2");

        ini_free(ini);
    }

    it("should merge keys from duplicate sections") {
        const char* content =
            "[section1]\n"
            "key1 = value1\n"
            "[section2]\n"
            "key2 = value2\n"
            "[section1]\n"
            "key3 = value3\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "section1", "key1"), "value1");
        check_equal(ini_get(ini, "section1", "key3"), "value3");
        check_equal(ini_get(ini, "section2", "key2"), "value2");

        ini_free(ini);
    }
  }

  describe("Global Scope") {
    it("should handle keys defined before any section correctly") {
        const char* content =
            "global_key = global_value\n"
            "[section1]\n"
            "key1 = value1\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get(ini, "", "global_key"), "global_value");
        check_equal(ini_get(ini, NULL, "global_key"), "global_value");
        check_equal(ini_get(ini, "section1", "key1"), "value1");

        ini_free(ini);
    }
  }

  describe("Type Conversion") {
    it("should retrieve integer values correctly") {
        const char* content =
            "[section1]\n"
            "port = 8080\n"
            "hex = 0xFF\n"
            "invalid = abc\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_get_int(ini, "section1", "port", 0), 8080);
        check_equal(ini_get_int(ini, "section1", "hex", 0), 255);
        check_equal(ini_get_int(ini, "section1", "invalid", 42), 42);
        check_equal(ini_get_int(ini, "section1", "missing", 99), 99);

        ini_free(ini);
    }

    it("should retrieve boolean values correctly") {
        const char* content =
            "[section1]\n"
            "enabled1 = true\n"
            "enabled2 = yes\n"
            "enabled3 = on\n"
            "enabled4 = 1\n"
            "disabled1 = false\n"
            "disabled2 = no\n"
            "disabled3 = off\n"
            "disabled4 = 0\n"
            "invalid = abc\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check(ini_get_bool(ini, "section1", "enabled1", false));
        check(ini_get_bool(ini, "section1", "enabled2", false));
        check(ini_get_bool(ini, "section1", "enabled3", false));
        check(ini_get_bool(ini, "section1", "enabled4", false));
        check(!ini_get_bool(ini, "section1", "disabled1", true));
        check(!ini_get_bool(ini, "section1", "disabled2", true));
        check(!ini_get_bool(ini, "section1", "disabled3", true));
        check(!ini_get_bool(ini, "section1", "disabled4", true));
        check(ini_get_bool(ini, "section1", "invalid", true));
        check(!ini_get_bool(ini, "section1", "missing", false));

        ini_free(ini);
    }

    it("should retrieve double precision values correctly") {
        const char* content =
            "[section1]\n"
            "pi = 3.14159\n"
            "negative = -1.5\n"
            "invalid = abc\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_within(ini_get_double(ini, "section1", "pi", 0.0), 3.14159, 0.0001);
        check_within(ini_get_double(ini, "section1", "negative", 0.0), -1.5, 0.0001);
        check_within(ini_get_double(ini, "section1", "invalid", 1.0), 1.0, 0.0001);

        ini_free(ini);
    }
  }

  describe("Iteration Support") {
    it("should report the correct section count") {
        const char* content =
            "[section1]\n"
            "key1 = value1\n"
            "[section2]\n"
            "key2 = value2\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_section_count(ini), 2);

        ini_free(ini);
    }

    it("should allow iterating over keys in a section") {
        const char* content =
            "[section1]\n"
            "key1 = value1\n"
            "key2 = value2\n"
            "key3 = value3\n";

        ini_t* ini = ini_parse(content, strlen(content));

        check_not_null(ini);
        check_equal(ini_key_count(ini, "section1"), 3);
        check_not_null(ini_key_name(ini, "section1", 0));
        check_not_null(ini_key_name(ini, "section1", 1));
        check_not_null(ini_key_name(ini, "section1", 2));
        check_null(ini_key_name(ini, "section1", 3));

      ini_free(ini);
    }
  }

  it("should preserve a long value while skipping whitespace and comments") {
    enum { VALUE_BYTES = 4096 };
    const char prefix[] = "  # ignored comment\n[simd]\nvalue = ";
    const char suffix[] = "   ; ignored trailing comment\n";
    char* content = (char*)malloc(sizeof(prefix) + VALUE_BYTES + sizeof(suffix));
    size_t offset = 0;

    check_not_null(content);
    memcpy(content + offset, prefix, sizeof(prefix) - 1);
    offset += sizeof(prefix) - 1;
    memset(content + offset, 'x', VALUE_BYTES);
    offset += VALUE_BYTES;
    memcpy(content + offset, suffix, sizeof(suffix));

    ini_t* ini = ini_parse(content, offset + sizeof(suffix) - 1);
    check_not_null(ini);
    check_equal(strlen(ini_get(ini, "simd", "value")), VALUE_BYTES);
    check_equal(ini_get(ini, "simd", "value"), content + sizeof(prefix) - 1, VALUE_BYTES);

    ini_free(ini);
    free(content);
  }
}
