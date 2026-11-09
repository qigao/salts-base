/**
 * @file ini_lexer.h
 * @brief INI Lexer Definitions
 */

#ifndef INI_LEXER_H
#define INI_LEXER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INI_LEX_STATE_NORMAL = 0,
    INI_LEX_STATE_VALUE
} ini_lex_state_t;

typedef struct {
    int type;
    const char *value;
    size_t length;
} ini_token_t;

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
    int line;
    ini_lex_state_t state;
} ini_lexer_t;

void ini_lexer_init(ini_lexer_t *lexer, const char *input, size_t length);
int ini_lexer_next(ini_lexer_t *lexer, ini_token_t *token);

#ifdef __cplusplus
}
#endif

#endif /* INI_LEXER_H */
