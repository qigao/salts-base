#ifndef CYAML_YPATH_EXPR_PARSER_H
#define CYAML_YPATH_EXPR_PARSER_H

#include "cyaml_ypath_internal.h"

#include <stddef.h>

typedef struct {
    ypath_expr_t* expr;
    const char* start;
} ypath_expr_token_t;

typedef struct {
    ypath_ast_storage_t* storage;
    ypath_expr_t* root;
    const char* error;
    uint32_t error_pos;
    uint32_t token_pos;
} ypath_expr_parse_ctx_t;

ypath_expr_t* cyaml_ypath_expr_make_unary(
    ypath_expr_parse_ctx_t* ctx, ypath_op_t op, ypath_expr_t* arg);
ypath_expr_t* cyaml_ypath_expr_make_binary(
    ypath_expr_parse_ctx_t* ctx, ypath_op_t op, ypath_expr_t* left, ypath_expr_t* right);
ypath_expr_t* cyaml_ypath_expr_make_cond(
    ypath_expr_parse_ctx_t* ctx, ypath_expr_t* cond, ypath_expr_t* then_expr,
    ypath_expr_t* else_expr);
void cyaml_ypath_expr_set_error(ypath_expr_parse_ctx_t* ctx, const char* error);

void* YPathExprParseAlloc(void* (*malloc_proc)(size_t));
void YPathExprParse(
    void* parser, int token, ypath_expr_token_t value, ypath_expr_parse_ctx_t* ctx);
void YPathExprParseFree(void* parser, void (*free_proc)(void*));

#endif // CYAML_YPATH_EXPR_PARSER_H
