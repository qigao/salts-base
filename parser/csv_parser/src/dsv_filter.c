#include "dsv_filter.h"
#include "dsv_filter_expr_parser.h"
#include "query_vm.h"
#include <tstr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    char *name;
    char *raw_name;
    enum { COL_UNUSED = 0, COL_NUMBER, COL_STRING, COL_DYNAMIC } type;
    size_t index;
} dsv_col_t;

typedef enum { DSV_OP_EQ = 0, DSV_OP_NE, DSV_OP_GT, DSV_OP_GE, DSV_OP_LT, DSV_OP_LE } dsv_op_t;
typedef enum { DSV_JOIN_AND = 0, DSV_JOIN_OR } dsv_join_t;

typedef struct {
    size_t col_idx;
    int col_type;
    int lhs_is_simple_col;
    int lhs_has_arith;
    dsv_expr_item_t *lhs_expr;
    size_t lhs_expr_count;
    dsv_op_t op;
    int rhs_is_string;
    double rhs_num;
    char *rhs_str;
    size_t rhs_str_len;
} dsv_clause_t;

typedef enum {
    DSV_OPERAND_COLUMN = 0,
    DSV_OPERAND_NUMBER,
    DSV_OPERAND_STRING
} dsv_operand_kind_t;

typedef struct {
    dsv_operand_kind_t kind;
    size_t col_idx;
    int as_number;
    double num;
    const char *str;
    size_t str_len;
} dsv_operand_t;

/* DSV qvm_value_t type tags used by the QVM execution callbacks. */
enum {
    DSV_QVM_INVALID = 0,
    DSV_QVM_NUMBER = 1,
    DSV_QVM_STRING = 2,
    DSV_QVM_BOOL = 3
};

static char *dsv_strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *new_s = (char *)malloc(len + 1);
    if (new_s) {
        memcpy(new_s, s, len);
        new_s[len] = '\0';
    }
    return new_s;
}

struct dsv_filter_s {
    const csv_doc_t *doc;
    size_t header_row;

    dsv_col_t *cols;
    size_t col_count;

    dsv_clause_t *clauses;
    dsv_join_t *joins;
    size_t clause_count;
    int compiled;
    csv_scan_predicate_t *scan_predicates;
    int scan_plan_compatible;

    qvm_instruction_t *expr_vm;
    uint32_t expr_vm_count;
    dsv_operand_t *operands;
    size_t operand_count;
    int qvm_ready;
    qvm_limits_t qvm_limits;
    qvm_diagnostic_t qvm_diagnostic;
    int qvm_diagnostics;

    char output_delim;
    char error_msg[256];
};

static void dsv_qvm_diagnostic_clear(qvm_diagnostic_t *diagnostic) {
    if (!diagnostic) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = QVM_STATUS_OK;
    diagnostic->instruction = QVM_NO_INSTRUCTION;
    diagnostic->opcode = QVM_NO_OPCODE;
    diagnostic->operand = QVM_NO_OPERAND;
}

const qvm_diagnostic_t *dsv_filter_qvm_diagnostic(const dsv_filter_t *filter) {
    return filter ? &filter->qvm_diagnostic : NULL;
}

static void set_error(dsv_filter_t *filter, const char *msg) {
    if (!filter) return;
    if (!msg) {
        filter->error_msg[0] = '\0';
        return;
    }
    strncpy(filter->error_msg, msg, sizeof(filter->error_msg) - 1);
    filter->error_msg[sizeof(filter->error_msg) - 1] = '\0';
}

const char *dsv_filter_error(dsv_filter_t *filter) {
    if (!filter) return "";
    return filter->error_msg;
}

static bool dsv_ends_with(const char *str, const char *suffix) {
    size_t len = strlen(str);
    size_t slen = strlen(suffix);
    if (len < slen) return false;
    const char *end = str + len - slen;
    while (*suffix) {
        if (tolower((unsigned char)*end) != tolower((unsigned char)*suffix)) return false;
        end++;
        suffix++;
    }
    return true;
}

static inline double dsv_fast_atof(const char *s, size_t len) {
    if (!s || len == 0) return 0.0;

    const char *p = s;
    const char *end = s + len;
    int neg = 0;

    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    unsigned long long mantissa = 0;
    int frac_digits = 0;

    while (p < end && (*p >= '0' && *p <= '9')) {
        mantissa = mantissa * 10ULL + (unsigned long long)(*p - '0');
        p++;
    }

    if (p < end && *p == '.') {
        p++;
        while (p < end && (*p >= '0' && *p <= '9')) {
            mantissa = mantissa * 10ULL + (unsigned long long)(*p - '0');
            frac_digits++;
            p++;
        }
    }

    {
        double result = (double)mantissa;
        static const double pow10[] = {
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
            1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
        };
        if (frac_digits > 0) {
            if (frac_digits < 19) result /= pow10[frac_digits];
            else {
                int i;
                for (i = 0; i < frac_digits; i++) result /= 10.0;
            }
        }
        return neg ? -result : result;
    }
}

static void free_clause_data(dsv_clause_t *clause) {
    size_t i;
    if (!clause) return;
    free(clause->rhs_str);
    if (clause->lhs_expr) {
        for (i = 0; i < clause->lhs_expr_count; i++) {
            free(clause->lhs_expr[i].ident);
        }
    }
    free(clause->lhs_expr);
    clause->rhs_str = NULL;
    clause->lhs_expr = NULL;
    clause->rhs_str_len = 0;
    clause->lhs_expr_count = 0;
    clause->lhs_is_simple_col = 0;
    clause->lhs_has_arith = 0;
}

static void free_plan(dsv_filter_t *filter) {
    size_t i;
    if (!filter) return;
    if (filter->clauses) {
        for (i = 0; i < filter->clause_count; i++) {
            free_clause_data(&filter->clauses[i]);
        }
    }
    free(filter->clauses);
    free(filter->joins);
    free(filter->scan_predicates);
    free(filter->expr_vm);
    free(filter->operands);
    filter->clauses = NULL;
    filter->joins = NULL;
    filter->scan_predicates = NULL;
    filter->expr_vm = NULL;
    filter->operands = NULL;
    filter->clause_count = 0;
    filter->compiled = 0;
    filter->scan_plan_compatible = 0;
    filter->expr_vm_count = 0;
    filter->operand_count = 0;
    filter->qvm_ready = 0;
}

static void skip_ws(const char **cur, const char *end) {
    while (*cur < end && isspace((unsigned char)**cur)) (*cur)++;
}

static int parse_identifier(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    if (p >= end || !(isalpha((unsigned char)*p) || *p == '_')) return 0;
    *start = p++;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    *len = (size_t)(p - *start);
    *cur = p;
    return 1;
}

static int parse_number_token(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    const char *s = p;
    int digits = 0;
    if (p < end && (*p == '+' || *p == '-')) p++;
    while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    if (p < end && *p == '.') {
        p++;
        while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    }
    if (digits == 0) return 0;
    *start = s;
    *len = (size_t)(p - s);
    *cur = p;
    return 1;
}

static int parse_op(const char **cur, const char *end, dsv_op_t *op) {
    const char *p = *cur;
    if (p >= end) return 0;
    if (*p == '=' && p + 1 < end && p[1] == '=') { *op = DSV_OP_EQ; *cur = p + 2; return 1; }
    if (*p == '!' && p + 1 < end && p[1] == '=') { *op = DSV_OP_NE; *cur = p + 2; return 1; }
    if (*p == '>' && p + 1 < end && p[1] == '=') { *op = DSV_OP_GE; *cur = p + 2; return 1; }
    if (*p == '<' && p + 1 < end && p[1] == '=') { *op = DSV_OP_LE; *cur = p + 2; return 1; }
    if (*p == '>') { *op = DSV_OP_GT; *cur = p + 1; return 1; }
    if (*p == '<') { *op = DSV_OP_LT; *cur = p + 1; return 1; }
    return 0;
}

static int parse_join(const char **cur, const char *end, dsv_join_t *join) {
    const char *p = *cur;
    const char *s;
    size_t len;
    if (p >= end || !isalpha((unsigned char)*p)) return 0;
    s = p;
    while (p < end && isalpha((unsigned char)*p)) p++;
    len = (size_t)(p - s);
    if (len == 3 &&
        tolower((unsigned char)s[0]) == 'a' &&
        tolower((unsigned char)s[1]) == 'n' &&
        tolower((unsigned char)s[2]) == 'd') {
        *join = DSV_JOIN_AND;
        *cur = p;
        return 1;
    }
    if (len == 2 &&
        tolower((unsigned char)s[0]) == 'o' &&
        tolower((unsigned char)s[1]) == 'r') {
        *join = DSV_JOIN_OR;
        *cur = p;
        return 1;
    }
    return 0;
}

