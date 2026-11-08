// re2c $INPUT -o $OUTPUT
#include "dsv_filter_expr_lexer.h"
#include "dsv_filter_expr_grammar_gen.h"
#include <stdio.h>

void dsv_expr_lexer_init(dsv_expr_lexer_t *lexer, const char *input, size_t len) {
    if (!lexer) return;
    lexer->input = input;
    lexer->cursor = input;
    lexer->limit = input + len;
    lexer->marker = input;
    lexer->error[0] = '\0';
}

int dsv_expr_lexer_next(dsv_expr_lexer_t *lexer, dsv_expr_token_t *token) {
    const char *YYCURSOR;
    const char *YYMARKER;
    const char *YYLIMIT;
    const char *token_start;

    if (!lexer || !token) return -1;
    YYCURSOR = lexer->cursor;
    YYMARKER = lexer->marker;
    YYLIMIT = lexer->limit;

again:
    token->type = 0;
    token->value = NULL;
    token->length = 0;
    token_start = YYCURSOR;

    /*!re2c
        re2c:define:YYCTYPE = "unsigned char";
        re2c:yyfill:enable = 0;
        re2c:eof = 0;

        ws = [\x09\x0a\x0d\x20]+;
        ident = [A-Za-z_][A-Za-z0-9_]*;
        number = [0-9]+ ("." [0-9]+)?;

        $ {
            token->type = 0;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 0;
        }

        ws {
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            goto again;
        }

        ident {
            token->type = DSV_EXPR_TOKEN_IDENT;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        number {
            token->type = DSV_EXPR_TOKEN_NUMBER;
            token->value = token_start;
            token->length = (size_t)(YYCURSOR - token_start);
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        "+" {
            token->type = DSV_EXPR_TOKEN_PLUS;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        "-" {
            token->type = DSV_EXPR_TOKEN_MINUS;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        "*" {
            token->type = DSV_EXPR_TOKEN_MUL;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        "/" {
            token->type = DSV_EXPR_TOKEN_DIV;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        "(" {
            token->type = DSV_EXPR_TOKEN_LPAREN;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        ")" {
            token->type = DSV_EXPR_TOKEN_RPAREN;
            token->value = token_start;
            token->length = 1;
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return 1;
        }

        * {
            snprintf(lexer->error, sizeof(lexer->error), "invalid character in expression");
            lexer->cursor = YYCURSOR;
            lexer->marker = YYMARKER;
            return -1;
        }
    */
}
