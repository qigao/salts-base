/**
 * @file datetime_lexer.re
 * @brief Date-Time Lexer using re2c
 */

#include "datetime_lexer.h"
#include "datetime_grammar_gen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int parse_int(const char *start, const char *end) {
    int val = 0;
    while (start < end) {
        val = val * 10 + (*start - '0');
        start++;
    }
    return val;
}

int datetime_lexer_next(datetime_lexer_t *lexer, datetime_token_t *token) {
    if (!lexer || !token) return -1;

    const char *YYCURSOR = lexer->cursor;
    const char *YYMARKER;
    const char *YYLIMIT = lexer->limit;
    const char *token_start;

    token->value = NULL;
    token->length = 0;
    token->int_value = 0;

    token_start = YYCURSOR;

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        digit = [0-9];

        // End of input
        $ {
            token->type = 0;
            lexer->cursor = YYCURSOR;
            return 0;
        }

        "," { token->type = DATETIME_TOKEN_COMMA; goto token_done; }
        ":" { token->type = DATETIME_TOKEN_COLON; goto token_done; }
        "-" { token->type = DATETIME_TOKEN_HYPHEN; goto token_done; }
        "/" { token->type = DATETIME_TOKEN_SLASH; goto token_done; }
        "." { token->type = DATETIME_TOKEN_DOT; goto token_done; }
        " " { token->type = DATETIME_TOKEN_SPACE; goto token_done; }
        "T" { token->type = DATETIME_TOKEN_T; goto token_done; }
        "+" { token->type = DATETIME_TOKEN_PLUS; goto token_done; }

        "Sun" | "Sunday"    { token->type = DATETIME_TOKEN_SUN; goto token_done; }
        "Mon" | "Monday"    { token->type = DATETIME_TOKEN_MON; goto token_done; }
        "Tue" | "Tuesday"   { token->type = DATETIME_TOKEN_TUE; goto token_done; }
        "Wed" | "Wednesday" { token->type = DATETIME_TOKEN_WED; goto token_done; }
        "Thu" | "Thursday"  { token->type = DATETIME_TOKEN_THU; goto token_done; }
        "Fri" | "Friday"    { token->type = DATETIME_TOKEN_FRI; goto token_done; }
        "Sat" | "Saturday"  { token->type = DATETIME_TOKEN_SAT; goto token_done; }

        "Jan" | "January"   { token->type = DATETIME_TOKEN_JAN; goto token_done; }
        "Feb" | "February"  { token->type = DATETIME_TOKEN_FEB; goto token_done; }
        "Mar" | "March"     { token->type = DATETIME_TOKEN_MAR; goto token_done; }
        "Apr" | "April"     { token->type = DATETIME_TOKEN_APR; goto token_done; }
        "May"               { token->type = DATETIME_TOKEN_MAY; goto token_done; }
        "Jun" | "June"      { token->type = DATETIME_TOKEN_JUN; goto token_done; }
        "Jul" | "July"      { token->type = DATETIME_TOKEN_JUL; goto token_done; }
        "Aug" | "August"    { token->type = DATETIME_TOKEN_AUG; goto token_done; }
        "Sep" | "September" { token->type = DATETIME_TOKEN_SEP; goto token_done; }
        "Oct" | "October"   { token->type = DATETIME_TOKEN_OCT; goto token_done; }
        "Nov" | "November"  { token->type = DATETIME_TOKEN_NOV; goto token_done; }
        "Dec" | "December"  { token->type = DATETIME_TOKEN_DEC; goto token_done; }

        "GMT" | "UTC" | "Z" { token->type = DATETIME_TOKEN_TZ_CONST; goto token_done; }

        digit{9} {
            token->type = DATETIME_TOKEN_DIGIT9;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        digit{8} {
            token->type = DATETIME_TOKEN_DIGIT8;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        digit{6} {
            token->type = DATETIME_TOKEN_DIGIT6;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        digit{4} {
            token->type = DATETIME_TOKEN_DIGIT4;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        digit{3} {
            token->type = DATETIME_TOKEN_DIGIT3;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        digit{1,2} {
            token->type = DATETIME_TOKEN_DIGIT12;
            token->int_value = parse_int(token_start, YYCURSOR);
            goto token_done;
        }

        // Invalid character
        * {
            token->type = -1;
            lexer->cursor = YYCURSOR;
            return -1;
        }
    */

token_done:
    token->value = token_start;
    token->length = (size_t)(YYCURSOR - token_start);
    lexer->cursor = YYCURSOR;
    return 1;
}

void datetime_lexer_init(datetime_lexer_t *lexer, const char *input, size_t length) {
    if (!lexer) return;
    lexer->input = input;
    lexer->cursor = input;
    lexer->limit = input + length;
}
