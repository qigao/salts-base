// re2c -o cyaml_ypath_lexer_gen.c cyaml_ypath_lexer.re

#include "cyaml_internal.h"
#include "cyaml_ypath_lexer.h"

#include <math.h>

#define YPATH_MAX_EXPONENT 308

static void ypath_lex_number(ypath_lexer_t* lexer, const char* start, const char* end)
{
    const char* cursor = start;
    bool neg = (*cursor == '-');
    if (neg)
        cursor++;

    bool overflow = false;
    uint64_t uval = cyaml_parse_u64_n(&cursor, end, &overflow);

    if (cursor < end && (*cursor == '.' || *cursor == 'e' || *cursor == 'E')) {
        double fval = (double)uval;
        if (*cursor == '.') {
            cursor++;
            double frac = 0.1;
            while (cursor < end && CYAML_IS_DIGIT(*cursor)) {
                fval += (*cursor++ - '0') * frac;
                frac *= 0.1;
            }
        }
        if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
            cursor++;
            int exp_sign = 1;
            if (cursor < end && (*cursor == '+' || *cursor == '-'))
                exp_sign = (*cursor++ == '-') ? -1 : 1;
            bool exp_overflow = false;
            uint64_t exp_val = cyaml_parse_u64_n(&cursor, end, &exp_overflow);
            if (exp_overflow || exp_val > YPATH_MAX_EXPONENT) {
                lexer->tok.type = YPATH_TOK_ERROR;
                lexer->error = "exponent overflow";
                return;
            }
            fval *= pow(10.0, exp_sign * (int)exp_val);
        }
        lexer->tok.type = YPATH_TOK_FLOAT;
        lexer->tok.val.f = neg ? -fval : fval;
    } else {
        lexer->tok.type = YPATH_TOK_INT;
        if (overflow)
            lexer->tok.val.i = neg ? INT64_MIN : INT64_MAX;
        else if (neg)
            lexer->tok.val.i = (uval > (uint64_t)INT64_MAX + 1) ? INT64_MIN : -(int64_t)uval;
        else
            lexer->tok.val.i = (uval > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)uval;
    }
}

void ypath_lex_init(ypath_lexer_t* lexer, const char* path)
{
    lexer->src = lexer->cur = path;
    lexer->end = path + strlen(path);
    lexer->error = NULL;
    lexer->in_filter = false;
    lexer->tok = (ypath_token_t) { YPATH_TOK_EOF, path, 0, { 0 } };
}

