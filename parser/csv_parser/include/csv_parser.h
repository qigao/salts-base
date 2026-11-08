/**
 * @file csv_parser.h
 * @brief RFC 4180 Compliant CSV Parser using re2c + Lemon
 *
 * Features:
 * - Full RFC 4180 compliance
 * - Zero-copy parsing where possible
 * - Streaming (SAX-like) API using O(largest field) memory
 * - Batch API for convenient access
 * - Arena-based memory management
 * - vstr (string view) support for zero-copy field access
 */

#ifndef CSV_PARSER_H
#define CSV_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <vstr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Batch/DOM API - Parse entire document into memory
 * ============================================================================ */

typedef struct csv_doc_s csv_doc_t;
typedef struct csv_sax_parser_s csv_sax_parser_t;
typedef struct csv_cursor_s csv_cursor_t;

typedef struct {
    bool has_header;      // First row is header (default: false)
    char delimiter;       // Field delimiter (default: ',')
    char quote;           // Quote character (default: '"')
    bool skip_empty_rows; // Skip empty rows (default: true)
} csv_options_t;

#define CSV_OPTIONS_DEFAULT { false, ',', '"', true }

csv_doc_t  *csv_parse(const char *content, size_t len);
csv_doc_t  *csv_parse_opts(const char *content, size_t len, const csv_options_t *opts);
csv_doc_t  *csv_parse_file(const char *filename);
csv_doc_t  *csv_parse_file_opts(const char *filename, const csv_options_t *opts);
void        csv_free(csv_doc_t *doc);

size_t      csv_row_count(const csv_doc_t *doc);
size_t      csv_column_count(const csv_doc_t *doc);
bool        csv_has_header(const csv_doc_t *doc);

/**
 * Read-only cursor over parsed rows. Row lookup is O(1); materializing the
 * field-view cache is O(columns) per row and uses O(columns) reusable memory.
 * The document must outlive the cursor. The cursor is single-thread owned;
 * field views are borrowed from the document and become invalid after
 * next/rewind/free.
 */
csv_cursor_t *csv_cursor_new(const csv_doc_t *doc, size_t first_row);
void         csv_cursor_free(csv_cursor_t *cursor);
int          csv_cursor_rewind(csv_cursor_t *cursor, size_t first_row);
int          csv_cursor_next(csv_cursor_t *cursor);
int          csv_cursor_error(const csv_cursor_t *cursor);
size_t       csv_cursor_row_index(const csv_cursor_t *cursor);
const vstr *csv_cursor_fields(const csv_cursor_t *cursor, size_t *field_count);
vstr       csv_cursor_field_v(const csv_cursor_t *cursor, size_t col);

const char *csv_get(const csv_doc_t *doc, size_t row, size_t col);
size_t      csv_get_len(const csv_doc_t *doc, size_t row, size_t col);
vstr      csv_get_v(const csv_doc_t *doc, size_t row, size_t col);

const char *csv_header_get(const csv_doc_t *doc, size_t col);
size_t      csv_header_get_len(const csv_doc_t *doc, size_t col);
vstr      csv_header_get_v(const csv_doc_t *doc, size_t col);

int         csv_get_int(const csv_doc_t *doc, size_t row, size_t col, int def);
double      csv_get_double(const csv_doc_t *doc, size_t row, size_t col, double def);
bool        csv_get_bool(const csv_doc_t *doc, size_t row, size_t col, bool def);

size_t      csv_find_column(const csv_doc_t *doc, const char *header_name);
size_t      csv_find_column_v(const csv_doc_t *doc, vstr header_name);
const char *csv_get_by_name(const csv_doc_t *doc, size_t row, const char *col_name);
vstr      csv_get_by_name_v(const csv_doc_t *doc, size_t row, vstr col_name);

const char *csv_get_error(void);

/** Serialize a parsed document back to CSV string.
 *  @return malloc'd string, caller must free(). NULL on invalid input,
 *  allocation failure, or serialized-size overflow. */
char       *csv_to_string(const csv_doc_t *doc);
/** Serialize a parsed document and return its exact byte length in out_len. */
char       *csv_to_string_n(const csv_doc_t *doc, size_t *out_len);

/** Serialized byte sink. Chunk boundaries have no semantic meaning. */
typedef int (*csv_write_fn)(const void *data, size_t len, void *user);

/** Serialize the document to a byte sink. */
int         csv_write(const csv_doc_t *doc, csv_write_fn write, void *user);

/** Serialize one complete logical record per callback invocation.
 *  A quoted field's embedded newlines remain within the same record callback. */
int         csv_write_records(const csv_doc_t *doc, csv_write_fn write, void *user);

/** Serialize and write to file. @return 0 on success, -1 on error. */
int         csv_write_file(const csv_doc_t *doc, const char *filename);

