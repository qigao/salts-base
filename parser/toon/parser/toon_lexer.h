#ifndef TOON_LEXER_H
#define TOON_LEXER_H

#include <stddef.h>
#include <stdint.h>
#include <cmeta_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOON_TOKEN_ERROR = -1,
    TOON_TOKEN_EOF = 0,
    TOON_TOKEN_COLON = 1,
    TOON_TOKEN_LBRACKET = 2,
    TOON_TOKEN_RBRACKET = 3,
    TOON_TOKEN_LBRACE = 4,
    TOON_TOKEN_RBRACE = 5,
    TOON_TOKEN_COMMA = 6,
    TOON_TOKEN_NEWLINE = 7,
    TOON_TOKEN_INDENT = 8,
    TOON_TOKEN_DEDENT = 9,
    TOON_TOKEN_KEY = 10,
    TOON_TOKEN_VALUE = 11,
    TOON_TOKEN_NUMBER = 12,
    TOON_TOKEN_BOOL = 13,
    TOON_TOKEN_NULL = 14
} toon_token_type_t;

typedef struct {
    toon_token_type_t type;
    const char *value;
    size_t length;
    int int_val;
    double float_val;
    int bool_val;
} toon_token_t;

typedef struct {
    const char *input;
    const char *cursor;
    const char *limit;
    const char *marker;
    int line;

    int indent_stack[256];
    int indent_depth;
    int pending_dedents;

    int state; // 0 = start of line, 1 = middle of line, 2 = after colon

    char error[256];
} toon_lexer_t;


typedef struct {
    struct toonObject *root;
    struct toonObject *last_node; // Tail of the root children list
    struct toonObject *columns;
    mem_pool_t *arena;
    int error;
    int line;
    char error_msg[256];
} toon_parse_ctx_t;

void toon_lexer_init(toon_lexer_t *lexer, const char *input, size_t length);
int toon_lexer_next(toon_lexer_t *lexer, toon_token_t *token);

#ifdef __cplusplus
}
#endif

#endif /* TOON_LEXER_H */
