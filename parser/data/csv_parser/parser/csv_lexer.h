/**
 * @file csv_lexer.h
 * @brief CSV Lexer Definitions (RFC 4180)
 */

#ifndef CSV_LEXER_H
#define CSV_LEXER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int         type;
    const char *value;
    size_t      length;
    int         needs_unescape;  // 1 if quoted field with escaped quotes
} csv_token_t;

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
    const char *marker;
    int         line;
    int         column;
    char        error[256];
} csv_lexer_t;

void csv_lexer_init(csv_lexer_t *lexer, const char *input, size_t length);
int  csv_lexer_next(csv_lexer_t *lexer, csv_token_t *token);
void csv_lexer_reset_state(void);

#ifdef __cplusplus
}
#endif

#endif /* CSV_LEXER_H */
