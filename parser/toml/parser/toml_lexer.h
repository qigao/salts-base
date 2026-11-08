/**
 * @file toml_lexer.h
 * @brief TOML Lexer Interface
 */

#ifndef TOML_LEXER_H
#define TOML_LEXER_H

#include "toml_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *start;
    const char *cursor;
    const char *limit;
    const char *marker;
    int line;
    int column;
    char error_msg[256];
} toml_lexer_t;

void toml_lexer_init(toml_lexer_t *lexer, const char *input, size_t len);
int toml_lexer_next(toml_lexer_t *lexer, toml_token_t *token);

#ifdef __cplusplus
}
#endif

#endif /* TOML_LEXER_H */
