// re2c $INPUT -o $OUTPUT
/**
 * @file csv_lexer.re
 * @brief CSV Lexer using re2c (RFC 4180 compliant)
 *
 * RFC 4180 Summary:
 * - Fields separated by commas
 * - Records separated by CRLF (also accepts LF)
 * - Fields may be quoted with double quotes
 * - Quoted fields may contain commas, newlines, and escaped quotes ("")
 *
 * This lexer is stateful to handle empty fields:
 * - Emits synthetic empty FIELD token between consecutive commas
 * - Emits empty FIELD at start of row before comma
 * - Emits empty FIELD at end of row after comma
 *
 * Build: re2c -o csv_lexer_gen.c csv_lexer.re
 */

#include "csv_lexer.h"
#include "csv_lexer_simde.h"
#include "csv_grammar_gen.h"
#include <string.h>
#include <stdio.h>

void csv_lexer_init(csv_lexer_t *lexer, const char *input, size_t length) {
    if (!lexer) return;
    lexer->input  = input;
    lexer->cursor = input;
    lexer->limit  = input + length;
    lexer->marker = input;
    lexer->line   = 1;
    lexer->column = 1;
    lexer->error[0] = '\0';
}

static inline int has_escaped_quote(const char *p, size_t len) {
    const char *end = p + len;
    while (p < end - 1) {
        if (p[0] == '"' && p[1] == '"') return 1;
        p++;
    }
    return 0;
}

// State tracking for empty fields
typedef enum {
    STATE_ROW_START,      // At start of row (or after newline)
    STATE_AFTER_FIELD,    // Just emitted a field
    STATE_AFTER_COMMA,    // Just emitted a comma, need to check for empty field
} lexer_state_t;

#ifdef _MSC_VER
static __declspec(thread) lexer_state_t g_state = STATE_ROW_START;
#else
static __thread lexer_state_t g_state = STATE_ROW_START;
#endif

int csv_lexer_next(csv_lexer_t *lexer, csv_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER = lexer->marker;
    const char *YYLIMIT  = lexer->limit;
    const char *token_start;

    token->value = NULL;
    token->length = 0;
    token->needs_unescape = 0;

    // Check for empty field conditions
    if (g_state == STATE_AFTER_COMMA) {
        // Peek at next character
        if (YYCURSOR >= YYLIMIT || *YYCURSOR == ',' || *YYCURSOR == '\n' || *YYCURSOR == '\r') {
            // Empty field: emit synthetic FIELD token
            token->type = CSV_TOKEN_FIELD;
            token->value = "";
            token->length = 0;
            token->needs_unescape = 0;
            g_state = STATE_AFTER_FIELD;
            return 1;
        }
    }

    if (g_state == STATE_ROW_START && YYCURSOR < YYLIMIT && *YYCURSOR == ',') {
        // Empty field at start of row
        token->type = CSV_TOKEN_FIELD;
        token->value = "";
        token->length = 0;
        token->needs_unescape = 0;
        g_state = STATE_AFTER_FIELD;
        return 1;
    }

    if (YYCURSOR >= YYLIMIT) {
        // End of input - check if we need trailing empty field
        if (g_state == STATE_AFTER_COMMA) {
            token->type = CSV_TOKEN_FIELD;
            token->value = "";
            token->length = 0;
            g_state = STATE_ROW_START;
            return 1;
        }
        token->type = 0;
        g_state = STATE_ROW_START;
        return 0;
    }

    token_start = YYCURSOR;

    const char *unquoted_end = csv_find_unquoted_field_end_simde(YYCURSOR, YYLIMIT);
    if (unquoted_end > YYCURSOR) {
        token->type = CSV_TOKEN_FIELD;
        token->value = token_start;
        token->length = (size_t)(unquoted_end - token_start);
        token->needs_unescape = 0;
        lexer->column += (int)token->length;
        lexer->cursor = unquoted_end;
        lexer->marker = YYMARKER;
        g_state = STATE_AFTER_FIELD;
        return 1;
    }

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        crlf    = "\r\n" | "\n";
        textdata = [^\x00\x2c\x22\x0d\x0a];
        unquoted = textdata+;
        quoted_char = [^\x00\x22] | "\x22\x22";
        quoted = "\x22" quoted_char* "\x22";

        $ {
            token->type = 0;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            g_state = STATE_ROW_START;
            return 0;
        }

        "," {
            token->type = CSV_TOKEN_COMMA;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            lexer->column++;
            g_state = STATE_AFTER_COMMA;
            return 1;
        }

        crlf {
            token->type = CSV_TOKEN_NEWLINE;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            lexer->line++;
            lexer->column = 1;
            g_state = STATE_ROW_START;
            return 1;
        }

        quoted {
            token->type = CSV_TOKEN_FIELD;
            token->value = token_start + 1;
            token->length = (size_t)(YYCURSOR - token_start - 2);
            token->needs_unescape = has_escaped_quote(token->value, token->length);
            for (const char *p = token_start; p < YYCURSOR; p++) {
                if (*p == '\n') {
                    lexer->line++;
                    lexer->column = 1;
                } else {
                    lexer->column++;
                }
            }
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            g_state = STATE_AFTER_FIELD;
            return 1;
        }

        unquoted {
            token->type = CSV_TOKEN_FIELD;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            token->needs_unescape = 0;
            lexer->column += (int)token->length;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            g_state = STATE_AFTER_FIELD;
            return 1;
        }

        * {
            snprintf(lexer->error, sizeof(lexer->error),
                     "Unexpected character 0x%02X at line %d, column %d",
                     (unsigned char)*token_start, lexer->line, lexer->column);
            token->type = 0;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return -1;
        }
    */
}

void csv_lexer_reset_state(void) {
    g_state = STATE_ROW_START;
}
