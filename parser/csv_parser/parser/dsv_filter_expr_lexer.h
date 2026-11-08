#ifndef DSV_FILTER_EXPR_LEXER_H
#define DSV_FILTER_EXPR_LEXER_H

#include "dsv_filter_expr_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
    const char *marker;
    char error[256];
} dsv_expr_lexer_t;

void dsv_expr_lexer_init(dsv_expr_lexer_t *lexer, const char *input, size_t len);
int dsv_expr_lexer_next(dsv_expr_lexer_t *lexer, dsv_expr_token_t *token);

#ifdef __cplusplus
}
#endif

#endif
