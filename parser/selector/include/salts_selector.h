/**
 * @file salts_selector.h
 * @brief Bounded resource-selector DSL compiled by re2c and Lemon.
 */
#ifndef SALTS_SELECTOR_H
#define SALTS_SELECTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_SELECTOR_LANGUAGE_VERSION_V1 1u
#define SALTS_SELECTOR_MAX_SOURCE_BYTES_V1 4096u
#define SALTS_SELECTOR_MAX_CANONICAL_BYTES_V1 32768u
#define SALTS_SELECTOR_MAX_TOKENS_V1 512u
#define SALTS_SELECTOR_MAX_NODES_V1 256u
#define SALTS_SELECTOR_MAX_DEPTH_V1 16u
#define SALTS_SELECTOR_MAX_LIST_ITEMS_V1 128u
#define SALTS_SELECTOR_MAX_STRING_BYTES_V1 256u
#define SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1 64u
#define SALTS_SELECTOR_DIAGNOSTIC_BYTES_V1 160u

typedef struct salts_selector_program_s salts_selector_program_t;

typedef enum salts_selector_status_e {
  SALTS_SELECTOR_OK = 0,
  SALTS_SELECTOR_INVALID_ARGUMENT = -1,
  SALTS_SELECTOR_SYNTAX_ERROR = -2,
  SALTS_SELECTOR_SEMANTIC_ERROR = -3,
  SALTS_SELECTOR_RESOURCE_LIMIT = -4,
  SALTS_SELECTOR_NO_MEMORY = -5,
  SALTS_SELECTOR_BUFFER_TOO_SMALL = -6,
  SALTS_SELECTOR_EVALUATION_ERROR = -7,
  SALTS_SELECTOR_INVALID_UTF8 = -8
} salts_selector_status_t;

typedef struct salts_selector_diagnostic_v1_s {
  size_t size;
  salts_selector_status_t status;
  size_t byte_offset;
  uint32_t line;
  uint32_t column;
  char message[SALTS_SELECTOR_DIAGNOSTIC_BYTES_V1];
} salts_selector_diagnostic_v1_t;

#define SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE \
  (sizeof(salts_selector_diagnostic_v1_t))

/**
 * Compile-time field policy. allowed_fields and their strings are borrowed
 * only for the compile call. A `tag.<identifier>` field is admitted only when
 * allow_tag_fields is non-zero. Field names are case-sensitive.
 */
typedef struct salts_selector_schema_v1_s {
  size_t size;
  const char *const *allowed_fields;
  size_t allowed_field_count;
  int allow_tag_fields;
} salts_selector_schema_v1_t;

#define SALTS_SELECTOR_SCHEMA_V1_SIZE (sizeof(salts_selector_schema_v1_t))

/**
 * Resolve a string field. Return 1 when present, 0 when absent, and a negative
 * value on failure. A present value must return a non-NULL pointer even when
 * its length is zero. The returned value is borrowed through the callback only.
 */
typedef int (*salts_selector_resolve_field_fn)(
    void *context, const char *field, size_t field_size,
    const char **out_value, size_t *out_value_size);

/** Return exactly 1 when present, 0 when absent, and a negative value on failure. */
typedef int (*salts_selector_has_capability_fn)(
    void *context, const char *capability, size_t capability_size);

typedef struct salts_selector_eval_ops_v1_s {
  size_t size;
  salts_selector_resolve_field_fn resolve_field;
  salts_selector_has_capability_fn has_capability;
} salts_selector_eval_ops_v1_t;

#define SALTS_SELECTOR_EVAL_OPS_V1_SIZE \
  (sizeof(salts_selector_eval_ops_v1_t))

/**
 * Compile one UTF-8 selector into an immutable owned program.
 *
 * Grammar:
 *   expr := expr "||" expr | expr "&&" expr | "!" expr | "(" expr ")"
 *         | field ("==" | "!=") string
 *         | field ("in" | "not" "in") "[" strings "]"
 *         | "has" "(" field ")"
 *         | "capability" "(" string ")"
 *
 * On success, *out_program is caller-owned and must be destroyed. On failure,
 * it remains NULL. The source is borrowed only for this call.
 */
int salts_selector_compile_v1(
    const char *source, size_t source_size,
    const salts_selector_schema_v1_t *schema,
    salts_selector_program_t **out_program,
    salts_selector_diagnostic_v1_t *diagnostic);

/** Destroy a program. No concurrent evaluator may still use it. */
void salts_selector_program_destroy(salts_selector_program_t *program);

/** Return the fixed language version used by the compiled program. */
uint32_t salts_selector_program_language_version(
    const salts_selector_program_t *program);

/** Return the number of leaf predicates in the program. */
size_t salts_selector_program_predicate_count(
    const salts_selector_program_t *program);

/**
 * Render the deterministic canonical selector. required excludes the NUL.
 * Passing output=NULL and output_capacity=0 is a supported size query.
 */
int salts_selector_program_canonical_v1(
    const salts_selector_program_t *program, char *output,
    size_t output_capacity, size_t *required,
    salts_selector_diagnostic_v1_t *diagnostic);

/**
 * Evaluate synchronously without allocation or I/O. Missing comparison fields
 * produce UNKNOWN, which remains UNKNOWN under `!` and is a final non-match;
 * use `has(field)` or `!has(field)` to select by presence explicitly.
 */
int salts_selector_program_evaluate_v1(
    const salts_selector_program_t *program,
    const salts_selector_eval_ops_v1_t *ops, void *context, int *out_match,
    salts_selector_diagnostic_v1_t *diagnostic);

#ifdef __cplusplus
}
#endif

#endif
