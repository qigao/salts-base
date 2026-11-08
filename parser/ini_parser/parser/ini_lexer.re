// re2c $INPUT -o $OUTPUT
/**
 * @file ini_lexer.re
 * @brief INI File Lexer using re2c
 *
 * Tokenizes INI file content into: SECTION, KEY, VALUE, NEWLINE, COMMENT
 *
 * Build: re2c -o ini_lexer_gen.c ini_lexer.re
 */

#include "ini_lexer.h"
#include "ini_grammar_gen.h"
#include "simd_scan.h"
#include <string.h>

int ini_lex(ini_lexer_t *lexer, ini_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start;

    token->value = NULL;
    token->length = 0;

lex_start:
    const char *whitespace_end = salts_simd_skip_horizontal_whitespace(YYCURSOR, YYLIMIT);
    if (whitespace_end > YYCURSOR) {
        YYCURSOR = whitespace_end;
        goto lex_start;
    }
    token_start = YYCURSOR;

    if (YYCURSOR < YYLIMIT && (*YYCURSOR == ';' || *YYCURSOR == '#')) {
        const char *comment_end = salts_simd_find_any4(YYCURSOR + 1, YYLIMIT,
                                                        '\r', '\n', '\0', '\0');
        token->type = INI_TOKEN_COMMENT;
        token->value = token_start;
        token->length = (size_t)(comment_end - token_start);
        lexer->cursor = comment_end;
        return 1;
    }

    /*!re2c
        re2c:define:YYCTYPE = "char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        ws       = [ \t];
        newline  = "\r\n" | "\r" | "\n";
        comment  = [;#] [^\r\n\x00]*;
        section  = "[" [^\]\r\n\x00]+ "]";
        key      = [a-zA-Z_][a-zA-Z0-9_.-]*;

        // End of input
        $ {
            token->type = 0;
            lexer->cursor = YYCURSOR;
            return 0;
        }

        // Skip whitespace at line start
        ws+ {
            goto lex_start;
        }

        // Newline
        newline {
            token->type = INI_TOKEN_NEWLINE;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            lexer->cursor = YYCURSOR;
            lexer->line++;
            return 1;
        }

        // Comment line
        comment {
            token->type = INI_TOKEN_COMMENT;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Section header [name]
        section {
            token->type = INI_TOKEN_SECTION;
            token->value = token_start + 1;  // skip '['
            token->length = (size_t)(YYCURSOR - token_start - 2);  // exclude '[' and ']'
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Key = Value pattern
        key ws* "=" ws* {
            token->type = INI_TOKEN_KEY;
            // Find actual key end (before whitespace and '=')
            const char *key_end = token_start;
            while (*key_end && *key_end != '=' && *key_end != ' ' && *key_end != '\t') {
                key_end++;
            }
            token->value = token_start;
            token->length = (size_t)(key_end - token_start);
            lexer->cursor = YYCURSOR;
            lexer->state = INI_LEX_STATE_VALUE;
            return 1;
        }

        // Any other character is invalid
        * {
            token->type = 0;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            return -1;
        }
    */
}

int ini_lex_value(ini_lexer_t *lexer, ini_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start = YYCURSOR;

    token->value = NULL;
    token->length = 0;

    if (YYCURSOR < YYLIMIT && *YYCURSOR != '\r' && *YYCURSOR != '\n' &&
        *YYCURSOR != ';' && *YYCURSOR != '#') {
        const char *value_end = salts_simd_find_any5(YYCURSOR, YYLIMIT,
                                                      '\r', '\n', ';', '#', '\0');
        const char *trimmed_end = value_end;
        while (trimmed_end > token_start &&
               (*(trimmed_end - 1) == ' ' || *(trimmed_end - 1) == '\t')) {
            --trimmed_end;
        }
        token->type = INI_TOKEN_VALUE;
        token->value = token_start;
        token->length = (size_t)(trimmed_end - token_start);
        lexer->cursor = value_end;
        lexer->state = INI_LEX_STATE_NORMAL;
        return 1;
    }

    /*!re2c
        re2c:define:YYCTYPE = "char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        val_char = [^\r\n;#\x00];
        val      = val_char+;

        // End of input
        $ {
            token->type = INI_TOKEN_VALUE;
            token->value = token_start;
            token->length = 0;
            lexer->cursor = YYCURSOR;
            lexer->state = INI_LEX_STATE_NORMAL;
            return 1;
        }

        // Empty value (immediate newline or comment)
        [\r\n;#] {
            token->type = INI_TOKEN_VALUE;
            token->value = token_start;
            token->length = 0;
            lexer->cursor = token_start;  // Don't consume newline/comment
            lexer->state = INI_LEX_STATE_NORMAL;
            return 1;
        }

        // Value (trim trailing whitespace)
        val {
            const char *val_end = YYCURSOR;
            // Trim trailing whitespace
            while (val_end > token_start && (*(val_end-1) == ' ' || *(val_end-1) == '\t')) {
                val_end--;
            }
            token->type = INI_TOKEN_VALUE;
            token->value = token_start;
            token->length = (size_t)(val_end - token_start);
            lexer->cursor = YYCURSOR;
            lexer->state = INI_LEX_STATE_NORMAL;
            return 1;
        }
    */
}

void ini_lexer_init(ini_lexer_t *lexer, const char *input, size_t length) {
    if (!lexer) return;
    lexer->input = input;
    lexer->cursor = input;
    lexer->limit = input + length;
    lexer->line = 1;
    lexer->state = INI_LEX_STATE_NORMAL;
}

int ini_lexer_next(ini_lexer_t *lexer, ini_token_t *token) {
    if (lexer->state == INI_LEX_STATE_VALUE) {
        return ini_lex_value(lexer, token);
    }
    return ini_lex(lexer, token);
}