static void trim_ws_range(const char **start, const char **end) {
    while (*start < *end && isspace((unsigned char)**start)) (*start)++;
    while (*end > *start && isspace((unsigned char)*((*end) - 1))) (*end)--;
}

static int parse_simple_identifier_range(const char *start, const char *end, const char **id_start, size_t *id_len) {
    const char *cur = start;
    if (!parse_identifier(&cur, end, id_start, id_len)) return 0;
    skip_ws(&cur, end);
    return cur == end;
}

static int find_top_level_cmp(const char *start, const char *end, const char **op_start, const char **after_op, dsv_op_t *op) {
    const char *p = start;
    int depth = 0;
    while (p < end) {
        if (*p == '(') {
            depth++;
            p++;
            continue;
        }
        if (*p == ')') {
            if (depth > 0) depth--;
            p++;
            continue;
        }
        if (depth == 0) {
            const char *tmp = p;
            dsv_op_t found_op;
            if (parse_op(&tmp, end, &found_op)) {
                *op_start = p;
                *after_op = tmp;
                *op = found_op;
                return 1;
            }
        }
        p++;
    }
    return 0;
}

static int dsv_is_arith_op_char(char ch) {
    return ch == '+' || ch == '-' || ch == '*' || ch == '/';
}

static int check_lhs_parentheses(const char *start, const char *end, const char **msg_out) {
    const char *p = start;
    int depth = 0;
    while (p < end) {
        if (*p == '(') {
            const char *q = p + 1;
            depth++;
            while (q < end && isspace((unsigned char)*q)) q++;
            if (q < end && *q == ')') {
                if (msg_out) *msg_out = "invalid filter: empty parentheses in arithmetic expression";
                return 0;
            }
        } else if (*p == ')') {
            if (depth == 0) {
                if (msg_out) *msg_out = "invalid filter: unbalanced parentheses in arithmetic expression";
                return 0;
            }
            depth--;
        }
        p++;
    }
    if (depth != 0) {
        if (msg_out) *msg_out = "invalid filter: unbalanced parentheses in arithmetic expression";
        return 0;
    }
    return 1;
}

static int has_trailing_arith_op(const char *start, const char *end) {
    const char *p = end;
    while (p > start && isspace((unsigned char)*(p - 1))) p--;
    if (p <= start) return 0;
    return dsv_is_arith_op_char(*(p - 1));
}

static int parse_string_lit(const char **cur, const char *end, char **out, size_t *out_len) {
    const char *p = *cur;
    size_t cap = 32;
    size_t len = 0;
    char *buf;
    if (p >= end || *p != '"') return 0;
    p++;
    buf = (char *)malloc(cap);
    if (!buf) return 0;

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
                    if (!nb) { free(buf); return 0; }
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
            if (!nb) { free(buf); return 0; }
            buf = nb;
            cap = ncap;
        }
        buf[len++] = ch;
    }

    if (p > end || (p <= end && *(p - 1) != '"')) {
        free(buf);
        return 0;
    }

    if (len + 1 > cap) {
        char *nb = (char *)realloc(buf, len + 1);
        if (!nb) { free(buf); return 0; }
        buf = nb;
    }
    buf[len] = '\0';

    *out = buf;
    *out_len = len;
    *cur = p;
    return 1;
}

static int cmp_bytes(const char *a, size_t alen, const char *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) return c;
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static int eval_num(dsv_op_t op, double lhs, double rhs) {
    switch (op) {
        case DSV_OP_EQ: return lhs == rhs;
        case DSV_OP_NE: return lhs != rhs;
        case DSV_OP_GT: return lhs > rhs;
        case DSV_OP_GE: return lhs >= rhs;
        case DSV_OP_LT: return lhs < rhs;
        case DSV_OP_LE: return lhs <= rhs;
        default: return 0;
    }
}

static int eval_str(dsv_op_t op, vstr lhs, const char *rhs, size_t rhs_len) {
    int c;
    if (!lhs.data) lhs = vstr_from_buf("", 0);
    c = cmp_bytes(lhs.data, lhs.len, rhs, rhs_len);
    switch (op) {
        case DSV_OP_EQ: return c == 0;
        case DSV_OP_NE: return c != 0;
        case DSV_OP_GT: return c > 0;
        case DSV_OP_GE: return c >= 0;
        case DSV_OP_LT: return c < 0;
        case DSV_OP_LE: return c <= 0;
        default: return 0;
    }
}

static int find_col(const dsv_filter_t *filter, const char *name, size_t len) {
    size_t i;
    for (i = 0; i < filter->col_count; i++) {
        const dsv_col_t *c = &filter->cols[i];
        if (c->type == COL_UNUSED) continue;
        if (c->name && strlen(c->name) == len && memcmp(c->name, name, len) == 0) return (int)i;
        if (c->raw_name && strlen(c->raw_name) == len && memcmp(c->raw_name, name, len) == 0) return (int)i;
    }
    return -1;
}

static int parse_lhs_numeric_expr(dsv_filter_t *filter, const char *start, const char *end, dsv_clause_t *clause) {
    dsv_expr_output_t out = {0};
    char expr_error[256] = {0};
    size_t i;
    int has_column = 0;
    const char *paren_msg = NULL;

    if (!check_lhs_parentheses(start, end, &paren_msg)) {
        set_error(filter, paren_msg);
        return 0;
    }

    if (!dsv_expr_parse(start, (size_t)(end - start), &out, expr_error, sizeof(expr_error))) {
        if (strstr(expr_error, "invalid character") != NULL) {
            set_error(filter, "invalid filter: invalid character in arithmetic expression");
        } else if (has_trailing_arith_op(start, end)) {
            set_error(filter, "invalid filter: expected column or number after +/-");
        } else {
            set_error(filter, "invalid filter: malformed arithmetic expression");
        }
        return 0;
    }
    if (out.count == 0) {
        dsv_expr_output_free(&out);
        set_error(filter, "invalid filter: expected column");
        return 0;
    }

    for (i = 0; i < out.count; i++) {
        dsv_expr_item_t *it = &out.items[i];
        if (it->kind == DSV_EXPR_ITEM_IDENT) {
            int col_idx = find_col(filter, it->ident, it->ident_len);
            if (col_idx < 0) {
                dsv_expr_output_free(&out);
                set_error(filter, "invalid filter: unknown column");
                return 0;
            }
            if (filter->cols[col_idx].type != COL_NUMBER) {
                dsv_expr_output_free(&out);
                set_error(filter, "invalid filter: arithmetic requires numeric columns");
                return 0;
            }
            it->col_idx = (size_t)col_idx;
            has_column = 1;
        }
    }

    if (!has_column) {
        dsv_expr_output_free(&out);
        set_error(filter, "invalid filter: expected column");
        return 0;
    }

    clause->col_type = COL_NUMBER;
    clause->lhs_is_simple_col = 0;
    clause->lhs_has_arith = 1;
    clause->lhs_expr = out.items;
    clause->lhs_expr_count = out.count;
    return 1;
}

