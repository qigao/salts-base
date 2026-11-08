#include <tinytest.hpp>
#include "datetime_parser.h"
#include <time.h>
#include <string.h>

#define check_parse(str, dt) \
    do { \
        int _res = datetime_parse(str, strlen(str), dt); \
        if (_res != 0) printf("  Parse failed for: [%s]\n", str); \
        check_equal(_res, 0); \
    } while(0)

suite("datetime_parser") {
    it("should parse RFC-822 / HTTP date-time (Format 47)") {
        const char *date_str = "Sat, 04 Mar 2006 13:27:54 GMT";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 4);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.has_tz, 1);
        check_equal(dt.tz_offset, 0);
    }

    it("should parse RFC-822 with offset (Format 47)") {
        const char *date_str = "Sat, 04 Mar 2006 13:27:54 -0234";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.tz_offset, -154); // -(2*60 + 34)
    }

    it("should parse ISO-8601 variations (Format 35, 44)") {
        const char *date_str = "2006-03-14T13:27:54";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
        check_equal(dt.hour, 13);

        const char *date_tz = "2006-03-04T13:27:54+03:45";
        check_parse(date_tz, &dt);
        check_equal(dt.tz_offset, 225); // 3*60 + 45
    }

    it("should parse compact dates (Format 00)") {
        const char *date_str = "20060314";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
    }

    it("should parse dotted dates (Format 10)") {
        const char *date_str = "14.03.2006";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
    }

    it("should parse NCSA Common Log format (Format 46)") {
        const char *date_str = "04/Mar/2006:13:27:54 -0537";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 4);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.tz_offset, -337); // -(5*60 + 37)
    }

    it("should parse time with milliseconds (Format 16)") {
        const char *time_str = "13:27:54.123";
        datetime_t dt;
        check_parse(time_str, &dt);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.millisecond, 123);
    }

    it("should parse date with hyphens and short months (Format 12-15)") {
        const char *date_str = "14-Mar-2006";
        datetime_t dt;
        check_parse(date_str, &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
    }

    it("should parse various date formats (Formats 02, 04, 06, 08)") {
        datetime_t dt;
        check_parse("2006/03/14", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);

        check_parse("14/03/2006", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);

        check_parse("2006-03-14", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);

        check_parse("14-03-2006", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
    }

    it("should parse various time formats (Formats 18, 20, 22, 23)") {
        datetime_t dt;
        check_parse("13 27 54 123", &dt);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.millisecond, 123);

        check_parse("13.27.54.123", &dt);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.millisecond, 123);

        check_parse("1327", &dt);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);

        check_parse("132754", &dt);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
    }

    it("should parse combined datetime and ISO8601 variations (Formats 25, 45)") {
        datetime_t dt;
        check_parse("20060314 13:27:54.123", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 14);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.second, 54);
        check_equal(dt.millisecond, 123);

        check_parse("2006-03-04T13:27+03:45", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 4);
        check_equal(dt.hour, 13);
        check_equal(dt.minute, 27);
        check_equal(dt.tz_offset, 225);
    }

    it("should parse short years in RFC-822 (Format 12-13)") {
        datetime_t dt;
        check_parse("07-Mar-06", &dt);
        check_equal(dt.year, 2006);
        check_equal(dt.month, 3);
        check_equal(dt.day, 7);

        check_parse("07-Mar-75", &dt);
        check_equal(dt.year, 1975);
    }
}
