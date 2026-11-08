/**
 * @file json_parser.h
 * @brief Lightweight JSON Parser using re2c + Lemon
 */

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "query_vm.h"
#include <vstr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct json_value_s json_value_t;
typedef struct json_path_result_s json_path_result_t;
typedef struct json_path_program_s json_path_program_t;
typedef struct json_sax_parser_s json_sax_parser_t;
typedef struct json_path_stream_s json_path_stream_t;

typedef enum {
  JSON_NULL,
  JSON_BOOL,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT
} json_type_t;

json_value_t *json_parse(const char *content, size_t len);
json_value_t *json_parse_file(const char *filename);
void json_free(json_value_t *value);

json_type_t json_type(const json_value_t *value);
bool json_is_null(const json_value_t *value);
bool json_bool(const json_value_t *value);
double json_number(const json_value_t *value);
/**
 * Return the exact decimal token retained by a JSON number node.
 * @param value JSON number node.
 * @param len Optional output byte length; set to zero when unavailable.
 * @return Borrowed token text, or NULL when value is not a number.
 */
const char *json_number_text(const json_value_t *value, size_t *len);
const char *json_string(const json_value_t *value);
size_t json_string_len(const json_value_t *value);
vstr json_string_v(const json_value_t *value);

size_t json_object_size(const json_value_t *obj);
const char *json_object_key(const json_value_t *obj, size_t index);
size_t json_object_key_len(const json_value_t *obj, size_t index);
vstr json_object_key_v(const json_value_t *obj, size_t index);
json_value_t *json_object_value(const json_value_t *obj, size_t index);
json_value_t *json_object_get(const json_value_t *obj, const char *key);
json_value_t *json_object_get_v(const json_value_t *obj, vstr key);

size_t json_array_size(const json_value_t *arr);
json_value_t *json_array_get(const json_value_t *arr, size_t index);

int json_get_int(const json_value_t *obj, const char *key, int def);
bool json_get_bool(const json_value_t *obj, const char *key, bool def);
double json_get_double(const json_value_t *obj, const char *key, double def);
const char *json_get_string(const json_value_t *obj, const char *key);
vstr json_get_string_v(const json_value_t *obj, const char *key);

int json_get_int_v(const json_value_t *obj, vstr key, int def);
bool json_get_bool_v(const json_value_t *obj, vstr key, bool def);
double json_get_double_v(const json_value_t *obj, vstr key, double def);
vstr json_get_string_vv(const json_value_t *obj, vstr key);

/** Borrowed diagnostic for the calling thread, invalidated by its next parser call. */
const char *json_get_error(void);
char *json_serialize(const json_value_t *value, size_t *out_len);
char *json_serialize_pretty(const json_value_t *value, size_t *out_len);
char *json_serialize_pretty_crlf(const json_value_t *value, size_t *out_len);
void json_serialize_free(char *str);

/* ============================================================================
 * JSONPath Query
 * API
 * ============================================================================ */

json_value_t *json_path_get(const json_value_t *root, const char *expr);
json_path_result_t *json_path_query(const json_value_t *root, const char *expr);

/** Compile a JSONPath expression into an immutable reusable program.
 * @param expr NUL-terminated JSONPath expression; copied by this call.
 * @return Owned program, or NULL for invalid syntax, resource limits, or OOM.
 * Inspect json_path_get_error() after failure. The program does not retain expr.
 */
json_path_program_t *json_path_compile(const char *expr);
json_path_program_t *json_path_compile_ex(const char *expr,
                                          const qvm_limits_t *limits,
                                          qvm_diagnostic_t *diagnostic);

/** Execute a compiled JSONPath and return its first match.
 * @param root Borrowed JSON tree, which must remain alive for the call.
 * @param program Borrowed immutable program, reusable across JSON trees.
 * @return Borrowed value from root, or NULL when there is no match or arguments
 * are invalid. The returned value is invalidated when root is freed.
 */
json_value_t *json_path_get_compiled(const json_value_t *root,
                                     const json_path_program_t *program);
json_value_t *json_path_get_compiled_ex(const json_value_t *root,
                                        const json_path_program_t *program,
                                        qvm_diagnostic_t *diagnostic);