static int eval_lhs_numeric_expr(const dsv_filter_t *filter, const dsv_clause_t *clause, size_t row_index, double *out_value) {
    size_t i;
    size_t sp = 0;
    size_t cap = 64;
    double local_stack[64];
    double *stack = local_stack;
    int ok = 0;

    if (clause->lhs_is_simple_col) {
        if (clause->col_idx >= filter->col_count) return 0;
        *out_value = csv_get_double(filter->doc, row_index, clause->col_idx, 0.0);
        return 1;
    }
    if (!clause->lhs_expr || clause->lhs_expr_count == 0) return 0;

    if (clause->lhs_expr_count > cap) {
        stack = (double *)malloc(sizeof(double) * clause->lhs_expr_count);
        if (!stack) return 0;
        cap = clause->lhs_expr_count;
    }

    for (i = 0; i < clause->lhs_expr_count; i++) {
        const dsv_expr_item_t *it = &clause->lhs_expr[i];
        if (it->kind == DSV_EXPR_ITEM_NUMBER) {
            if (sp >= cap) goto done;
            stack[sp++] = it->num;
        } else if (it->kind == DSV_EXPR_ITEM_IDENT) {
            if (it->col_idx >= filter->col_count) goto done;
            if (sp >= cap) goto done;
            stack[sp++] = csv_get_double(filter->doc, row_index, it->col_idx, 0.0);
        } else if (it->kind == DSV_EXPR_ITEM_NEG) {
            if (sp < 1) goto done;
            stack[sp - 1] = -stack[sp - 1];
        } else {
            double rhs, lhs, v;
            if (sp < 2) goto done;
            rhs = stack[--sp];
            lhs = stack[--sp];
            switch (it->kind) {
                case DSV_EXPR_ITEM_ADD: v = lhs + rhs; break;
                case DSV_EXPR_ITEM_SUB: v = lhs - rhs; break;
                case DSV_EXPR_ITEM_MUL: v = lhs * rhs; break;
                case DSV_EXPR_ITEM_DIV: v = lhs / rhs; break;
                default: goto done;
            }
            stack[sp++] = v;
        }
    }

    if (sp != 1) goto done;
    *out_value = stack[0];
    ok = 1;

done:
    if (stack != local_stack) free(stack);
    return ok;
}

static vstr dsv_value_at(const vstr *fields, size_t field_count, size_t col_idx) {
    if (!fields || col_idx >= field_count) return vstr_from_buf("", 0);
    return fields[col_idx];
}

static double dsv_value_double_at(const vstr *fields, size_t field_count, size_t col_idx) {
    vstr value = dsv_value_at(fields, field_count, col_idx);
    return dsv_fast_atof(value.data, value.len);
}

static int eval_lhs_numeric_expr_values(const dsv_filter_t *filter, const dsv_clause_t *clause,
                                        const vstr *fields, size_t field_count,
                                        double *out_value) {
    size_t i;
    size_t sp = 0;
    size_t cap = 64;
    double local_stack[64];
    double *stack = local_stack;
    int ok = 0;

    if (clause->lhs_is_simple_col) {
        if (clause->col_idx >= filter->col_count) return 0;
        *out_value = dsv_value_double_at(fields, field_count, clause->col_idx);
        return 1;
    }
    if (!clause->lhs_expr || clause->lhs_expr_count == 0) return 0;

    if (clause->lhs_expr_count > cap) {
        stack = (double *)malloc(sizeof(double) * clause->lhs_expr_count);
        if (!stack) return 0;
        cap = clause->lhs_expr_count;
    }

    for (i = 0; i < clause->lhs_expr_count; i++) {
        const dsv_expr_item_t *it = &clause->lhs_expr[i];
        if (it->kind == DSV_EXPR_ITEM_NUMBER) {
            if (sp >= cap) goto done;
            stack[sp++] = it->num;
        } else if (it->kind == DSV_EXPR_ITEM_IDENT) {
            if (it->col_idx >= filter->col_count) goto done;
            if (sp >= cap) goto done;
            stack[sp++] = dsv_value_double_at(fields, field_count, it->col_idx);
        } else if (it->kind == DSV_EXPR_ITEM_NEG) {
            if (sp < 1) goto done;
            stack[sp - 1] = -stack[sp - 1];
        } else {
            double rhs, lhs, v;
            if (sp < 2) goto done;
            rhs = stack[--sp];
            lhs = stack[--sp];
            switch (it->kind) {
                case DSV_EXPR_ITEM_ADD: v = lhs + rhs; break;
                case DSV_EXPR_ITEM_SUB: v = lhs - rhs; break;
                case DSV_EXPR_ITEM_MUL: v = lhs * rhs; break;
                case DSV_EXPR_ITEM_DIV: v = lhs / rhs; break;
                default: goto done;
            }
            stack[sp++] = v;
        }
    }

    if (sp != 1) goto done;
    *out_value = stack[0];
    ok = 1;

done:
    if (stack != local_stack) free(stack);
    return ok;
}

/* ---------------------------------------------------------------------------
 * QVM execution: DSV filter expressions are lowered to query_vm bytecode. The
 * VM owns register/operand verification and dispatch; this frontend owns
 * column resolution, numeric coercion and comparison semantics through
 * qvm_exec_ops_t. If lowering is impossible (register overflow, allocation
 * failure) the native evaluators remain the fallback, so user-visible
 * behavior is unchanged.
 * ------------------------------------------------------------------------- */

typedef struct {
    qvm_instruction_t *insns;
    size_t count;
    size_t capacity;
    dsv_operand_t *operands;
    size_t operand_count;
    size_t operand_capacity;
    int overflow;
} dsv_qvm_builder_t;

static int dsv_qvm_emit(dsv_qvm_builder_t *builder, uint8_t op, uint16_t dst,
                        uint32_t arg, uint32_t src1, uint32_t src2) {
    qvm_instruction_t *insns;
    size_t new_capacity;
    if (builder->overflow) return 0;
    if (builder->count == builder->capacity) {
        new_capacity = builder->capacity ? builder->capacity * 2U : 32U;
        if (new_capacity > QVM_DEFAULT_MAX_INSTRUCTIONS) {
            builder->overflow = 1;
            return 0;
        }
        insns = (qvm_instruction_t *)realloc(builder->insns,
                                             new_capacity * sizeof(*insns));
        if (!insns) {
            builder->overflow = 1;
            return 0;
        }
        builder->insns = insns;
        builder->capacity = new_capacity;
    }
    builder->insns[builder->count].op = op;
    builder->insns[builder->count].reserved = 0;
    builder->insns[builder->count].dst = dst;
    builder->insns[builder->count].arg = arg;
    builder->insns[builder->count].src1 = src1;
    builder->insns[builder->count].src2 = src2;
    builder->count++;
    return 1;
}

static uint32_t dsv_qvm_add_operand(dsv_qvm_builder_t *builder,
                                    const dsv_operand_t *operand) {
    dsv_operand_t *operands;
    size_t new_capacity;
    if (builder->overflow || !operand) return UINT32_MAX;
    if (builder->operand_count == builder->operand_capacity) {
        new_capacity = builder->operand_capacity ? builder->operand_capacity * 2U : 8U;
        if (new_capacity > QVM_DEFAULT_MAX_OPERANDS) {
            builder->overflow = 1;
            return UINT32_MAX;
        }
        operands = (dsv_operand_t *)realloc(builder->operands,
                                            new_capacity * sizeof(*operands));
        if (!operands) {
            builder->overflow = 1;
            return UINT32_MAX;
        }
        builder->operands = operands;
        builder->operand_capacity = new_capacity;
    }
    if (builder->operand_count >= UINT32_MAX) {
        builder->overflow = 1;
        return UINT32_MAX;
    }
    builder->operands[builder->operand_count] = *operand;
    builder->operand_count++;
    return (uint32_t)(builder->operand_count - 1);
}

static uint32_t dsv_qvm_add_operand_number(dsv_qvm_builder_t *builder, double num) {
    dsv_operand_t operand;
    memset(&operand, 0, sizeof(operand));
    operand.kind = DSV_OPERAND_NUMBER;
    operand.num = num;
    return dsv_qvm_add_operand(builder, &operand);
}

static uint32_t dsv_qvm_add_operand_string(dsv_qvm_builder_t *builder,
                                           const char *str, size_t len) {
    dsv_operand_t operand;
    memset(&operand, 0, sizeof(operand));
    operand.kind = DSV_OPERAND_STRING;
    operand.str = str;
    operand.str_len = len;
    return dsv_qvm_add_operand(builder, &operand);
}

static uint32_t dsv_qvm_add_operand_column(dsv_qvm_builder_t *builder,
                                           size_t col_idx, int as_number) {
    dsv_operand_t operand;
    memset(&operand, 0, sizeof(operand));
    operand.kind = DSV_OPERAND_COLUMN;
    operand.col_idx = col_idx;
    operand.as_number = as_number;
    return dsv_qvm_add_operand(builder, &operand);
}

static uint8_t dsv_qvm_arith_opcode(int kind) {
    switch (kind) {
        case DSV_EXPR_ITEM_ADD: return QVM_OP_ADD;
        case DSV_EXPR_ITEM_SUB: return QVM_OP_SUB;
        case DSV_EXPR_ITEM_MUL: return QVM_OP_MUL;
        case DSV_EXPR_ITEM_DIV: return QVM_OP_DIV;
        default: return QVM_OP_COUNT_VALUE;
    }
}

/* Lower a numeric LHS expression (postfix item stream) into QVM registers.
 * Register 0 is the clause result; temporaries start at register 1. */
