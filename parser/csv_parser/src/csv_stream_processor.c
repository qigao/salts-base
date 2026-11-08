#include "csv_stream_processor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include "cmeta_buffer.h"

/* ── Fast double parser for financial CSV data ────────────────────── */
/* No scientific notation, no locale, no inf/nan — just [-]digits[.digits].
 * Single division via pow10 lookup table.  ~3-5× faster than strtod. */

static inline double fast_atof(const char *s, size_t len) {
    if (len == 0) return 0.0;

    const char *p = s;
    const char *end = s + len;

    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    uint64_t mantissa = 0;
    int frac_digits = 0;

    while (p < end && (*p >= '0' && *p <= '9'))
        mantissa = mantissa * 10 + (uint64_t)(*p++ - '0');

    if (p < end && *p == '.') {
        p++;
        while (p < end && (*p >= '0' && *p <= '9')) {
            mantissa = mantissa * 10 + (uint64_t)(*p++ - '0');
            frac_digits++;
        }
    }

    double result = (double)mantissa;

    if (frac_digits > 0) {
        static const double pow10[] = {
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
            1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
        };
        if (frac_digits < 19)
            result /= pow10[frac_digits];
        else {
            for (int i = 0; i < frac_digits; i++) result /= 10.0;
        }
    }

    return neg ? -result : result;
}

/* ── Column descriptor ────────────────────────────────────────────── */

typedef enum { COL_RAW = 0, COL_NUMBER, COL_STRING } col_type_t;

typedef struct {
    char       *name;       /* stripped name (without _n/_s suffix) */
    char       *raw_name;   /* original header name */
    col_type_t  type;
    size_t      index;
} sp_col_t;

typedef enum { FILTER_OP_EQ = 0, FILTER_OP_NE, FILTER_OP_GT, FILTER_OP_GE, FILTER_OP_LT, FILTER_OP_LE } filter_op_t;
typedef enum { FILTER_JOIN_AND = 0, FILTER_JOIN_OR } filter_join_t;

typedef struct {
    size_t      col_idx;
    col_type_t  col_type;
    filter_op_t op;
    bool        rhs_is_string;
    double      rhs_num;
    char       *rhs_str;
    size_t      rhs_str_len;
} filter_clause_t;

/* ── Growing string store (arena-style, append-only) ──────────────── */

typedef struct str_entry_s {
    char   *str;
    struct str_entry_s *next;
} str_entry_t;

typedef struct {
    mem_buffer_t *rows_buf;   /* rows[row_idx] → linked list of col entries */
    size_t        row_count;
    size_t        col_count;
} str_store_t;

static void str_store_init(str_store_t *s, size_t col_count, mem_pool_t *arena) {
    s->col_count = col_count;
    s->row_count = 0;
    s->rows_buf = mem_get_buffer(arena, 256 * sizeof(str_entry_t *));
    if (s->rows_buf) mem_set_used(s->rows_buf, 0);
}

static void str_store_push_row(str_store_t *s, const char **fields, const size_t *field_lens,
                                size_t field_count, bool *col_selected, sp_col_t *cols_meta,
                                mem_pool_t *arena) {
    if (!s->rows_buf) return;
    if (s->rows_buf->used + sizeof(str_entry_t *) > s->rows_buf->capacity) {
        size_t new_cap = s->rows_buf->capacity * 2;
        mem_buffer_t *nb = mem_get_buffer(arena, new_cap);
        if (!nb) return;
        memcpy(nb->data, s->rows_buf->data, s->rows_buf->used);
        mem_set_used(nb, s->rows_buf->used);
        mem_unref(s->rows_buf);
        s->rows_buf = nb;
    }

    size_t cols = field_count < s->col_count ? field_count : s->col_count;
    str_entry_t *entries = MEM_ALLOC_ARRAY(arena, str_entry_t, cols);
    if (!entries) return;

    for (size_t i = 0; i < cols; i++) {
        if (!col_selected[i] || cols_meta[i].type != COL_STRING) {
            entries[i].str = NULL;
            entries[i].next = NULL;
            continue;
        }
        char *str_ptr = MEM_ALLOC_ARRAY(arena, char, field_lens[i] + 1);
        if (str_ptr) {
            memcpy(str_ptr, fields[i], field_lens[i]);
            str_ptr[field_lens[i]] = '\0';
        }
        entries[i].str = str_ptr;
        entries[i].next = NULL;
    }

    str_entry_t **row_ptr = (str_entry_t **)(s->rows_buf->data + s->rows_buf->used);
    *row_ptr = entries;
    mem_set_used(s->rows_buf, s->rows_buf->used + sizeof(str_entry_t *));
    s->row_count++;
}

