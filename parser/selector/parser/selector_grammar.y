%name SelectorV1Parse
%token_prefix SELECTOR_V1_TOKEN_
%token_type {selector_semantic_t}
%default_type {selector_semantic_t}
%stack_size 256

%extra_argument {selector_parse_ctx_t *ctx}
%token_destructor { (void)ctx; (void)$$; }

%include {
#include "selector_internal.h"
}

%left OR.
%left AND.
%right BANG.

%token AND OR EQ NE BANG LPAREN RPAREN LBRACKET RBRACKET COMMA IN NOT HAS CAPABILITY STRING FIELD.

%start_symbol input

input ::= or_expr(A). { ctx->root = A.node; }

or_expr(A) ::= or_expr(B) OR and_expr(C).
  { A = selector_make_binary(ctx, SELECTOR_NODE_OR, B, C); }
or_expr(A) ::= and_expr(B). { A = B; }

and_expr(A) ::= and_expr(B) AND unary_expr(C).
  { A = selector_make_binary(ctx, SELECTOR_NODE_AND, B, C); }
and_expr(A) ::= unary_expr(B). { A = B; }

unary_expr(A) ::= BANG unary_expr(B).
  { A = selector_make_not(ctx, B); }
unary_expr(A) ::= LPAREN or_expr(B) RPAREN. { A = B; }
unary_expr(A) ::= predicate(B). { A = B; }

predicate(A) ::= FIELD(F) EQ STRING(V).
  { A = selector_make_compare(ctx, SELECTOR_NODE_EQ, F, V); }
predicate(A) ::= FIELD(F) NE STRING(V).
  { A = selector_make_compare(ctx, SELECTOR_NODE_NE, F, V); }
predicate(A) ::= FIELD(F) IN string_list(L).
  { A = selector_make_membership(ctx, SELECTOR_NODE_IN, F, L); }
predicate(A) ::= FIELD(F) NOT IN string_list(L).
  { A = selector_make_membership(ctx, SELECTOR_NODE_NOT_IN, F, L); }
predicate(A) ::= HAS LPAREN FIELD(F) RPAREN.
  { A = selector_make_has(ctx, F); }
predicate(A) ::= CAPABILITY LPAREN STRING(V) RPAREN.
  { A = selector_make_capability(ctx, V); }

string_list(A) ::= LBRACKET list_items(B) RBRACKET. { A = B; }
list_items(A) ::= STRING(V). { A = selector_list_first(ctx, V); }
list_items(A) ::= list_items(B) COMMA STRING(V).
  { A = selector_list_append(ctx, B, V); }

%syntax_error {
  selector_ctx_fail(ctx, SALTS_SELECTOR_SYNTAX_ERROR, ctx->current_offset,
                    "selector syntax error");
}

%parse_failure {
  selector_ctx_fail(ctx, SALTS_SELECTOR_SYNTAX_ERROR, ctx->current_offset,
                    "selector parse failed");
}

%stack_overflow {
  selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, ctx->current_offset,
                    "selector parser stack limit exceeded");
}