static int dsv_qvm_lower_arith(dsv_qvm_builder_t *builder,
                               const dsv_clause_t *clause) {
    uint16_t regs[QVM_MAX_REGISTERS];
    size_t sp = 0;
    uint16_t free_regs[QVM_MAX_REGISTERS];
    size_t free_count = 0;
    uint16_t next_reg = 1;
    size_t i;

    for (i = 0; i < clause->lhs_expr_count; i++) {
        const dsv_expr_item_t *item = &clause->lhs_expr[i];
        if (item->kind == DSV_EXPR_ITEM_NUMBER ||
            item->kind == DSV_EXPR_ITEM_IDENT) {
            uint16_t reg;
            uint32_t operand;
            uint8_t load_op;
            if (sp >= QVM_MAX_REGISTERS) return 0;
            if (free_count > 0) reg = free_regs[--free_count];
            else if (next_reg < QVM_MAX_REGISTERS) reg = next_reg++;
            else return 0;
            if (item->kind == DSV_EXPR_ITEM_NUMBER) {
                operand = dsv_qvm_add_operand_number(builder, item->num);
                load_op = QVM_OP_LOAD_CONST;
            } else {
                operand = dsv_qvm_add_operand_column(builder, item->col_idx, 1);
                load_op = QVM_OP_LOAD_PATH;
            }
            if (operand == UINT32_MAX) return 0;
            if (!dsv_qvm_emit(builder, load_op, reg, 0, operand, 0)) return 0;
            regs[sp++] = reg;
        } else if (item->kind == DSV_EXPR_ITEM_NEG) {
            uint16_t reg;
            if (sp == 0) return 0;
            reg = regs[--sp];
            if (!dsv_qvm_emit(builder, QVM_OP_NEG, reg, 0, reg, 0)) return 0;
            regs[sp++] = reg;
        } else {
            uint8_t opcode = dsv_qvm_arith_opcode(item->kind);
            uint16_t rhs;
            uint16_t lhs;
            if (opcode == QVM_OP_COUNT_VALUE || sp < 2) return 0;
            rhs = regs[--sp];
            lhs = regs[--sp];
            if (free_count < QVM_MAX_REGISTERS) free_regs[free_count++] = rhs;
            if (!dsv_qvm_emit(builder, opcode, lhs, 0, lhs, rhs)) return 0;
            regs[sp++] = lhs;
        }
    }
    if (sp != 1 || builder->overflow) return 0;

    {
        uint32_t operand = dsv_qvm_add_operand_number(builder, clause->rhs_num);
        uint16_t rhs_reg;
        if (operand == UINT32_MAX) return 0;
        if (free_count > 0) rhs_reg = free_regs[--free_count];
        else if (next_reg < QVM_MAX_REGISTERS) rhs_reg = next_reg++;
        else return 0;
        if (!dsv_qvm_emit(builder, QVM_OP_LOAD_CONST, rhs_reg, 0, operand, 0)) return 0;
        if (!dsv_qvm_emit(builder, QVM_OP_CMP, 0, (uint32_t)clause->op,
                          regs[0], rhs_reg)) return 0;
    }
    return 1;
}

static int dsv_qvm_lower_simple(dsv_qvm_builder_t *builder,
                                const dsv_clause_t *clause) {
    uint32_t lhs_operand;
    uint32_t rhs_operand;
    uint8_t opcode;
    if (clause->rhs_is_string) {
        lhs_operand = dsv_qvm_add_operand_column(builder, clause->col_idx, 0);
        rhs_operand = dsv_qvm_add_operand_string(builder, clause->rhs_str,
                                                 clause->rhs_str_len);
        opcode = QVM_OP_CMP_LEAF_STRING;
    } else {
        lhs_operand = dsv_qvm_add_operand_column(builder, clause->col_idx, 1);
        rhs_operand = dsv_qvm_add_operand_number(builder, clause->rhs_num);
        opcode = QVM_OP_CMP_LEAF_NUMBER;
    }
    if (lhs_operand == UINT32_MAX || rhs_operand == UINT32_MAX) return 0;
    return dsv_qvm_emit(builder, opcode, 0, (uint32_t)clause->op,
                        lhs_operand, rhs_operand);
}

/* Highest register index + 1 referenced by the emitted slice. Mirrors the
 * verifier's register classification in query_vm.c. */
static uint32_t dsv_qvm_register_count(const qvm_instruction_t *insns,
                                       uint32_t count) {
    uint32_t max_register = 1;
    uint32_t i;
    for (i = 0; i < count; i++) {
        const qvm_instruction_t *insn = &insns[i];
        uint8_t op = insn->op;
        if (op != QVM_OP_JMP && op != QVM_OP_JMP_FALSE &&
            op != QVM_OP_JMP_TRUE && insn->dst + 1U > max_register)
            max_register = insn->dst + 1U;
        if (op == QVM_OP_CMP || op == QVM_OP_ADD || op == QVM_OP_SUB ||
            op == QVM_OP_MUL || op == QVM_OP_DIV) {
            if (insn->src1 + 1U > max_register) max_register = insn->src1 + 1U;
            if (insn->src2 + 1U > max_register) max_register = insn->src2 + 1U;
        }
        if (op == QVM_OP_NEG || op == QVM_OP_JMP_FALSE ||
            op == QVM_OP_JMP_TRUE) {
            if (insn->src1 + 1U > max_register) max_register = insn->src1 + 1U;
        }
    }
    if (max_register > QVM_MAX_REGISTERS) max_register = QVM_MAX_REGISTERS;
    return max_register;
}

/* Lower the whole clause/join expression into one verified slice. Returns 1
 * when the slice is ready; otherwise the native evaluators stay in charge. */
static int dsv_lower_to_qvm(dsv_filter_t *filter, const qvm_limits_t *limits,
                            qvm_diagnostic_t *diagnostic) {
    dsv_qvm_builder_t builder;
    qvm_verify_error_t verify_error;
    size_t i;
    int pending_jump = -1;
    uint32_t register_count;

    if (!filter || filter->clause_count == 0) return 0;
    memset(&builder, 0, sizeof(builder));
    dsv_qvm_diagnostic_clear(diagnostic);

    for (i = 0; i < filter->clause_count; i++) {
        const dsv_clause_t *clause = &filter->clauses[i];
        if (clause->lhs_has_arith) {
            if (!dsv_qvm_lower_arith(&builder, clause)) goto fallback;
        } else {
            if (!dsv_qvm_lower_simple(&builder, clause)) goto fallback;
        }
        if (pending_jump >= 0) {
            builder.insns[pending_jump].arg = (uint32_t)builder.count;
            pending_jump = -1;
        }
        if (i + 1 < filter->clause_count) {
            uint8_t op = filter->joins[i] == DSV_JOIN_AND
                             ? QVM_OP_JMP_FALSE
                             : QVM_OP_JMP_TRUE;
            if (!dsv_qvm_emit(&builder, op, 0, 0, 0, 0)) goto fallback;
            pending_jump = (int)(builder.count - 1);
        }
    }
    if (pending_jump >= 0)
        builder.insns[pending_jump].arg = (uint32_t)builder.count;
    if (builder.overflow || builder.count == 0 || builder.operand_count == 0)
        goto fallback;

    register_count = dsv_qvm_register_count(builder.insns, (uint32_t)builder.count);
    if (qvm_verify_slice_ex(builder.insns, (uint32_t)builder.count, 0,
                            (uint32_t)builder.count, register_count,
                            (uint32_t)builder.operand_count, 0, limits,
                            &verify_error) != QVM_STATUS_OK) {
        if (diagnostic) *diagnostic = verify_error;
        goto fallback;
    }

    filter->expr_vm = builder.insns;
    filter->expr_vm_count = (uint32_t)builder.count;
    filter->operands = builder.operands;
    filter->operand_count = builder.operand_count;
    filter->qvm_ready = 1;
    filter->qvm_limits = limits ? *limits : qvm_default_limits();
    filter->qvm_diagnostics = limits != NULL;
    return 1;

fallback:
    free(builder.insns);
    free(builder.operands);
    filter->qvm_ready = 0;
    return 0;
}

typedef struct {
    const dsv_filter_t *filter;
    const vstr *fields;
    size_t field_count;
    size_t row_index;
} dsv_qvm_ctx_t;

static void dsv_qvm_make_invalid(void *opaque, qvm_value_t *out) {
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = DSV_QVM_INVALID;
}

