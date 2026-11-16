#ifndef DATETIME_LEXER_H
#define DATETIME_LEXER_H

#include "datetime_types.h"

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
} datetime_lexer_t;

void datetime_lexer_init(datetime_lexer_t *lexer, const char *input, size_t length);
int datetime_lexer_next(datetime_lexer_t *lexer, datetime_token_t *token);

#endif // DATETIME_LEXER_H
