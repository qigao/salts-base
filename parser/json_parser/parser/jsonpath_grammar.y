/**
 * @file jsonpath_grammar.y
 * @brief JSONPath Parser Grammar (Lemon)
 */

%name JsonPathParse
%token_prefix JSONPATH_TOKEN_
%token_type {jsonpath_opcode_t *}
%default_type {jsonpath_opcode_t *}
%stack_size 512

%extra_argument {jsonpath_parse_ctx_t *ctx}

%left AND.
%left OR.
%left UNION.
%nonassoc EQ NE GT GE LT LE MATCH.
%right NOT.

%include {
#include "jsonpath_types.h"
#include <stddef.h>

#define alloc_op(type, num, number, str, ...) \
  jsonpath_alloc_op(ctx, type, num, number, str, ##__VA_ARGS__, NULL)

static jsonpath_opcode_t *jsonpath_slice_op(jsonpath_parse_ctx_t *ctx,
                                            jsonpath_opcode_t *start,
                                            jsonpath_opcode_t *end,
                                            jsonpath_opcode_t *step) {
  jsonpath_opcode_t *op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_SLICE, 0, 0.0, NULL, NULL);
  if (!op) return NULL;
  if (start) {
    op->num = start->num;
    op->slice_mask |= JSONPATH_SLICE_HAS_START;
  }
  if (end) {
    op->num2 = end->num;
    op->slice_mask |= JSONPATH_SLICE_HAS_END;
  }
  if (step) {
    op->num3 = step->num;
    op->slice_mask |= JSONPATH_SLICE_HAS_STEP;
  }
  return op;
}
}

%token AND OR UNION EQ NE GT GE LT LE MATCH CONTAINS CONTAINS_CI NOT LENGTH COUNT MATCHFUNC SEARCHFUNC.
%token LABEL ROOT THIS DOT DESCENDANT WILDCARD REGEXP BROPEN BRCLOSE BOOL NUMBER STRING POPEN PCLOSE COLON QMARK.

%start_symbol input

input ::= expr(A). { ctx->path = A; }

expr(A) ::= LABEL(B) EQ path(C). { A = B; B->down = C; }
expr(A) ::= path(B).             { A = B; }

path(A) ::= ROOT segments(B). { A = alloc_op(JSONPATH_TOKEN_ROOT, 0, 0.0, NULL, B); }
path(A) ::= THIS segments(B). { A = alloc_op(JSONPATH_TOKEN_THIS, 0, 0.0, NULL, B); }
path(A) ::= ROOT(B).         { A = B; }
path(A) ::= THIS(B).         { A = B; }

segments(A) ::= segments(B) segment(C). { A = jsonpath_append_op(B, C); }
segments(A) ::= segment(B).             { A = B; }

segment(A) ::= DOT LABEL(B).                  { A = B; }
segment(A) ::= DOT WILDCARD(B).               { A = B; }
segment(A) ::= BROPEN union_exps(B) BRCLOSE.  { A = B; if (B) B->is_selector = 1; }
segment(A) ::= BROPEN slice_exp(B) BRCLOSE.   { A = B; }
segment(A) ::= BROPEN QMARK or_exps(B) BRCLOSE. { A = B; if (B) B->is_selector = 1; }
segment(A) ::= DESCENDANT LABEL(B).           { A = alloc_op(JSONPATH_TOKEN_DESCENDANT, 0, 0.0, NULL, B); }
segment(A) ::= DESCENDANT WILDCARD(B).        { A = alloc_op(JSONPATH_TOKEN_DESCENDANT, 0, 0.0, NULL, B); }
segment(A) ::= DESCENDANT BROPEN union_exps(B) BRCLOSE.
    { A = alloc_op(JSONPATH_TOKEN_DESCENDANT, 0, 0.0, NULL, B); }

union_exps(A) ::= union_exp(B). { A = B->sibling ? alloc_op(JSONPATH_TOKEN_UNION, 0, 0.0, NULL, B) : B; }

union_exp(A) ::= union_exp(B) UNION or_exps(C). { A = jsonpath_append_op(B, C); }
union_exp(A) ::= or_exps(B).                    { A = B; }

