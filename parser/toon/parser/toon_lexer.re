// re2c $INPUT -o $OUTPUT
#include "toon_lexer.h"
#include "toon_grammar_gen.h"
#include "simd_scan.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifndef LIKELY
#ifdef _MSC_VER
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#else
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#endif

static inline const char *skip_ws(const char *p, const char *end) {
    return salts_simd_skip_horizontal_whitespace(p, end);
}

static inline int lex_is_bool(const char *s, size_t len, int *val) {
    if (len == 4 && strncmp(s, "true", 4) == 0) { *val = 1; return 1; }
    if (len == 5 && strncmp(s, "false", 5) == 0) { *val = 0; return 1; }
    return 0;
}

static inline int lex_is_null(const char *s, size_t len) {
    return (len == 4 && strncmp(s, "null", 4) == 0);
}

void toon_lexer_init(toon_lexer_t *lexer, const char *input, size_t length) {
    if (!lexer) return;
    lexer->input = input;
    lexer->cursor = input;
    lexer->limit = input + length;
    lexer->marker = NULL;
    lexer->line = 1;
    lexer->indent_depth = 0;
    lexer->indent_stack[0] = 0;
    lexer->pending_dedents = 0;
    lexer->state = 0;
    lexer->error[0] = '\0';
}

int toon_lexer_next(toon_lexer_t *lexer, toon_token_t *token) {
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        token->type = TOON_TOKEN_DEDENT;
        return 1;
    }

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start;

    token->value = NULL;
    token->length = 0;

lex_start:
    if (lexer->state == 0) {
        // Start of line: count indentation
        int spaces = 0;
        while (YYCURSOR < YYLIMIT && *YYCURSOR == ' ') {
            YYCURSOR++;
            spaces++;
        }

        // Check if the rest of the line is empty or a comment
        const char *temp = salts_simd_skip_horizontal_whitespace(YYCURSOR, YYLIMIT);

        if (temp == YYLIMIT || *temp == '\n' || *temp == '\r' || *temp == '#') {
            // Skip empty lines/comments for indentation purposes
            if (temp < YYLIMIT && (*temp == '\n' || *temp == '\r')) {
                YYCURSOR = temp + 1;
                if (*temp == '\r' && YYCURSOR < YYLIMIT && *YYCURSOR == '\n') YYCURSOR++;
                lexer->line++;
                goto lex_start;
            } else if (temp < YYLIMIT && *temp == '#') {
                YYCURSOR = salts_simd_find_any4(temp + 1, YYLIMIT, '\r', '\n', '\0', '\0');
                goto lex_start;
            } else if (temp == YYLIMIT) {
                YYCURSOR = YYLIMIT;
                goto handle_eof;
            }
        }

        int level = spaces / 2;
        int current_level = lexer->indent_stack[lexer->indent_depth];

        if (level > current_level) {
            lexer->indent_depth++;
            lexer->indent_stack[lexer->indent_depth] = level;
            lexer->state = 1;
            lexer->cursor = YYCURSOR;
            token->type = TOON_TOKEN_INDENT;
            return 1;
        } else if (level < current_level) {
            int dedents = 0;
            while (lexer->indent_depth > 0 && lexer->indent_stack[lexer->indent_depth] > level) {
                lexer->indent_depth--;
                dedents++;
            }
            lexer->pending_dedents = dedents - 1;
            lexer->state = 1;
            lexer->cursor = YYCURSOR;
            token->type = TOON_TOKEN_DEDENT;
            return 1;
        }
        lexer->state = 1;
    }

