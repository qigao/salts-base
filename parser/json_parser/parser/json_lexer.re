// re2c $INPUT -o $OUTPUT
/**
 * @file json_lexer.re
 * @brief JSON Lexer using re2c
 *
 * Tokenizes JSON: { } [ ] : , string number true false null
 *
 * Build: re2c -o json_lexer_gen.c json_lexer.re
 */

#include "json_lexer.h"
#include "json_grammar_gen.h"
#include "json_lexer_whitespace.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline const char *skip_whitespace(const char *p, const char *end) {
    return json_skip_rfc_whitespace_simde(p, end);
}

static inline int has_escape(const char *p, size_t len) {
    return memchr(p, '\\', len) != NULL;
}

static double parse_number(const char *start, const char *end) {
    char buf[64];
    size_t len = (size_t)(end - start);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    return strtod(buf, NULL);
}

static int decode_hex4(const char *s) {
    int val = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        val = (val << 4) | d;
    }
    return val;
}

int json_lexer_next(json_lexer_t *lexer, json_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start;

    token->value = NULL;
    token->length = 0;
    token->num_value = 0.0;
    token->has_escape = 0;

lex_start:
    YYCURSOR = skip_whitespace(YYCURSOR, YYLIMIT);
    token_start = YYCURSOR;

    if (YYCURSOR < YYLIMIT && *YYCURSOR == '"') {
        const char *string_end = json_find_plain_ascii_string_end(YYCURSOR + 1, YYLIMIT);
        if (string_end) {
            token->type = JSON_TOKEN_STRING;
            token->value = YYCURSOR + 1;
            token->length = (size_t)(string_end - token->value);
            token->has_escape = 0;
            lexer->cursor = string_end + 1;
            return 1;
        }
    }

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        digit   = [0-9];
        digit1  = [1-9];
        int     = "-"? ("0" | digit1 digit*);
        frac    = "." digit+;
        exp     = [eE] [+-]? digit+;
        number  = int frac? exp?;

        hex     = [0-9a-fA-F];
        escape  = "\\" ["\\/bfnrt];
        unicode = "\\u" hex{4};
        char    = [^\x00-\x1f"\\] | escape | unicode;
        string  = "\"" char* "\"";

        // End of input
        $ {
            token->type = 0;
            lexer->cursor = YYCURSOR;
            return 0;
        }

        // Structural characters
        "{" {
            token->type = JSON_TOKEN_LBRACE;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "}" {
            token->type = JSON_TOKEN_RBRACE;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "[" {
            token->type = JSON_TOKEN_LBRACKET;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "]" {
            token->type = JSON_TOKEN_RBRACKET;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        ":" {
            token->type = JSON_TOKEN_COLON;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "," {
            token->type = JSON_TOKEN_COMMA;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Keywords
        "true" {
            token->type = JSON_TOKEN_TRUE;
            token->value = token_start;
            token->length = 4;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "false" {
            token->type = JSON_TOKEN_FALSE;
            token->value = token_start;
            token->length = 5;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "null" {
            token->type = JSON_TOKEN_NULL;
            token->value = token_start;
            token->length = 4;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // String (excluding quotes from value)
        string {
            token->type = JSON_TOKEN_STRING;
            token->value = token_start + 1;
            token->length = (size_t)(YYCURSOR - token_start - 2);
            token->has_escape = has_escape(token->value, token->length);
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Number
        number {
            token->type = JSON_TOKEN_NUMBER;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            token->num_value = parse_number(token_start, YYCURSOR);
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Invalid character
        * {
            snprintf(lexer->error, sizeof(lexer->error),
                     "Unexpected character '%c' at line %d", *token_start, lexer->line);
            token->type = 0;
            lexer->cursor = YYCURSOR;
            return -1;
        }
    */
}

void json_lexer_init(json_lexer_t *lexer, const char *input, size_t length) {
    if (!lexer) return;
    lexer->input = input;
    lexer->cursor = input;
    lexer->limit = input + length;
    lexer->line = 1;
    lexer->column = 1;
    lexer->error[0] = '\0';
}
