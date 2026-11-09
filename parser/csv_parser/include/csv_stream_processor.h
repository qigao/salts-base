#ifndef CSV_STREAM_PROCESSOR_H
#define CSV_STREAM_PROCESSOR_H

/**
 * @file csv_stream_processor.h
 * @brief Streaming CSV processor line-buffered, filter-capable, vector-accumulating.
 *
 * Designed so that csv_stream_processor_feed() has the exact signature of
 * http_data_cb, enabling zero-glue wiring:
 *
 *   csv_stream_processor_t *proc = csv_stream_processor_create(NULL);
 *   csv_stream_processor_set_filter(proc, "price > 100");
 *   http_receive_stream_get(client, url, csv_stream_processor_feed, proc);
 *   csv_stream_processor_finish(proc);
 *
 *
 * Limitation: does not support quoted fields with embedded newlines.
 * Tick/market data never has this; use the DOM API for edge cases.
 */

#include <stddef.h>
#include <stdbool.h>
#include <csv_parser.h>
#include <salts/platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct csv_stream_processor_s csv_stream_processor_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

csv_stream_processor_t *csv_stream_processor_create(const csv_options_t *opts);
void csv_stream_processor_destroy(csv_stream_processor_t *p);

/* ── Configuration (call before feeding data) ─────────────────────── */

/**
 * @brief Set a CSVPath filter expression. Header columns ending in _n are
 *        numeric, _s are string; plain headers are dynamically comparable.
 *        Expression example: "price > 100 and volume > 10000"
 * @return true on success (expression is compiled when header arrives).
 */
bool csv_stream_processor_set_filter(csv_stream_processor_t *p, const char *expr);

/**
 * @brief Select which columns to store. Only these columns will be accumulated.
 *        If not called, ALL columns are stored (expensive for wide data).
 * @param names Comma-separated column names, e.g. "price,volume,close".
 *              Matched against stripped names (without _n/_s suffix).
 */
void csv_stream_processor_set_columns(csv_stream_processor_t *p, const char *names);

/* Feed data; signature matches http_data_cb. */

void csv_stream_processor_feed(const char *data, size_t len, void *user_data);

/**
 * @brief Signal end of stream. Flushes any remaining buffered line.
 */
void csv_stream_processor_finish(csv_stream_processor_t *p);

/* ── Query results ────────────────────────────────────────────────── */

size_t      csv_stream_processor_row_count(const csv_stream_processor_t *p);
size_t      csv_stream_processor_col_count(const csv_stream_processor_t *p);
const char *csv_stream_processor_col_name(const csv_stream_processor_t *p, size_t idx);
size_t      csv_stream_processor_col_index(const csv_stream_processor_t *p, const char *name);

/**
 * @brief Get accumulated numeric data for a column.
 * @param col Column index.
 * @param out_len Receives the number of matched rows.
 * @return Pointer to double array, owned by processor. NULL if column is not numeric.
 */
const double *csv_stream_processor_col_data(const csv_stream_processor_t *p,
                                                       size_t col, size_t *out_len);

/**
 * @brief Get a string field from a matched row.
 * @param row Row index within matched results (0-based).
 * @param col Column index.
 * @return Pointer to null-terminated string, owned by processor. NULL on out-of-bounds.
 */
const char *csv_stream_processor_get_str(const csv_stream_processor_t *p,
                                                    size_t row, size_t col);

const char *csv_stream_processor_error(const csv_stream_processor_t *p);

#ifdef __cplusplus
}
#endif

#endif /* CSV_STREAM_PROCESSOR_H */
