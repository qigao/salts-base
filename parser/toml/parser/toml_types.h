/**
 * @file toml_types.h
 * @brief TOML Parser Internal Types
 */

#ifndef TOML_TYPES_H
#define TOML_TYPES_H

#include "toml.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOML_TOKEN_ERROR = -1,
    TOML_TOKEN_EOF = 0,
    TOML_TOKEN_DOT = 1,
    TOML_TOKEN_COMMA,
    TOML_TOKEN_EQUAL,
    TOML_TOKEN_LBRACE,
    TOML_TOKEN_RBRACE,
    TOML_TOKEN_LBRACKET,
    TOML_TOKEN_RBRACKET,
    TOML_TOKEN_NEWLINE,
    TOML_TOKEN_STRING,
    TOML_TOKEN_KEY,
    TOML_TOKEN_VAL_INT,
    TOML_TOKEN_VAL_FLOAT,
    TOML_TOKEN_VAL_BOOL,
    TOML_TOKEN_VAL_DATETIME
} toml_token_type_t;

typedef struct {
    toml_token_type_t type;
    char *value;
    size_t len;
    toml_pos_t pos;
} toml_token_t;

typedef struct {
    toml_token_t tokens[10];
    int count;
} toml_path_t;

typedef struct {
    toml_table_t *root;
    toml_table_t *current_table;
    int error;
    char error_msg[256];
} toml_parse_ctx_t;

// Helper functions for semantic actions
toml_table_t* toml_helper_create_table(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key, bool is_array_of_tables);
void toml_helper_add_keyval(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key, toml_token_t val);
toml_table_t* toml_helper_walk_path(toml_parse_ctx_t *ctx, toml_table_t *start, toml_token_t *keys, int num_keys, bool create_intermediate);

toml_array_t* toml_helper_create_array(toml_parse_ctx_t *ctx);
void toml_helper_array_append_value(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_token_t val);
void toml_helper_array_append_array(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_array_t *sub);
void toml_helper_array_append_table(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_table_t *sub);
void toml_helper_add_array(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key, toml_array_t *arr);
void toml_helper_add_inline_table(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key, toml_table_t *sub);
toml_table_t* toml_helper_create_inline_table(toml_parse_ctx_t *ctx);

typedef struct {
    toml_path_t path;
    void* val;
    int type; // 0: simple, 1: array, 2: table
} inline_kv_t;

void toml_helper_add_any(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_path_t path, void *val, int type);

#ifdef __cplusplus
}
#endif

#endif /* TOML_TYPES_H */
