#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"
#include "toml_lexer.h"
#include "toml_grammar_gen.h"
#include "toml_types.h"

// =============================================================================
// Memory Management (from original toml-c)
// =============================================================================

#define ALIGN8(sz) (((sz) + 7) & ~7)

static void* CALLOC(size_t nmemb, size_t sz) {
    size_t nb = ALIGN8(sz) * nmemb;
    void* p  = malloc(nb);
    if (p) {
        memset(p, 0, nb);
    }
    return p;
}

static char* STRDUP(const char* s) {
    size_t len = strlen(s);
    char* p   = (char*)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}

static char* STRNDUP(const char* s, size_t n) {
    size_t len = strnlen(s, n);
    char*  p   = (char*)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}

static void xfree(const void* x) {
    if (x)
        free((void*)(intptr_t)x);
}

static void* expand(void* p, size_t sz, size_t newsz) {
    void* s = malloc(newsz);
    if (!s) return 0;
    if (p) {
        memcpy(s, p, sz);
        free(p);
    }
    return s;
}

static void** expand_ptrarr(void** p, int n) {
    void** s = (void**)malloc((size_t)(n + 1) * sizeof(void*));
    if (!s) return 0;
    if (p) {
        memcpy(s, p, (size_t)n * sizeof(void*));
        free(p);
    }
    s[n] = 0;
    return s;
}

static toml_arritem_t* expand_arritem(toml_arritem_t* p, int n) {
    toml_arritem_t* pp = (toml_arritem_t*)expand(p, (size_t)n * sizeof(*p), (size_t)(n + 1) * sizeof(*p));
    if (!pp) return 0;
    memset(&pp[n], 0, sizeof(pp[n]));
    return pp;
}

// =============================================================================
// Table Management Helpers
// =============================================================================

static int check_key(toml_table_t* tbl, const char* key, toml_keyval_t** ret_val, toml_array_t** ret_arr, toml_table_t** ret_tbl) {
    int i;
    if (ret_tbl) *ret_tbl = 0;
    if (ret_arr) *ret_arr = 0;
    if (ret_val) *ret_val = 0;

    for (i = 0; i < tbl->nkval; i++) {
        if (strcmp(key, tbl->kval[i]->key) == 0) {
            if (ret_val) *ret_val = tbl->kval[i];
            return 'v';
        }
    }
    for (i = 0; i < tbl->narr; i++) {
        if (strcmp(key, tbl->arr[i]->key) == 0) {
            if (ret_arr) *ret_arr = tbl->arr[i];
            return 'a';
        }
    }
    for (i = 0; i < tbl->ntbl; i++) {
        if (strcmp(key, tbl->tbl[i]->key) == 0) {
            if (ret_tbl) *ret_tbl = tbl->tbl[i];
            return 't';
        }
    }
    return 0;
}

static char* normalize_key(toml_token_t tok, int* keylen) {
    if (tok.type == TOML_TOKEN_STRING) {
        if (tok.len >= 2) {
            *keylen = (int)tok.len - 2;
            return STRNDUP(tok.value + 1, (size_t)*keylen);
        }
    }
    *keylen = (int)tok.len;
    return STRNDUP(tok.value, tok.len);
}

toml_table_t* toml_helper_walk_path(toml_parse_ctx_t *ctx, toml_table_t *start, toml_token_t *keys, int num_keys, bool create_intermediate) {
    toml_table_t *curr = start ? start : ctx->root;
    for (int i = 0; i < num_keys; i++) {
        int keylen;
        char *keyval = normalize_key(keys[i], &keylen);
        if (!keyval) return NULL;

        toml_table_t *next = NULL;
        int res = check_key(curr, keyval, NULL, NULL, &next);

        if (res && res != 't') {
            // Found a value or array where a table path was expected
            ctx->error = 1;
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", keys[i].pos.line, keys[i].pos.col);
            free(keyval);
            return NULL;
        }

        if (!next) {
            if (create_intermediate) {
                int n = curr->ntbl;
                toml_table_t** base = (toml_table_t**)expand_ptrarr((void**)curr->tbl, n);
                if (!base) { free(keyval); return NULL; }
                curr->tbl = base;
                base[n] = (toml_table_t*)CALLOC(1, sizeof(*base[n]));
                if (!base[n]) { free(keyval); return NULL; }
                base[n]->key = keyval;
                base[n]->keylen = keylen;
                base[n]->implicit = true;
                next = base[n];
                curr->ntbl++;
            } else {
                free(keyval);
                return NULL;
            }
        } else {
            free(keyval);
        }
        curr = next;
    }
    return curr;
}