static void str_store_free(str_store_t *s) {
    if (s->rows_buf) mem_unref(s->rows_buf);
}

/* ── Growing double vector ────────────────────────────────────────── */

typedef struct {
    double *data;
    size_t  len;
    size_t  cap;
} dvec_t;

static void dvec_push(dvec_t *v, double val, mem_pool_t *arena) {
    if (v->len >= v->cap) {
        size_t new_cap = v->cap ? v->cap + v->cap / 2 : 4096; /* 1.5x growth */
        double *nd = MEM_ALLOC_ARRAY(arena, double, new_cap);
        if (!nd) return;
        if (v->data) memcpy(nd, v->data, v->len * sizeof(double));
        /* Old memory will be collected when arena is freed */
        v->data = nd;
        v->cap = new_cap;
    }
    v->data[v->len++] = val;
}

/* ── Processor internals ──────────────────────────────────────────── */

struct csv_stream_processor_s {
    mem_pool_t arena;

    /* Line buffer */
    char  *line_buf;
    size_t line_len;
    size_t line_cap;

    /* Header / columns */
    sp_col_t *cols;
    size_t    col_count;
    bool      header_parsed;

    /* Filter (optional) */
    char           *filter_expr_str;
    bool            has_filter;
    bool            filter_compiled;
    filter_clause_t *filter_clauses;
    filter_join_t   *filter_joins;
    size_t          filter_clause_count;

    /* Column selection */
    char  *select_cols_str;
    bool  *col_selected;
    bool   has_col_selection;

    /* Result vectors */
    dvec_t *num_vecs;

    /* String store */
    str_store_t str_store;

    /* Matched row count */
    size_t match_count;

    /* Options */
    csv_options_t opts;

    /* Error */
    char error[256];
};

/* ── Helpers ──────────────────────────────────────────────────────── */

static void set_error(csv_stream_processor_t *p, const char *msg) {
    if (msg) {
        strncpy(p->error, msg, sizeof(p->error) - 1);
        p->error[sizeof(p->error) - 1] = '\0';
    } else {
        p->error[0] = '\0';
    }
}

