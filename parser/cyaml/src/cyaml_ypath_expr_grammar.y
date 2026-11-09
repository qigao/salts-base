%name YPathExprParse
%token_prefix YPATH_EXPR_TOKEN_
%token_type {ypath_expr_token_t}
%default_type {ypath_expr_t*}
%stack_size 64

%extra_argument {ypath_expr_parse_ctx_t *ctx}

%include {
#include "cyaml_ypath_expr_parser.h"
}

%token PRIMARY OR AND EQ NE LT LE GT GE PLUS MINUS MUL DIV BANG LPAREN RPAREN
       BAND BOR CARET LSHIFT RSHIFT TILDE QUESTION COLON COMMA IDIV MATCHES.

%start_symbol input

input ::= expr(A). { ctx->root = A; }

expr(A) ::= conditional_expr(B). { A = B; }

conditional_expr(A) ::= or_expr(B). { A = B; }
conditional_expr(A) ::= or_expr(B) QUESTION conditional_expr(C) COLON conditional_expr(D). {
    A = cyaml_ypath_expr_make_cond(ctx, B, C, D);
}

or_expr(A) ::= or_expr(B) OR and_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_OR, B, C);
}
or_expr(A) ::= and_expr(B). { A = B; }

and_expr(A) ::= and_expr(B) AND equality_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_AND, B, C);
}
and_expr(A) ::= equality_expr(B). { A = B; }

equality_expr(A) ::= equality_expr(B) EQ bitor_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_EQ, B, C);
}
equality_expr(A) ::= equality_expr(B) NE bitor_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_NE, B, C);
}
equality_expr(A) ::= bitor_expr(B). { A = B; }

bitor_expr(A) ::= bitor_expr(B) BOR bitxor_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_BOR, B, C);
}
bitor_expr(A) ::= bitxor_expr(B). { A = B; }

bitxor_expr(A) ::= bitxor_expr(B) CARET bitand_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_BXOR, B, C);
}
bitxor_expr(A) ::= bitand_expr(B). { A = B; }

bitand_expr(A) ::= bitand_expr(B) BAND relational_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_BAND, B, C);
}
bitand_expr(A) ::= relational_expr(B). { A = B; }

relational_expr(A) ::= relational_expr(B) LT shift_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_LT, B, C);
}
relational_expr(A) ::= relational_expr(B) LE shift_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_LE, B, C);
}
relational_expr(A) ::= relational_expr(B) GT shift_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_GT, B, C);
}
relational_expr(A) ::= relational_expr(B) GE shift_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_GE, B, C);
}
relational_expr(A) ::= shift_expr(B). { A = B; }

shift_expr(A) ::= shift_expr(B) LSHIFT additive_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_LSHIFT, B, C);
}
shift_expr(A) ::= shift_expr(B) RSHIFT additive_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_RSHIFT, B, C);
}
shift_expr(A) ::= additive_expr(B). { A = B; }

additive_expr(A) ::= additive_expr(B) PLUS multiplicative_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_ADD, B, C);
}
additive_expr(A) ::= additive_expr(B) MINUS multiplicative_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_SUB, B, C);
}
additive_expr(A) ::= multiplicative_expr(B). { A = B; }

multiplicative_expr(A) ::= multiplicative_expr(B) MUL unary_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_MUL, B, C);
}
multiplicative_expr(A) ::= multiplicative_expr(B) DIV unary_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_DIV, B, C);
}
multiplicative_expr(A) ::= multiplicative_expr(B) IDIV unary_expr(C). {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_IDIV, B, C);
}
multiplicative_expr(A) ::= unary_expr(B). { A = B; }

unary_expr(A) ::= MINUS unary_expr(B). {
    A = cyaml_ypath_expr_make_unary(ctx, YPATH_OP_NEG, B);
}
unary_expr(A) ::= BANG unary_expr(B). {
    A = cyaml_ypath_expr_make_unary(ctx, YPATH_OP_NOT, B);
}
unary_expr(A) ::= TILDE unary_expr(B). {
    A = cyaml_ypath_expr_make_unary(ctx, YPATH_OP_BNOT, B);
}
unary_expr(A) ::= primary(B). { A = B; }

primary(A) ::= PRIMARY(B). { A = B.expr; }
primary(A) ::= LPAREN expr(B) RPAREN. { A = B; }
primary(A) ::= MATCHES LPAREN expr(B) COMMA expr(C) RPAREN. {
    A = cyaml_ypath_expr_make_binary(ctx, YPATH_OP_MATCHES, B, C);
}

%syntax_error {
    cyaml_ypath_expr_set_error(ctx, "invalid expression");
}

%parse_failure {
    cyaml_ypath_expr_set_error(ctx, "invalid expression");
}

%stack_overflow {
    cyaml_ypath_expr_set_error(ctx, "expression too complex");
}