toml_table_t* toml_helper_create_table(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key_tok, bool is_array_of_tables) {
    int keylen;
    char *keyval = normalize_key(key_tok, &keylen);
    if (!keyval) return NULL;

    if (is_array_of_tables) {
        toml_array_t *arr = NULL;
        int res = check_key(parent, keyval, NULL, &arr, NULL);
        if (res && res != 'a') {
             ctx->error = 1;
             snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", key_tok.pos.line, key_tok.pos.col);
             free(keyval);
             return NULL;
        }
        if (!arr) {
            int n = parent->narr;
            toml_array_t** base = (toml_array_t**)expand_ptrarr((void**)parent->arr, n);
            if (!base) { free(keyval); return NULL; }
            parent->arr = base;
            base[n] = (toml_array_t*)CALLOC(1, sizeof(*base[n]));
            if (!base[n]) { free(keyval); return NULL; }
            base[n]->key = keyval;
            base[n]->keylen = keylen;
            base[n]->kind = 't';
            arr = base[n];
            parent->narr++;
        } else {
            free(keyval);
        }

        int n = arr->nitem;
        toml_arritem_t* items = expand_arritem(arr->item, n);
        if (!items) return NULL;
        arr->item = items;
        arr->item[n].tbl = (toml_table_t*)CALLOC(1, sizeof(toml_table_t));
        arr->item[n].tbl->key = STRDUP("__anon__");
        arr->nitem++;
        return arr->item[n].tbl;
    } else {
        toml_table_t *tbl = NULL;
        int res = check_key(parent, keyval, NULL, NULL, &tbl);
        if (res) {
            if (res == 't' && tbl->implicit) {
                tbl->implicit = false;
                free(keyval);
                return tbl;
            }
            ctx->error = 1;
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", key_tok.pos.line, key_tok.pos.col);
            free(keyval);
            return NULL;
        }

        int n = parent->ntbl;
        toml_table_t** base = (toml_table_t**)expand_ptrarr((void**)parent->tbl, n);
        if (!base) { free(keyval); return NULL; }
        parent->tbl = base;
        base[n] = (toml_table_t*)CALLOC(1, sizeof(*base[n]));
        base[n]->key = keyval;
        base[n]->keylen = keylen;
        parent->ntbl++;
        return base[n];
    }
}

void toml_helper_add_keyval(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key_tok, toml_token_t val_tok) {
    int keylen;
    char *keyval = normalize_key(key_tok, &keylen);
    if (!keyval) return;

    if (check_key(parent, keyval, NULL, NULL, NULL)) {
        ctx->error = 1;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", key_tok.pos.line, key_tok.pos.col);
        free(keyval);
        return;
    }

    int n = parent->nkval;
    toml_keyval_t** base = (toml_keyval_t**)expand_ptrarr((void**)parent->kval, n);
    if (!base) { free(keyval); return; }
    parent->kval = base;
    base[n] = (toml_keyval_t*)CALLOC(1, sizeof(*base[n]));
    base[n]->key = keyval;
    base[n]->keylen = keylen;
    base[n]->val = STRNDUP(val_tok.value, val_tok.len);
    parent->nkval++;
}

toml_array_t* toml_helper_create_array(toml_parse_ctx_t *ctx) {
    toml_array_t *arr = (toml_array_t*)CALLOC(1, sizeof(toml_array_t));
    return arr;
}