static void dsv_qvm_make_bool(void *opaque, int value, qvm_value_t *out) {
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = DSV_QVM_BOOL;
    out->num = value ? 1 : 0;
}

static void dsv_qvm_make_number(void *opaque, double value, qvm_value_t *out) {
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = DSV_QVM_NUMBER;
    out->number = value;
}

static void dsv_qvm_make_string(void *opaque, const char *value, size_t len,
                                qvm_value_t *out) {
    (void)opaque;
    memset(out, 0, sizeof(*out));
    out->type = DSV_QVM_STRING;
    out->str = value ? value : "";
    out->length = value ? len : 0;
}

static int dsv_qvm_resolve_value(const dsv_qvm_ctx_t *ctx, uint32_t operand,
                                 qvm_value_t *out) {
    const dsv_filter_t *filter = ctx->filter;
    const dsv_operand_t *op;
    if (!filter || operand >= filter->operand_count) return 0;
    op = &filter->operands[operand];
    switch (op->kind) {
        case DSV_OPERAND_COLUMN:
            if (op->as_number) {
                double value;
                if (ctx->fields)
                    value = dsv_value_double_at(ctx->fields, ctx->field_count, op->col_idx);
                else
                    value = csv_get_double(filter->doc, ctx->row_index, op->col_idx, 0.0);
                dsv_qvm_make_number(NULL, value, out);
            } else {
                vstr value;
                if (ctx->fields)
                    value = dsv_value_at(ctx->fields, ctx->field_count, op->col_idx);
                else
                    value = csv_get_v(filter->doc, ctx->row_index, op->col_idx);
                dsv_qvm_make_string(NULL, value.data, value.len, out);
            }
            return 1;
        case DSV_OPERAND_NUMBER:
            dsv_qvm_make_number(NULL, op->num, out);
            return 1;
        case DSV_OPERAND_STRING:
            dsv_qvm_make_string(NULL, op->str, op->str_len, out);
            return 1;
        default:
            return 0;
    }
}

static int dsv_qvm_resolve(void *opaque, uint32_t operand, qvm_value_t *out) {
    return dsv_qvm_resolve_value((const dsv_qvm_ctx_t *)opaque, operand, out);
}

static int dsv_qvm_truthy(void *opaque, const qvm_value_t *value) {
    (void)opaque;
    if (!value) return 0;
    switch (value->type) {
        case DSV_QVM_BOOL: return value->num != 0;
        case DSV_QVM_NUMBER: return value->number != 0.0;
        case DSV_QVM_STRING: return value->length > 0;
        default: return 0;
    }
}

static int dsv_qvm_binary(void *opaque, qvm_opcode_t op, uint32_t arg,
                          const qvm_value_t *left, const qvm_value_t *right,
                          qvm_value_t *out) {
    (void)opaque;
    switch (op) {
        case QVM_OP_ADD:
        case QVM_OP_SUB:
        case QVM_OP_MUL:
        case QVM_OP_DIV: {
            /* out may alias left/right (dst == src1 register reuse), so read
             * the operands before writing the result. */
            double lhs_number;
            double rhs_number;
            double result;
            if (!left || !right || left->type != DSV_QVM_NUMBER ||
                right->type != DSV_QVM_NUMBER)
                return 0;
            lhs_number = left->number;
            rhs_number = right->number;
            switch (op) {
                case QVM_OP_ADD: result = lhs_number + rhs_number; break;
                case QVM_OP_SUB: result = lhs_number - rhs_number; break;
                case QVM_OP_MUL: result = lhs_number * rhs_number; break;
                case QVM_OP_DIV: result = lhs_number / rhs_number; break;
                default: result = 0.0; break;
            }
            dsv_qvm_make_number(NULL, result, out);
            return 1;
        }
        case QVM_OP_CMP:
            if (!left || !right) return 0;
            if (left->type == DSV_QVM_NUMBER && right->type == DSV_QVM_NUMBER) {
                dsv_qvm_make_bool(NULL, eval_num((dsv_op_t)arg, left->number, right->number), out);
                return 1;
            }
            if (left->type == DSV_QVM_STRING && right->type == DSV_QVM_STRING) {
                dsv_qvm_make_bool(NULL,
                                  eval_str((dsv_op_t)arg,
                                           vstr_from_buf(left->str, left->length),
                                           right->str, right->length),
                                  out);
                return 1;
            }
            return 0;
        default:
            return 0;
    }
}

static int dsv_qvm_unary(void *opaque, qvm_opcode_t op, const qvm_value_t *input,
                         qvm_value_t *out) {
    (void)opaque;
    if (op != QVM_OP_NEG) return 0;
    if (!input || input->type != DSV_QVM_NUMBER) return 0;
    dsv_qvm_make_number(NULL, -input->number, out);
    return 1;
}

static int dsv_qvm_leaf(void *opaque, qvm_opcode_t op, uint32_t arg,
                        uint32_t src1, uint32_t src2, qvm_value_t *out) {
    const dsv_qvm_ctx_t *ctx = (const dsv_qvm_ctx_t *)opaque;
    qvm_value_t lhs;
    qvm_value_t rhs;
    int result;
    if (!ctx || (op != QVM_OP_CMP_LEAF_NUMBER && op != QVM_OP_CMP_LEAF_STRING))
        return 0;
    if (!dsv_qvm_resolve_value(ctx, src1, &lhs)) return 0;
    if (!dsv_qvm_resolve_value(ctx, src2, &rhs)) return 0;
    if (op == QVM_OP_CMP_LEAF_NUMBER) {
        if (lhs.type != DSV_QVM_NUMBER || rhs.type != DSV_QVM_NUMBER) return 0;
        result = eval_num((dsv_op_t)arg, lhs.number, rhs.number);
    } else {
        if (lhs.type != DSV_QVM_STRING || rhs.type != DSV_QVM_STRING) return 0;
        result = eval_str((dsv_op_t)arg, vstr_from_buf(lhs.str, lhs.length),
                          rhs.str, rhs.length);
    }
    dsv_qvm_make_bool(NULL, result, out);
    return 1;
}

static const qvm_exec_ops_t dsv_qvm_ops = {
    dsv_qvm_resolve,      dsv_qvm_truthy,
    dsv_qvm_binary,       dsv_qvm_unary,
    NULL,                 NULL,
    NULL,                 dsv_qvm_leaf,
    dsv_qvm_make_invalid, dsv_qvm_make_bool,
    dsv_qvm_make_number,  dsv_qvm_make_string};

dsv_filter_t *dsv_filter_create(const csv_doc_t *doc, size_t header_row_index) {
    if (!doc) return NULL;
    if (header_row_index >= csv_row_count(doc)) return NULL;

    dsv_filter_t *f = (dsv_filter_t*)calloc(1, sizeof(dsv_filter_t));
    if (!f) return NULL;
    f->doc = doc;
    f->header_row = header_row_index;
    f->output_delim = '|';
    f->qvm_limits = qvm_default_limits();
    dsv_qvm_diagnostic_clear(&f->qvm_diagnostic);
    f->col_count = csv_column_count(doc);
    f->cols = (dsv_col_t*)calloc(f->col_count, sizeof(dsv_col_t));
    if (!f->cols) {
        free(f);
        return NULL;
    }

    for (size_t i = 0; i < f->col_count; ++i) {
        const char *raw_name = csv_get(doc, header_row_index, i);
        if (!raw_name) raw_name = "";

        size_t name_len = strlen(raw_name);
        f->cols[i].raw_name = dsv_strndup(raw_name, name_len);
        f->cols[i].index = i;
        if (dsv_ends_with(raw_name, "_n")) {
            f->cols[i].type = COL_NUMBER;
            f->cols[i].name = dsv_strndup(raw_name, name_len - 2);
        } else if (dsv_ends_with(raw_name, "_s")) {
            f->cols[i].type = COL_STRING;
            f->cols[i].name = dsv_strndup(raw_name, name_len - 2);
        } else {
            f->cols[i].type = COL_DYNAMIC;
            f->cols[i].name = dsv_strndup(raw_name, name_len);
        }
    }

    return f;
}

void dsv_filter_destroy(dsv_filter_t *filter) {
    if (!filter) return;
    free_plan(filter);
    for (size_t i = 0; i < filter->col_count; ++i) {
        if (filter->cols[i].name) free(filter->cols[i].name);
        if (filter->cols[i].raw_name) free(filter->cols[i].raw_name);
    }
    free(filter->cols);
    free(filter);
}