/* ============================================================================
 * Streaming/SAX API - O(largest field) memory, callback-based parsing
 * ============================================================================ */

typedef struct csv_stream_handler_s {
    int (*on_row_start)(void *ctx, size_t row_index);
    int (*on_field)(void *ctx, size_t row_index, size_t col_index,
                    const char *value, size_t len);
    int (*on_row_end)(void *ctx, size_t row_index, size_t field_count);
} csv_stream_handler_t;

/** Incremental CSV SAX parser. Callback field views are valid only during the callback. */
csv_sax_parser_t *csv_sax_parser_create(const csv_stream_handler_t *handler, void *ctx,
                                        const csv_options_t *opts);
int               csv_sax_parser_feed(csv_sax_parser_t *parser, const char *data, size_t len);
int               csv_sax_parser_finish(csv_sax_parser_t *parser);
const char       *csv_sax_parser_error(const csv_sax_parser_t *parser);
void              csv_sax_parser_destroy(csv_sax_parser_t *parser);

int csv_parse_stream(const char *content, size_t len,
                     const csv_stream_handler_t *handler, void *ctx);

int csv_parse_stream_opts(const char *content, size_t len,
                          const csv_stream_handler_t *handler, void *ctx,
                          const csv_options_t *opts);

/* ============================================================================
 * Direct scan API - zero-DOM predicate filtering and projection
 * ============================================================================ */

/* The direct scanner uses the RFC 4180 comma/double-quote lexer. */
enum { CSV_SCAN_MAX_COLUMNS = 256, CSV_SCAN_MAX_PREDICATES = 64 };

typedef enum {
    CSV_SCAN_VALUE_TEXT = 0,
    CSV_SCAN_VALUE_INT64,
    CSV_SCAN_VALUE_DOUBLE
} csv_scan_value_type_t;

typedef enum {
    CSV_SCAN_OP_EQ = 0,
    CSV_SCAN_OP_NE,
    CSV_SCAN_OP_GT,
    CSV_SCAN_OP_GE,
    CSV_SCAN_OP_LT,
    CSV_SCAN_OP_LE
} csv_scan_op_t;

/** One predicate in a direct scan. All plan predicates are ANDed together. */
typedef struct {
    size_t                column;
    csv_scan_value_type_t type;
    csv_scan_op_t         op;
    int64_t               integer;
    double                number;
    vstr                text;
} csv_scan_predicate_t;

/** A projected column. Values are emitted in this order for every match. */
typedef struct {
    size_t                column;
    csv_scan_value_type_t type;
} csv_scan_projection_t;

typedef struct {
    csv_scan_value_type_t type;
    union {
        int64_t integer;
        double  number;
        vstr  text;
    } value;
} csv_scan_value_t;

/**
 * Called for each matched row. Values are borrowed from the input buffer or
 * scanner-owned scratch storage and must not outlive this callback.
 */
typedef int (*csv_scan_match_fn)(void *ctx, size_t row_index,
                                 const csv_scan_value_t *values, size_t value_count);

typedef struct {
    const csv_scan_predicate_t *predicates;
    size_t                      predicate_count;
    const csv_scan_projection_t *projections;
    size_t                      projection_count;
    csv_scan_match_fn           on_match;
    void                       *ctx;
} csv_scan_plan_t;

/**
 * Scan an in-memory RFC 4180 comma/double-quote CSV source without building a
 * document. opts may be NULL; non-default delimiter/quote options are rejected.
 * When opts->has_header is true, the first logical record is skipped. Returns
 * 0 on success and -1 for malformed input, an invalid plan, conversion error,
 * or a non-zero match callback result. matched_count may be NULL.
 */
int csv_filter_scan_opts(const char *content, size_t len, const csv_options_t *opts,
                         const csv_scan_plan_t *plan, size_t *matched_count);

/* ============================================================================
 * Iterator API - Memory-efficient row-by-row access
 * ============================================================================ */

typedef struct csv_iter_s csv_iter_t;

csv_iter_t *csv_iter_new(const char *content, size_t len);
csv_iter_t *csv_iter_new_opts(const char *content, size_t len, const csv_options_t *opts);
void        csv_iter_free(csv_iter_t *iter);

bool        csv_iter_next(csv_iter_t *iter);
size_t      csv_iter_field_count(const csv_iter_t *iter);
const char *csv_iter_field(const csv_iter_t *iter, size_t col);
size_t      csv_iter_field_len(const csv_iter_t *iter, size_t col);
vstr      csv_iter_field_v(const csv_iter_t *iter, size_t col);
size_t      csv_iter_row_index(const csv_iter_t *iter);

#ifdef __cplusplus
}
#endif

#endif /* CSV_PARSER_H */