void toml_helper_array_append_value(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_token_t val_tok) {
    int n = arr->nitem;
    toml_arritem_t* items = expand_arritem(arr->item, n);
    if (!items) return;
    arr->item = items;
    arr->item[n].valtype = 's';
    arr->item[n].val = STRNDUP(val_tok.value, val_tok.len);
    arr->nitem++;
}

void toml_helper_array_append_array(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_array_t *sub) {
    int n = arr->nitem;
    toml_arritem_t* items = expand_arritem(arr->item, n);
    if (!items) return;
    arr->item = items;
    arr->item[n].arr = sub;
    arr->nitem++;
}

void toml_helper_array_append_table(toml_parse_ctx_t *ctx, toml_array_t *arr, toml_table_t *sub) {
    int n = arr->nitem;
    toml_arritem_t* items = expand_arritem(arr->item, n);
    if (!items) return;
    arr->item = items;
    arr->item[n].tbl = sub;
    arr->nitem++;
}

void toml_helper_add_array(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key_tok, toml_array_t *arr) {
    int keylen;
    char *keyval = normalize_key(key_tok, &keylen);
    if (!keyval) return;

    if (check_key(parent, keyval, NULL, NULL, NULL)) {
        ctx->error = 1;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", key_tok.pos.line, key_tok.pos.col);
        free(keyval);
        // Should we free arr? Probably not here as it might be partially linked.
        return;
    }

    int n = parent->narr;
    toml_array_t** base = (toml_array_t**)expand_ptrarr((void**)parent->arr, n);
    if (!base) { free(keyval); return; }
    parent->arr = base;
    parent->arr[n] = arr;
    arr->key = keyval;
    arr->keylen = keylen;
    parent->narr++;
}

toml_table_t* toml_helper_create_inline_table(toml_parse_ctx_t *ctx) {
    toml_table_t *tbl = (toml_table_t*)CALLOC(1, sizeof(toml_table_t));
    tbl->readonly = true;
    return tbl;
}

void toml_helper_add_inline_table(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_token_t key_tok, toml_table_t *sub) {
    int keylen;
    char *keyval = normalize_key(key_tok, &keylen);
    if (!keyval) return;

    if (check_key(parent, keyval, NULL, NULL, NULL)) {
        ctx->error = 1;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "at %d:%d: key already defined", key_tok.pos.line, key_tok.pos.col);
        free(keyval);
        return;
    }

    int n = parent->ntbl;
    toml_table_t** base = (toml_table_t**)expand_ptrarr((void**)parent->tbl, n);
    if (!base) { free(keyval); return; }
    parent->tbl = base;
    parent->tbl[n] = sub;
    sub->key = keyval;
    sub->keylen = keylen;
    parent->ntbl++;
}

void toml_helper_add_any(toml_parse_ctx_t *ctx, toml_table_t *parent, toml_path_t path, void *val, int type) {
    toml_table_t *target = toml_helper_walk_path(ctx, parent, path.tokens, path.count - 1, true);
    if (!target) return;
    toml_token_t last_key = path.tokens[path.count - 1];

    if (type == 0) {
        toml_token_t *tok = (toml_token_t*)val;
        toml_helper_add_keyval(ctx, target, last_key, *tok);
        free(tok);
    } else if (type == 1) {
        toml_helper_add_array(ctx, target, last_key, (toml_array_t*)val);
    } else if (type == 2) {
        toml_helper_add_inline_table(ctx, target, last_key, (toml_table_t*)val);
    }
}

// =============================================================================
// Parser Implementation
// =============================================================================

void* TomlParseAlloc(void* (*mallocProc)(size_t));
void TomlParseFree(void* p, void (*freeProc)(void*));
void TomlParse(void* yyp, int yymajor, toml_token_t yyminor, toml_parse_ctx_t* ctx);

