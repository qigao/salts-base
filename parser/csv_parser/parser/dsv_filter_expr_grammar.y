%name DsvExprParse
%token_prefix DSV_EXPR_TOKEN_
%token_type {dsv_expr_token_t}
%default_type {int}
%stack_size 256

%extra_argument {dsv_expr_parse_ctx_t *ctx}

%include {
#include "dsv_filter_expr_parser.h"
}

%token IDENT NUMBER PLUS MINUS MUL DIV LPAREN RPAREN.

%start_symbol input

input ::= expr.

expr ::= add_expr.

add_expr ::= mul_expr.
add_expr ::= add_expr PLUS mul_expr. {
    if (!dsv_expr_ctx_push_op(ctx, DSV_EXPR_ITEM_ADD)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}
add_expr ::= add_expr MINUS mul_expr. {
    if (!dsv_expr_ctx_push_op(ctx, DSV_EXPR_ITEM_SUB)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}

mul_expr ::= unary_expr.
mul_expr ::= mul_expr MUL unary_expr. {
    if (!dsv_expr_ctx_push_op(ctx, DSV_EXPR_ITEM_MUL)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}
mul_expr ::= mul_expr DIV unary_expr. {
    if (!dsv_expr_ctx_push_op(ctx, DSV_EXPR_ITEM_DIV)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}

unary_expr ::= primary.
unary_expr ::= PLUS unary_expr.
unary_expr ::= MINUS unary_expr. {
    if (!dsv_expr_ctx_push_op(ctx, DSV_EXPR_ITEM_NEG)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}

primary ::= IDENT(T). {
    if (!dsv_expr_ctx_push_ident(ctx, T.value, T.length)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}
primary ::= NUMBER(T). {
    if (!dsv_expr_ctx_push_number(ctx, T.value, T.length)) {
        dsv_expr_ctx_set_error(ctx, "expression build failed");
    }
}
primary ::= LPAREN expr RPAREN.

%syntax_error {
    dsv_expr_ctx_set_error(ctx, "expression syntax error");
}

%parse_failure {
    dsv_expr_ctx_set_error(ctx, "expression parse failure");
}
