#include "dsv_filter.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    size_t count;
    size_t last_row;
    char last_row_text[256];
} run_ctx_t;

typedef struct {
    size_t count;
    int64_t sum;
} scan_ctx_t;

static int on_scan_match(void *user_data, size_t row_index,
                         const csv_scan_value_t *values, size_t value_count) {
    scan_ctx_t *ctx = (scan_ctx_t *)user_data;
    (void)row_index;
    if (!ctx || !values || value_count != 1 || values[0].type != CSV_SCAN_VALUE_INT64)
        return -1;
    ctx->count++;
    ctx->sum += values[0].value.integer;
    return 0;
}

static void on_row(void *user_data, size_t row_index, const char *rendered_row) {
    run_ctx_t *ctx = (run_ctx_t *)user_data;
    ctx->count++;
    ctx->last_row = row_index;
    if (rendered_row) {
        size_t n = strlen(rendered_row);
        if (n >= sizeof(ctx->last_row_text)) n = sizeof(ctx->last_row_text) - 1;
        memcpy(ctx->last_row_text, rendered_row, n);
        ctx->last_row_text[n] = '\0';
    } else {
        ctx->last_row_text[0] = '\0';
    }
}

spec("dsv_filter") {
    describe("Compile and Evaluate") {
        it("should filter numeric expression") {
            const char *csv = "price_n,volume_n,sym_s\n"
                              "50,100,AAPL\n"
                              "150,200,GOOG\n"
                              "200,50,MSFT\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "price > 100 and volume >= 100"));

            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should filter string expression") {
            const char *csv = "price_n,sym_s\n"
                              "100,AAPL\n"
                              "200,GOOG\n"
                              "150,AAPL\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "sym == \"AAPL\""));

            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 1);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should evaluate compiled filters on field views") {
            const char *csv = "price_n,volume_n,sym_s\n";
            vstr row1[] = {
                vstr_from_buf("50", 2),
                vstr_from_buf("100", 3),
                vstr_from_buf("AAPL", 4),
            };
            vstr row2[] = {
                vstr_from_buf("150", 3),
                vstr_from_buf("200", 3),
                vstr_from_buf("GOOG", 4),
            };
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "price > 100 and sym == \"GOOG\""));

            check_equal(dsv_filter_check_values(f, row1, 3), 0);
            check_equal(dsv_filter_check_values(f, row2, 3), 1);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should scan raw CSV through a compiled simple filter") {
            const char *header = "age_n,country_s,score_n\n";
            const char *csv = "age_n,country_s,score_n\n"
                              "21,CN,91\n"
                              "22,US,99\n"
                              "23,CN,90\n"
                              "24,CN,92\n";
            const csv_scan_projection_t projection = {
                .column = 0,
                .type = CSV_SCAN_VALUE_INT64,
            };
            csv_options_t opts = CSV_OPTIONS_DEFAULT;
            scan_ctx_t ctx = {0};
            size_t matches = 0;
            csv_doc_t *doc = csv_parse(header, strlen(header));
            dsv_filter_t *filter;

            check_not_null(doc);
            filter = dsv_filter_create(doc, 0);
            check_not_null(filter);
            check(dsv_filter_compile(filter, "score > 90 and country == \"CN\""));
            opts.has_header = true;
            check_equal(dsv_filter_scan(filter, csv, strlen(csv), &opts, &projection, 1,
                                         on_scan_match, &ctx, &matches), 0);
            check_equal(matches, 2);
            check_equal(ctx.count, 2);
            check_equal(ctx.sum, 45);

            dsv_filter_destroy(filter);
            csv_free(doc);
        }

        it("should filter plain header columns as dynamic values") {
            const char *csv = "id,side,symbol\n"
                              "10,Buy,ABCD\n"
                              "11,Sell,WXYZ\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "id > 10 and side == \"Sell\""));

            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should run callback for matched rows") {
            const char *csv = "price_n,volume_n,sym_s\n"
                              "50,100,AAPL\n"
                              "150,200,GOOG\n"
                              "250,300,MSFT\n";
            run_ctx_t ctx = {0};

            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            dsv_filter_set_output_delimiter(f, '|');
            check(dsv_filter_compile(f, "price >= 150"));

            dsv_filter_run(f, on_row, &ctx);

            check_equal(ctx.count, 2);
            check_equal(ctx.last_row, 3);
            check_equal(ctx.last_row_text, "250|300|MSFT");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should render matched rows with csv escaping") {
            const char *csv = "id_n,text_s\n"
                              "1,\"hello|world\"\n"
                              "2,\"say \"\"hi\"\"\"\n";
            run_ctx_t ctx = {0};

            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            dsv_filter_set_output_delimiter(f, '|');
            check(dsv_filter_compile(f, "id >= 2"));

            dsv_filter_run(f, on_row, &ctx);

            check_equal(ctx.count, 1);
            check_equal(ctx.last_row_text, "2|\"say \"\"hi\"\"\"");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should filter with lhs arithmetic using two numeric columns") {
            const char *csv = "left_n,right_n,sym_s\n"
                              "2,3,A\n"
                              "4,6,B\n"
                              "7,1,C\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "left + right >= 10"));

            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should filter with lhs arithmetic using numeric literal") {
            const char *csv = "value_n,sym_s\n"
                              "5,A\n"
                              "3,B\n"
                              "7,C\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "value - 2 == 3"));

            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should filter with chained lhs arithmetic terms") {
            const char *csv = "a_n,b_n,c_n\n"
                              "1,2,3\n"
                              "4,1,0\n"
                              "5,2,1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "a + b - 1 + c == 5"));

            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should support unary sign before arithmetic column term") {
            const char *csv = "a_n,b_n\n"
                              "5,3\n"
                              "7,5\n"
                              "2,5\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "a + -b == 2"));
            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            check(dsv_filter_compile(f, "a - -b == 12"));
            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should support multiplication and division precedence") {
            const char *csv = "a_n,b_n,c_n\n"
                              "2,3,4\n"
                              "6,2,4\n"
                              "9,3,1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(dsv_filter_compile(f, "a + b * c == 14"));
            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            check(dsv_filter_compile(f, "a / b + c == 7"));
            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should support unary sign with multiplication term") {
            const char *csv = "a_n,b_n\n"
                              "2,3\n"
                              "3,3\n"
                              "2,2\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(dsv_filter_compile(f, "a * -b == -6"));
            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should support parentheses precedence in lhs arithmetic") {
            const char *csv = "a_n,b_n,c_n\n"
                              "2,3,4\n"
                              "4,1,2\n"
                              "3,2,1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(dsv_filter_compile(f, "a * (b + c) == 14"));
            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);

            check(dsv_filter_compile(f, "a * (b + (c - 1)) == 8"));
            check_equal(dsv_filter_check_row(f, 1), 0);
            check_equal(dsv_filter_check_row(f, 2), 1);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should reuse a dynamic column as number and as string") {
            const char *csv = "code,flag_n\n"
                              "3,1\n"
                              "2,1\n"
                              "1,1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "code > 1 and code == \"3\""));

            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should evaluate a long AND chain through the VM") {
            const char *csv = "a_n,b_n\n"
                              "2,3\n"
                              "5,3\n"
                              "1,3\n"
                              "2,0\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f,
                "a > 0 and b > 0 and a < 10 and b < 10 and "
                "a >= 2 and b >= 2 and a != 5 and b != 5"));

            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);
            check_equal(dsv_filter_check_row(f, 3), 0);
            check_equal(dsv_filter_check_row(f, 4), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fall back to the native evaluator when VM registers overflow") {
            const char *csv = "a_n\n"
                              "1\n"
                              "2\n";
            char expr[2048];
            size_t off = 0;
            int i;
            csv_doc_t *doc;
            dsv_filter_t *f;

            expr[off++] = 'a';
            for (i = 0; i < 69; i++) {
                int n = snprintf(expr + off, sizeof(expr) - off, "+1");
                check(n > 0 && (size_t)n < sizeof(expr) - off);
                off += (size_t)n;
            }
            {
                int n = snprintf(expr + off, sizeof(expr) - off, "==70");
                check(n > 0 && (size_t)n < sizeof(expr) - off);
                off += (size_t)n;
            }

            doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);
            f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, expr));
            check_equal(dsv_filter_check_row(f, 1), 1);
            check_equal(dsv_filter_check_row(f, 2), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should evaluate lhs arithmetic on field views") {
            const char *csv = "left_n,right_n,sym_s\n";
            vstr row1[] = {
                vstr_from_buf("5", 1),
                vstr_from_buf("3", 1),
                vstr_from_buf("A", 1),
            };
            vstr row2[] = {
                vstr_from_buf("4", 1),
                vstr_from_buf("2", 1),
                vstr_from_buf("B", 1),
            };
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);
            check(dsv_filter_compile(f, "left + right == 8"));

            check_equal(dsv_filter_check_values(f, row1, 3), 1);
            check_equal(dsv_filter_check_values(f, row2, 3), 0);

            dsv_filter_destroy(f);
            csv_free(doc);
        }
    }

    describe("Error Handling") {

        it("should reject OR expressions in direct scan mode") {
            const char *header = "score_n,country_s\n";
            const char *csv = "91,CN\n99,US\n";
            size_t matches = 0;
            csv_doc_t *doc = csv_parse(header, strlen(header));
            dsv_filter_t *filter;

            check_not_null(doc);
            filter = dsv_filter_create(doc, 0);
            check_not_null(filter);
            check(dsv_filter_compile(filter, "score > 90 or country == \"CN\""));
            check_equal(dsv_filter_scan(filter, csv, strlen(csv), NULL, NULL, 0,
                                         NULL, NULL, &matches), -1);
            check_contains(dsv_filter_error(filter), "simple predicates");
            check_equal(matches, 0);

            dsv_filter_destroy(filter);
            csv_free(doc);
        }

        it("should return -1 when row check is called before compile") {
            const char *csv = "x_n\n1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check_equal(dsv_filter_check_row(f, 1), -1);

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should handle null and invalid handles safely") {
            const char *csv = "x_n\n1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            check_equal(dsv_filter_error(NULL), "");
            check_null(dsv_filter_create(doc, 99));

            csv_free(doc);
        }

        it("should fail compile on unknown column") {
            const char *csv = "x_n\n1\n2\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "missing > 1"));
            check(dsv_filter_error(f)[0] != '\0');

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail compile on empty expression") {
            const char *csv = "x_n\n1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, ""));
            check(dsv_filter_error(f)[0] != '\0');

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when arithmetic is used on string column") {
            const char *csv = "sym_s,val_n\nA,1\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "sym + 1 == 2"));
            check_equal(dsv_filter_error(f), "invalid filter: arithmetic on string column");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when arithmetic rhs term is a string column") {
            const char *csv = "price_n,sym_s\n10,A\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price + sym > 10"));
            check_equal(dsv_filter_error(f), "invalid filter: arithmetic requires numeric columns");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when arithmetic compares with string value") {
            const char *csv = "price_n,sym_s\n10,A\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price + 1 == \"11\""));
            check_equal(dsv_filter_error(f), "invalid filter: arithmetic cannot compare to string");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when arithmetic rhs term is missing") {
            const char *csv = "price_n\n10\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price + == 10"));
            check_equal(dsv_filter_error(f), "invalid filter: expected column or number after +/-");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when a later arithmetic rhs term is missing") {
            const char *csv = "price_n\n10\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price + 1 - == 10"));
            check_equal(dsv_filter_error(f), "invalid filter: expected column or number after +/-");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when unary sign has no arithmetic term") {
            const char *csv = "price_n\n10\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price + - == 10"));
            check_equal(dsv_filter_error(f), "invalid filter: expected column or number after +/-");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when multiplication rhs term is missing") {
            const char *csv = "price_n\n10\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "price * == 10"));
            check_equal(dsv_filter_error(f), "invalid filter: expected column or number after +/-");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when parentheses are unbalanced in lhs arithmetic") {
            const char *csv = "a_n,b_n\n1,2\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "a * (b + 1 == 3"));
            check_equal(dsv_filter_error(f), "invalid filter: unbalanced parentheses in arithmetic expression");

            check(!dsv_filter_compile(f, "a * () == 0"));
            check_equal(dsv_filter_error(f), "invalid filter: empty parentheses in arithmetic expression");

            dsv_filter_destroy(f);
            csv_free(doc);
        }

        it("should fail when arithmetic expression has invalid character") {
            const char *csv = "a_n,b_n\n1,2\n";
            csv_doc_t *doc = csv_parse(csv, strlen(csv));
            check_not_null(doc);

            dsv_filter_t *f = dsv_filter_create(doc, 0);
            check_not_null(f);

            check(!dsv_filter_compile(f, "a * (b + @) == 0"));
            check_equal(dsv_filter_error(f), "invalid filter: invalid character in arithmetic expression");

            dsv_filter_destroy(f);
            csv_free(doc);
        }
    }
}