toml_table_t* toml_parse(char* toml, char* errbuf, int errbufsz) {
    toml_lexer_t lexer;
    toml_token_t token;
    toml_parse_ctx_t ctx = {0};

    if (errbufsz > 0) errbuf[0] = 0;

    ctx.root = (toml_table_t*)CALLOC(1, sizeof(toml_table_t));
    if (!ctx.root) return NULL;
    ctx.current_table = ctx.root;

    toml_lexer_init(&lexer, toml, strlen(toml));

    void *parser = TomlParseAlloc(malloc);
    if (!parser) {
        toml_free(ctx.root);
        return NULL;
    }

    int status;
    while ((status = toml_lexer_next(&lexer, &token)) > 0) {
        TomlParse(parser, token.type, token, &ctx);
        if (ctx.error) break;
    }

    if (status < 0 && !ctx.error) {
        ctx.error = 1;
        snprintf(ctx.error_msg, sizeof(ctx.error_msg), "at %d:%d: %s", token.pos.line, token.pos.col, lexer.error_msg);
    }

    if (!ctx.error) {
        toml_token_t eof_token = {0};
        eof_token.type = 0;
        eof_token.pos.line = lexer.line;
        eof_token.pos.col = lexer.column;
        TomlParse(parser, 0, eof_token, &ctx);
    }

    TomlParseFree(parser, free);

    if (ctx.error) {
        if (errbuf && errbufsz > 0) {
            strncpy(errbuf, ctx.error_msg, (size_t)errbufsz - 1);
            errbuf[errbufsz - 1] = 0;
        }
        toml_free(ctx.root);
        return NULL;
    }

    return ctx.root;
}

toml_table_t* toml_parse_file(FILE* fp, char* errbuf, int errbufsz) {
    size_t bufsz = 0;
    char* buf   = 0;
    size_t off   = 0;
    size_t inc   = 1024;

    while (!feof(fp)) {
        if (bufsz == 1024 * 20) inc = 1024 * 20;
        if (off == bufsz) {
            size_t xsz = bufsz + inc;
            char* x   = (char*)expand(buf, bufsz, xsz);
            if (!x) {
                if (errbuf && errbufsz > 0) snprintf(errbuf, (size_t)errbufsz, "out of memory");
                xfree(buf);
                return 0;
            }
            buf   = x;
            bufsz = xsz;
        }
        errno = 0;
        size_t n = fread(buf + off, 1, bufsz - off, fp);
        if (ferror(fp)) {
            if (errbuf && errbufsz > 0) snprintf(errbuf, (size_t)errbufsz, "%s", (errno ? strerror(errno) : "Error reading file"));
            xfree(buf);
            return 0;
        }
        off += n;
    }

    if (off == bufsz) {
        size_t xsz = bufsz + 1;
        char* x   = (char*)expand(buf, bufsz, xsz);
        if (!x) {
            if (errbuf && errbufsz > 0) snprintf(errbuf, (size_t)errbufsz, "out of memory");
            xfree(buf);
            return 0;
        }
        buf   = x;
        bufsz = xsz;
    }
    buf[off] = 0;

    toml_table_t* ret = toml_parse(buf, errbuf, errbufsz);
    xfree(buf);
    return ret;
}

// =============================================================================
// Query Functions (from original toml-c)
// =============================================================================

static void xfree_kval(toml_keyval_t* p) {
    if (!p) return;
    xfree(p->key);
    xfree(p->val);
    xfree(p);
}

static void xfree_tbl(toml_table_t* p);

static void xfree_arr(toml_array_t* p) {
    if (!p) return;
    xfree(p->key);
    const int n = p->nitem;
    for (int i = 0; i < n; i++) {
        toml_arritem_t* a = &p->item[i];
        if (a->val) xfree(a->val);
        else if (a->arr) xfree_arr(a->arr);
        else if (a->tbl) xfree_tbl(a->tbl);
    }
    xfree(p->item);
    xfree(p);
}

static void xfree_tbl(toml_table_t* p) {
    if (!p) return;
    xfree(p->key);
    for (int i = 0; i < p->nkval; i++) xfree_kval(p->kval[i]);
    xfree(p->kval);
    for (int i = 0; i < p->narr; i++) xfree_arr(p->arr[i]);
    xfree(p->arr);
    for (int i = 0; i < p->ntbl; i++) xfree_tbl(p->tbl[i]);
    xfree(p->tbl);
    xfree(p);
}

