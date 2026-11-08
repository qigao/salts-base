#ifndef CYAML_YPATH_LEXER_H
#define CYAML_YPATH_LEXER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    YPATH_TOK_EOF = 0,
    YPATH_TOK_SLASH,
    YPATH_TOK_DOT,
    YPATH_TOK_DOTDOT,
    YPATH_TOK_STAR,
    YPATH_TOK_STARSTAR,
    YPATH_TOK_LBRACKET,
    YPATH_TOK_RBRACKET,
    YPATH_TOK_LPAREN,
    YPATH_TOK_RPAREN,
    YPATH_TOK_COLON,
    YPATH_TOK_QUESTION,
    YPATH_TOK_AT,
    YPATH_TOK_OR,
    YPATH_TOK_AND,
    YPATH_TOK_EQ,
    YPATH_TOK_NE,
    YPATH_TOK_LT,
    YPATH_TOK_LE,
    YPATH_TOK_GT,
    YPATH_TOK_GE,
    YPATH_TOK_PLUS,
    YPATH_TOK_MINUS,
    YPATH_TOK_DIV,
    YPATH_TOK_BANG,
    YPATH_TOK_BAND,
    YPATH_TOK_BOR,
    YPATH_TOK_CARET,
    YPATH_TOK_LSHIFT,
    YPATH_TOK_RSHIFT,
    YPATH_TOK_TILDE,
    YPATH_TOK_COMMA,
    YPATH_TOK_IDIV,
    YPATH_TOK_MATCHES,
    YPATH_TOK_IDENT,
    YPATH_TOK_INT,
    YPATH_TOK_FLOAT,
    YPATH_TOK_STRING,
    YPATH_TOK_TRUE,
    YPATH_TOK_FALSE,
    YPATH_TOK_NULL,
    YPATH_TOK_ERROR
} ypath_tok_t;

typedef struct {
    ypath_tok_t type;
    const char* start;
    uint32_t len;
    union {
        int64_t i;
        double f;
    } val;
} ypath_token_t;

typedef struct {
    const char* src;
    const char* cur;
    const char* end;
    ypath_token_t tok;
    const char* error;
    bool in_filter;
} ypath_lexer_t;

void ypath_lex_init(ypath_lexer_t* lexer, const char* path);
void ypath_lex_next(ypath_lexer_t* lexer);

#endif // CYAML_YPATH_LEXER_H
