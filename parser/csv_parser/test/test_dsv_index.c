#include "dsv_filter.h"
#include "dsv_index.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

static const char DSV_QUERY_SOURCE[] =
    "age_n,country_s,score_n\n"
    "21,CN,91\n"
    "22,US,99\n"
    "23,CN,90\n"
    "24,CN,92\n";

typedef struct {
    char *index_path;
    dsv_index_t *index;
    csv_doc_t *header;
    dsv_filter_t *filter;
} dsv_query_fixture_t;

static int dsv_query_fixture_init(dsv_query_fixture_t *fixture) {
    dsv_index_config_t config = {
        .text_column = 1,
        .number_column = 2,
        .covering_int64_column = 0,
        .has_header = true,
    };
    memset(fixture, 0, sizeof(*fixture));
    fixture->index_path = tt_make_temp_file("dsv-index-query", ".idx");
    fixture->index = dsv_index_create();
    fixture->header = csv_parse("age_n,country_s,score_n\n",
                                strlen("age_n,country_s,score_n\n"));
    if (!fixture->index_path || !fixture->index || !fixture->header) return -1;
    fixture->filter = dsv_filter_create(fixture->header, 0);
    if (!fixture->filter) return -1;
    if (dsv_index_build_memory(fixture->index, fixture->index_path,
                               DSV_QUERY_SOURCE, strlen(DSV_QUERY_SOURCE), &config) != 0)
        return -1;
    return dsv_index_open_memory(fixture->index, fixture->index_path,
                                 DSV_QUERY_SOURCE, strlen(DSV_QUERY_SOURCE));
}

static void dsv_query_fixture_destroy(dsv_query_fixture_t *fixture) {
    dsv_filter_destroy(fixture->filter);
    csv_free(fixture->header);
    dsv_index_destroy(fixture->index);
    if (fixture->index_path) {
        (void)tt_remove_file(fixture->index_path);
        free(fixture->index_path);
    }
}

static int dsv_query_execute(dsv_query_fixture_t *fixture, const char *expression,
                             size_t *count, int64_t *sum) {
    dsv_index_cursor_t cursor;
    dsv_index_row_t row;
    int rc;
    *count = 0;
    *sum = 0;
    if (!dsv_filter_compile(fixture->filter, expression) ||
        dsv_filter_index_seek(fixture->filter, fixture->index, &cursor) != 0)
        return -1;
    while ((rc = dsv_index_cursor_next(fixture->index, &cursor, &row)) > 0) {
        if (!row.has_covering_int64) return -1;
        ++*count;
        *sum += row.covering_int64;
    }
    return rc;
}