void toml_free(toml_table_t* tbl) {
    xfree_tbl(tbl);
}

const char* toml_table_key(const toml_table_t* tbl, int keyidx, int* keylen) {
    if (keyidx < tbl->nkval) {
        if (keylen) *keylen = tbl->kval[keyidx]->keylen;
        return tbl->kval[keyidx]->key;
    }
    if ((keyidx -= tbl->nkval) < tbl->narr) {
        if (keylen) *keylen = tbl->arr[keyidx]->keylen;
        return tbl->arr[keyidx]->key;
    }
    if ((keyidx -= tbl->narr) < tbl->ntbl) {
        if (keylen) *keylen = tbl->tbl[keyidx]->keylen;
        return tbl->tbl[keyidx]->key;
    }
    if (keylen) *keylen = 0;
    return 0;
}

toml_unparsed_t toml_table_unparsed(const toml_table_t* tbl, const char* key) {
    for (int i = 0; i < tbl->nkval; i++)
        if (strcmp(key, tbl->kval[i]->key) == 0)
            return tbl->kval[i]->val;
    return 0;
}

toml_array_t* toml_table_array(const toml_table_t* tbl, const char* key) {
    for (int i = 0; i < tbl->narr; i++)
        if (strcmp(key, tbl->arr[i]->key) == 0)
            return tbl->arr[i];
    return 0;
}

toml_table_t* toml_table_table(const toml_table_t* tbl, const char* key) {
    for (int i = 0; i < tbl->ntbl; i++)
        if (strcmp(key, tbl->tbl[i]->key) == 0)
            return tbl->tbl[i];
    return 0;
}

toml_unparsed_t toml_array_unparsed(const toml_array_t* arr, int idx) {
    return (0 <= idx && idx < arr->nitem) ? arr->item[idx].val : 0;
}

int toml_table_len(const toml_table_t* tbl) {
    return tbl->nkval + tbl->narr + tbl->ntbl;
}

int toml_array_len(const toml_array_t* arr) {
    return arr->nitem;
}

toml_array_t* toml_array_array(const toml_array_t* arr, int idx) {
    return (0 <= idx && idx < arr->nitem) ? arr->item[idx].arr : 0;
}

toml_table_t* toml_array_table(const toml_array_t* arr, int idx) {
    return (0 <= idx && idx < arr->nitem) ? arr->item[idx].tbl : 0;
}

static int scan_digits(const char* p, int n) {
    int ret = 0;
    for (; n > 0 && isdigit(*p); n--, p++)
        ret = 10 * ret + (*p - '0');
    return n ? -1 : ret;
}

static bool scan_date(const char* p, int* YY, int* MM, int* DD) {
    int year  = scan_digits(p, 4);
    int month = (year >= 0 && p[4] == '-') ? scan_digits(p + 5, 2) : -1;
    int day   = (month >= 0 && p[7] == '-') ? scan_digits(p + 8, 2) : -1;
    if (YY) *YY = year;
    if (MM) *MM = month;
    if (DD) *DD = day;
    return (year >= 0 && month >= 0 && day >= 0);
}

static bool scan_time(const char* p, int* hh, int* mm, int* ss) {
    int hour   = scan_digits(p, 2);
    int minute = (hour >= 0 && p[2] == ':') ? scan_digits(p + 3, 2) : -1;
    int second = (minute >= 0 && p[5] == ':') ? scan_digits(p + 6, 2) : -1;
    if (hh) *hh = hour;
    if (mm) *mm = minute;
    if (ss) *ss = second;
    return (hour >= 0 && minute >= 0);
}

