#include "dsv_filter_expr_parser.h"
#include "dsv_filter_expr_lexer.h"
#include "dsv_filter_expr_grammar_gen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int dsv_expr_ctx_push_item(dsv_expr_parse_ctx_t *ctx, const dsv_expr_item_t *item) {
    dsv_expr_item_t *new_items;
    if (!ctx || !item) return 0;
    new_items = (dsv_expr_item_t *)realloc(ctx->out.items, sizeof(dsv_expr_item_t) * (ctx->out.count + 1));
    if (!new_items) return 0;
    ctx->out.items = new_items;
    ctx->out.items[ctx->out.count] = *item;
    ctx->out.count++;
    return 1;
}

void dsv_expr_ctx_set_error(dsv_expr_parse_ctx_t *ctx, const char *msg) {
    if (!ctx || ctx->error) return;
    ctx->error = 1;
    if (!msg) msg = "expression error";
    strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
    ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
}

int dsv_expr_ctx_push_number(dsv_expr_parse_ctx_t *ctx, const char *value, size_t len) {
    dsv_expr_item_t item;
    char *tmp;
    if (!ctx || !value) return 0;
    tmp = (char *)malloc(len + 1);
    if (!tmp) return 0;
    memcpy(tmp, value, len);
    tmp[len] = '\0';

    memset(&item, 0, sizeof(item));
    item.kind = DSV_EXPR_ITEM_NUMBER;
    item.num = strtod(tmp, NULL);
    free(tmp);
    return dsv_expr_ctx_push_item(ctx, &item);
}

int dsv_expr_ctx_push_ident(dsv_expr_parse_ctx_t *ctx, const char *value, size_t len) {
    dsv_expr_item_t item;
    if (!ctx || !value) return 0;
    memset(&item, 0, sizeof(item));
    item.kind = DSV_EXPR_ITEM_IDENT;
    item.ident = (char *)malloc(len + 1);
    if (!item.ident) return 0;
    memcpy(item.ident, value, len);
    item.ident[len] = '\0';
    item.ident_len = len;
    if (!dsv_expr_ctx_push_item(ctx, &item)) {
        free(item.ident);
        return 0;
    }
    return 1;
}

int dsv_expr_ctx_push_op(dsv_expr_parse_ctx_t *ctx, int kind) {
    dsv_expr_item_t item;
    if (!ctx) return 0;
    memset(&item, 0, sizeof(item));
    item.kind = kind;
    if (!dsv_expr_ctx_push_item(ctx, &item)) return 0;
    ctx->out.has_operator = 1;
    return 1;
}

void dsv_expr_output_free(dsv_expr_output_t *out) {
    size_t i;
    if (!out) return;
    if (out->items) {
        for (i = 0; i < out->count; i++) {
            free(out->items[i].ident);
        }
        free(out->items);
    }
    out->items = NULL;
    out->count = 0;
    out->has_operator = 0;
}

int dsv_expr_parse(const char *expr, size_t len, dsv_expr_output_t *out, char *error_msg, size_t error_msg_cap) {
    dsv_expr_parse_ctx_t ctx;
    dsv_expr_lexer_t lexer;
    dsv_expr_token_t token;
    void *parser;
    int ret;

    if (!out) return 0;
    out->items = NULL;
    out->count = 0;
    out->has_operator = 0;

    if (!expr || len == 0) {
        if (error_msg && error_msg_cap > 0) {
            strncpy(error_msg, "expression is empty", error_msg_cap - 1);
            error_msg[error_msg_cap - 1] = '\0';
        }
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.input = expr;

    parser = DsvExprParseAlloc(malloc);
    if (!parser) {
        if (error_msg && error_msg_cap > 0) {
            strncpy(error_msg, "oom parsing expression", error_msg_cap - 1);
            error_msg[error_msg_cap - 1] = '\0';
        }
        return 0;
    }

    dsv_expr_lexer_init(&lexer, expr, len);
    while ((ret = dsv_expr_lexer_next(&lexer, &token)) > 0) {
        DsvExprParse(parser, token.type, token, &ctx);
        if (ctx.error) break;
    }

    if (ret < 0 && !ctx.error) {
        dsv_expr_ctx_set_error(&ctx, lexer.error[0] ? lexer.error : "expression lex error");
    }

    if (!ctx.error) {
        memset(&token, 0, sizeof(token));
        DsvExprParse(parser, 0, token, &ctx);
    }
    DsvExprParseFree(parser, free);

    if (ctx.error) {
        dsv_expr_output_free(&ctx.out);
        if (error_msg && error_msg_cap > 0) {
            strncpy(error_msg, ctx.error_msg, error_msg_cap - 1);
            error_msg[error_msg_cap - 1] = '\0';
        }
        return 0;
    }

    *out = ctx.out;
    return 1;
}