or_exps(A) ::= or_exp(B). { A = B->sibling ? alloc_op(JSONPATH_TOKEN_OR, 0, 0.0, NULL, B) : B; }

or_exp(A) ::= or_exp(B) OR and_exps(C). { A = jsonpath_append_op(B, C); }
or_exp(A) ::= and_exps(B).              { A = B; }

and_exps(A) ::= and_exp(B). { A = B->sibling ? alloc_op(JSONPATH_TOKEN_AND, 0, 0.0, NULL, B) : B; }

and_exp(A) ::= and_exp(B) AND cmp_exp(C). { A = jsonpath_append_op(B, C); }
and_exp(A) ::= cmp_exp(B).                { A = B; }

cmp_exp(A) ::= unary_exp(B) LT unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_LT, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) LE unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_LE, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) GT unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_GT, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) GE unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_GE, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) EQ unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_EQ, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) NE unary_exp(C).    { A = alloc_op(JSONPATH_TOKEN_NE, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B) MATCH unary_exp(C). { A = alloc_op(JSONPATH_TOKEN_MATCH, 0, 0.0, NULL, B, C); }
cmp_exp(A) ::= unary_exp(B).                    { A = B; }

unary_exp(A) ::= BOOL(B).                 { A = B; }
unary_exp(A) ::= NUMBER(B).               { A = B; }
unary_exp(A) ::= STRING(B).               { A = B; }
unary_exp(A) ::= REGEXP(B).               { A = B; }
unary_exp(A) ::= WILDCARD(B).             { A = B; }
unary_exp(A) ::= POPEN or_exps(B) PCLOSE. { A = B; }
unary_exp(A) ::= LENGTH POPEN or_exps(B) PCLOSE. { A = alloc_op(JSONPATH_TOKEN_LENGTH, 0, 0.0, NULL, B); }
unary_exp(A) ::= LENGTH POPEN PCLOSE.             { A = alloc_op(JSONPATH_TOKEN_LENGTH, 0, 0.0, NULL, NULL); }
unary_exp(A) ::= COUNT POPEN or_exps(B) PCLOSE.   { A = alloc_op(JSONPATH_TOKEN_COUNT, 0, 0.0, NULL, B); }
unary_exp(A) ::= MATCHFUNC POPEN or_exps(B) UNION or_exps(C) PCLOSE.
    { A = alloc_op(JSONPATH_TOKEN_MATCHFUNC, 0, 0.0, NULL, B, C); }
unary_exp(A) ::= SEARCHFUNC POPEN or_exps(B) UNION or_exps(C) PCLOSE.
    { A = alloc_op(JSONPATH_TOKEN_SEARCHFUNC, 0, 0.0, NULL, B, C); }
unary_exp(A) ::= CONTAINS POPEN or_exps(B) UNION or_exps(C) PCLOSE.
    { A = alloc_op(JSONPATH_TOKEN_CONTAINS, 0, 0.0, NULL, B, C); }
unary_exp(A) ::= CONTAINS_CI POPEN or_exps(B) UNION or_exps(C) PCLOSE.
    { A = alloc_op(JSONPATH_TOKEN_CONTAINS_CI, 0, 0.0, NULL, B, C); }
unary_exp(A) ::= NOT unary_exp(B).        { A = alloc_op(JSONPATH_TOKEN_NOT, 0, 0.0, NULL, B); }
unary_exp(A) ::= path(B).                 { A = B; }

slice_exp(A) ::= slice_opt(B) COLON slice_opt(C).
    { A = jsonpath_slice_op(ctx, B, C, NULL); }
slice_exp(A) ::= slice_opt(B) COLON slice_opt(C) COLON slice_opt(D).
    { A = jsonpath_slice_op(ctx, B, C, D); }
slice_opt(A) ::= NUMBER(B). { A = B; }
slice_opt(A) ::= .          { A = NULL; }

%syntax_error {
  ctx->error_code = 1;
  ctx->error_pos = ctx->off;
}

%parse_failure {
  ctx->error_code = 1;
  ctx->error_pos = ctx->off;
}
