#ifndef SALTS_SCRIPT_DSV_FILTER_H
#define SALTS_SCRIPT_DSV_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include "csv_parser.h"
#include "dsv_index.h"
#include "query_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dsv_filter_s dsv_filter_t;

dsv_filter_t *dsv_filter_create(const csv_doc_t *doc, size_t header_row_index);
void dsv_filter_destroy(dsv_filter_t *filter);
const char *dsv_filter_error(dsv_filter_t *filter);
/**
 * Compile filter expression.
 * Supports:
 * - and/or between clauses
 * - comparison: == != > >= < <=
 * - numeric lhs arithmetic: + - * /, unary +/-, parentheses
 * - string literal rhs with quotes
 */
bool dsv_filter_compile(dsv_filter_t *filter, const char *expression);
bool dsv_filter_compile_ex(dsv_filter_t *filter, const char *expression,
                           const qvm_limits_t *limits,
                           qvm_diagnostic_t *diagnostic);
const qvm_diagnostic_t *dsv_filter_qvm_diagnostic(const dsv_filter_t *filter);
void dsv_filter_set_output_delimiter(dsv_filter_t *filter, char delimiter);
int dsv_filter_check_row(dsv_filter_t *filter, size_t row_index);
int dsv_filter_check_values(dsv_filter_t *filter, const vstr *fields, size_t field_count);

/** rendered_row is borrowed and valid only during the callback. */
typedef void (*dsv_row_callback_t)(void *user_data, size_t row_index, const char *rendered_row);
void dsv_filter_run(dsv_filter_t *filter, dsv_row_callback_t callback, void *user_data);

/**
 * Scan raw CSV content through a compiled simple-AND filter without building a
 * csv_doc_t for the data rows. The filter's document is used only for column
 * metadata. Arithmetic and OR expressions are rejected by this API rather than
 * falling back to DOM evaluation. Projection values are borrowed and follow
 * csv_scan_match_fn lifetime rules.
 */
int dsv_filter_scan(dsv_filter_t *filter, const char *content, size_t len,
                    const csv_options_t *opts,
                    const csv_scan_projection_t *projections, size_t projection_count,
                    csv_scan_match_fn on_match, void *ctx, size_t *matched_count);

/**
 * Compile the complete simple predicate expression into a bounded index
 * cursor. AND intersects ranges, OR unions ranges, and numeric != creates two
 * ranges. Boolean joins preserve the filter's existing left-associative
 * semantics. Every final range must constrain the index's leading text key.
 */
int dsv_filter_index_seek(dsv_filter_t *filter, const dsv_index_t *index,
                          dsv_index_cursor_t *cursor);

#ifdef __cplusplus
}
#endif

#endif // SALTS_SCRIPT_DSV_FILTER_H
