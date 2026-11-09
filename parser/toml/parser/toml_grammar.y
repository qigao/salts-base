/**
 * @file toml_grammar.y
 * @brief TOML Parser Grammar (Lemon)
 */

%name TomlParse
%token_prefix TOML_TOKEN_
%token_type {toml_token_t}

%extra_argument {toml_parse_ctx_t *ctx}

%include {
#include "toml_lexer.h"
#include "toml_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { void* ptr; int type; } any_val_t;

static void toml_add_simple_kv(toml_parse_ctx_t *ctx, toml_table_t *target, toml_path_t path, toml_token_t val) {
    toml_table_t *t = toml_helper_walk_path(ctx, target, path.tokens, path.count - 1, true);
    if (!t) return;
    toml_helper_add_keyval(ctx, t, path.tokens[path.count - 1], val);
}

static void toml_add_array_kv(toml_parse_ctx_t *ctx, toml_table_t *target, toml_path_t path, toml_array_t *arr) {
    toml_table_t *t = toml_helper_walk_path(ctx, target, path.tokens, path.count - 1, true);
    if (!t) return;
    toml_helper_add_array(ctx, t, path.tokens[path.count - 1], arr);
}

static void toml_add_table_kv(toml_parse_ctx_t *ctx, toml_table_t *target, toml_path_t path, toml_table_t *tbl) {
    toml_table_t *t = toml_helper_walk_path(ctx, target, path.tokens, path.count - 1, true);
    if (!t) return;
    toml_helper_add_inline_table(ctx, t, path.tokens[path.count - 1], tbl);
}
}

%start_symbol start

start ::= toml.

toml ::= statements.

statements ::= statements statement.
statements ::= .

statement ::= table_def.
statement ::= key_val_stmt.
statement ::= NEWLINE.

table_def ::= LBRACKET path(P) RBRACKET. {
    toml_table_t *parent = toml_helper_walk_path(ctx, ctx->root, P.tokens, P.count - 1, true);
    if (parent) {
        ctx->current_table = toml_helper_create_table(ctx, parent, P.tokens[P.count-1], false);
    }
}

table_def ::= LBRACKET LBRACKET path(P) RBRACKET RBRACKET. {
    toml_table_t *parent = toml_helper_walk_path(ctx, ctx->root, P.tokens, P.count - 1, true);
    if (parent) {
        ctx->current_table = toml_helper_create_table(ctx, parent, P.tokens[P.count-1], true);
    }
}

key_val_stmt ::= path(P) EQUAL any_value(V). {
    if (V.type == 0) toml_add_simple_kv(ctx, ctx->current_table, P, *((toml_token_t*)V.ptr));
    else if (V.type == 1) toml_add_array_kv(ctx, ctx->current_table, P, (toml_array_t*)V.ptr);
    else if (V.type == 2) toml_add_table_kv(ctx, ctx->current_table, P, (toml_table_t*)V.ptr);
    if (V.type == 0) free(V.ptr);
}

%type any_value {any_val_t}
any_value(A) ::= simple_value(V). {
    A.type = 0;
    A.ptr = malloc(sizeof(toml_token_t));
    memcpy(A.ptr, &V, sizeof(toml_token_t));
}
any_value(A) ::= array(V). { A.type = 1; A.ptr = V; }
any_value(A) ::= inline_table(V). { A.type = 2; A.ptr = V; }

%type simple_value {toml_token_t}
simple_value(A) ::= STRING(T). { A = T; }
simple_value(A) ::= VAL_INT(T). { A = T; }
simple_value(A) ::= VAL_FLOAT(T). { A = T; }
simple_value(A) ::= VAL_BOOL(T). { A = T; }
simple_value(A) ::= VAL_DATETIME(T). { A = T; }

%type path {toml_path_t}
path(A) ::= key_part(K). {
    A.tokens[0] = K;
    A.count = 1;
}
path(A) ::= path(P) DOT key_part(K). {
    if (P.count < 10) {
        P.tokens[P.count] = K;
        P.count++;
    }
    A = P;
}

%type key_part {toml_token_t}
key_part(A) ::= KEY(T). { A = T; }
key_part(A) ::= STRING(T). { A = T; }


%type array {toml_array_t*}
array(A) ::= LBRACKET array_elements(E) RBRACKET. { A = E; }

%type array_elements {toml_array_t*}
array_elements(A) ::= . { A = toml_helper_create_array(ctx); }
array_elements(A) ::= array_list(L). { A = L; }
array_elements(A) ::= array_list(L) COMMA. { A = L; }

%type array_list {toml_array_t*}
array_list(A) ::= any_value(V). {
    A = toml_helper_create_array(ctx);
    if (V.type == 0) toml_helper_array_append_value(ctx, A, *((toml_token_t*)V.ptr));
    else if (V.type == 1) toml_helper_array_append_array(ctx, A, (toml_array_t*)V.ptr);
    else if (V.type == 2) toml_helper_array_append_table(ctx, A, (toml_table_t*)V.ptr);
    if (V.type == 0) free(V.ptr);
}
array_list(A) ::= array_list(L) COMMA any_value(V). {
    if (V.type == 0) toml_helper_array_append_value(ctx, L, *((toml_token_t*)V.ptr));
    else if (V.type == 1) toml_helper_array_append_array(ctx, L, (toml_array_t*)V.ptr);
    else if (V.type == 2) toml_helper_array_append_table(ctx, L, (toml_table_t*)V.ptr);
    if (V.type == 0) free(V.ptr);
    A = L;
}


%type inline_table {toml_table_t*}
inline_table(A) ::= LBRACE inline_elements(E) RBRACE. { A = E; }

%type inline_elements {toml_table_t*}
inline_elements(A) ::= . { A = toml_helper_create_inline_table(ctx); }
inline_elements(A) ::= inline_list(L). { A = L; }
inline_elements(A) ::= inline_list(L) COMMA. { A = L; }

%type inline_list {toml_table_t*}
inline_list(A) ::= inline_kv(KV). {
    A = toml_helper_create_inline_table(ctx);
    toml_helper_add_any(ctx, A, KV.path, KV.val, KV.type);
}
inline_list(A) ::= inline_list(L) COMMA inline_kv(KV). {
    toml_helper_add_any(ctx, L, KV.path, KV.val, KV.type);
    A = L;
}

%type inline_kv {inline_kv_t}
inline_kv(A) ::= path(P) EQUAL any_value(V). {
    A.path = P;
    A.val = V.ptr;
    A.type = V.type;
}

%syntax_error {
    if (ctx->error) return;
    ctx->error = 1;
    const char* msg = "syntax error";
    if (yyminor.type == 0) msg = "missing '='";
    else if (yyminor.type == TOML_TOKEN_LBRACE) msg = "expected a string";
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: %s", yyminor.pos.line, yyminor.pos.col, msg);
}

%parse_failure {
    if (ctx->error) return;
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at 0:0: parse failure");
}
