/**
 * @file toon_grammar.y
 * @brief TOON Parser Grammar (Lemon)
 */

%name ToonParse
%token_prefix TOON_TOKEN_
%token_type {toon_token_t}
%default_type {toonObject*}
%extra_argument {toon_parse_ctx_t *ctx}
%stack_size 1024

%include {
#include "toon_lexer.h"
#include "toonc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    toonObject *head;
    toonObject *tail;
} toon_node_list_t;



static void add_child_optimized(toon_parse_ctx_t *ctx, toonObject *parent, toonObject *child) {
    if (!parent || !child) return;
    if (!parent->child) {
        parent->child = child;
    } else {
        toonObject *curr = parent->child;
        while (curr->next) curr = curr->next;
        curr->next = child;
    }
}

static toonObject *post_process_table(toon_parse_ctx_t *ctx, toonObject *table, toonObject *cols) {
    if (!table || !cols) return table;
    // For each row (which is a KV_LIST), convert to KV_OBJ using cols
    for (size_t i = 0; i < table->array.len; i++) {
        toonObject *row_list = table->array.items[i];
        if (row_list->kvtype != KV_LIST) continue;

        toonObject *row_obj = TOONc_newObjectArena(ctx->arena, KV_OBJ);
        toonObject *last_prop = NULL;

        for (size_t j = 0; j < cols->array.len && j < row_list->array.len; j++) {
            toonObject *val = row_list->array.items[j];
            toonObject *col_name = cols->array.items[j];

            toonObject *prop = val;

            prop->key = mem_strdup(ctx->arena, col_name->str.ptr);

            if (!last_prop) {
                row_obj->child = prop;
            } else {
                last_prop->next = prop;
            }
            last_prop = prop;

            row_list->array.items[j] = NULL;
        }
        table->array.items[i] = row_obj;
    }
    return table;
}
}

%token COLON LBRACKET RBRACKET LBRACE RBRACE COMMA NEWLINE.
%left PREFER_SHIFT.
%left INDENT DEDENT.
%token KEY VALUE NUMBER BOOL NULL.

%type sequence {toon_node_list_t}
%type node {toon_node_list_t}
%type children {toonObject*}

%start_symbol start

start ::= sequence(S). { ctx->root->child = S.head; }

sequence(A) ::= . { A.head = A.tail = NULL; }
sequence(A) ::= sequence(S) node(N). {
    if (N.head) {
        if (!S.head) {
            S = N;
        } else {
            S.tail->next = N.head;
            S.tail = N.tail;
        }
    }
    A = S;
}

node(A) ::= pair(P). { A.head = P; A.tail = P; P->next = NULL; }
node(A) ::= NEWLINE. { A.head = A.tail = NULL; }
node(A) ::= INDENT sequence(S) DEDENT. { A = S; }
node(A) ::= error NEWLINE. { ctx->error = 0; ctx->error_msg[0] = '\0'; A.head = A.tail = NULL; }

pair(A) ::= KEY(K) COLON value(V). {
    A = V;
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
}

pair(A) ::= KEY(K) COLON NEWLINE INDENT children(C) DEDENT. {
    A = TOONc_newObjectArena(ctx->arena, KV_OBJ);
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
    A->child = C;
}

pair(A) ::= KEY(K) LBRACKET NUMBER RBRACKET COLON list_values(L) NEWLINE. {
    A = L;
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
}

pair(A) ::= KEY(K) LBRACKET NUMBER(N) RBRACKET COLON NEWLINE. {
    A = TOONc_newListObjArena(ctx->arena, (size_t)N.int_val);
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
}

pair(A) ::= KEY(K) LBRACKET NUMBER(N) RBRACKET LBRACE column_names(C) RBRACE COLON NEWLINE INDENT table_rows(T) DEDENT. {
    A = post_process_table(ctx, T, C);
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
    TOONc_listReserveArena(ctx->arena, A, (size_t)N.int_val);
}

pair(A) ::= KEY(K) LBRACKET NUMBER RBRACKET LBRACE column_names(C) RBRACE COLON NEWLINE. [PREFER_SHIFT] {
    // Empty table
    A = TOONc_newListObjArena(ctx->arena, 0);
    A->key = mem_alloc(ctx->arena, K.length + 1);
    memcpy(A->key, K.value, K.length);
    A->key[K.length] = '\0';
    // TOONc_free(C);
}

children(A) ::= sequence(S). { A = S.head; }

value(A) ::= VALUE(T).  { A = TOONc_newStringObjArena(ctx->arena, (char*)T.value, T.length); }
value(A) ::= KEY(T).    { A = TOONc_newStringObjArena(ctx->arena, (char*)T.value, T.length); }
value(A) ::= NUMBER(T). { A = TOONc_newDoubleObjArena(ctx->arena, T.float_val); if (T.float_val == (int)T.float_val) { A->kvtype = KV_INT; A->i = T.int_val; } }
value(A) ::= BOOL(T).   { A = TOONc_newBoolObjArena(ctx->arena, T.bool_val); }
value(A) ::= NULL.      { A = TOONc_newNullObjArena(ctx->arena); }
value(A) ::= LBRACKET list_values(L) RBRACKET. { A = L; }
value(A) ::= LBRACKET RBRACKET. { A = TOONc_newListObjArena(ctx->arena, 0); }

list_values(A) ::= value(V). {
    A = TOONc_newListObjArena(ctx->arena, 8);
    TOONc_listPushArena(ctx->arena, A, V);
}
list_values(A) ::= list_values(L) COMMA value(V). {
    TOONc_listPushArena(ctx->arena, L, V);
    A = L;
}

column_names(A) ::= KEY(K). {
    A = TOONc_newListObjArena(ctx->arena, 8);
    TOONc_listPushArena(ctx->arena, A, TOONc_newStringObjArena(ctx->arena, (char*)K.value, K.length));
}
column_names(A) ::= column_names(L) COMMA KEY(K). {
    TOONc_listPushArena(ctx->arena, L, TOONc_newStringObjArena(ctx->arena, (char*)K.value, K.length));
    A = L;
}

table_rows(A) ::= table_row(R). {
    A = TOONc_newListObjArena(ctx->arena, 8);
    TOONc_listPushArena(ctx->arena, A, R);
}
table_rows(A) ::= table_rows(L) NEWLINE table_row(R). {
    TOONc_listPushArena(ctx->arena, L, R);
    A = L;
}
table_rows(A) ::= table_rows(L) NEWLINE. { A = L; }

table_row(A) ::= list_values(L). {
    A = L;
}

%syntax_error {
    ctx->error = 1;
    if (TOKEN.length > 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Syntax error at token %d (%.*s) at line %d",
                 TOKEN.type, (int)TOKEN.length, (char*)TOKEN.value, ctx->line);
    } else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Syntax error at token %d at line %d",
                 TOKEN.type, ctx->line);
    }
}
