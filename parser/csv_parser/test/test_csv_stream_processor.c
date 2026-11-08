/**
 * @file test_csv_stream_processor.c
 * @brief Tests for csv_stream_processor — streaming CSV parse + filter + vector accumulation
 */

#include "csv_stream_processor.h"
#include "tinytest.h"
#include <fmt.h>
#include <string.h>
#include <stdio.h>

spec("csv_stream_processor") {

    describe("Basic streaming") {

        it("should parse a complete CSV fed in one chunk") {
            const char *csv = "price_n,volume_n,symbol_s\n"
                              "100.5,1000,AAPL\n"
                              "200.3,2000,GOOG\n"
                              "50.1,500,MSFT\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_error(p), "");
            check_equal(csv_stream_processor_row_count(p), 3);
            check_equal(csv_stream_processor_col_count(p), 3);

            size_t len = 0;
            const double *prices = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 3);
            check_within(prices[0], 100.5, 0.01);
            check_within(prices[1], 200.3, 0.01);
            check_within(prices[2], 50.1, 0.01);

            const double *volumes = csv_stream_processor_col_data(p, 1, &len);
            check_equal(len, 3);
            check_within(volumes[0], 1000.0, 0.01);
            check_within(volumes[1], 2000.0, 0.01);
            check_within(volumes[2], 500.0, 0.01);

            /* String access */
            check_equal(csv_stream_processor_get_str(p, 0, 2), "AAPL");
            check_equal(csv_stream_processor_get_str(p, 1, 2), "GOOG");
            check_equal(csv_stream_processor_get_str(p, 2, 2), "MSFT");
            check_null(csv_stream_processor_col_data(p, 2, &len));
            check_equal(len, 0);

            csv_stream_processor_destroy(p);
        }

        it("should handle data fed in small chunks") {
            const char *csv = "x_n,y_n\n10,20\n30,40\n50,60\n";
            size_t total = strlen(csv);

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            /* Feed 7 bytes at a time */
            for (size_t i = 0; i < total; i += 7) {
                size_t chunk = 7;
                if (i + chunk > total) chunk = total - i;
                csv_stream_processor_feed(csv + i, chunk, p);
            }
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 3);

            size_t len = 0;
            const double *x = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 3);
            check_within(x[0], 10.0, 0.01);
            check_within(x[1], 30.0, 0.01);
            check_within(x[2], 50.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should handle data without trailing newline") {
            const char *csv = "a_n,b_n\n1,2\n3,4";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);

            size_t len = 0;
            const double *a = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 2);
            check_within(a[0], 1.0, 0.01);
            check_within(a[1], 3.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should handle CRLF line endings") {
            const char *csv = "val_n\r\n100\r\n200\r\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);

            size_t len = 0;
            const double *v = csv_stream_processor_col_data(p, 0, &len);
            check_within(v[0], 100.0, 0.01);
            check_within(v[1], 200.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should honor custom delimiter and quote options") {
            const char *csv = "name_s;value_n\n"
                              "'hello; world';42\n";
            csv_options_t opts = CSV_OPTIONS_DEFAULT;
            opts.delimiter = ';';
            opts.quote = '\'';

            csv_stream_processor_t *p = csv_stream_processor_create(&opts);
            check_not_null(p);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_error(p), "");
            check_equal(csv_stream_processor_row_count(p), 1);
            check_equal(csv_stream_processor_get_str(p, 0, 0), "hello; world");

            size_t len = 0;
            const double *v = csv_stream_processor_col_data(p, 1, &len);
            check_equal(len, 1);
            check_within(v[0], 42.0, 0.01);

            csv_stream_processor_destroy(p);
        }
    }

    describe("Column lookup") {

        it("should find columns by stripped name") {
            const char *csv = "price_n,name_s,volume_n\n1,X,2\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_col_index(p, "price"), 0);
            check_equal(csv_stream_processor_col_index(p, "name"), 1);
            check_equal(csv_stream_processor_col_index(p, "volume"), 2);

            /* Also match raw name */
            check_equal(csv_stream_processor_col_index(p, "price_n"), 0);
            check_equal(csv_stream_processor_col_index(p, "name_s"), 1);

            /* Not found */
            check_equal(csv_stream_processor_col_index(p, "nonexistent"), (size_t)-1);

            csv_stream_processor_destroy(p);
        }

        it("should return column names") {
            const char *csv = "alpha_n,beta_s\n1,x\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_col_name(p, 0), "alpha_n");
            check_equal(csv_stream_processor_col_name(p, 1), "beta_s");
            check_null(csv_stream_processor_col_name(p, 99));

            csv_stream_processor_destroy(p);
        }
    }

    describe("Filtering") {

        it("should filter rows with numeric expression") {
            const char *csv = "price_n,volume_n\n"
                              "50,100\n"
                              "150,200\n"
                              "80,300\n"
                              "200,400\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_filter(p, "price > 100");
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);

            size_t len = 0;
            const double *prices = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 2);
            check_within(prices[0], 150.0, 0.01);
            check_within(prices[1], 200.0, 0.01);

            const double *volumes = csv_stream_processor_col_data(p, 1, &len);
            check_within(volumes[0], 200.0, 0.01);
            check_within(volumes[1], 400.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should filter with compound expression") {
            const char *csv = "price_n,volume_n\n"
                              "150,50\n"
                              "150,200\n"
                              "50,500\n"
                              "200,300\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_filter(p, "price > 100 and volume > 100");
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);

            size_t len = 0;
            const double *prices = csv_stream_processor_col_data(p, 0, &len);
            check_within(prices[0], 150.0, 0.01);
            check_within(prices[1], 200.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should pass all rows when no filter is set") {
            const char *csv = "x_n\n1\n2\n3\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 3);
            csv_stream_processor_destroy(p);
        }

        it("should filter with string comparison") {
            const char *csv = "price_n,sym_s\n"
                              "100,AAPL\n"
                              "200,GOOG\n"
                              "150,AAPL\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_filter(p, "sym == \"AAPL\"");
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);

            size_t len = 0;
            const double *prices = csv_stream_processor_col_data(p, 0, &len);
            check_within(prices[0], 100.0, 0.01);
            check_within(prices[1], 150.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should not crash on empty filter expression") {
            const char *csv = "x_n\n1\n2\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            check(csv_stream_processor_set_filter(p, ""));
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check(csv_stream_processor_error(p)[0] != '\0');
            check_equal(csv_stream_processor_row_count(p), 2);

            csv_stream_processor_destroy(p);
        }

        it("should not crash on unknown filter column") {
            const char *csv = "x_n\n1\n2\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            check(csv_stream_processor_set_filter(p, "missing > 1"));
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check(csv_stream_processor_error(p)[0] != '\0');
            check_equal(csv_stream_processor_row_count(p), 2);

            csv_stream_processor_destroy(p);
        }

        it("should not crash on invalid filter operator") {
            const char *csv = "x_n\n1\n2\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            check(csv_stream_processor_set_filter(p, "x <> 1"));
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check(csv_stream_processor_error(p)[0] != '\0');
            check_equal(csv_stream_processor_row_count(p), 2);

            csv_stream_processor_destroy(p);
        }

        it("should not crash on unterminated string literal in filter") {
            const char *csv = "sym_s\nAAPL\nGOOG\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            check(csv_stream_processor_set_filter(p, "sym == \"AAPL"));
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check(csv_stream_processor_error(p)[0] != '\0');
            check_equal(csv_stream_processor_row_count(p), 2);

            csv_stream_processor_destroy(p);
        }

        it("should not crash on dangling logical operator") {
            const char *csv = "x_n\n1\n2\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);

            check(csv_stream_processor_set_filter(p, "x > 1 and"));
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check(csv_stream_processor_error(p)[0] != '\0');
            check_equal(csv_stream_processor_row_count(p), 2);

            csv_stream_processor_destroy(p);
        }
    }

    describe("Quoted fields") {

        it("should handle quoted fields with commas") {
            const char *csv = "name_s,value_n\n"
                              "\"hello, world\",42\n"
                              "simple,99\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 2);
            check_equal(csv_stream_processor_get_str(p, 0, 0), "hello, world");
            check_equal(csv_stream_processor_get_str(p, 1, 0), "simple");

            csv_stream_processor_destroy(p);
        }

        it("should handle escaped quotes") {
            const char *csv = "msg_s,val_n\n"
                              "\"say \"\"hi\"\"\",1\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 1);
            check_equal(csv_stream_processor_get_str(p, 0, 0), "say \"hi\"");

            csv_stream_processor_destroy(p);
        }
    }

    describe("Edge cases") {

        it("should handle empty input gracefully") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 0);
            check_equal(csv_stream_processor_col_count(p), 0);

            csv_stream_processor_destroy(p);
        }

        it("should handle header-only input") {
            const char *csv = "a_n,b_n\n";

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_feed(csv, strlen(csv), p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 0);
            check_equal(csv_stream_processor_col_count(p), 2);

            csv_stream_processor_destroy(p);
        }

        it("should handle a long header field without overflowing") {
            enum { NAME_BYTES = 700 };
            char csv[NAME_BYTES + 16];
            size_t csv_len = NAME_BYTES + sizeof("_n\n42\n") - 1;
            memset(csv, 'x', NAME_BYTES);
            memcpy(csv + NAME_BYTES, "_n\n42\n", sizeof("_n\n42\n"));

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            check_not_null(p);
            csv_stream_processor_feed(csv, csv_len, p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_error(p), "");
            check_equal(csv_stream_processor_row_count(p), 1);
            check_equal(csv_stream_processor_col_count(p), 1);

            size_t len = 0;
            const double *v = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 1);
            check_within(v[0], 42.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should handle single byte feeds") {
            const char *csv = "v_n\n42\n";
            size_t total = strlen(csv);

            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            for (size_t i = 0; i < total; i++) {
                csv_stream_processor_feed(csv + i, 1, p);
            }
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 1);
            size_t len = 0;
            const double *v = csv_stream_processor_col_data(p, 0, &len);
            check_within(v[0], 42.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should handle large number of rows") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);

            const char *header = "x_n,y_n\n";
            csv_stream_processor_feed(header, strlen(header), p);

            char row[64];
            for (int i = 0; i < 10000; i++) {
                int n = fmt(row, sizeof(row), "{},{}\n", i, i * 2);
                csv_stream_processor_feed(row, (size_t)n, p);
            }
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 10000);

            size_t len = 0;
            const double *x = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 10000);
            check_within(x[0], 0.0, 0.01);
            check_within(x[9999], 9999.0, 0.01);

            const double *y = csv_stream_processor_col_data(p, 1, &len);
            check_within(y[9999], 19998.0, 0.01);

            csv_stream_processor_destroy(p);
        }

        it("should handle large rows with filter") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_filter(p, "x > 5000");

            const char *header = "x_n\n";
            csv_stream_processor_feed(header, strlen(header), p);

            char row[64];
            for (int i = 0; i < 10000; i++) {
                int n = fmt(row, sizeof(row), "{}\n", i);
                csv_stream_processor_feed(row, (size_t)n, p);
            }
            csv_stream_processor_finish(p);

            /* 5001..9999 = 4999 rows */
            check_equal(csv_stream_processor_row_count(p), 4999);

            size_t len = 0;
            const double *x = csv_stream_processor_col_data(p, 0, &len);
            check_within(x[0], 5001.0, 0.01);
            check_within(x[4998], 9999.0, 0.01);

            csv_stream_processor_destroy(p);
        }
    }

    describe("Large file simulation") {

        it("should stream 1M rows with 6 columns selecting only close and volume") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_columns(p, "close,volume");

            const char *header = "timestamp_n,open_n,high_n,low_n,close_n,volume_n\n";
            csv_stream_processor_feed(header, strlen(header), p);

            /* Feed in ~4KB chunks to simulate HTTP streaming */
            char buf[4096];
            size_t buf_len = 0;
            int total_rows = 1000000;

            for (int i = 0; i < total_rows; i++) {
                double base = 100.0 + (double)(i % 1000) * 0.01;
                int n = fmt(buf + buf_len, sizeof(buf) - buf_len,
                            "{},{:.2f},{:.2f},{:.2f},{:.2f},{}\n",
                            1700000000 + i,
                            base, base + 0.5, base - 0.3, base + 0.1,
                            1000 + (i % 5000));

                buf_len += (size_t)n;

                if (buf_len > sizeof(buf) - 128) {
                    csv_stream_processor_feed(buf, buf_len, p);
                    buf_len = 0;
                }
            }
            if (buf_len > 0) csv_stream_processor_feed(buf, buf_len, p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), (size_t)total_rows);
            check_equal(csv_stream_processor_col_count(p), 6);

            /* Selected columns have data */
            size_t len = 0;
            const double *close_col = csv_stream_processor_col_data(p, 4, &len);
            check_equal(len, (size_t)total_rows);
            check_within(close_col[0], 100.1, 0.01);
            check_within(close_col[999999], 110.09, 0.01);

            const double *vol = csv_stream_processor_col_data(p, 5, &len);
            check_equal(len, (size_t)total_rows);
            check_within(vol[0], 1000.0, 0.01);

            /* Unselected columns have no data */
            const double *ts = csv_stream_processor_col_data(p, 0, &len);
            check_equal(len, 0);
            (void)ts;

            csv_stream_processor_destroy(p);
        }

        it("should stream 1M rows with filter (keep ~10%)") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);
            csv_stream_processor_set_filter(p, "volume > 5500");

            const char *header = "price_n,volume_n\n";
            csv_stream_processor_feed(header, strlen(header), p);

            char buf[4096];
            size_t buf_len = 0;
            int total_rows = 1000000;
            int expected_match = 0;

            for (int i = 0; i < total_rows; i++) {
                int vol = 1000 + (i % 5000);
                if (vol > 5500) expected_match++;

                int n = fmt(buf + buf_len, sizeof(buf) - buf_len,
                            "{:.2f},{}\n", 100.0 + (double)i * 0.001, vol);
                buf_len += (size_t)n;

                if (buf_len > sizeof(buf) - 64) {
                    csv_stream_processor_feed(buf, buf_len, p);
                    buf_len = 0;
                }
            }
            if (buf_len > 0) csv_stream_processor_feed(buf, buf_len, p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), (size_t)expected_match);

            /* Verify filtered data is correct — first match should have volume > 5500 */
            size_t len = 0;
            const double *volumes = csv_stream_processor_col_data(p, 1, &len);
            check_equal(len, (size_t)expected_match);
            for (size_t i = 0; i < len && i < 100; i++) {
                check(volumes[i] > 5500.0);
            }

            csv_stream_processor_destroy(p);
        }

        it("should stream 1M rows fed byte-by-byte for first 1000 then chunked") {
            csv_stream_processor_t *p = csv_stream_processor_create(NULL);

            const char *header = "val_n\n";
            /* Feed header byte by byte */
            for (size_t i = 0; i < strlen(header); i++) {
                csv_stream_processor_feed(header + i, 1, p);
            }

            char buf[8192];
            size_t buf_len = 0;

            for (int i = 0; i < 1000000; i++) {
                int n = fmt(buf + buf_len, sizeof(buf) - buf_len, "{}\n", i);
                buf_len += (size_t)n;

                if (i < 100) {
                    /* First 100 rows: feed byte by byte to stress line buffer */
                    csv_stream_processor_feed(buf, buf_len, p);
                    buf_len = 0;
                } else if (buf_len > sizeof(buf) - 32) {
                    csv_stream_processor_feed(buf, buf_len, p);
                    buf_len = 0;
                }
            }
            if (buf_len > 0) csv_stream_processor_feed(buf, buf_len, p);
            csv_stream_processor_finish(p);

            check_equal(csv_stream_processor_row_count(p), 1000000);

            size_t len = 0;
            const double *v = csv_stream_processor_col_data(p, 0, &len);
            check_within(v[0], 0.0, 0.01);
            check_within(v[999999], 999999.0, 0.01);

            csv_stream_processor_destroy(p);
        }
    }
}