int toml_value_timestamp(toml_unparsed_t src, toml_timestamp_t* ret) {
    if (!src) return -1;
    const char* p = src;
    memset(ret, 0, sizeof(*ret));
    if (scan_date(p, &ret->year, &ret->month, &ret->day)) {
        if (ret->month < 1 || ret->day < 1 || ret->month > 12 || ret->day > 31) return -1;
        if (ret->month == 2 && ret->day > (ret->year % 4 == 0 && (ret->year % 100 != 0 || ret->year % 400 == 0) ? 29 : 28)) return -1;
        ret->kind = 'D';
        p += 10;
        if (*p == 'T' || *p == 't' || *p == ' ') p++;
    }
    if (scan_time(p, &ret->hour, &ret->minute, &ret->second)) {
        ret->kind = (ret->kind == 'D' ? 'l' : 't');
        p += 8;
        if (*p == '.') {
            p++;
            while (isdigit((unsigned char)*p)) p++;
        }
        if (*p == 'Z' || *p == 'z' || *p == '+' || *p == '-') ret->kind = 'd';
    }
    return 0;
}

int toml_value_bool(toml_unparsed_t src, bool* ret) {
    if (!src) return -1;
    if (strcmp(src, "true") == 0) { if (ret) *ret = true; return 0; }
    if (strcmp(src, "false") == 0) { if (ret) *ret = false; return 0; }
    return -1;
}

int toml_value_int(toml_unparsed_t src, int64_t* ret) {
    if (!src) return -1;
    char *endp;
    *ret = strtoll(src, &endp, 0);
    return (*endp == '\0') ? 0 : -1;
}

int toml_value_double(toml_unparsed_t src, double* ret) {
    if (!src) return -1;
    char *endp;
    *ret = strtod(src, &endp);
    return (*endp == '\0') ? 0 : -1;
}

int toml_value_string(toml_unparsed_t src, char** ret, int* len) {
    if (!src) return -1;
    size_t slen = strlen(src);
    if (src[0] == '"' || src[0] == '\'') {
        if (slen < 2) return -1;
        *ret = STRNDUP(src + 1, slen - 2);
        if (len) *len = (int)slen - 2;
        return 0;
    }
    *ret = STRDUP(src);
    if (len) *len = (int)slen;
    return 0;
}

toml_value_t toml_array_string(const toml_array_t* arr, int idx) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_array_unparsed(arr, idx);
    ret.ok = (toml_value_string(s, &ret.u.s, &ret.u.sl) == 0);
    return ret;
}

toml_value_t toml_array_bool(const toml_array_t* arr, int idx) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_array_unparsed(arr, idx);
    ret.ok = (toml_value_bool(s, &ret.u.b) == 0);
    return ret;
}

toml_value_t toml_array_int(const toml_array_t* arr, int idx) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_array_unparsed(arr, idx);
    ret.ok = (toml_value_int(s, &ret.u.i) == 0);
    return ret;
}

toml_value_t toml_array_double(const toml_array_t* arr, int idx) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_array_unparsed(arr, idx);
    ret.ok = (toml_value_double(s, &ret.u.d) == 0);
    return ret;
}

toml_value_t toml_array_timestamp(const toml_array_t* arr, int idx) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_array_unparsed(arr, idx);
    ret.ok = (toml_value_timestamp(s, &ret.u.ts) == 0);
    return ret;
}

toml_value_t toml_table_string(const toml_table_t* tbl, const char* key) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_table_unparsed(tbl, key);
    ret.ok = (toml_value_string(s, &ret.u.s, &ret.u.sl) == 0);
    return ret;
}

toml_value_t toml_table_bool(const toml_table_t* tbl, const char* key) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_table_unparsed(tbl, key);
    ret.ok = (toml_value_bool(s, &ret.u.b) == 0);
    return ret;
}

toml_value_t toml_table_int(const toml_table_t* tbl, const char* key) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_table_unparsed(tbl, key);
    ret.ok = (toml_value_int(s, &ret.u.i) == 0);
    return ret;
}

toml_value_t toml_table_double(const toml_table_t* tbl, const char* key) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_table_unparsed(tbl, key);
    ret.ok = (toml_value_double(s, &ret.u.d) == 0);
    return ret;
}

toml_value_t toml_table_timestamp(const toml_table_t* tbl, const char* key) {
    toml_value_t ret = {0};
    toml_unparsed_t s = toml_table_unparsed(tbl, key);
    ret.ok = (toml_value_timestamp(s, &ret.u.ts) == 0);
    return ret;
}
