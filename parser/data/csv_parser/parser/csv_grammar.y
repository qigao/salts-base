/**
 * @file csv_grammar.y
 * @brief CSV Parser Grammar (Lemon) - RFC 4180 compliant
 *
 * Minimal grammar - structure logic is in semantic actions:
 *   csv   -> items
 *   items -> item | items item
 *   item  -> FIELD | COMMA | NEWLINE
 *
 * Build: lemon -Tlempar.c csv_grammar.y
 */

%name CsvParse
%token_prefix CSV_TOKEN_
%token_type {csv_token_t}
%default_type {void*}
%stack_size 256

%extra_argument {csv_parse_ctx_t *ctx}

%include {
#include "csv_lexer.h"
#include "csv_types.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char *unescape_quotes(csv_arena_t *arena, const char *src, size_t len, size_t *out_len) {
    char *dst = (char *)csv_arena_alloc(arena, len + 1);
    if (!dst) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '"' && i + 1 < len && src[i + 1] == '"') {
            dst[j++] = '"';
            i++;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    if (out_len) *out_len = j;
    return dst;
}

static void ensure_current_row(csv_parse_ctx_t *ctx) {
    if (!ctx->current_row) {
        ctx->current_row = csv_row_new_arena(ctx->arena);
    }
}

static void add_field(csv_parse_ctx_t *ctx, const char *value, size_t len, int needs_unescape) {
    ensure_current_row(ctx);

    if (needs_unescape) {
        size_t unesc_len;
        char *unesc = unescape_quotes(ctx->arena, value, len, &unesc_len);
        csv_row_add_field(ctx->arena, ctx->current_row, unesc, unesc_len, 1);
    } else {
        csv_row_add_field(ctx->arena, ctx->current_row, value, len, 0);
    }
}

static void finish_row(csv_parse_ctx_t *ctx) {
    if (ctx->current_row && ctx->current_row->field_count > 0) {
        csv_doc_add_row(ctx->doc, ctx->current_row);
    }
    ctx->current_row = csv_row_new_arena(ctx->arena);
}
}

%token FIELD COMMA NEWLINE.

%start_symbol csv

csv ::= items. {
    // Finish any remaining row
    if (ctx->current_row && ctx->current_row->field_count > 0) {
        csv_doc_add_row(ctx->doc, ctx->current_row);
        ctx->current_row = NULL;
    }
}

items ::= item.
items ::= items item.

item ::= FIELD(T). {
    add_field(ctx, T.value, T.length, T.needs_unescape);
}

item ::= COMMA. {
    // Comma without preceding field means empty field handled by lexer
    // Just continue
}

item ::= NEWLINE. {
    finish_row(ctx);
}

%syntax_error {
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "CSV syntax error");
}

%parse_failure {
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "CSV parse failure");
}