void ypath_lex_next(ypath_lexer_t* lexer)
{
    const char* YYCURSOR = lexer->cur;
    const char* YYMARKER;
    const char* YYLIMIT = lexer->end;
    const char* token_start;

scan:
    token_start = YYCURSOR;

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        digit = [0-9];
        ident = [A-Za-z_] [A-Za-z0-9_-]*;
        number = "-"? digit+ ("." digit*)? ([eE] [+-]? digit*)?;
        dqchar = [^\x00"\\] | "\\" [^\x00];
        sqchar = [^\x00'\\] | "\\" [^\x00];
        dstring = "\"" dqchar* "\"";
        sstring = "'" sqchar* "'";
        unterminated_dstring = "\"" dqchar*;
        unterminated_sstring = "'" sqchar*;

        [ \t]+ {
            goto scan;
        }

        $ {
            lexer->tok = (ypath_token_t) { YPATH_TOK_EOF, YYCURSOR, 0, { 0 } };
            lexer->cur = YYCURSOR;
            return;
        }

        "/" {
            lexer->tok.type = lexer->in_filter ? YPATH_TOK_DIV : YPATH_TOK_SLASH;
            goto token_done;
        }
        ".." { lexer->tok.type = YPATH_TOK_DOTDOT; goto token_done; }
        "." { lexer->tok.type = YPATH_TOK_DOT; goto token_done; }
        "**" { lexer->tok.type = YPATH_TOK_STARSTAR; goto token_done; }
        "*" { lexer->tok.type = YPATH_TOK_STAR; goto token_done; }
        "[" { lexer->tok.type = YPATH_TOK_LBRACKET; goto token_done; }
        "]" { lexer->tok.type = YPATH_TOK_RBRACKET; goto token_done; }
        "(" { lexer->tok.type = YPATH_TOK_LPAREN; goto token_done; }
        ")" { lexer->tok.type = YPATH_TOK_RPAREN; goto token_done; }
        ":" { lexer->tok.type = YPATH_TOK_COLON; goto token_done; }
        "?" { lexer->tok.type = YPATH_TOK_QUESTION; goto token_done; }
        "@" { lexer->tok.type = YPATH_TOK_AT; goto token_done; }
        "+" { lexer->tok.type = YPATH_TOK_PLUS; goto token_done; }
        "-" { lexer->tok.type = YPATH_TOK_MINUS; goto token_done; }
        "^" { lexer->tok.type = YPATH_TOK_CARET; goto token_done; }
        "~" { lexer->tok.type = YPATH_TOK_TILDE; goto token_done; }
        "," { lexer->tok.type = YPATH_TOK_COMMA; goto token_done; }
        "||" { lexer->tok.type = YPATH_TOK_OR; goto token_done; }
        "&&" { lexer->tok.type = YPATH_TOK_AND; goto token_done; }
        "==" { lexer->tok.type = YPATH_TOK_EQ; goto token_done; }
        "!=" { lexer->tok.type = YPATH_TOK_NE; goto token_done; }
        "!" { lexer->tok.type = YPATH_TOK_BANG; goto token_done; }
        "<<" { lexer->tok.type = YPATH_TOK_LSHIFT; goto token_done; }
        ">>" { lexer->tok.type = YPATH_TOK_RSHIFT; goto token_done; }
        "<=" { lexer->tok.type = YPATH_TOK_LE; goto token_done; }
        "<" { lexer->tok.type = YPATH_TOK_LT; goto token_done; }
        ">=" { lexer->tok.type = YPATH_TOK_GE; goto token_done; }
        ">" { lexer->tok.type = YPATH_TOK_GT; goto token_done; }

        "|" { lexer->tok.type = YPATH_TOK_BOR; goto token_done; }
        "&" { lexer->tok.type = YPATH_TOK_BAND; goto token_done; }
        "=" {
            lexer->tok.type = YPATH_TOK_ERROR;
            lexer->error = "expected ==";
            goto token_done;
        }

        "true" { lexer->tok.type = YPATH_TOK_TRUE; goto token_done; }
        "false" { lexer->tok.type = YPATH_TOK_FALSE; goto token_done; }
        "null" { lexer->tok.type = YPATH_TOK_NULL; goto token_done; }
        "idiv" {
            lexer->tok.type = lexer->in_filter ? YPATH_TOK_IDIV : YPATH_TOK_IDENT;
            goto token_done;
        }
        "matches" {
            lexer->tok.type = lexer->in_filter ? YPATH_TOK_MATCHES : YPATH_TOK_IDENT;
            goto token_done;
        }

        dstring | sstring {
            lexer->tok.type = YPATH_TOK_STRING;
            lexer->tok.start = token_start + 1;
            lexer->tok.len = (uint32_t)(YYCURSOR - token_start - 2);
            lexer->cur = YYCURSOR;
            return;
        }
        unterminated_dstring | unterminated_sstring {
            lexer->tok.type = YPATH_TOK_STRING;
            lexer->tok.start = token_start + 1;
            lexer->tok.len = (uint32_t)(YYCURSOR - token_start - 1);
            lexer->cur = YYCURSOR;
            return;
        }

        number {
            ypath_lex_number(lexer, token_start, YYCURSOR);
            goto token_done;
        }
        ident {
            lexer->tok.type = YPATH_TOK_IDENT;
            goto token_done;
        }

        * {
            lexer->tok.type = YPATH_TOK_ERROR;
            lexer->error = "unexpected character";
            goto token_done;
        }
    */

token_done:
    lexer->tok.start = token_start;
    lexer->tok.len = (uint32_t)(YYCURSOR - token_start);
    lexer->cur = YYCURSOR;
}