/** Execute a compiled JSONPath and collect every match.
 * @param root Borrowed JSON tree, which must remain alive until result use ends.
 * @param program Borrowed immutable program; do not free it during execution.
 * @return Owned result handle, including an empty result for no matches, or NULL
 * for invalid arguments/OOM. Result values are borrowed from root.
 */
json_path_result_t *json_path_query_compiled(
    const json_value_t *root, const json_path_program_t *program);
json_path_result_t *json_path_query_compiled_ex(
    const json_value_t *root, const json_path_program_t *program,
    qvm_diagnostic_t *diagnostic);

/** Free a compiled JSONPath program. NULL is accepted.
 * The caller must ensure no execution is using program concurrently.
 */
void json_path_program_free(json_path_program_t *program);

size_t json_path_result_size(const json_path_result_t *result);
json_value_t *json_path_result_get(const json_path_result_t *result, size_t index);
void json_path_result_free(json_path_result_t *result);
const char *json_path_get_error(void);

/* ============================================================================
 * Builder API
 *
 * ============================================================================ */

json_value_t *json_create_object(void);
json_value_t *json_create_array(void);
json_value_t *json_create_string(const char *str);
/** Create an owned JSON string from exactly len bytes.
 * @param str Source bytes, copied before
 * return; must not be NULL.
 * @param len Byte length; embedded NUL bytes are allowed.
 * @return
 * New root value owned by the caller, or NULL on invalid input/OOM.
 */
json_value_t *json_create_string_n(const char *str, size_t len);
json_value_t *json_create_number(double num);
json_value_t *json_create_int64(int64_t num);
/**
 * Create a JSON integer preserving the complete unsigned 64-bit decimal value.
 * @param num Unsigned integer value.
 * @return New root value owned by the caller, or NULL on OOM.
 */
json_value_t *json_create_uint64(uint64_t num);
json_value_t *json_create_bool(bool val);
json_value_t *json_create_null(void);
json_value_t *json_clone(const json_value_t *value);

/** Add a C-string key/value pair and report whether it was committed.
 * On success obj owns val.
 * On failure ownership remains unchanged.
 */
bool json_object_add_checked(json_value_t *obj, const char *key, json_value_t *val);
/** Add a length-delimited key/value pair. The key is copied and may contain NUL.
 * On success obj
 * owns val. On failure ownership remains unchanged.
 */
bool json_object_add_n(json_value_t *obj, const char *key, size_t key_len, json_value_t *val);
/** Append val and report whether it was committed.
 * On success arr owns val. On failure ownership
 * remains unchanged.
 */
bool json_array_add_checked(json_value_t *arr, json_value_t *val);

/* Compatibility wrappers. Prefer the checked APIs in new code. */
void json_object_add(json_value_t *obj, const char *key, json_value_t *val);
void json_array_add(json_value_t *arr, json_value_t *val);

void json_object_set_string(json_value_t *obj, const char *key, const char *val);
void json_object_set_number(json_value_t *obj, const char *key, double val);
void json_object_set_bool(json_value_t *obj, const char *key, bool val);
void json_object_set_null(json_value_t *obj, const char *key);

/* ============================================================================
 * SAX/Stream API - callback-based parsing without a retained DOM
 * ============================================================================ */

typedef struct json_sax_handler_s {
  int (*on_null)(void *ctx);
  int (*on_bool)(void *ctx, bool val);
  int (*on_number)(void *ctx, double val);
  int (*on_string)(void *ctx, const char *val, size_t len);
  int (*on_object_start)(void *ctx);
  int (*on_object_key)(void *ctx, const char *key, size_t len);
  int (*on_object_end)(void *ctx);
  int (*on_array_start)(void *ctx);
  int (*on_array_end)(void *ctx);
} json_sax_handler_t;

/** SAX callback table that preserves the exact JSON number token.
 * Number slices are borrowed and valid only for the callback duration.
 */
typedef struct json_sax_handler_raw_s {
  int (*on_null)(void *ctx);
  int (*on_bool)(void *ctx, bool val);
  int (*on_number)(void *ctx, const char *val, size_t len);
  int (*on_string)(void *ctx, const char *val, size_t len);
  int (*on_object_start)(void *ctx);
  int (*on_object_key)(void *ctx, const char *key, size_t len);
  int (*on_object_end)(void *ctx);
  int (*on_array_start)(void *ctx);
  int (*on_array_end)(void *ctx);
} json_sax_handler_raw_t;