lex_loop:
    const char *whitespace_end = skip_ws(YYCURSOR, YYLIMIT);
    if (whitespace_end > YYCURSOR) {
        YYCURSOR = whitespace_end;
        goto lex_loop;
    }
    token_start = YYCURSOR;

    if (YYCURSOR < YYLIMIT && *YYCURSOR == '#') {
        YYCURSOR = salts_simd_find_any4(YYCURSOR + 1, YYLIMIT, '\r', '\n', '\0', '\0');
        goto lex_start;
    }

    if (YYCURSOR < YYLIMIT && *YYCURSOR == '"') {
        const char *string_end = salts_simd_find_any4(YYCURSOR + 1, YYLIMIT,
                                                       '"', '\r', '\n', '\0');
        if (string_end < YYLIMIT && *string_end == '"') {
            token->type = TOON_TOKEN_VALUE;
            token->value = token_start + 1;
            token->length = (size_t)(string_end - token_start - 1);
            lexer->cursor = string_end + 1;
            return 1;
        }
    }

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        digit = [0-9];
        number = "-"? digit+ ("." digit+)? ([eE] [+-]? digit+)?;
        key = [a-zA-Z_][a-zA-Z0-9_.]*;
        newline = "\r\n" | "\r" | "\n";
        comment = "#" [^\r\n]*;
        string1 = "\"" [^"\r\n]* "\"";
        ws = [ \t]+;

        ws { goto lex_loop; }

        $ {
handle_eof:
            if (lexer->indent_depth > 0) {
                lexer->pending_dedents = lexer->indent_depth - 1;
                lexer->indent_depth = 0;
                token->type = TOON_TOKEN_DEDENT;
                lexer->cursor = YYCURSOR;
                return 1;
            }
            token->type = TOON_TOKEN_EOF;
            lexer->cursor = YYCURSOR;
            return 0;
        }

        newline {
            lexer->line++;
            lexer->state = 0;
            lexer->cursor = YYCURSOR;
            token->type = TOON_TOKEN_NEWLINE;
            return 1;
        }

        comment {
            lexer->cursor = YYCURSOR;
            goto lex_start;
        }

        ":" {
            token->type = TOON_TOKEN_COLON;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "[" {
            token->type = TOON_TOKEN_LBRACKET;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "]" {
            token->type = TOON_TOKEN_RBRACKET;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "{" {
            token->type = TOON_TOKEN_LBRACE;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "}" {
            token->type = TOON_TOKEN_RBRACE;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        "," {
            token->type = TOON_TOKEN_COMMA;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        number {
            token->type = TOON_TOKEN_NUMBER;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);

            // Fast path for numbers: avoid redundant parsing
            char *end;
            token->int_val = (int)strtoll(token_start, &end, 10);
            if (end < YYCURSOR && (*end == '.' || *end == 'e' || *end == 'E')) {
                token->float_val = strtod(token_start, NULL);
            } else {
                token->float_val = (double)token->int_val;
            }

            lexer->cursor = YYCURSOR;
            return 1;
        }

        string1 {
            token->type = TOON_TOKEN_VALUE;
            token->value = token_start + 1;
            token->length = (size_t)(YYCURSOR - token_start - 2);
            lexer->cursor = YYCURSOR;
            return 1;
        }

        key {
            // Check for bool/null
            size_t len = (size_t)(YYCURSOR - token_start);
            if (lex_is_bool(token_start, len, &token->bool_val)) {
                token->type = TOON_TOKEN_BOOL;
            } else if (lex_is_null(token_start, len)) {
                token->type = TOON_TOKEN_NULL;
            } else {
                token->type = TOON_TOKEN_KEY;
            }
            token->value = token_start;
            token->length = len;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Catch-all for values (including spaces)
        // Body excludes commas to allow them as delimiters in lists/tables
        [^\r\n,:# \t[\]{}][^,\r\n,#[\]{}]* {
            // If we are NOT in state 2 (expecting value), then we are expecting a Key.
            // In Key mode, we should not consume the colon.
            if (lexer->state != 2) {
                // Search for the first colon in the matched string
                size_t len = (size_t)(YYCURSOR - token_start);
                const char *colon = (const char*)memchr(token_start, ':', len);

                if (colon) {
                    // Found a colon! This means we matched "key: value" or "key:"
                    // We must rollback to the colon so it can be matched as a COLON token next.
                    YYCURSOR = colon;

                    // Trim trailing spaces from the key
                    const char *key_end = YYCURSOR;
                    while (key_end > token_start && (*(key_end-1) == ' ' || *(key_end-1) == '\t')) key_end--;
                    token->length = (size_t)(key_end - token_start);

                    // Re-evaluate the token type for the truncated string
                    if (lex_is_bool(token_start, token->length, &token->bool_val)) {
                        token->type = TOON_TOKEN_BOOL;
                    } else if (lex_is_null(token_start, token->length)) {
                        token->type = TOON_TOKEN_NULL;
                    } else {
                        token->type = TOON_TOKEN_KEY;
                    }
                    token->value = token_start;
                    lexer->cursor = YYCURSOR;
                    return 1;
                }
            }

            // If we are here, we are either in Value mode (swallow colons)
            // OR we are in Key mode but didn't find a colon (simple key)

            // If expected Key but no colon found, it's just a KEY (unless bool/null)
            if (lexer->state != 2) {
                 // Trim trailing spaces here too
                 const char *key_end = YYCURSOR;
                 while (key_end > token_start && (*(key_end-1) == ' ' || *(key_end-1) == '\t')) key_end--;
                 token->length = (size_t)(key_end - token_start);

                 if (lex_is_bool(token_start, token->length, &token->bool_val)) {
                     token->type = TOON_TOKEN_BOOL;
                 } else if (lex_is_null(token_start, token->length)) {
                     token->type = TOON_TOKEN_NULL;
                 } else {
                     token->type = TOON_TOKEN_KEY;
                 }
                 token->value = token_start;
                 lexer->cursor = YYCURSOR;
                 return 1;
            }

            // Otherwise, it matches as a VALUE
            // Trim trailing spaces
            const char *end = YYCURSOR;
            while (end > token_start && (*(end-1) == ' ' || *(end-1) == '\t')) end--;

            token->type = TOON_TOKEN_VALUE;
            token->value = token_start;
            token->length = (size_t)(end - token_start);
            lexer->cursor = YYCURSOR;
            return 1;
        }

        * {
            snprintf(lexer->error, sizeof(lexer->error), "Unexpected character '%c' at line %d", *token_start, lexer->line);
            token->type = TOON_TOKEN_ERROR;
            lexer->cursor = YYCURSOR;
            return -1;
        }
    */
}