static int build_direct_scan_plan(dsv_filter_t *filter) {
    size_t i;
    csv_scan_predicate_t *predicates;

    filter->scan_plan_compatible = 0;
    if (filter->clause_count == 0) return 0;
    for (i = 0; i + 1 < filter->clause_count; ++i) {
        if (filter->joins[i] != DSV_JOIN_AND) return 0;
    }
    for (i = 0; i < filter->clause_count; ++i) {
        if (!filter->clauses[i].lhs_is_simple_col || filter->clauses[i].lhs_has_arith)
            return 0;
    }

    predicates = (csv_scan_predicate_t *)calloc(filter->clause_count, sizeof(*predicates));
    if (!predicates) return -1;
    for (i = 0; i < filter->clause_count; ++i) {
        const dsv_clause_t *clause = &filter->clauses[i];
        predicates[i].column = clause->col_idx;
        predicates[i].op = (csv_scan_op_t)clause->op;
        if (clause->rhs_is_string) {
            predicates[i].type = CSV_SCAN_VALUE_TEXT;
            predicates[i].text = vstr_from_buf(clause->rhs_str, clause->rhs_str_len);
        } else {
            predicates[i].type = CSV_SCAN_VALUE_DOUBLE;
            predicates[i].number = clause->rhs_num;
        }
    }
    filter->scan_predicates = predicates;
    filter->scan_plan_compatible = 1;
    return 1;
}