spec("dsv_index") {
    it("should build and seek a static composite file index") {
        const char *csv = "age_n,country_s,score_n\n"
                          "21,CN,91\n"
                          "22,US,99\n"
                          "23,CN,90\n"
                          "24,CN,92\n";
        dsv_index_config_t config = {
            .text_column = 1,
            .number_column = 2,
            .covering_int64_column = 0,
            .has_header = true,
        };
        char *csv_path = tt_make_temp_file("dsv-index-source", ".csv");
        char *index_path = tt_make_temp_file("dsv-index-sidecar", ".idx");
        dsv_index_t *index = dsv_index_create();
        csv_doc_t *header = csv_parse("age_n,country_s,score_n\n",
                                      strlen("age_n,country_s,score_n\n"));
        dsv_filter_t *filter;
        dsv_index_cursor_t cursor;
        dsv_index_row_t row;
        size_t count = 0;
        int64_t sum = 0;
        vstr last_row = vstr_from_buf(NULL, 0);
        int rc;

        check_not_null(csv_path);
        check_not_null(index_path);
        check_not_null(index);
        check_not_null(header);
        check_equal(tt_write_file(csv_path, csv, strlen(csv)), 0);
        check_equal(dsv_index_build_file(index, csv_path, index_path, &config), 0);
        check_equal(dsv_index_open_file(index, csv_path, index_path), 0);
        check_equal(dsv_index_count(index), 4);
        check_equal(dsv_index_text_column(index), 1);
        check_equal(dsv_index_number_column(index), 2);
        check_equal(dsv_index_covering_column(index), 0);

        filter = dsv_filter_create(header, 0);
        check_not_null(filter);
        check(dsv_filter_compile(filter, "score > 90 and country == \"CN\""));
        check_equal(dsv_filter_index_seek(filter, index, &cursor), 0);
        while ((rc = dsv_index_cursor_next(index, &cursor, &row)) > 0) {
            check(row.has_covering_int64);
            sum += row.covering_int64;
            last_row = dsv_index_row_view(index, &row);
            ++count;
        }
        check_equal(rc, 0);
        check_equal(count, 2);
        check_equal(sum, 45);
        check_equal(vstr_eq(last_row, vstr_from_cstr("24,CN,92\n")), 1);

        dsv_filter_destroy(filter);
        csv_free(header);
        dsv_index_destroy(index);
        check_equal(tt_remove_file(index_path), 0);
        check_equal(tt_remove_file(csv_path), 0);
        free(index_path);
        free(csv_path);
    }

    it("should reject a sidecar built for different source bytes") {
        const char *source = "age,country,score\n21,CN,91\n";
        char changed[64];
        dsv_index_config_t config = {
            .text_column = 1,
            .number_column = 2,
            .covering_int64_column = 0,
            .has_header = true,
        };
        char *index_path = tt_make_temp_file("dsv-index-stale", ".idx");
        dsv_index_t *index = dsv_index_create();

        check_not_null(index_path);
        check_not_null(index);
        check(strlen(source) < sizeof(changed));
        memcpy(changed, source, strlen(source) + 1);
        changed[18] = '3';
        check_equal(dsv_index_build_memory(index, index_path, source, strlen(source), &config), 0);
        check_equal(dsv_index_open_memory(index, index_path, changed, strlen(changed)), -1);
        check_contains(dsv_index_error(index), "hash mismatch");

        dsv_index_destroy(index);
        check_equal(tt_remove_file(index_path), 0);
        free(index_path);
    }

    it("should enforce the configured entry capacity") {
        const char *source = "country,score\nCN,91\nCN,92\n";
        dsv_index_config_t config = {
            .text_column = 0,
            .number_column = 1,
            .covering_int64_column = DSV_INDEX_NO_COLUMN,
            .max_entries = 1,
            .has_header = true,
        };
        char *index_path = tt_make_temp_file("dsv-index-capacity", ".idx");
        dsv_index_t *index = dsv_index_create();

        check_not_null(index_path);
        check_not_null(index);
        check_equal(dsv_index_build_memory(index, index_path, source, strlen(source), &config), -1);
        check_contains(dsv_index_error(index), "capacity exceeded");

        dsv_index_destroy(index);
        check_equal(tt_remove_file(index_path), 0);
        free(index_path);
    }

    it("should union OR prefixes and then intersect a numeric range") {
        dsv_query_fixture_t fixture;
        size_t count;
        int64_t sum;

        check_equal(dsv_query_fixture_init(&fixture), 0);
        check_equal(dsv_query_execute(
                         &fixture,
                         "country == \"CN\" or country == \"US\" and score > 90",
                         &count, &sum),
                     0);
        check_equal(count, 3);
        check_equal(sum, 67);
        dsv_query_fixture_destroy(&fixture);
    }

    it("should merge duplicate OR ranges without duplicate rows") {
        dsv_query_fixture_t fixture;
        size_t count;
        int64_t sum;

        check_equal(dsv_query_fixture_init(&fixture), 0);
        check_equal(dsv_query_execute(
                         &fixture,
                         "country == \"CN\" or country == \"CN\"",
                         &count, &sum),
                     0);
        check_equal(count, 3);
        check_equal(sum, 68);
        dsv_query_fixture_destroy(&fixture);
    }

    it("should split numeric not-equal into two index ranges") {
        dsv_query_fixture_t fixture;
        size_t count;
        int64_t sum;

        check_equal(dsv_query_fixture_init(&fixture), 0);
        check_equal(dsv_query_execute(
                         &fixture, "country == \"CN\" and score != 90",
                         &count, &sum),
                     0);
        check_equal(count, 2);
        check_equal(sum, 45);
        dsv_query_fixture_destroy(&fixture);
    }

    it("should reject an OR branch without a text index prefix") {
        dsv_query_fixture_t fixture;
        dsv_index_cursor_t cursor;

        check_equal(dsv_query_fixture_init(&fixture), 0);
        check(dsv_filter_compile(fixture.filter,
                                 "country == \"CN\" or score > 90"));
        check_equal(dsv_filter_index_seek(fixture.filter, fixture.index, &cursor), -1);
        check_contains(dsv_filter_error(fixture.filter), "every OR range");
        dsv_query_fixture_destroy(&fixture);
    }

    it("should reject more query ranges than the cursor capacity") {
        dsv_query_fixture_t fixture;
        dsv_index_query_t queries[DSV_INDEX_MAX_QUERY_RANGES + 1];
        dsv_index_cursor_t cursor;
        size_t i;

        check_equal(dsv_query_fixture_init(&fixture), 0);
        memset(queries, 0, sizeof(queries));
        for (i = 0; i < DSV_INDEX_MAX_QUERY_RANGES + 1; ++i)
            queries[i].text_equals = vstr_from_cstr("CN");
        check_equal(dsv_index_seek_many(fixture.index, queries,
                                         DSV_INDEX_MAX_QUERY_RANGES + 1, &cursor),
                     -1);
        dsv_query_fixture_destroy(&fixture);
    }
}
