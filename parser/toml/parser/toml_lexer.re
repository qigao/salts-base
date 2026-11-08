// re2c $INPUT -o $OUTPUT
/**
 * @file toml_lexer.re
 * @brief TOML Lexer using re2c
 */

#include "toml_lexer.h"
#include "toml_grammar_gen.h"
#include "simd_scan.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int toml_lexer_next(toml_lexer_t *lexer, toml_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start;

lex_start:
    const char *whitespace_end = salts_simd_skip_horizontal_whitespace(YYCURSOR, YYLIMIT);
    if (whitespace_end > YYCURSOR) {
        lexer->column += (int)(whitespace_end - YYCURSOR);
        YYCURSOR = whitespace_end;
        goto lex_start;
    }
    token_start = YYCURSOR;

    if (YYCURSOR < YYLIMIT && *YYCURSOR == '#') {
        const char *comment_end = salts_simd_find_any4(YYCURSOR + 1, YYLIMIT,
                                                        '\r', '\n', '\0', '\0');
        lexer->column += (int)(comment_end - token_start);
        YYCURSOR = comment_end;
        goto lex_start;
    }

    if ((size_t)(YYLIMIT - YYCURSOR) < 3U || YYCURSOR[0] != '"' ||
        YYCURSOR[1] != '"' || YYCURSOR[2] != '"') {
        if (YYCURSOR < YYLIMIT && *YYCURSOR == '"') {
            const char *string_end = salts_simd_find_any5(YYCURSOR + 1, YYLIMIT,
                                                           '"', '\\', '\r', '\n', '\0');
            if (string_end < YYLIMIT && *string_end == '"') {
                token->type = TOML_TOKEN_STRING;
                YYCURSOR = string_end + 1;
                goto tok_done;
            }
        }
    }

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        digit   = [0-9];
        hex     = [0-9a-fA-F];
        bare    = [a-zA-Z0-9_-]+;
        whitespace = [ \t]+;

        // Skip whitespace
        whitespace {
            lexer->column += (int)(YYCURSOR - token_start);
            goto lex_start;
        }

        // Comments
        "#" [^\n\r]* {
            lexer->column += (int)(YYCURSOR - token_start);
            goto lex_start;
        }

        // End of input
        $ {
            token->type = 0;
            token->pos.line = lexer->line;
            token->pos.col = lexer->column;
            lexer->cursor = YYCURSOR;
            return 0;
        }

        // Structural characters
        "." { token->type = TOML_TOKEN_DOT; goto tok_done; }
        "," { token->type = TOML_TOKEN_COMMA; goto tok_done; }
        "=" { token->type = TOML_TOKEN_EQUAL; goto tok_done; }
        "{" { token->type = TOML_TOKEN_LBRACE; goto tok_done; }
        "}" { token->type = TOML_TOKEN_RBRACE; goto tok_done; }
        "[" { token->type = TOML_TOKEN_LBRACKET; goto tok_done; }
        "]" { token->type = TOML_TOKEN_RBRACKET; goto tok_done; }
        "\n" | "\r\n" | "\r" {
            token->type = TOML_TOKEN_NEWLINE;
            token->pos.line = lexer->line;
            token->pos.col = lexer->column;
            lexer->line++;
            lexer->column = 1;
            lexer->cursor = YYCURSOR;
            return 1;
        }

        // Keywords
        "true"  { token->type = TOML_TOKEN_VAL_BOOL; goto tok_done; }
        "false" { token->type = TOML_TOKEN_VAL_BOOL; goto tok_done; }

        // Local Time
        digit{2} ":" digit{2} ":" digit{2} ("." digit+)? {
            token->type = TOML_TOKEN_VAL_DATETIME; goto tok_done;
        }

        // Offset Date-Time, Local Date-Time, and Local Date
        digit{4} "-" digit{2} "-" digit{2} ([Tt ] digit{2} ":" digit{2} ":" digit{2} ("." digit+)? ([Zz]|[+-]digit{2}":"digit{2})?)? {
            token->type = TOML_TOKEN_VAL_DATETIME; goto tok_done;
        }

        // Floats
        [+-]? (digit+ ("." digit*)? | "." digit+) ([eE] [+-]? digit+)? { token->type = TOML_TOKEN_VAL_FLOAT; goto tok_done; }

        // Integers
        "0x" hex+ { token->type = TOML_TOKEN_VAL_INT; goto tok_done; }
        "0o" [0-7]+ { token->type = TOML_TOKEN_VAL_INT; goto tok_done; }
        "0b" [01]+ { token->type = TOML_TOKEN_VAL_INT; goto tok_done; }
        [+-]? digit+ { token->type = TOML_TOKEN_VAL_INT; goto tok_done; }

        // Multi-line basic strings
        "\"\"\"" ([^"] | "\"" [^"] | "\"\"" [^"])* "\"\"\"" { token->type = TOML_TOKEN_STRING; goto tok_done; }
        // Multi-line literal strings
        "'''" ([^'] | "'" [^'] | "''" [^'])* "'''" { token->type = TOML_TOKEN_STRING; goto tok_done; }

        // Strings
        "\"" ([^"\\\n] | "\\" [btnfr"\\/]) * "\"" { token->type = TOML_TOKEN_STRING; goto tok_done; }
        "'" [^'\n]* "'" { token->type = TOML_TOKEN_STRING; goto tok_done; }

        // Unterminated strings
        "\"" ([^"\\\n\r] | "\\" [btnfr"\\/])* {
            token->type = TOML_TOKEN_ERROR;
            snprintf(lexer->error_msg, sizeof(lexer->error_msg), "unterminated quote (\")");
            token->pos.line = lexer->line;
            token->pos.col = lexer->column + (int)(YYCURSOR - token_start) - 1;
            if (token->pos.col < 1) token->pos.col = 1;
            lexer->cursor = YYCURSOR;
            return -1;
        }
        "'" [^'\n\r]* {
            token->type = TOML_TOKEN_ERROR;
            snprintf(lexer->error_msg, sizeof(lexer->error_msg), "unterminated quote (')");
            token->pos.line = lexer->line;
            token->pos.col = lexer->column + (int)(YYCURSOR - token_start) - 1;
            if (token->pos.col < 1) token->pos.col = 1;
            lexer->cursor = YYCURSOR;
            return -1;
        }

        // Bare Key
        bare { token->type = TOML_TOKEN_KEY; goto tok_done; }

        // Invalid character
        * {
            token->type = TOML_TOKEN_ERROR;
            snprintf(lexer->error_msg, sizeof(lexer->error_msg), "invalid character");
            token->pos.line = lexer->line;
            token->pos.col = lexer->column;
            lexer->cursor = YYCURSOR;
            return -1;
        }
    */
tok_done:
    token->value = (char *)token_start;
    token->len = (size_t)(YYCURSOR - token_start);
    token->pos.line = lexer->line;
    token->pos.col = lexer->column;
    lexer->cursor = YYCURSOR;
    lexer->column += (int)token->len;
    return 1;
}

void toml_lexer_init(toml_lexer_t *lexer, const char *input, size_t len) {
    lexer->start = input;
    lexer->cursor = input;
    lexer->limit = input + len;
    lexer->line = 1;
    lexer->column = 1;
}