static bool ends_with_ci(const char *str, size_t len, const char *suffix, size_t slen) {
    if (len < slen) return false;
    for (size_t i = 0; i < slen; i++) {
        if (tolower((unsigned char)str[len - slen + i]) != tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

static char *strndup_c(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = '\0'; }
    return d;
}

static void free_filter_plan(csv_stream_processor_t *p) {
    if (!p) return;
    if (p->filter_clauses) {
        for (size_t i = 0; i < p->filter_clause_count; i++) {
            free(p->filter_clauses[i].rhs_str);
        }
    }
    free(p->filter_clauses);
    free(p->filter_joins);
    p->filter_clauses = NULL;
    p->filter_joins = NULL;
    p->filter_clause_count = 0;
    p->filter_compiled = false;
}

static void free_column_names(csv_stream_processor_t *p) {
    if (!p || !p->cols) return;
    for (size_t i = 0; i < p->col_count; i++) {
        free(p->cols[i].name);
        free(p->cols[i].raw_name);
        p->cols[i].name = NULL;
        p->cols[i].raw_name = NULL;
    }
}

static void skip_ws(const char **cur, const char *end) {
    while (*cur < end && isspace((unsigned char)**cur)) (*cur)++;
}

static bool parse_identifier(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    if (p >= end || !(isalpha((unsigned char)*p) || *p == '_')) return false;
    const char *s = p++;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    *start = s;
    *len = (size_t)(p - s);
    *cur = p;
    return true;
}

static bool parse_number_token(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    const char *s = p;
    int digits = 0;
    if (p < end && (*p == '+' || *p == '-')) p++;
    while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    if (p < end && *p == '.') {
        p++;
        while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    }
    if (digits == 0) return false;
    *start = s;
    *len = (size_t)(p - s);
    *cur = p;
    return true;
}

static bool parse_filter_op(const char **cur, const char *end, filter_op_t *op) {
    const char *p = *cur;
    if (p >= end) return false;
    if (*p == '=' && p + 1 < end && p[1] == '=') { *op = FILTER_OP_EQ; *cur = p + 2; return true; }
    if (*p == '!' && p + 1 < end && p[1] == '=') { *op = FILTER_OP_NE; *cur = p + 2; return true; }
    if (*p == '>' && p + 1 < end && p[1] == '=') { *op = FILTER_OP_GE; *cur = p + 2; return true; }
    if (*p == '<' && p + 1 < end && p[1] == '=') { *op = FILTER_OP_LE; *cur = p + 2; return true; }
    if (*p == '>') { *op = FILTER_OP_GT; *cur = p + 1; return true; }
    if (*p == '<') { *op = FILTER_OP_LT; *cur = p + 1; return true; }
    return false;
}

static bool parse_filter_join(const char **cur, const char *end, filter_join_t *join) {
    const char *p = *cur;
    if (p >= end || !isalpha((unsigned char)*p)) return false;
    const char *s = p;
    while (p < end && isalpha((unsigned char)*p)) p++;
    size_t len = (size_t)(p - s);
    if (len == 3 &&
        tolower((unsigned char)s[0]) == 'a' &&
        tolower((unsigned char)s[1]) == 'n' &&
        tolower((unsigned char)s[2]) == 'd') {
        *join = FILTER_JOIN_AND;
        *cur = p;
        return true;
    }
    if (len == 2 &&
        tolower((unsigned char)s[0]) == 'o' &&
        tolower((unsigned char)s[1]) == 'r') {
        *join = FILTER_JOIN_OR;
        *cur = p;
        return true;
    }
    return false;
}

static bool parse_string_literal(const char **cur, const char *end, char **out, size_t *out_len) {
    const char *p = *cur;
    if (p >= end || *p != '"') return false;
    p++;

    size_t cap = 32;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    while (p < end) {
        char ch = *p++;
        if (ch == '"') break;
        if (ch == '\\' && p < end) {
            char esc = *p++;
            if (esc == '"' || esc == '\\') ch = esc;
            else {
                if (len + 2 > cap) {
                    size_t ncap = cap * 2;
                    char *nb = (char *)realloc(buf, ncap);
                    if (!nb) { free(buf); return false; }
                    buf = nb;
                    cap = ncap;
                }
                buf[len++] = '\\';
                ch = esc;
            }
        }
        if (len + 1 > cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); return false; }
            buf = nb;
            cap = ncap;
        }
        buf[len++] = ch;
    }

    if (p > end || (p <= end && *(p - 1) != '"')) {
        free(buf);
        return false;
    }

    if (len + 1 > cap) {
        char *nb = (char *)realloc(buf, len + 1);
        if (!nb) { free(buf); return false; }
        buf = nb;
    }
    buf[len] = '\0';

    *out = buf;
    *out_len = len;
    *cur = p;
    return true;
}

static bool filter_op_eval_num(filter_op_t op, double lhs, double rhs) {
    switch (op) {
        case FILTER_OP_EQ: return lhs == rhs;
        case FILTER_OP_NE: return lhs != rhs;
        case FILTER_OP_GT: return lhs > rhs;
        case FILTER_OP_GE: return lhs >= rhs;
        case FILTER_OP_LT: return lhs < rhs;
        case FILTER_OP_LE: return lhs <= rhs;
        default: return false;
    }
}