/** Stream callbacks for a compiled, streamable JSONPath program.
 *
 * Only the selected subtrees are delivered. on_match_start() is called before
 * the matching value's event, and on_match_end() is called after its closing
 * event. The embedded SAX callbacks are borrowed-event callbacks: strings,
 * object keys, and raw numbers are valid only for the duration of the callback.
 * Stream programs support root/key/index/wildcard paths and key/index unions;
 * filter and recursive expressions must use the DOM JSONPath API.
 */
typedef struct json_path_stream_handler_s {
  int (*on_match_start)(void *ctx, json_type_t type);
  int (*on_match_end)(void *ctx, json_type_t type);
  json_sax_handler_raw_t events;
} json_path_stream_handler_t;

int json_parse_sax(const char *content, size_t len, const json_sax_handler_t *handler, void *ctx);
/** Parse one document while delivering exact JSON number tokens.
 * @param content JSON input; must not be NULL.
 * @param len Input length; must be non-zero.
 * @param handler Raw-number callback table; must not be NULL.
 * @param ctx User context passed unchanged to callbacks.
 * @return 0 on success, -1 on invalid input, syntax error, allocation failure,
 * or a non-zero callback result.
 */
int json_parse_sax_raw(const char *content, size_t len,
                       const json_sax_handler_raw_t *handler, void *ctx);

/* Incremental SAX parser. Call feed() with any chunk size, then finish() once at
 * EOF. Memory is O(depth + longest incomplete token + unconsumed input).
 * Callback pointers are valid only for the duration of the callback. */
json_sax_parser_t *json_sax_parser_create(const json_sax_handler_t *handler, void *ctx);
/** Create an incremental parser that preserves exact number tokens.
 * @param handler Raw-number callback table; must not be NULL and is copied.
 * @param ctx User context passed unchanged to callbacks.
 * @return Owned parser released by json_sax_parser_destroy(), or NULL on invalid
 * arguments/allocation failure.
 */
json_sax_parser_t *json_sax_parser_create_raw(const json_sax_handler_raw_t *handler, void *ctx);
int json_sax_parser_feed(json_sax_parser_t *parser, const char *data, size_t len);
int json_sax_parser_finish(json_sax_parser_t *parser);
const char *json_sax_parser_error(const json_sax_parser_t *parser);
void json_sax_parser_destroy(json_sax_parser_t *parser);

/** Create a matcher for a streamable compiled JSONPath program.
 * The program is borrowed and must remain alive until the stream is destroyed.
 * @param program Borrowed immutable program from json_path_compile().
 * @param handler Callback table copied by this call; individual callbacks are optional.
 * @param ctx User context passed unchanged to callbacks.
 * @return Owned stream matcher, or NULL on invalid arguments, unsupported
 * expression, resource limit, or OOM.
 * The failure reason is available through json_path_get_error().
 * @code
 * json_path_program_t *program = json_path_compile("$.items[*].id");
 * json_path_stream_handler_t handler = {0};
 * handler.events.on_number = consume_number_token;
 * json_path_stream_t *stream = json_path_stream_create(program, &handler, ctx);
 * json_path_stream_feed(stream, chunk, chunk_len);
 * json_path_stream_finish(stream);
 * json_path_stream_destroy(stream);
 * json_path_program_free(program);
 * @endcode
 */
json_path_stream_t *json_path_stream_create(const json_path_program_t *program,
                                             const json_path_stream_handler_t *handler,
                                             void *ctx);

/** Feed an arbitrary input chunk. The chunk is borrowed only for this call.
 * @return 0 on success, -1 on invalid state, JSON syntax, allocation, resource,
 * or callback failure.
 */
int json_path_stream_feed(json_path_stream_t *stream, const char *data, size_t len);

/** Finish the one-document stream at EOF.
 * @return 0 on success, -1 on incomplete input, invalid state, or callback failure.
 */
int json_path_stream_finish(json_path_stream_t *stream);

/** Return the number of completed matches observed by the stream. */
size_t json_path_stream_match_count(const json_path_stream_t *stream);

/** Return the stream-local error, or NULL when no error has occurred. */
const char *json_path_stream_error(const json_path_stream_t *stream);

/** Destroy a stream matcher. NULL is accepted. */
void json_path_stream_destroy(json_path_stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif /* JSON_PARSER_H */
