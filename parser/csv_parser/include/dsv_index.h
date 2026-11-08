#ifndef SALTS_SCRIPT_DSV_INDEX_H
#define SALTS_SCRIPT_DSV_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "csv_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DSV_INDEX_MAX_TEXT_KEY = 64,
    DSV_INDEX_DEFAULT_MAX_ENTRIES = 1000000,
    DSV_INDEX_MAX_QUERY_RANGES = 32
};
#define DSV_INDEX_NO_COLUMN ((size_t)-1)

typedef struct dsv_index_s dsv_index_t;

typedef struct {
    size_t text_column;
    size_t number_column;
    size_t covering_int64_column;
    size_t max_entries;
    bool has_header;
} dsv_index_config_t;

typedef struct {
    vstr text_equals;
    bool has_lower_number;
    bool lower_inclusive;
    double lower_number;
    bool has_upper_number;
    bool upper_inclusive;
    double upper_number;
} dsv_index_query_t;

typedef struct {
    uint64_t row_offset;
    uint32_t row_length;
    bool has_covering_int64;
    int64_t covering_int64;
} dsv_index_row_t;

typedef struct {
    uint64_t _position;
    double _lower_number;
    double _upper_number;
    uint16_t _text_length;
    uint8_t _has_lower;
    uint8_t _lower_inclusive;
    uint8_t _has_upper;
    uint8_t _upper_inclusive;
    uint8_t _finished;
    char _text[DSV_INDEX_MAX_TEXT_KEY];
} dsv_index_range_cursor_t;

/**
 * Stack-owned, bounded cursor borrowing an open dsv_index_t. Closing the
 * index invalidates the cursor. A cursor contains no allocation and supports
 * at most DSV_INDEX_MAX_QUERY_RANGES normalized ranges.
 */
typedef struct {
    size_t _range_count;
    size_t _range_index;
    dsv_index_range_cursor_t _ranges[DSV_INDEX_MAX_QUERY_RANGES];
} dsv_index_cursor_t;

dsv_index_t *dsv_index_create(void);
void dsv_index_destroy(dsv_index_t *index);
void dsv_index_close(dsv_index_t *index);
const char *dsv_index_error(const dsv_index_t *index);

/**
 * Build or atomically replace a sidecar from a borrowed CSV buffer.
 * max_entries=0 selects DSV_INDEX_DEFAULT_MAX_ENTRIES. The handle must be
 * closed. Returns 0 or -1; details are available through dsv_index_error().
 */
int dsv_index_build_memory(dsv_index_t *index, const char *index_path,
                           const char *content, size_t len,
                           const dsv_index_config_t *config);

/** Build a sidecar from a static CSV file using Salts mmap and file APIs. */
int dsv_index_build_file(dsv_index_t *index, const char *csv_path,
                         const char *index_path, const dsv_index_config_t *config);

/**
 * Open an index while borrowing content until dsv_index_close(). Source size
 * and byte hash must match the sidecar.
 */
int dsv_index_open_memory(dsv_index_t *index, const char *index_path,
                          const char *content, size_t len);

/**
 * Open and own read-only mappings for both files. Source size and mtime must
 * match; close/destroy invalidates every cursor and row view.
 */
int dsv_index_open_file(dsv_index_t *index, const char *csv_path,
                        const char *index_path);

size_t dsv_index_count(const dsv_index_t *index);
size_t dsv_index_text_column(const dsv_index_t *index);
size_t dsv_index_number_column(const dsv_index_t *index);
size_t dsv_index_covering_column(const dsv_index_t *index);

/**
 * O(log n) seek followed by O(1) amortized cursor iteration. The query must
 * constrain the leading text key by equality; number bounds are optional.
 */
int dsv_index_seek(const dsv_index_t *index, const dsv_index_query_t *query,
                   dsv_index_cursor_t *cursor);

/**
 * Seek the union of query ranges. Overlapping ranges are merged before
 * iteration, so each indexed row is returned at most once. Time complexity is
 * O(q log q + q log n + matches), where q is bounded by
 * DSV_INDEX_MAX_QUERY_RANGES and n is the index entry count.
 */
int dsv_index_seek_many(const dsv_index_t *index,
                        const dsv_index_query_t *queries, size_t query_count,
                        dsv_index_cursor_t *cursor);
int dsv_index_cursor_next(const dsv_index_t *index, dsv_index_cursor_t *cursor,
                          dsv_index_row_t *row);

/** Borrow the exact source record, including its original record terminator. */
vstr dsv_index_row_view(const dsv_index_t *index, const dsv_index_row_t *row);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SCRIPT_DSV_INDEX_H */
