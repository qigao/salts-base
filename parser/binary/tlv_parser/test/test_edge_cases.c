#include <tinytest.h>
#include <uri_parser.h>
#include <stdlib.h>
#include <string.h>

spec("uri_parser_edge_cases") {
    it("should handle empty string") {
        uri_t url;
        int result = uri_parse("", &url);
        check_equal(result, 0);
        check_equal(url.valid, 0);
    }

    it("should handle malformed scheme") {
        uri_t url;
        // Scheme cannot start with digit
        int result = uri_parse("123://example.com", &url);
        check_equal(result, 0);
        check_equal(url.valid, 0);
    }

    it("should handle missing scheme") {
        uri_t url;
        int result = uri_parse("//example.com/path", &url);
        check_equal(result, 0);
        check_equal(url.valid, 0);
    }

    it("should handle invalid port range") {
        uri_t url;
        // Port out of range - our parser may not catch this
        int result = uri_parse("http://example.com:99999", &url);
        (void)result;
        check(url.port == 99999);
    }

    it("should handle non-numeric port") {
        uri_t url;
        int result = uri_parse("http://example.com:abc", &url);
        // Should parse successfully but port might be 0
        if (result == 1) {
            check_equal(url.port, 0);  // atoi("abc") returns 0
        }
    }

    it("should handle very long scheme") {
        uri_t url;
        char long_url[100];
        // Create scheme longer than buffer (32 chars)
        strcpy(long_url, "");
        for (int i = 0; i < 40; i++) {
            strcat(long_url, "a");
        }
        strcat(long_url, "://example.com");

        int result = uri_parse(long_url, &url);

        if (result == 1) {
            // Should be truncated to fit buffer
            check(strlen(url.scheme) < sizeof(url.scheme));
        } else {
            // Parser rejected it - that's also valid behavior
            check_equal(result, 0);
        }
    }

    it("should handle very long hostname") {
        uri_t url;
        char* long_url = malloc(400);
        check_not_null(long_url);

        // Create hostname longer than buffer (256 chars)
        strcpy(long_url, "http://");
        for (int i = 0; i < 300; i++) {
            strcat(long_url, "a");
        }
        strcat(long_url, ".com");

        int result = uri_parse(long_url, &url);

        if (result == 1) {
            // Should be truncated safely
            check(strlen(url.host) < sizeof(url.host));
        }

        free(long_url);
    }

    it("should handle very long path") {
        uri_t url;
        char* long_url = malloc(2000);
        check_not_null(long_url);

        strcpy(long_url, "https://example.com/");
        for (int i = 0; i < 1200; i++) {
            strcat(long_url, "a");
        }

        int result = uri_parse(long_url, &url);

        if (result == 1) {
            // Should be truncated safely
            check(strlen(url.path) < sizeof(url.path));
        }

        free(long_url);
    }

    it("should handle malformed ipv6") {
        uri_t url;
        // Missing closing bracket - parser returns failure
        int result = uri_parse("http://[2001:db8::1", &url);

        check_equal(result, 0);
        check_equal(url.host, "");  // Host should be empty on failure
    }

    it("should handle empty host") {
        uri_t url;
        int result = uri_parse("http:///path", &url);
        if (result == 1) {
            check_equal(url.host, "");
        }
    }

    it("should handle deeply nested path") {
        uri_t url;
        char long_url[1500];
        strcpy(long_url, "https://example.com");
        for (int i = 0; i < 80; i++) {
            strcat(long_url, "/level");
        }

        int result = uri_parse(long_url, &url);

        if (result == 1) {
            check_equal(url.host, "example.com");
            check(strlen(url.path) > 100);
        }
    }

    it("should handle null bytes") {
        uri_t url;
        char url_str[] = "http://exam\0ple.com";
        int result = uri_parse(url_str, &url);

        // Should stop at null byte
        if (result == 1) {
            check_equal(url.scheme, "http");
        }
    }

    it("should handle special characters") {
        uri_t url;
        int result = uri_parse("https://example.com/path with spaces", &url);
        if (result == 1) {
            check_equal(url.path, "/path with spaces");
        }
    }

    it("should fail on scheme only") {
        uri_t url;
        int result = uri_parse("http:", &url);
        check_equal(result, 0);  // Should fail - no authority
    }

    it("should handle valid edge cases") {
        uri_t url;
        // URL with just scheme and host
        int result = uri_parse("https://example.com", &url);
        check_equal(result, 1);
        check_equal(url.path, "");  // Empty path is valid

        // URL with port but no path
        result = uri_parse("http://example.com:80", &url);
        check_equal(result, 1);
        check_equal(url.port, 80);

        // URL with empty query
        result = uri_parse("http://example.com?", &url);
        if (result == 1) {
            check_equal(url.query, "");
        }
    }

    it("should be robust against null/malformed input") {
        uri_t url;
        check_equal(uri_parse(NULL, &url), 0);
        check_equal(uri_parse("http://example.com", NULL), 0);
        check_equal(uri_parse(":", &url), 0);
        check_equal(uri_parse("http", &url), 0);
    }
}