static bool dsv_filter_compile_impl(dsv_filter_t *filter, const char *expression,
                                    const qvm_limits_t *limits,
                                    qvm_diagnostic_t *diagnostic, int strict_qvm) {
    const char *cur;
    const char *end;
    if (!filter || !expression) return false;

    free_plan(filter);
    set_error(filter, NULL);
    dsv_qvm_diagnostic_clear(&filter->qvm_diagnostic);

    cur = expression;
    end = expression + strlen(expression);

    while (1) {
        dsv_clause_t clause;
        const char *lhs_start;
        const char *lhs_end;
        const char *op_start = NULL;
        const char *after_op = NULL;
        const char *id_start = NULL;
        size_t id_len = 0;
        dsv_op_t op;

        memset(&clause, 0, sizeof(clause));

        skip_ws(&cur, end);
        if (cur >= end) {
            if (filter->clause_count == 0) {
                set_error(filter, "invalid filter: empty expression");
            } else {
                set_error(filter, "invalid filter: expected column");
            }
            free_plan(filter);
            return false;
        }

        lhs_start = cur;
        if (!find_top_level_cmp(cur, end, &op_start, &after_op, &op)) {
            const char *paren_msg = NULL;
            if (!check_lhs_parentheses(cur, end, &paren_msg) && paren_msg) {
                set_error(filter, paren_msg);
            } else {
                set_error(filter, "invalid filter: expected operator");
            }
            free_clause_data(&clause);
            free_plan(filter);
            return false;
        }
        lhs_end = op_start;
        trim_ws_range(&lhs_start, &lhs_end);
        if (lhs_start >= lhs_end) {
            set_error(filter, "invalid filter: expected column");
            free_clause_data(&clause);
            free_plan(filter);
            return false;
        }
        clause.op = op;
        cur = after_op;

        if (parse_simple_identifier_range(lhs_start, lhs_end, &id_start, &id_len)) {
            int col_idx = find_col(filter, id_start, id_len);
            if (col_idx < 0) {
                set_error(filter, "invalid filter: unknown column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            clause.lhs_is_simple_col = 1;
            clause.col_idx = (size_t)col_idx;
            clause.col_type = filter->cols[col_idx].type;
            clause.lhs_has_arith = 0;
        } else {
            const char *probe = lhs_start;
            const char *first_id_start = NULL;
            size_t first_id_len = 0;
            if (parse_identifier(&probe, lhs_end, &first_id_start, &first_id_len)) {
                const char *after_id = probe;
                skip_ws(&after_id, lhs_end);
                if (after_id < lhs_end) {
                    int first_col_idx = find_col(filter, first_id_start, first_id_len);
                    if (first_col_idx >= 0 && filter->cols[first_col_idx].type == COL_STRING) {
                        set_error(filter, "invalid filter: arithmetic on string column");
                        free_clause_data(&clause);
                        free_plan(filter);
                        return false;
                    }
                }
            }
            if (!parse_lhs_numeric_expr(filter, lhs_start, lhs_end, &clause)) {
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        }

        skip_ws(&cur, end);
        if (cur < end && *cur == '"') {
            clause.rhs_is_string = 1;
            if (!parse_string_lit(&cur, end, &clause.rhs_str, &clause.rhs_str_len)) {
                set_error(filter, "invalid filter: bad string literal");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            if (clause.lhs_has_arith) {
                set_error(filter, "invalid filter: arithmetic cannot compare to string");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            if (!clause.lhs_is_simple_col ||
                (clause.col_type != COL_STRING && clause.col_type != COL_DYNAMIC)) {
                set_error(filter, "invalid filter: string on non-string column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        } else {
            const char *num_start = NULL;
            size_t num_len = 0;
            clause.rhs_is_string = 0;
            if (!parse_number_token(&cur, end, &num_start, &num_len)) {
                set_error(filter, "invalid filter: expected value");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            clause.rhs_num = dsv_fast_atof(num_start, num_len);
            if (clause.lhs_is_simple_col && clause.col_type == COL_STRING) {
                set_error(filter, "invalid filter: number on string column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        }

        {
            dsv_clause_t *new_clauses =
                (dsv_clause_t *)realloc(filter->clauses, sizeof(dsv_clause_t) * (filter->clause_count + 1));
            if (!new_clauses) {
                set_error(filter, "oom compiling filter");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            filter->clauses = new_clauses;
            filter->clauses[filter->clause_count] = clause;
            filter->clause_count++;
        }

        skip_ws(&cur, end);
        if (cur >= end) break;

        {
            dsv_join_t join;
            if (!parse_join(&cur, end, &join)) {
                set_error(filter, "invalid filter: expected and/or");
                free_plan(filter);
                return false;
            }
            dsv_join_t *new_joins =
                (dsv_join_t *)realloc(filter->joins, sizeof(dsv_join_t) * filter->clause_count);
            if (!new_joins) {
                set_error(filter, "oom compiling filter");
                free_plan(filter);
                return false;
            }
            filter->joins = new_joins;
            filter->joins[filter->clause_count - 1] = join;
        }
    }

    filter->compiled = 1;
    if (build_direct_scan_plan(filter) < 0) {
        set_error(filter, "oom compiling direct scan plan");
        free_plan(filter);
        return false;
    }
    if (!dsv_lower_to_qvm(filter, limits, &filter->qvm_diagnostic) && strict_qvm) {
        if (filter->qvm_diagnostic.status == QVM_STATUS_OK) {
            filter->qvm_diagnostic.status = QVM_STATUS_UNSUPPORTED;
            filter->qvm_diagnostic.instruction = QVM_NO_INSTRUCTION;
            filter->qvm_diagnostic.opcode = QVM_NO_OPCODE;
            filter->qvm_diagnostic.operand = QVM_NO_OPERAND;
            filter->qvm_diagnostic.message = "CSV filter cannot be lowered to query VM";
        }
        if (diagnostic) *diagnostic = filter->qvm_diagnostic;
        set_error(filter, filter->qvm_diagnostic.message);
        free_plan(filter);
        return false;
    }
    if (diagnostic) *diagnostic = filter->qvm_diagnostic;
    return true;
}

bool dsv_filter_compile(dsv_filter_t *filter, const char *expression) {
    return dsv_filter_compile_impl(filter, expression, NULL, NULL, 0);
}

bool dsv_filter_compile_ex(dsv_filter_t *filter, const char *expression,
                           const qvm_limits_t *limits,
                           qvm_diagnostic_t *diagnostic) {
    return dsv_filter_compile_impl(filter, expression, limits, diagnostic, 1);
}

void dsv_filter_set_output_delimiter(dsv_filter_t *filter, char delimiter) {
    if (filter) filter->output_delim = delimiter;
}

static int dsv_field_needs_quote(vstr value, char delimiter) {
    size_t i;
    if (!value.data) return 0;
    for (i = 0; i < value.len; i++) {
        char ch = value.data[i];
        if (ch == delimiter || ch == '"' || ch == '\r' || ch == '\n') return 1;
    }
    return 0;
}

static size_t dsv_rendered_field_len(vstr value, char delimiter) {
    size_t i;
    size_t len = value.len;
    if (!dsv_field_needs_quote(value, delimiter)) return len;
    len = 2;
    for (i = 0; i < value.len; i++) len += value.data[i] == '"' ? 2 : 1;
    return len;
}

static void dsv_render_field(char *dst, size_t *offset, vstr value, char delimiter) {
    size_t i;
    int quote = dsv_field_needs_quote(value, delimiter);
    if (!value.data) value = vstr_from_buf("", 0);
    if (quote) dst[(*offset)++] = '"';
    for (i = 0; i < value.len; i++) {
        if (quote && value.data[i] == '"') dst[(*offset)++] = '"';
        dst[(*offset)++] = value.data[i];
    }
    if (quote) dst[(*offset)++] = '"';
}

static int dsv_render_values(const dsv_filter_t *filter, const vstr *fields,
                             size_t field_count, tstr *buffer) {
    size_t total = 0;
    size_t col;
    tstr next;

    for (col = 0; col < filter->col_count; ++col) {
        vstr value = dsv_value_at(fields, field_count, col);
        size_t field_len = dsv_rendered_field_len(value, filter->output_delim);
        size_t separator_len = col > 0 ? 1 : 0;
        if (field_len > (size_t)-1 - separator_len ||
            total > (size_t)-1 - separator_len - field_len) {
            return 0;
        }
        total += separator_len + field_len;
    }

    next = tstr_reserve(*buffer, total);
    if (!next) return 0;
    *buffer = next;

    {
        size_t offset = 0;
        for (col = 0; col < filter->col_count; ++col) {
            vstr value = dsv_value_at(fields, field_count, col);
            if (col > 0) (*buffer)[offset++] = filter->output_delim;
            dsv_render_field(*buffer, &offset, value, filter->output_delim);
        }
        if (!tstr_set_len_checked(*buffer, offset)) return 0;
    }
    return 1;
}

static int dsv_filter_check_row_native(dsv_filter_t *filter, size_t row_index) {
    size_t i;
    int result = 0;
    if (!filter || !filter->compiled) return -1;
    if (row_index >= csv_row_count(filter->doc)) return -1;

    for (i = 0; i < filter->clause_count; i++) {
        dsv_clause_t *c = &filter->clauses[i];
        int clause_ok;

        if (c->rhs_is_string) {
            vstr lhs = csv_get_v(filter->doc, row_index, c->col_idx);
            clause_ok = eval_str(c->op, lhs, c->rhs_str, c->rhs_str_len);
        } else {
            double lhs = 0.0;
            if (!eval_lhs_numeric_expr(filter, c, row_index, &lhs)) {
                return -1;
            }
            clause_ok = eval_num(c->op, lhs, c->rhs_num);
        }

        if (i == 0) {
            result = clause_ok;
        } else if (filter->joins[i - 1] == DSV_JOIN_AND) {
            result = result && clause_ok;
        } else {
            result = result || clause_ok;
        }
    }

    return result ? 1 : 0;
}

static int dsv_filter_check_values_native(dsv_filter_t *filter, const vstr *fields, size_t field_count) {
    size_t i;
    int result = 0;
    if (!filter || !filter->compiled || !fields) return -1;

    for (i = 0; i < filter->clause_count; i++) {
        dsv_clause_t *c = &filter->clauses[i];
        int clause_ok;

        if (c->rhs_is_string) {
            vstr lhs = dsv_value_at(fields, field_count, c->col_idx);
            clause_ok = eval_str(c->op, lhs, c->rhs_str, c->rhs_str_len);
        } else {
            double lhs = 0.0;
            if (!eval_lhs_numeric_expr_values(filter, c, fields, field_count, &lhs)) {
                return -1;
            }
            clause_ok = eval_num(c->op, lhs, c->rhs_num);
        }

        if (i == 0) {
            result = clause_ok;
        } else if (filter->joins[i - 1] == DSV_JOIN_AND) {
            result = result && clause_ok;
        } else {
            result = result || clause_ok;
        }
    }

    return result ? 1 : 0;
}

static int dsv_qvm_run(dsv_filter_t *filter, dsv_qvm_ctx_t *ctx,
                       qvm_value_t *result) {
    int status = qvm_execute_ex(
        filter->expr_vm, filter->expr_vm_count, 0, filter->expr_vm_count,
        &dsv_qvm_ops, ctx, NULL, result, &filter->qvm_limits,
        filter->qvm_diagnostics ? &filter->qvm_diagnostic : NULL);
    if (status != QVM_STATUS_OK && !filter->qvm_diagnostics) {
        dsv_qvm_diagnostic_clear(&filter->qvm_diagnostic);
        filter->qvm_diagnostic.status = (qvm_status_t)status;
        filter->qvm_diagnostic.message = "CSV filter query VM execution failed";
    }
    return status == QVM_STATUS_OK;
}

int dsv_filter_check_row(dsv_filter_t *filter, size_t row_index) {
    dsv_qvm_ctx_t ctx;
    qvm_value_t result;
    if (!filter || !filter->compiled) return -1;
    if (row_index >= csv_row_count(filter->doc)) return -1;
    if (!filter->qvm_ready || !filter->expr_vm)
        return dsv_filter_check_row_native(filter, row_index);
    memset(&ctx, 0, sizeof(ctx));
    ctx.filter = filter;
    ctx.row_index = row_index;
    if (!dsv_qvm_run(filter, &ctx, &result)) {
        set_error(filter, "filter evaluation failed");
        return -1;
    }
    return dsv_qvm_truthy(&ctx, &result) ? 1 : 0;
}

int dsv_filter_check_values(dsv_filter_t *filter, const vstr *fields,
                            size_t field_count) {
    dsv_qvm_ctx_t ctx;
    qvm_value_t result;
    if (!filter || !filter->compiled || !fields) return -1;
    if (!filter->qvm_ready || !filter->expr_vm)
        return dsv_filter_check_values_native(filter, fields, field_count);
    memset(&ctx, 0, sizeof(ctx));
    ctx.filter = filter;
    ctx.fields = fields;
    ctx.field_count = field_count;
    if (!dsv_qvm_run(filter, &ctx, &result)) {
        set_error(filter, "filter evaluation failed");
        return -1;
    }
    return dsv_qvm_truthy(&ctx, &result) ? 1 : 0;
}

void dsv_filter_run(dsv_filter_t *filter, dsv_row_callback_t callback, void *user_data) {
    csv_cursor_t *cursor;
    tstr rendered = NULL;
    int rc;
    if (!filter || !callback) return;

    cursor = csv_cursor_new(filter->doc, filter->header_row + 1);
    if (!cursor) {
        set_error(filter, "oom creating CSV cursor");
        return;
    }

    while ((rc = csv_cursor_next(cursor)) > 0) {
        size_t field_count = 0;
        const vstr *fields = csv_cursor_fields(cursor, &field_count);
        int match = dsv_filter_check_values(filter, fields, field_count);
        if (match != 1) continue;
        if (!dsv_render_values(filter, fields, field_count, &rendered)) {
            set_error(filter, "oom rendering row");
            break;
        }
        callback(user_data, csv_cursor_row_index(cursor), rendered);
    }

    if (rc < 0 || csv_cursor_error(cursor)) set_error(filter, "CSV cursor error");
    tstr_free(rendered);
    csv_cursor_free(cursor);
}

int dsv_filter_scan(dsv_filter_t *filter, const char *content, size_t len,
                    const csv_options_t *opts,
                    const csv_scan_projection_t *projections, size_t projection_count,
                    csv_scan_match_fn on_match, void *ctx, size_t *matched_count) {
    csv_scan_plan_t plan;
    int rc;

    if (matched_count) *matched_count = 0;
    if (!filter || !filter->compiled || !content) return -1;
    if (!filter->scan_plan_compatible) {
        set_error(filter, "direct scan requires simple predicates joined only by AND");
        return -1;
    }

    memset(&plan, 0, sizeof(plan));
    plan.predicates = filter->scan_predicates;
    plan.predicate_count = filter->clause_count;
    plan.projections = projections;
    plan.projection_count = projection_count;
    plan.on_match = on_match;
    plan.ctx = ctx;
    rc = csv_filter_scan_opts(content, len, opts, &plan, matched_count);
    if (rc != 0) {
        set_error(filter, csv_get_error());
        return -1;
    }
    set_error(filter, NULL);
    return 0;
}

static void dsv_index_apply_lower(dsv_index_query_t *query, double value, bool inclusive) {
    if (!query->has_lower_number || value > query->lower_number ||
        (value == query->lower_number && !inclusive && query->lower_inclusive)) {
        query->has_lower_number = true;
        query->lower_number = value;
        query->lower_inclusive = inclusive;
    }
}

static void dsv_index_apply_upper(dsv_index_query_t *query, double value, bool inclusive) {
    if (!query->has_upper_number || value < query->upper_number ||
        (value == query->upper_number && !inclusive && query->upper_inclusive)) {
        query->has_upper_number = true;
        query->upper_number = value;
        query->upper_inclusive = inclusive;
    }
}

typedef struct {
    dsv_index_query_t query;
    int has_text;
} dsv_index_plan_range_t;

static int dsv_index_query_is_empty(const dsv_index_query_t *query) {
    return query->has_lower_number && query->has_upper_number &&
           (query->lower_number > query->upper_number ||
            (query->lower_number == query->upper_number &&
             (!query->lower_inclusive || !query->upper_inclusive)));
}

static int dsv_index_plan_append(dsv_index_plan_range_t *ranges, size_t *count,
                                 const dsv_index_plan_range_t *range) {
    if (dsv_index_query_is_empty(&range->query)) return 1;
    if (*count >= DSV_INDEX_MAX_QUERY_RANGES) return 0;
    ranges[(*count)++] = *range;
    return 1;
}

static int dsv_index_plan_intersect_text(dsv_filter_t *filter,
                                         dsv_index_plan_range_t *ranges,
                                         size_t *range_count,
                                         const dsv_clause_t *clause) {
    vstr value = vstr_from_buf(clause->rhs_str, clause->rhs_str_len);
    size_t input;
    size_t output = 0;
    if (clause->op != DSV_OP_EQ && clause->op != DSV_OP_NE) {
        set_error(filter, "text index predicates support only == and !=");
        return 0;
    }
    for (input = 0; input < *range_count; ++input) {
        dsv_index_plan_range_t range = ranges[input];
        if (!range.has_text) {
            if (clause->op == DSV_OP_NE) {
                set_error(filter, "text != requires an existing equality-constrained index prefix");
                return 0;
            }
            range.query.text_equals = value;
            range.has_text = 1;
            ranges[output++] = range;
        } else {
            int equal = vstr_eq(range.query.text_equals, value);
            if ((clause->op == DSV_OP_EQ && equal) ||
                (clause->op == DSV_OP_NE && !equal))
                ranges[output++] = range;
        }
    }
    *range_count = output;
    return 1;
}

static int dsv_index_plan_intersect_number(dsv_filter_t *filter,
                                           dsv_index_plan_range_t *ranges,
                                           size_t *range_count,
                                           const dsv_clause_t *clause) {
    dsv_index_plan_range_t next[DSV_INDEX_MAX_QUERY_RANGES];
    size_t next_count = 0;
    size_t i;
    for (i = 0; i < *range_count; ++i) {
        dsv_index_plan_range_t range = ranges[i];
        switch (clause->op) {
            case DSV_OP_EQ:
                dsv_index_apply_lower(&range.query, clause->rhs_num, true);
                dsv_index_apply_upper(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_NE: {
                dsv_index_plan_range_t lower = range;
                dsv_index_plan_range_t upper = range;
                dsv_index_apply_upper(&lower.query, clause->rhs_num, false);
                dsv_index_apply_lower(&upper.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &lower) ||
                    !dsv_index_plan_append(next, &next_count, &upper))
                    goto capacity;
                break;
            }
            case DSV_OP_GT:
                dsv_index_apply_lower(&range.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_GE:
                dsv_index_apply_lower(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_LT:
                dsv_index_apply_upper(&range.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_LE:
                dsv_index_apply_upper(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            default:
                set_error(filter, "numeric index predicate is unsupported");
                return 0;
        }
    }
    memcpy(ranges, next, next_count * sizeof(next[0]));
    *range_count = next_count;
    return 1;

capacity:
    set_error(filter, "index predicate exceeds DSV_INDEX_MAX_QUERY_RANGES");
    return 0;
}

static int dsv_index_plan_intersect_clause(dsv_filter_t *filter,
                                           dsv_index_plan_range_t *ranges,
                                           size_t *range_count,
                                           const dsv_clause_t *clause,
                                           size_t text_column,
                                           size_t number_column) {
    if (clause->col_idx == text_column && clause->rhs_is_string)
        return dsv_index_plan_intersect_text(filter, ranges, range_count, clause);
    if (clause->col_idx == number_column && !clause->rhs_is_string)
        return dsv_index_plan_intersect_number(filter, ranges, range_count, clause);
    set_error(filter, "filter contains a predicate not covered by this index");
    return 0;
}

static int dsv_index_plan_union_clause(dsv_filter_t *filter,
                                       dsv_index_plan_range_t *ranges,
                                       size_t *range_count,
                                       const dsv_clause_t *clause,
                                       size_t text_column,
                                       size_t number_column) {
    dsv_index_plan_range_t range;
    memset(&range, 0, sizeof(range));
    if (clause->col_idx == text_column && clause->rhs_is_string) {
        if (clause->op != DSV_OP_EQ) {
            set_error(filter, "OR on the text index prefix requires equality");
            return 0;
        }
        range.has_text = 1;
        range.query.text_equals = vstr_from_buf(clause->rhs_str, clause->rhs_str_len);
        if (!dsv_index_plan_append(ranges, range_count, &range)) goto capacity;
        return 1;
    }
    if (clause->col_idx == number_column && !clause->rhs_is_string) {
        dsv_index_plan_range_t additions[DSV_INDEX_MAX_QUERY_RANGES];
        size_t addition_count = 1;
        additions[0] = range;
        if (!dsv_index_plan_intersect_number(filter, additions, &addition_count, clause))
            return 0;
        if (*range_count > DSV_INDEX_MAX_QUERY_RANGES - addition_count) goto capacity;
        memcpy(ranges + *range_count, additions, addition_count * sizeof(additions[0]));
        *range_count += addition_count;
        return 1;
    }
    set_error(filter, "filter contains an OR predicate not covered by this index");
    return 0;

capacity:
    set_error(filter, "index predicate exceeds DSV_INDEX_MAX_QUERY_RANGES");
    return 0;
}

int dsv_filter_index_seek(dsv_filter_t *filter, const dsv_index_t *index,
                          dsv_index_cursor_t *cursor) {
    dsv_index_plan_range_t ranges[DSV_INDEX_MAX_QUERY_RANGES];
    dsv_index_query_t queries[DSV_INDEX_MAX_QUERY_RANGES];
    size_t text_column;
    size_t number_column;
    size_t range_count = 1;
    size_t i;

    if (!filter || !filter->compiled || !index || !cursor) return -1;
    text_column = dsv_index_text_column(index);
    number_column = dsv_index_number_column(index);
    if (text_column == DSV_INDEX_NO_COLUMN || number_column == DSV_INDEX_NO_COLUMN) {
        set_error(filter, "index is not open");
        return -1;
    }

    memset(ranges, 0, sizeof(ranges));
    for (i = 0; i < filter->clause_count; ++i) {
        const dsv_clause_t *clause = &filter->clauses[i];
        int ok;
        if (!clause->lhs_is_simple_col || clause->lhs_has_arith) {
            set_error(filter, "index seek requires simple column predicates");
            return -1;
        }
        if (i == 0 || filter->joins[i - 1] == DSV_JOIN_AND) {
            ok = dsv_index_plan_intersect_clause(filter, ranges, &range_count,
                                                 clause, text_column, number_column);
        } else {
            ok = dsv_index_plan_union_clause(filter, ranges, &range_count,
                                             clause, text_column, number_column);
        }
        if (!ok) return -1;
    }
    for (i = 0; i < range_count; ++i) {
        if (!ranges[i].has_text) {
            set_error(filter, "every OR range must constrain the leading text index column");
            return -1;
        }
        queries[i] = ranges[i].query;
    }
    if (range_count == 0) {
        memset(cursor, 0, sizeof(*cursor));
        set_error(filter, NULL);
        return 0;
    }
    if (dsv_index_seek_many(index, queries, range_count, cursor) != 0) {
        set_error(filter, "failed to seek DSV index");
        return -1;
    }
    set_error(filter, NULL);
    return 0;
}