static int cmp_bytes(const char *a, size_t alen, const char *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) return c;
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static bool filter_op_eval_str(filter_op_t op, const char *lhs, size_t lhs_len, const char *rhs, size_t rhs_len) {
    int c = cmp_bytes(lhs, lhs_len, rhs, rhs_len);
    switch (op) {
        case FILTER_OP_EQ: return c == 0;
        case FILTER_OP_NE: return c != 0;
        case FILTER_OP_GT: return c > 0;
        case FILTER_OP_GE: return c >= 0;
        case FILTER_OP_LT: return c < 0;
        case FILTER_OP_LE: return c <= 0;
        default: return false;
    }
}

static int find_filter_column(const csv_stream_processor_t *p, const char *name, size_t len) {
    for (size_t i = 0; i < p->col_count; i++) {
        if (p->cols[i].name && strlen(p->cols[i].name) == len &&
            memcmp(p->cols[i].name, name, len) == 0) {
            return (int)i;
        }
        if (p->cols[i].raw_name && strlen(p->cols[i].raw_name) == len &&
            memcmp(p->cols[i].raw_name, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool compile_filter_plan(csv_stream_processor_t *p) {
    free_filter_plan(p);

    if (!p->has_filter || !p->filter_expr_str || p->col_count == 0) {
        return true;
    }

    const char *cur = p->filter_expr_str;
    const char *end = p->filter_expr_str + strlen(p->filter_expr_str);

    while (1) {
        filter_clause_t clause;
        memset(&clause, 0, sizeof(clause));

        const char *id_start = NULL;
        size_t id_len = 0;
        filter_op_t op;

        skip_ws(&cur, end);
        if (!parse_identifier(&cur, end, &id_start, &id_len)) {
            set_error(p, "invalid filter: expected column name");
            free_filter_plan(p);
            return false;
        }

    int col_idx = find_filter_column(p, id_start, id_len);
        if (col_idx < 0) {
            set_error(p, "invalid filter: unknown column");
            free_filter_plan(p);
            return false;
        }
        clause.col_idx = (size_t)col_idx;
        clause.col_type = p->cols[clause.col_idx].type;

        skip_ws(&cur, end);
        if (!parse_filter_op(&cur, end, &op)) {
            set_error(p, "invalid filter: expected operator");
            free_filter_plan(p);
            return false;
        }
        clause.op = op;

        skip_ws(&cur, end);
        if (cur < end && *cur == '"') {
            clause.rhs_is_string = true;
            if (!parse_string_literal(&cur, end, &clause.rhs_str, &clause.rhs_str_len)) {
                set_error(p, "invalid filter: bad string literal");
                free_filter_plan(p);
                return false;
            }
            if (clause.col_type != COL_STRING && clause.col_type != COL_RAW) {
                set_error(p, "invalid filter: string value with numeric column");
                free(clause.rhs_str);
                free_filter_plan(p);
                return false;
            }
        } else {
            const char *num_start = NULL;
            size_t num_len = 0;
            clause.rhs_is_string = false;
            if (!parse_number_token(&cur, end, &num_start, &num_len)) {
                set_error(p, "invalid filter: expected number or string");
                free_filter_plan(p);
                return false;
            }
            clause.rhs_num = fast_atof(num_start, num_len);
            if (clause.col_type == COL_STRING) {
                set_error(p, "invalid filter: numeric value with string column");
                free_filter_plan(p);
                return false;
            }
        }

        filter_clause_t *new_clauses =
            (filter_clause_t *)realloc(p->filter_clauses, sizeof(filter_clause_t) * (p->filter_clause_count + 1));
        if (!new_clauses) {
            set_error(p, "OOM compiling filter");
            free(clause.rhs_str);
            free_filter_plan(p);
            return false;
        }
        p->filter_clauses = new_clauses;
        p->filter_clauses[p->filter_clause_count] = clause;
        p->filter_clause_count++;

        skip_ws(&cur, end);
        if (cur >= end) break;

        filter_join_t join;
        if (!parse_filter_join(&cur, end, &join)) {
            set_error(p, "invalid filter: expected and/or");
            free_filter_plan(p);
            return false;
        }
        filter_join_t *new_joins =
            (filter_join_t *)realloc(p->filter_joins, sizeof(filter_join_t) * p->filter_clause_count);
        if (!new_joins) {
            set_error(p, "OOM compiling filter");
            free_filter_plan(p);
            return false;
        }
        p->filter_joins = new_joins;
        p->filter_joins[p->filter_clause_count - 1] = join;
    }

    if (p->filter_clause_count == 0) {
        set_error(p, "invalid filter: empty expression");
        free_filter_plan(p);
        return false;
    }

    p->filter_compiled = true;
    set_error(p, NULL);
    return true;
}

static bool eval_filter_plan(const csv_stream_processor_t *p, const char **field_ptrs, const size_t *field_lens) {
    if (!p->filter_compiled || p->filter_clause_count == 0) return true;

    bool result = false;
    for (size_t i = 0; i < p->filter_clause_count; i++) {
        const filter_clause_t *c = &p->filter_clauses[i];
        bool clause_ok;
        if (c->rhs_is_string) {
            clause_ok = filter_op_eval_str(c->op,
                                           field_ptrs[c->col_idx], field_lens[c->col_idx],
                                           c->rhs_str, c->rhs_str_len);
        } else {
            double lhs = fast_atof(field_ptrs[c->col_idx], field_lens[c->col_idx]);
            clause_ok = filter_op_eval_num(c->op, lhs, c->rhs_num);
        }

        if (i == 0) {
            result = clause_ok;
        } else if (p->filter_joins[i - 1] == FILTER_JOIN_AND) {
            result = result && clause_ok;
        } else {
            result = result || clause_ok;
        }
    }

    return result;
}

/* ── Field splitting (simple, no multiline quoted fields) ─────────── */

typedef struct {
    const char *start;
    size_t      len;
} field_span_t;

static size_t split_csv_line(const char *line, size_t line_len, char delim, char quote,
                              field_span_t *out, size_t max_fields) {
    size_t count = 0;
    const char *p = line;
    const char *end = line + line_len;
    bool trailing_delim = false;

    if (line_len == 0) return 0;

    while (p < end && count < max_fields) {
        const char *field_start;
        size_t field_len;
        trailing_delim = false;

        if (*p == quote) {
            /* Quoted field */
            p++; /* skip opening quote */
            field_start = p;
            while (p < end) {
                if (*p == quote) {
                    if (p + 1 < end && *(p + 1) == quote) {
                        p += 2; /* escaped quote */
                    } else {
                        break; /* closing quote */
                    }
                } else {
                    p++;
                }
            }
            field_len = (size_t)(p - field_start);
            if (p < end && *p == quote) p++; /* skip closing quote */
            if (p < end && *p == delim) { p++; trailing_delim = true; }
        } else {
            /* Unquoted field */
            field_start = p;
            while (p < end && *p != delim) p++;
            field_len = (size_t)(p - field_start);
            if (p < end && *p == delim) { p++; trailing_delim = true; }
        }

        out[count].start = field_start;
        out[count].len = field_len;
        count++;
    }

    /* Trailing delimiter → one more empty field */
    if (trailing_delim && count < max_fields) {
        out[count].start = p;
        out[count].len = 0;
        count++;
    }

    return count;
}

/* ── Unescape quoted field in-place into buffer ───────────────────── */

static size_t unescape_field(const char *src, size_t len, char quote, char *dst) {
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == quote && i + 1 < len && src[i + 1] == quote) {
            dst[j++] = quote;
            i++;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    return j;
}

/* ── Parse header line ────────────────────────────────────────────── */

static void parse_header(csv_stream_processor_t *p, const char *line, size_t len) {
    field_span_t spans[256];
    size_t n = split_csv_line(line, len, p->opts.delimiter, p->opts.quote, spans, 256);

    p->col_count = n;
    p->cols = MEM_ALLOC_ARRAY(&p->arena, sp_col_t, n);
    p->num_vecs = MEM_ALLOC_ARRAY(&p->arena, dvec_t, n);
    if (!p->cols || !p->num_vecs) { set_error(p, "OOM in parse_header"); return; }

    /* Note: dvec_t starts zero-initialized due to arena/calloc nature, or we memset */
    memset(p->cols, 0, sizeof(sp_col_t) * n);
    memset(p->num_vecs, 0, sizeof(dvec_t) * n);

    for (size_t i = 0; i < n; i++) {
        char *tmp = MEM_ALLOC_ARRAY(&p->arena, char, spans[i].len + 1);
        if (!tmp) { set_error(p, "OOM in parse_header"); return; }
        size_t flen = unescape_field(spans[i].start, spans[i].len, p->opts.quote, tmp);

        /* Trim whitespace */
        char *s = tmp;
        while (flen > 0 && isspace((unsigned char)*s)) { s++; flen--; }
        while (flen > 0 && isspace((unsigned char)s[flen - 1])) flen--;

        p->cols[i].raw_name = strndup_c(s, flen);
        p->cols[i].index = i;

        if (flen >= 2 && ends_with_ci(s, flen, "_n", 2)) {
            p->cols[i].type = COL_NUMBER;
            p->cols[i].name = strndup_c(s, flen - 2);
        } else if (flen >= 2 && ends_with_ci(s, flen, "_s", 2)) {
            p->cols[i].type = COL_STRING;
            p->cols[i].name = strndup_c(s, flen - 2);
        } else {
            p->cols[i].type = COL_RAW;
            p->cols[i].name = strndup_c(s, flen);
        }
    }

    /* Resolve column selection */
    p->col_selected = (bool *)calloc(n, sizeof(bool));
    if (p->has_col_selection && p->select_cols_str) {
        /* Parse comma-separated names, match against stripped col names */
        char *sel_str = strndup_c(p->select_cols_str, strlen(p->select_cols_str));
        char *tok = sel_str;
        while (tok && *tok) {
            char *comma = strchr(tok, ',');
            size_t tok_len;
            if (comma) { tok_len = (size_t)(comma - tok); *comma = '\0'; }
            else tok_len = strlen(tok);

            /* Trim whitespace */
            while (tok_len > 0 && *tok == ' ') { tok++; tok_len--; }
            while (tok_len > 0 && tok[tok_len - 1] == ' ') tok_len--;

            for (size_t i = 0; i < n; i++) {
                if (p->cols[i].name && strlen(p->cols[i].name) == tok_len &&
                    memcmp(p->cols[i].name, tok, tok_len) == 0) {
                    p->col_selected[i] = true;
                    break;
                }
                if (p->cols[i].raw_name && strlen(p->cols[i].raw_name) == tok_len &&
                    memcmp(p->cols[i].raw_name, tok, tok_len) == 0) {
                    p->col_selected[i] = true;
                    break;
                }
            }

            tok = comma ? comma + 1 : NULL;
        }
        free(sel_str);
    } else {
        /* No selection — store all columns */
        for (size_t i = 0; i < n; i++) p->col_selected[i] = true;
    }

    /* Only allocate string store if there are selected string-typed columns */
    bool has_string_cols = false;
    for (size_t i = 0; i < n; i++) {
        if (p->col_selected[i] && p->cols[i].type == COL_STRING) { has_string_cols = true; break; }
    }
    str_store_init(&p->str_store, has_string_cols ? n : 0, &p->arena);

    if (p->has_filter && p->filter_expr_str) {
        if (!compile_filter_plan(p)) {
            p->filter_compiled = false;
        }
    }

    p->header_parsed = true;
}

/* ── Process one data row ─────────────────────────────────────────── */

static void process_row(csv_stream_processor_t *p, const char *line, size_t len) {
    field_span_t spans[256];
    size_t n = split_csv_line(line, len, p->opts.delimiter, p->opts.quote, spans, 256);
    if (n == 0) return;

    size_t cols = n < p->col_count ? n : p->col_count;

    /* Stack buffer — tick/market data rows are always < 2KB.
     * If this isn't enough, the upstream data is broken. */
    char buf[2048];
    size_t total_need = 0;
    for (size_t i = 0; i < cols; i++) total_need += spans[i].len + 1;
    for (size_t i = cols; i < p->col_count; i++) total_need += 1;
    if (total_need > sizeof(buf)) {
        set_error(p, "row too wide (>2KB) — check upstream data");
        return;
    }

    const char *field_ptrs[256];
    size_t field_lens[256];
    size_t offset = 0;

    for (size_t i = 0; i < cols; i++) {
        field_lens[i] = unescape_field(spans[i].start, spans[i].len, p->opts.quote, buf + offset);
        field_ptrs[i] = buf + offset;
        offset += field_lens[i] + 1;
    }
    for (size_t i = cols; i < p->col_count; i++) {
        buf[offset] = '\0';
        field_ptrs[i] = buf + offset;
        field_lens[i] = 0;
        offset += 1;
    }

    if (p->filter_compiled && !eval_filter_plan(p, field_ptrs, field_lens)) {
        return;
    }

    /* Accumulate only selected columns. */
    for (size_t i = 0; i < p->col_count; i++) {
        if (!p->col_selected[i]) continue;
        if (p->cols[i].type == COL_NUMBER) {
            double val = fast_atof(field_ptrs[i], field_lens[i]);
            dvec_push(&p->num_vecs[i], val, &p->arena);
        }
    }

    /* Store strings only for selected string-typed columns */
    if (p->str_store.col_count > 0)
        str_store_push_row(&p->str_store, field_ptrs, field_lens, p->col_count, p->col_selected, p->cols, &p->arena);

    p->match_count++;
}

/* ── Process buffered lines ───────────────────────────────────────── */

static void flush_lines(csv_stream_processor_t *p) {
    while (p->line_len > 0) {
        /* Find next newline */
        char *nl = (char *)memchr(p->line_buf, '\n', p->line_len);
        if (!nl) break;

        size_t row_len = (size_t)(nl - p->line_buf);

        /* Strip \r if present */
        size_t effective_len = row_len;
        if (effective_len > 0 && p->line_buf[effective_len - 1] == '\r')
            effective_len--;

        /* Skip empty lines */
        if (effective_len > 0) {
            if (!p->header_parsed) {
                parse_header(p, p->line_buf, effective_len);
            } else {
                process_row(p, p->line_buf, effective_len);
            }
        }

        /* Consume the line + newline */
        size_t consumed = row_len + 1;
        p->line_len -= consumed;
        if (p->line_len > 0)
            memmove(p->line_buf, p->line_buf + consumed, p->line_len);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

csv_stream_processor_t *csv_stream_processor_create(const csv_options_t *opts) {
    csv_stream_processor_t *p = (csv_stream_processor_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;

    mem_init(&p->arena, 65536);

    if (opts) {
        p->opts = *opts;
        if (p->opts.delimiter == '\0') p->opts.delimiter = ',';
        if (p->opts.quote == '\0') p->opts.quote = '"';
    } else {
        p->opts = (csv_options_t){ true, ',', '"', true };
    }

    p->line_cap = 65536;
    p->line_buf = (char *)malloc(p->line_cap); // keep line_buf as simple malloc since we feed chunks dynamically.
    if (!p->line_buf) { mem_destroy(&p->arena); free(p); return NULL; }
    p->line_len = 0;

    return p;
}

void csv_stream_processor_destroy(csv_stream_processor_t *p) {
    if (!p) return;

    free(p->line_buf);
    free(p->filter_expr_str);
    free(p->select_cols_str);
    free(p->col_selected);
    free_filter_plan(p);
    free_column_names(p);

    str_store_free(&p->str_store);
    mem_destroy(&p->arena);
    free(p);
}

bool csv_stream_processor_set_filter(csv_stream_processor_t *p, const char *expr) {
    if (!p || !expr) return false;
    free(p->filter_expr_str);
    p->filter_expr_str = strndup_c(expr, strlen(expr));
    p->has_filter = (p->filter_expr_str != NULL);
    p->filter_compiled = false;
    free_filter_plan(p);
    if (!p->has_filter) {
        set_error(p, "OOM in set_filter");
        return false;
    }

    if (p->header_parsed) {
        return compile_filter_plan(p);
    }
    return true;
}

void csv_stream_processor_set_columns(csv_stream_processor_t *p, const char *names) {
    if (!p || !names) return;
    free(p->select_cols_str);
    p->select_cols_str = strndup_c(names, strlen(names));
    p->has_col_selection = (p->select_cols_str != NULL);
}

void csv_stream_processor_feed(const char *data, size_t len, void *user_data) {
    csv_stream_processor_t *p = (csv_stream_processor_t *)user_data;
    if (!p || !data || len == 0) return;

    /* Grow line buffer if needed */
    size_t need = p->line_len + len;
    if (need > p->line_cap) {
        size_t new_cap = p->line_cap;
        while (new_cap < need) new_cap *= 2;
        char *nb = (char *)realloc(p->line_buf, new_cap);
        if (!nb) { set_error(p, "OOM in feed"); return; }
        p->line_buf = nb;
        p->line_cap = new_cap;
    }

    memcpy(p->line_buf + p->line_len, data, len);
    p->line_len += len;

    flush_lines(p);
}

void csv_stream_processor_finish(csv_stream_processor_t *p) {
    if (!p) return;

    /* Process any remaining data without trailing newline */
    if (p->line_len > 0) {
        size_t effective_len = p->line_len;
        if (effective_len > 0 && p->line_buf[effective_len - 1] == '\r')
            effective_len--;

        if (effective_len > 0) {
            if (!p->header_parsed) {
                parse_header(p, p->line_buf, effective_len);
            } else {
                process_row(p, p->line_buf, effective_len);
            }
        }
        p->line_len = 0;
    }
}

size_t csv_stream_processor_row_count(const csv_stream_processor_t *p) {
    return p ? p->match_count : 0;
}

size_t csv_stream_processor_col_count(const csv_stream_processor_t *p) {
    return p ? p->col_count : 0;
}

const char *csv_stream_processor_col_name(const csv_stream_processor_t *p, size_t idx) {
    if (!p || idx >= p->col_count) return NULL;
    return p->cols[idx].raw_name;
}

size_t csv_stream_processor_col_index(const csv_stream_processor_t *p, const char *name) {
    if (!p || !name) return (size_t)-1;
    size_t name_len = strlen(name);
    for (size_t i = 0; i < p->col_count; i++) {
        /* Match against stripped name (without _n/_s) */
        if (p->cols[i].name && strlen(p->cols[i].name) == name_len &&
            memcmp(p->cols[i].name, name, name_len) == 0)
            return i;
        /* Also match against raw name */
        if (p->cols[i].raw_name && strlen(p->cols[i].raw_name) == name_len &&
            memcmp(p->cols[i].raw_name, name, name_len) == 0)
            return i;
    }
    return (size_t)-1;
}

const double *csv_stream_processor_col_data(const csv_stream_processor_t *p,
                                              size_t col, size_t *out_len) {
    if (!p || col >= p->col_count || !p->num_vecs || p->cols[col].type != COL_NUMBER) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = p->num_vecs[col].len;
    return p->num_vecs[col].data;
}

const char *csv_stream_processor_get_str(const csv_stream_processor_t *p,
                                          size_t row, size_t col) {
    if (!p || row >= p->str_store.row_count || col >= p->str_store.col_count || !p->str_store.rows_buf)
        return NULL;

    str_entry_t **rows = (str_entry_t **)p->str_store.rows_buf->data;
    if (!rows) return NULL;

    str_entry_t *entries = rows[row];
    if (!entries) return NULL;

    return entries[col].str;
}

const char *csv_stream_processor_error(const csv_stream_processor_t *p) {
    return p ? p->error : "";
}
