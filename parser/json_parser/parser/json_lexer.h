/**
 * @file json_lexer.h
 * @brief JSON Lexer Definitions
 */

#ifndef JSON_LEXER_H
#define JSON_LEXER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int type;
    const char *value;
    size_t length;
    double num_value;
    int has_escape;  // 1 if string contains escape sequences
} json_token_t;

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
    int line;
    int column;
    char error[256];
} json_lexer_t;

void json_lexer_init(json_lexer_t *lexer, const char *input, size_t length);
int  json_lexer_next(json_lexer_t *lexer, json_token_t *token);

#ifdef __cplusplus
}
#endif

#endif /* JSON_LEXER_H */
