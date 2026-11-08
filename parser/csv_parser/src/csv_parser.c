/**
 * @file csv_parser.c
 * @brief CSV Parser Implementation (RFC 4180)
 */

#include "csv_parser.h"
#include "csv_types.h"
#include "csv_lexer.h"
#include "csv_grammar_gen.h"
#include <fmt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <memory_pool.h>
#ifdef _MSC_VER
static __declspec(thread) char g_error_msg[256] = {0};
#else
static __thread char g_error_msg[256] = {0};
#endif

/* ============================================================================
 * Arena Memory Management
 * ============================================================================ */

csv_arena_t *csv_arena_create(void) {
    return csv_arena_create_sized(CSV_POOL_MIN_SIZE);
}

csv_arena_t *csv_arena_create_sized(size_t hint_size) {
    if (hint_size < CSV_POOL_MIN_SIZE) hint_size = CSV_POOL_MIN_SIZE;
    if (hint_size > CSV_POOL_MAX_SIZE) hint_size = CSV_POOL_MAX_SIZE;

    csv_arena_t *arena = (csv_arena_t *)calloc(1, sizeof(csv_arena_t));
    if (!arena) return NULL;

    csv_pool_node_t *node = (csv_pool_node_t *)calloc(1, sizeof(csv_pool_node_t));
    if (!node) {
        free(arena);
        return NULL;
    }

    node->pool = pool_create(hint_size);
    if (!node->pool) {
        free(node);
        free(arena);
        return NULL;
    }

    arena->head = node;
    arena->current = node;
    arena->initial_size = hint_size;
    arena->external = 0;
    return arena;
}

void *csv_arena_alloc(csv_arena_t *arena, size_t size) {
    if (!arena || !arena->current) return NULL;

    void *ptr = pool_alloc(arena->current->pool, size);
    if (ptr) return ptr;

    size_t new_size = arena->initial_size * 2;
    if (new_size > CSV_POOL_MAX_SIZE) new_size = CSV_POOL_MAX_SIZE;
    if (new_size < size + 64) new_size = size + 64;

    csv_pool_node_t *node = (csv_pool_node_t *)calloc(1, sizeof(csv_pool_node_t));
    if (!node) return NULL;

    node->pool = pool_create(new_size);
    if (!node->pool) {
        free(node);
        return NULL;
    }

    arena->current->next = node;
    arena->current = node;

    return pool_alloc(node->pool, size);
}

char *csv_arena_strdup(csv_arena_t *arena, const char *str, size_t len) {
    char *dst = (char *)csv_arena_alloc(arena, len + 1);
    if (!dst) return NULL;
    memcpy(dst, str, len);
    dst[len] = '\0';
    return dst;
}

void csv_arena_free(csv_arena_t *arena) {
    if (!arena) return;

    if (!arena->external) {
        csv_pool_node_t *node = arena->head;
        while (node) {
            csv_pool_node_t *next = node->next;
            if (node->pool) pool_destroy(node->pool);
            free(node);
            node = next;
        }
    }
    free(arena);
}

/* ============================================================================
 * Internal Structure Helpers
 * ============================================================================ */

csv_doc_t *csv_doc_new_arena(csv_arena_t *arena) {
    csv_doc_t *doc = (csv_doc_t *)csv_arena_alloc(arena, sizeof(csv_doc_t));
    if (!doc) return NULL;
    memset(doc, 0, sizeof(csv_doc_t));
    doc->arena = arena;
    if (vec_init_bytes(&doc->row_index, sizeof(csv_row_node_t *),
                             _Alignof(csv_row_node_t *), SIZE_MAX) != STL_OK)
        return NULL;
    return doc;
}

csv_row_node_t *csv_row_new_arena(csv_arena_t *arena) {
    csv_row_node_t *row = (csv_row_node_t *)csv_arena_alloc(arena, sizeof(csv_row_node_t));
    if (!row) return NULL;
    memset(row, 0, sizeof(csv_row_node_t));
    return row;
}

void csv_row_add_field(csv_arena_t *arena, csv_row_node_t *row,
                       const char *value, size_t len, int owned) {
    csv_field_node_t *field = (csv_field_node_t *)csv_arena_alloc(arena, sizeof(csv_field_node_t));
    if (!field) return;

    // Always make a null-terminated copy for safe string operations
    if (owned) {
        // Already owned (from unescape), value is null-terminated
        field->value = value;
    } else {
        // Make a null-terminated copy
        char *copy = csv_arena_strdup(arena, value, len);
        field->value = copy ? copy : "";
    }
    field->length = len;
    field->owned = 1;  // Always owned now
    field->next = NULL;

    if (row->fields_tail) {
        row->fields_tail->next = field;
    } else {
        row->fields = field;
    }
    row->fields_tail = field;
    row->field_count++;
}

int csv_doc_add_row(csv_doc_t *doc, csv_row_node_t *row) {
    if (!doc || !row) return -1;
    if (vec_push(&doc->row_index, &row) != STL_OK) return -1;

    row->next = NULL;
    if (doc->rows_tail) {
        doc->rows_tail->next = row;
    } else {
        doc->rows = row;
    }
    doc->rows_tail = row;
    doc->row_count = vec_size(&doc->row_index);

    if (row->field_count > doc->column_count) {
        doc->column_count = row->field_count;
    }
    return 0;
}

static csv_field_node_t *get_field_at(csv_row_node_t *row, size_t col) {
    if (!row) return NULL;
    csv_field_node_t *field = row->fields;
    for (size_t i = 0; i < col && field; i++) {
        field = field->next;
    }
    return field;
}

static csv_row_node_t *get_row_at(const csv_doc_t *doc, size_t row_idx) {
    const csv_row_node_t *const *entry;
    if (!doc) return NULL;
    /* The index is derived while rows are appended and is invalidated with the document. */
    entry = (const csv_row_node_t *const *)vec_at_const(&doc->row_index, row_idx);
    return entry ? (csv_row_node_t *)*entry : NULL;
}

struct csv_cursor_s {
    const csv_doc_t *doc;
    vec_t fields;
    csv_row_node_t *current_row_node;
    csv_field_node_t *current_fields;
    size_t current_field_count;
    size_t next_row;
    size_t current_row;
    int fields_valid;
    int error;
};

csv_cursor_t *csv_cursor_new(const csv_doc_t *doc, size_t first_row) {
    csv_cursor_t *cursor;
    if (!doc || first_row > doc->row_count) return NULL;
    cursor = (csv_cursor_t *)calloc(1, sizeof(*cursor));
    if (!cursor) return NULL;
    if (vec_init_bytes(&cursor->fields, sizeof(vstr), _Alignof(vstr),
                             doc->column_count) != STL_OK) {
        free(cursor);
        return NULL;
    }
    cursor->doc = doc;
    cursor->next_row = first_row;
    cursor->current_row = (size_t)-1;
    cursor->current_row_node = NULL;
    return cursor;
}

void csv_cursor_free(csv_cursor_t *cursor) {
    if (!cursor) return;
    vec_destroy(&cursor->fields);
    free(cursor);
}

int csv_cursor_rewind(csv_cursor_t *cursor, size_t first_row) {
    if (!cursor || !cursor->doc || first_row > cursor->doc->row_count) return -1;
    cursor->next_row = first_row;
    cursor->current_row = (size_t)-1;
    cursor->current_row_node = NULL;
    cursor->current_fields = NULL;
    cursor->current_field_count = 0;
    cursor->fields_valid = 0;
    cursor->error = 0;
    vec_clear(&cursor->fields);
    return 0;
}

int csv_cursor_next(csv_cursor_t *cursor) {
    csv_row_node_t *row;
    csv_field_node_t *field;
    if (!cursor || cursor->error) return -1;
    if (cursor->next_row >= cursor->doc->row_count) {
        cursor->current_row = (size_t)-1;
        cursor->current_row_node = NULL;
        cursor->current_fields = NULL;
        cursor->current_field_count = 0;
        cursor->fields_valid = 0;
        vec_clear(&cursor->fields);
        return 0;
    }

    row = cursor->current_row_node ? cursor->current_row_node->next
                                   : get_row_at(cursor->doc, cursor->next_row);
    if (!row) {
        cursor->error = 1;
        return -1;
    }
    field = row->fields;
    cursor->current_row = cursor->next_row++;
    cursor->current_row_node = row;
    cursor->current_fields = field;
    cursor->current_field_count = row->field_count;
    cursor->fields_valid = 0;
    vec_clear(&cursor->fields);
    return 1;
}

int csv_cursor_error(const csv_cursor_t *cursor) {
    return cursor ? cursor->error : 1;
}

size_t csv_cursor_row_index(const csv_cursor_t *cursor) {
    return cursor ? cursor->current_row : (size_t)-1;
}

const vstr *csv_cursor_fields(const csv_cursor_t *cursor, size_t *field_count) {
    csv_cursor_t *mutable_cursor = (csv_cursor_t *)cursor;
    csv_field_node_t *field;
    vstr *views;
    size_t index;
    if (!cursor || cursor->error || cursor->current_row == (size_t)-1) {
        if (field_count) *field_count = 0;
        return NULL;
    }
    if (!cursor->fields_valid) {
        if (vec_resize(&mutable_cursor->fields, cursor->current_field_count) !=
            STL_OK) {
            mutable_cursor->error = 1;
            if (field_count) *field_count = 0;
            return NULL;
        }
        views = (vstr *)vec_data(&mutable_cursor->fields);
        field = cursor->current_fields;
        for (index = 0; index < vec_size(&cursor->fields); ++index) {
            if (!field) {
                mutable_cursor->error = 1;
                vec_clear(&mutable_cursor->fields);
                if (field_count) *field_count = 0;
                return NULL;
            }
            views[index] = vstr_from_buf(field->value, field->length);
            field = field->next;
        }
        mutable_cursor->fields_valid = 1;
    }
    if (field_count) *field_count = vec_size(&cursor->fields);
    if (vec_empty(&cursor->fields)) return NULL;
    return (const vstr *)vec_data_const(&cursor->fields);
}

vstr csv_cursor_field_v(const csv_cursor_t *cursor, size_t col) {
    csv_field_node_t *field;
    if (!cursor || cursor->error || cursor->current_row == (size_t)-1) {
        return vstr_from_buf(NULL, 0);
    }
    field = cursor->current_fields;
    while (field && col > 0) {
        field = field->next;
        --col;
    }
    return field ? vstr_from_buf(field->value, field->length) : vstr_from_buf(NULL, 0);
}

/* ============================================================================
 * Parser Core - Direct lexer-based parsing (no lemon needed for simple CSV)
 * ============================================================================ */

static char *unescape_quotes_impl(csv_arena_t *arena, const char *src, size_t len, size_t *out_len) {
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

static csv_options_t csv_normalize_opts(const csv_options_t *opts) {
    csv_options_t normalized = CSV_OPTIONS_DEFAULT;
    if (!opts) return normalized;
    normalized = *opts;
    if (normalized.delimiter == '\0') normalized.delimiter = ',';
    if (normalized.quote == '\0') normalized.quote = '"';
    return normalized;
}

static int csv_opts_use_fast_lexer(const csv_options_t *opts) {
    csv_options_t normalized = csv_normalize_opts(opts);
    return normalized.delimiter == ',' && normalized.quote == '"' && normalized.skip_empty_rows;
}

typedef int (*csv_scan_field_cb)(void *ctx, size_t row, size_t col, const char *value,
                                 size_t len);
typedef int (*csv_scan_row_end_cb)(void *ctx, size_t row, size_t field_count);

static int csv_buf_append(char **buf, size_t *len, size_t *cap, char ch) {
    size_t required;
    if (*len > SIZE_MAX - 2) return 0;
    required = *len + 2;
    if (*len + 1 >= *cap) {
        size_t new_cap = *cap && *cap <= SIZE_MAX / 2 ? (*cap * 2) : required;
        char *new_buf;
        if (new_cap < required) new_cap = required;
        new_buf = (char *)realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = ch;
    return 1;
}

static int csv_scan_emit_field(csv_scan_field_cb field_cb, void *ctx, size_t row, size_t col,
                               char *buf, size_t len) {
    const char *value = len > 0 ? buf : "";
    if (buf) buf[len] = '\0';
    return !field_cb || field_cb(ctx, row, col, value, len) == 0;
}

static int csv_scan_opts(const char *content, size_t len, const csv_options_t *opts,
                         csv_scan_field_cb field_cb, csv_scan_row_end_cb row_end_cb, void *ctx,
                         char *error, size_t error_cap) {
    csv_options_t o = csv_normalize_opts(opts);
    char *field = NULL;
    size_t field_len = 0;
    size_t field_cap = 0;
    size_t row = 0;
    size_t col = 0;
    size_t i = 0;
    int in_quote = 0;
    int field_started = 0;
    int row_started = 0;

    if (!content) return -1;
    while (i < len) {
        char ch = content[i++];
        if (in_quote) {
            if (ch == o.quote) {
                if (i < len && content[i] == o.quote) {
                    if (!csv_buf_append(&field, &field_len, &field_cap, o.quote)) goto oom;
                    i++;
                } else {
                    in_quote = 0;
                }
            } else {
                if (!csv_buf_append(&field, &field_len, &field_cap, ch)) goto oom;
            }
            field_started = 1;
            row_started = 1;
            continue;
        }

        if (!field_started && ch == o.quote) {
            in_quote = 1;
            field_started = 1;
            row_started = 1;
            continue;
        }

        if (ch == o.delimiter) {
            if (!csv_scan_emit_field(field_cb, ctx, row, col, field, field_len)) goto fail;
            col++;
            field_len = 0;
            field_started = 0;
            row_started = 1;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i < len && content[i] == '\n') i++;
            if (row_started || field_started || !o.skip_empty_rows) {
                if (row_started || field_started) {
                    if (!csv_scan_emit_field(field_cb, ctx, row, col, field, field_len)) goto fail;
                    col++;
                }
                if (row_end_cb && row_end_cb(ctx, row, col) != 0) goto fail;
                row++;
            }
            col = 0;
            field_len = 0;
            field_started = 0;
            row_started = 0;
            continue;
        }

        if (!csv_buf_append(&field, &field_len, &field_cap, ch)) goto oom;
        field_started = 1;
        row_started = 1;
    }

    if (in_quote) {
        if (error && error_cap > 0) {
            fmt_text(error, error_cap, "Unterminated quoted field");
        }
        free(field);
        return -1;
    }

    if (row_started || field_started) {
        if (!csv_scan_emit_field(field_cb, ctx, row, col, field, field_len)) goto fail;
        col++;
        if (row_end_cb && row_end_cb(ctx, row, col) != 0) goto fail;
    }

    free(field);
    return 0;

oom:
    if (error && error_cap > 0) fmt_text(error, error_cap, "Out of memory parsing CSV");
fail:
    free(field);
    return -1;
}

typedef struct {
    csv_doc_t *doc;
    csv_arena_t *arena;
    csv_row_node_t *current_row;
} csv_dom_scan_ctx_t;

static int csv_dom_scan_field(void *ctx, size_t row, size_t col, const char *value, size_t len) {
    csv_dom_scan_ctx_t *scan = (csv_dom_scan_ctx_t *)ctx;
    (void)row;
    (void)col;
    if (!scan->current_row) {
        scan->current_row = csv_row_new_arena(scan->arena);
        if (!scan->current_row) return -1;
    }
    csv_row_add_field(scan->arena, scan->current_row, value, len, 0);
    return 0;
}

static int csv_dom_scan_row_end(void *ctx, size_t row, size_t field_count) {
    csv_dom_scan_ctx_t *scan = (csv_dom_scan_ctx_t *)ctx;
    (void)row;
    (void)field_count;
    if (!scan->current_row) {
        scan->current_row = csv_row_new_arena(scan->arena);
        if (!scan->current_row) return -1;
    }
    if (csv_doc_add_row(scan->doc, scan->current_row) != 0) return -1;
    scan->current_row = NULL;
    return 0;
}

csv_doc_t *csv_parse(const char *content, size_t len) {
    return csv_parse_opts(content, len, NULL);
}

csv_doc_t *csv_parse_opts(const char *content, size_t len, const csv_options_t *opts) {
    csv_options_t normalized = csv_normalize_opts(opts);
    if (!content || len == 0) {
        fmt_text(g_error_msg, sizeof(g_error_msg), "Empty input");
        return NULL;
    }

    csv_arena_t *arena = csv_arena_create_sized(len < 4096 ? 4096 : len / 4);
    if (!arena) {
        fmt_text(g_error_msg, sizeof(g_error_msg), "Failed to allocate arena");
        return NULL;
    }

    csv_doc_t *doc = csv_doc_new_arena(arena);
    if (!doc) {
        csv_arena_free(arena);
        fmt_text(g_error_msg, sizeof(g_error_msg), "Failed to allocate document");
        return NULL;
    }

    if (csv_opts_use_fast_lexer(&normalized)) {
        csv_lexer_t lexer;
        csv_lexer_init(&lexer, content, len);
        csv_lexer_reset_state();

        csv_row_node_t *current_row = csv_row_new_arena(arena);
        csv_token_t token;
        int ret;
        if (!current_row) {
            fmt_text(g_error_msg, sizeof(g_error_msg), "Out of memory parsing CSV rows");
            vec_destroy(&doc->row_index);
            csv_arena_free(arena);
            return NULL;
        }

        while ((ret = csv_lexer_next(&lexer, &token)) > 0) {
            switch (token.type) {
                case CSV_TOKEN_FIELD: {
                    if (token.needs_unescape) {
                        size_t unesc_len;
                        char *unesc = unescape_quotes_impl(arena, token.value, token.length, &unesc_len);
                        csv_row_add_field(arena, current_row, unesc, unesc_len, 1);
                    } else {
                        csv_row_add_field(arena, current_row, token.value, token.length, 0);
                    }
                    break;
                }
                case CSV_TOKEN_COMMA:
                    // Empty fields handled by lexer state machine
                    break;
                case CSV_TOKEN_NEWLINE:
                    if (current_row->field_count > 0) {
                        if (csv_doc_add_row(doc, current_row) != 0) {
                            fmt_text(g_error_msg, sizeof(g_error_msg), "Out of memory indexing CSV rows");
                            vec_destroy(&doc->row_index);
                            csv_arena_free(arena);
                            return NULL;
                        }
                    }
                    current_row = csv_row_new_arena(arena);
                    if (!current_row) {
                        fmt_text(g_error_msg, sizeof(g_error_msg), "Out of memory parsing CSV rows");
                        vec_destroy(&doc->row_index);
                        csv_arena_free(arena);
                        return NULL;
                    }
                    break;
            }
        }

        // Add final row if not empty
        if (current_row && current_row->field_count > 0) {
            if (csv_doc_add_row(doc, current_row) != 0) {
                fmt_text(g_error_msg, sizeof(g_error_msg), "Out of memory indexing CSV rows");
                vec_destroy(&doc->row_index);
                csv_arena_free(arena);
                return NULL;
            }
        }

        if (ret < 0) {
            fmt(g_error_msg, sizeof(g_error_msg), "{}", lexer.error);
            vec_destroy(&doc->row_index);
            csv_arena_free(arena);
            return NULL;
        }
    } else {
        csv_dom_scan_ctx_t scan = { doc, arena, NULL };
        if (csv_scan_opts(content, len, &normalized, csv_dom_scan_field, csv_dom_scan_row_end,
                          &scan, g_error_msg, sizeof(g_error_msg)) != 0) {
            vec_destroy(&doc->row_index);
            csv_arena_free(arena);
            return NULL;
        }
    }

    // Handle header option
    if (normalized.has_header && doc->rows) {
        doc->header = doc->rows;
        doc->rows = doc->rows->next;
        if (vec_erase(&doc->row_index, 0, NULL) != STL_OK) {
            fmt_text(g_error_msg, sizeof(g_error_msg), "Failed to index CSV header");
            vec_destroy(&doc->row_index);
            csv_arena_free(arena);
            return NULL;
        }
        doc->row_count = vec_size(&doc->row_index);
        if (!doc->rows) doc->rows_tail = NULL;
    }

    return doc;
}

csv_doc_t *csv_parse_file(const char *filename) {
    return csv_parse_file_opts(filename, NULL);
}

csv_doc_t *csv_parse_file_opts(const char *filename, const csv_options_t *opts) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fmt(g_error_msg, sizeof(g_error_msg), "Cannot open file: {}", strerror(errno));
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        fmt_text(g_error_msg, sizeof(g_error_msg), "Empty file");
        return NULL;
    }

    char *content = (char *)malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        fmt_text(g_error_msg, sizeof(g_error_msg), "Out of memory");
        return NULL;
    }

    size_t read = fread(content, 1, (size_t)size, fp);
    fclose(fp);
    content[read] = '\0';

    csv_doc_t *doc = csv_parse_opts(content, read, opts);
    free(content);
    return doc;
}

void csv_free(csv_doc_t *doc) {
    if (!doc) return;
    vec_destroy(&doc->row_index);
    csv_arena_free(doc->arena);
}

/* ============================================================================
 * DOM API Accessors
 * ============================================================================ */

size_t csv_row_count(const csv_doc_t *doc) {
    return doc ? doc->row_count : 0;
}

size_t csv_column_count(const csv_doc_t *doc) {
    return doc ? doc->column_count : 0;
}

bool csv_has_header(const csv_doc_t *doc) {
    return doc && doc->header != NULL;
}

const char *csv_get(const csv_doc_t *doc, size_t row, size_t col) {
    csv_row_node_t *r = get_row_at(doc, row);
    csv_field_node_t *f = get_field_at(r, col);
    return f ? f->value : NULL;
}

size_t csv_get_len(const csv_doc_t *doc, size_t row, size_t col) {
    csv_row_node_t *r = get_row_at(doc, row);
    csv_field_node_t *f = get_field_at(r, col);
    return f ? f->length : 0;
}

vstr csv_get_v(const csv_doc_t *doc, size_t row, size_t col) {
    csv_row_node_t *r = get_row_at(doc, row);
    csv_field_node_t *f = get_field_at(r, col);
    return f ? vstr_from_buf(f->value, f->length) : vstr_from_buf(NULL, 0);
}

const char *csv_header_get(const csv_doc_t *doc, size_t col) {
    if (!doc || !doc->header) return NULL;
    csv_field_node_t *f = get_field_at(doc->header, col);
    return f ? f->value : NULL;
}

size_t csv_header_get_len(const csv_doc_t *doc, size_t col) {
    if (!doc || !doc->header) return 0;
    csv_field_node_t *f = get_field_at(doc->header, col);
    return f ? f->length : 0;
}

vstr csv_header_get_v(const csv_doc_t *doc, size_t col) {
    if (!doc || !doc->header) return vstr_from_buf(NULL, 0);
    csv_field_node_t *f = get_field_at(doc->header, col);
    return f ? vstr_from_buf(f->value, f->length) : vstr_from_buf(NULL, 0);
}

int csv_get_int(const csv_doc_t *doc, size_t row, size_t col, int def) {
    const char *val = csv_get(doc, row, col);
    if (!val || !*val) return def;
    return (int)strtol(val, NULL, 10);
}

double csv_get_double(const csv_doc_t *doc, size_t row, size_t col, double def) {
    const char *val = csv_get(doc, row, col);
    if (!val || !*val) return def;
    return strtod(val, NULL);
}

bool csv_get_bool(const csv_doc_t *doc, size_t row, size_t col, bool def) {
    const char *val = csv_get(doc, row, col);
    if (!val || !*val) return def;
    if (val[0] == '1' || val[0] == 't' || val[0] == 'T' ||
        val[0] == 'y' || val[0] == 'Y') return true;
    if (val[0] == '0' || val[0] == 'f' || val[0] == 'F' ||
        val[0] == 'n' || val[0] == 'N') return false;
    return def;
}

size_t csv_find_column(const csv_doc_t *doc, const char *header_name) {
    if (!doc || !doc->header || !header_name) return (size_t)-1;
    return csv_find_column_v(doc, vstr_from_cstr(header_name));
}

size_t csv_find_column_v(const csv_doc_t *doc, vstr header_name) {
    if (!doc || !doc->header || !header_name.data) return (size_t)-1;

    size_t col = 0;
    csv_field_node_t *f = doc->header->fields;
    while (f) {
        if (f->value && vstr_eq(vstr_from_buf(f->value, f->length), header_name)) {
            return col;
        }
        f = f->next;
        col++;
    }
    return (size_t)-1;
}

const char *csv_get_by_name(const csv_doc_t *doc, size_t row, const char *col_name) {
    size_t col = csv_find_column(doc, col_name);
    if (col == (size_t)-1) return NULL;
    return csv_get(doc, row, col);
}

vstr csv_get_by_name_v(const csv_doc_t *doc, size_t row, vstr col_name) {
    size_t col = csv_find_column_v(doc, col_name);
    if (col == (size_t)-1) return vstr_from_buf(NULL, 0);
    return csv_get_v(doc, row, col);
}

const char *csv_get_error(void) {
    return g_error_msg;
}

/* ============================================================================
 * Serialization — csv_to_string / csv_write_file (RFC 4180)
 * ============================================================================ */

static bool csv_field_needs_quoting(const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r') return true;
    }
    return false;
}

static bool csv_size_add(size_t *total, size_t amount) {
    if (amount > SIZE_MAX - *total) return false;
    *total += amount;
    return true;
}

static bool csv_field_quoted_len(const char *s, size_t len, size_t *out_len) {
    size_t n = 2;
    for (size_t i = 0; i < len; ++i) {
        if (!csv_size_add(&n, (s[i] == '"') ? 2 : 1)) return false;
    }
    *out_len = n;
    return true;
}

static size_t csv_write_field(char *buf, const char *s, size_t len, bool quote) {
    size_t pos = 0;
    if (quote) {
        buf[pos++] = '"';
        for (size_t i = 0; i < len; ++i) {
            if (s[i] == '"') buf[pos++] = '"';
            buf[pos++] = s[i];
        }
        buf[pos++] = '"';
    } else {
        memcpy(buf + pos, s, len);
        pos += len;
    }
    return pos;
}

static bool csv_row_serialized_len(csv_row_node_t *row, size_t *out_len) {
    size_t total = 0;
    size_t col = 0;
    for (csv_field_node_t *f = row->fields; f; f = f->next, ++col) {
        size_t field_len;
        if (col > 0 && !csv_size_add(&total, 1)) return false; /* comma */
        const char *s = f->value ? f->value : "";
        size_t len = f->value ? f->length : 0;
        if (csv_field_needs_quoting(s, len)) {
            if (!csv_field_quoted_len(s, len, &field_len)) return false;
        } else {
            field_len = len;
        }
        if (!csv_size_add(&total, field_len)) return false;
    }
    if (!csv_size_add(&total, 1)) return false; /* newline */
    *out_len = total;
    return true;
}

static size_t csv_row_write(char *buf, csv_row_node_t *row) {
    size_t pos = 0;
    size_t col = 0;
    for (csv_field_node_t *f = row->fields; f; f = f->next, ++col) {
        if (col > 0) buf[pos++] = ',';
        const char *s = f->value ? f->value : "";
        size_t len = f->value ? f->length : 0;
        bool quote = csv_field_needs_quoting(s, len);
        pos += csv_write_field(buf + pos, s, len, quote);
    }
    buf[pos++] = '\n';
    return pos;
}

char *csv_to_string_n(const csv_doc_t *doc, size_t *out_len) {
    csv_row_node_t *row;
    size_t row_len;
    size_t total = 0;
    size_t pos = 0;
    char *buf;

    if (out_len) *out_len = 0;
    if (!doc) return NULL;

    /* Pass 1: compute total size */
    if (doc->header &&
        (!csv_row_serialized_len(doc->header, &row_len) || !csv_size_add(&total, row_len)))
        return NULL;
    for (row = doc->rows; row; row = row->next) {
        if (!csv_row_serialized_len(row, &row_len) || !csv_size_add(&total, row_len)) return NULL;
    }
    if (total == SIZE_MAX) return NULL;

    buf = (char *)malloc(total + 1);
    if (!buf) return NULL;

    /* Pass 2: fill buffer */
    if (doc->header) pos += csv_row_write(buf + pos, doc->header);
    for (row = doc->rows; row; row = row->next)
        pos += csv_row_write(buf + pos, row);

    buf[pos] = '\0';
    if (out_len) *out_len = pos;
    return buf;
}

char *csv_to_string(const csv_doc_t *doc) {
    return csv_to_string_n(doc, NULL);
}

int csv_write_records(const csv_doc_t *doc, csv_write_fn write, void *user) {
    csv_row_node_t *row;
    size_t max_len = 0;
    size_t len;
    char *buf;

    if (!doc || !write) return -1;
    if (doc->header && !csv_row_serialized_len(doc->header, &max_len)) return -1;
    for (row = doc->rows; row; row = row->next) {
        if (!csv_row_serialized_len(row, &len)) return -1;
        if (len > max_len) max_len = len;
    }
    if (max_len == 0) return 0;

    buf = (char *)malloc(max_len);
    if (!buf) return -1;
    if (doc->header) {
        len = csv_row_write(buf, doc->header);
        if (write(buf, len, user) != 0) {
            free(buf);
            return -1;
        }
    }
    for (row = doc->rows; row; row = row->next) {
        len = csv_row_write(buf, row);
        if (write(buf, len, user) != 0) {
            free(buf);
            return -1;
        }
    }
    free(buf);
    return 0;
}

int csv_write(const csv_doc_t *doc, csv_write_fn write, void *user) {
    size_t len = 0;
    char *text;
    int rc;
    if (!doc || !write) return -1;
    text = csv_to_string_n(doc, &len);
    if (!text) return -1;
    if (len == 0) {
        free(text);
        return 0;
    }
    rc = write(text, len, user);
    free(text);
    return rc == 0 ? 0 : -1;
}

int csv_write_file(const csv_doc_t *doc, const char *filename) {
    if (!doc || !filename) return -1;

    size_t len = 0;
    char *str = csv_to_string_n(doc, &len);
    if (!str) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) { free(str); return -1; }

    size_t written = fwrite(str, 1, len, fp);
    fclose(fp);
    free(str);

    return (written == len) ? 0 : -1;
}

/* ============================================================================
 * Streaming/SAX API
 * ============================================================================ */

struct csv_sax_parser_s {
    csv_stream_handler_t handler;
    void *ctx;
    csv_options_t opts;
    char *field;
    size_t field_len;
    size_t field_cap;
    size_t row;
    size_t col;
    bool in_quote;
    bool quote_pending;
    bool field_started;
    bool row_started;
    bool row_open;
    bool skip_lf;
    bool finished;
    bool failed;
    char error[128];
};

static int csv_sax_fail(csv_sax_parser_t *parser, const char *message) {
    parser->failed = true;
    fmt(parser->error, sizeof(parser->error), "{}", message);
    return -1;
}

static int csv_sax_start_row(csv_sax_parser_t *parser) {
    if (parser->row_open) return 0;
    parser->row_open = true;
    if (parser->handler.on_row_start &&
        parser->handler.on_row_start(parser->ctx, parser->row) != 0)
        return csv_sax_fail(parser, "CSV row callback failed");
    return 0;
}

static int csv_sax_emit_field(csv_sax_parser_t *parser) {
    const char *value = parser->field_len > 0 ? parser->field : "";
    if (csv_sax_start_row(parser) != 0) return -1;
    if (parser->field) parser->field[parser->field_len] = '\0';
    if (parser->handler.on_field &&
        parser->handler.on_field(parser->ctx, parser->row, parser->col, value,
                                 parser->field_len) != 0)
        return csv_sax_fail(parser, "CSV field callback failed");
    parser->col++;
    parser->field_len = 0;
    parser->field_started = false;
    return 0;
}

static int csv_sax_end_row(csv_sax_parser_t *parser, bool has_field) {
    if (has_field && csv_sax_emit_field(parser) != 0) return -1;
    if (!parser->row_open && csv_sax_start_row(parser) != 0) return -1;
    if (parser->handler.on_row_end &&
        parser->handler.on_row_end(parser->ctx, parser->row, parser->col) != 0)
        return csv_sax_fail(parser, "CSV row callback failed");
    parser->row++;
    parser->col = 0;
    parser->row_started = false;
    parser->row_open = false;
    return 0;
}

csv_sax_parser_t *csv_sax_parser_create(const csv_stream_handler_t *handler, void *ctx,
                                        const csv_options_t *opts) {
    csv_sax_parser_t *parser;
    if (!handler) return NULL;
    parser = (csv_sax_parser_t *)calloc(1, sizeof(*parser));
    if (!parser) return NULL;
    parser->handler = *handler;
    parser->ctx = ctx;
    parser->opts = csv_normalize_opts(opts);
    return parser;
}

int csv_sax_parser_feed(csv_sax_parser_t *parser, const char *data, size_t len) {
    size_t i = 0;
    if (!parser || (!data && len != 0)) return -1;
    if (parser->failed || parser->finished) return -1;

    while (i < len) {
        char ch = data[i++];
        if (parser->skip_lf) {
            parser->skip_lf = false;
            if (ch == '\n') continue;
        }

        if (parser->in_quote) {
            if (parser->quote_pending) {
                if (ch == parser->opts.quote) {
                    if (!csv_buf_append(&parser->field, &parser->field_len, &parser->field_cap,
                                        parser->opts.quote))
                        return csv_sax_fail(parser, "Out of memory parsing CSV");
                    parser->quote_pending = false;
                    parser->field_started = true;
                    parser->row_started = true;
                    continue;
                }
                parser->quote_pending = false;
                parser->in_quote = false;
            } else if (ch == parser->opts.quote) {
                parser->quote_pending = true;
                continue;
            } else {
                if (!csv_buf_append(&parser->field, &parser->field_len, &parser->field_cap, ch))
                    return csv_sax_fail(parser, "Out of memory parsing CSV");
                parser->field_started = true;
                parser->row_started = true;
                continue;
            }
        }

        if (!parser->field_started && ch == parser->opts.quote) {
            parser->in_quote = true;
            parser->field_started = true;
            parser->row_started = true;
        } else if (ch == parser->opts.delimiter) {
            if (csv_sax_emit_field(parser) != 0) return -1;
            parser->row_started = true;
        } else if (ch == '\r' || ch == '\n') {
            bool has_field = parser->row_started || parser->field_started;
            if (has_field || !parser->opts.skip_empty_rows) {
                if (csv_sax_end_row(parser, has_field) != 0) return -1;
            }
            if (ch == '\r') parser->skip_lf = true;
        } else {
            if (!csv_buf_append(&parser->field, &parser->field_len, &parser->field_cap, ch))
                return csv_sax_fail(parser, "Out of memory parsing CSV");
            parser->field_started = true;
            parser->row_started = true;
        }
    }
    return 0;
}

int csv_sax_parser_finish(csv_sax_parser_t *parser) {
    if (!parser || parser->failed || parser->finished) return -1;
    parser->finished = true;
    if (parser->quote_pending) {
        parser->quote_pending = false;
        parser->in_quote = false;
    }
    if (parser->in_quote) return csv_sax_fail(parser, "Unterminated quoted field");
    if (parser->row_started || parser->field_started)
        return csv_sax_end_row(parser, true);
    return 0;
}

const char *csv_sax_parser_error(const csv_sax_parser_t *parser) {
    return parser && parser->error[0] ? parser->error : NULL;
}

void csv_sax_parser_destroy(csv_sax_parser_t *parser) {
    if (!parser) return;
    free(parser->field);
    free(parser);
}

int csv_parse_stream(const char *content, size_t len,
                     const csv_stream_handler_t *handler, void *ctx) {
    return csv_parse_stream_opts(content, len, handler, ctx, NULL);
}

int csv_parse_stream_opts(const char *content, size_t len,
                          const csv_stream_handler_t *handler, void *ctx,
                          const csv_options_t *opts) {
    csv_sax_parser_t *parser;
    int rc;
    if (!content || !handler) return -1;
    parser = csv_sax_parser_create(handler, ctx, opts);
    if (!parser) return -1;
    rc = csv_sax_parser_feed(parser, content, len);
    if (rc == 0) rc = csv_sax_parser_finish(parser);
    if (rc != 0 && csv_sax_parser_error(parser))
        fmt(g_error_msg, sizeof(g_error_msg), "{}", csv_sax_parser_error(parser));
    csv_sax_parser_destroy(parser);
    return rc;
}

/* ============================================================================
 * Direct scan API
 * ============================================================================ */

typedef struct {
    vstr value;
    int    needs_unescape;
    int    present;
} csv_scan_field_t;

typedef struct {
    char  *data;
    size_t capacity;
} csv_scan_decode_buffer_t;

static int csv_scan_fail(const char *message) {
    fmt(g_error_msg, sizeof(g_error_msg), "csv scan: {}", message);
    return -1;
}

static int csv_scan_parse_int64(vstr text, int64_t *value) {
    uint64_t magnitude = 0;
    uint64_t limit = (uint64_t)INT64_MAX;
    size_t index = 0;
    int negative = 0;

    if (!value || !text.data || text.len == 0) return -1;
    if (text.data[index] == '-' || text.data[index] == '+') {
        negative = text.data[index] == '-';
        ++index;
    }
    if (index == text.len) return -1;
    if (negative) ++limit;

    for (; index < text.len; ++index) {
        unsigned int digit;
        unsigned char ch = (unsigned char)text.data[index];
        if (ch < '0' || ch > '9') return -1;
        digit = (unsigned int)(ch - '0');
        if (magnitude > (limit - digit) / 10U) return -1;
        magnitude = magnitude * 10U + digit;
    }

    if (negative) {
        *value = magnitude == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)magnitude;
    } else {
        *value = (int64_t)magnitude;
    }
    return 0;
}

static int csv_scan_parse_double(vstr text, double *value) {
    const char *cursor;
    const char *end;
    uint64_t mantissa = 0;
    int fraction_digits = 0;
    int negative = 0;
    int digits = 0;
    double result;
    static const double powers_of_ten[] = {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18,
    };

    if (!value || !text.data || text.len == 0) return -1;
    cursor = text.data;
    end = text.data + text.len;
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
        ++cursor;
    }
    while (cursor < end && *cursor >= '0' && *cursor <= '9') {
        if (mantissa > (UINT64_MAX - (uint64_t)(*cursor - '0')) / 10U) return -1;
        mantissa = mantissa * 10U + (uint64_t)(*cursor++ - '0');
        ++digits;
    }
    if (cursor < end && *cursor == '.') {
        ++cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') {
            if (mantissa > (UINT64_MAX - (uint64_t)(*cursor - '0')) / 10U) return -1;
            mantissa = mantissa * 10U + (uint64_t)(*cursor++ - '0');
            ++fraction_digits;
            ++digits;
        }
    }
    if (digits == 0 || cursor != end) return -1;
    result = (double)mantissa;
    if (fraction_digits > 0) {
        if (fraction_digits < (int)(sizeof(powers_of_ten) / sizeof(powers_of_ten[0]))) {
            result /= powers_of_ten[fraction_digits];
        } else {
            while (fraction_digits-- > 0) result /= 10.0;
        }
    }
    *value = negative ? -result : result;
    return 0;
}

static int csv_scan_text_compare(csv_scan_field_t field, vstr rhs) {
    size_t source_index = 0;
    size_t rhs_index = 0;

    if (!field.value.data || !rhs.data) {
        if (!field.value.data && !rhs.data) return 0;
        return field.value.data ? 1 : -1;
    }
    if (!field.needs_unescape) {
        size_t shared = field.value.len < rhs.len ? field.value.len : rhs.len;
        int result = memcmp(field.value.data, rhs.data, shared);
        if (result != 0) return result;
        return field.value.len == rhs.len ? 0 : (field.value.len < rhs.len ? -1 : 1);
    }

    while (source_index < field.value.len && rhs_index < rhs.len) {
        char source = field.value.data[source_index++];
        if (source == '"' && source_index < field.value.len &&
            field.value.data[source_index] == '"') {
            ++source_index;
        }
        if (source != rhs.data[rhs_index])
            return (unsigned char)source < (unsigned char)rhs.data[rhs_index] ? -1 : 1;
        ++rhs_index;
    }
    if (source_index == field.value.len && rhs_index == rhs.len) return 0;
    return source_index == field.value.len ? -1 : 1;
}

static int csv_scan_compare_result(csv_scan_op_t op, int comparison) {
    switch (op) {
        case CSV_SCAN_OP_EQ: return comparison == 0;
        case CSV_SCAN_OP_NE: return comparison != 0;
        case CSV_SCAN_OP_GT: return comparison > 0;
        case CSV_SCAN_OP_GE: return comparison >= 0;
        case CSV_SCAN_OP_LT: return comparison < 0;
        case CSV_SCAN_OP_LE: return comparison <= 0;
        default: return 0;
    }
}

static int csv_scan_predicate_matches(const csv_scan_predicate_t *predicate,
                                      csv_scan_field_t field, int *matches) {
    int comparison;

    if (!predicate || !matches) return -1;
    if (predicate->type == CSV_SCAN_VALUE_INT64) {
        int64_t integer;
        if (field.needs_unescape || csv_scan_parse_int64(field.value, &integer) != 0)
            return -1;
        comparison = integer == predicate->integer ? 0 :
                     (integer < predicate->integer ? -1 : 1);
    } else if (predicate->type == CSV_SCAN_VALUE_DOUBLE) {
        double number;
        if (field.needs_unescape || csv_scan_parse_double(field.value, &number) != 0)
            return -1;
        comparison = number == predicate->number ? 0 :
                     (number < predicate->number ? -1 : 1);
    } else if (predicate->type == CSV_SCAN_VALUE_TEXT) {
        comparison = csv_scan_text_compare(field, predicate->text);
    } else {
        return -1;
    }
    *matches = csv_scan_compare_result(predicate->op, comparison);
    return 0;
}

static int csv_scan_decode(csv_scan_decode_buffer_t *buffer, csv_scan_field_t field,
                           vstr *value) {
    size_t source_index;
    size_t output_index = 0;

    if (!buffer || !value) return -1;
    if (!field.needs_unescape) {
        *value = field.value;
        return 0;
    }
    if (field.value.len == SIZE_MAX) return -1;
    if (buffer->capacity < field.value.len + 1U) {
        char *data = (char *)realloc(buffer->data, field.value.len + 1U);
        if (!data) return -1;
        buffer->data = data;
        buffer->capacity = field.value.len + 1U;
    }
    for (source_index = 0; source_index < field.value.len; ++source_index) {
        buffer->data[output_index++] = field.value.data[source_index];
        if (field.value.data[source_index] == '"' && source_index + 1 < field.value.len &&
            field.value.data[source_index + 1] == '"') {
            ++source_index;
        }
    }
    *value = vstr_from_buf(buffer->data, output_index);
    return 0;
}

static int csv_scan_finalize_row(const csv_scan_plan_t *plan, size_t row_index,
                                 csv_scan_field_t *predicates, csv_scan_field_t *projections,
                                 csv_scan_decode_buffer_t *decode_buffers,
                                 csv_scan_value_t *values, size_t *match_count) {
    size_t index;
    int matches = 1;

    for (index = 0; index < plan->predicate_count; ++index) {
        int predicate_matches;
        csv_scan_field_t field = predicates[index];
        if (!field.present) field.value = vstr_from_buf("", 0);
        if (csv_scan_predicate_matches(&plan->predicates[index], field, &predicate_matches) != 0)
            return csv_scan_fail("invalid integer predicate field");
        if (!predicate_matches) {
            matches = 0;
            break;
        }
    }
    if (!matches) return 0;

    for (index = 0; index < plan->projection_count; ++index) {
        csv_scan_field_t field = projections[index];
        if (!field.present) field.value = vstr_from_buf("", 0);
        values[index].type = plan->projections[index].type;
        if (values[index].type == CSV_SCAN_VALUE_INT64) {
            if (field.needs_unescape ||
                csv_scan_parse_int64(field.value, &values[index].value.integer) != 0)
                return csv_scan_fail("invalid integer projection field");
        } else if (values[index].type == CSV_SCAN_VALUE_DOUBLE) {
            if (field.needs_unescape ||
                csv_scan_parse_double(field.value, &values[index].value.number) != 0)
                return csv_scan_fail("invalid numeric projection field");
        } else if (values[index].type == CSV_SCAN_VALUE_TEXT) {
            if (csv_scan_decode(&decode_buffers[index], field, &values[index].value.text) != 0)
                return csv_scan_fail("out of memory decoding projection field");
        } else {
            return csv_scan_fail("invalid projection type");
        }
    }

    if (plan->on_match &&
        plan->on_match(plan->ctx, row_index, values, plan->projection_count) != 0)
        return csv_scan_fail("match callback failed");
    ++*match_count;
    return 0;
}

int csv_filter_scan_opts(const char *content, size_t len, const csv_options_t *opts,
                         const csv_scan_plan_t *plan, size_t *matched_count) {
    int predicate_columns[CSV_SCAN_MAX_COLUMNS];
    int predicate_next[CSV_SCAN_MAX_PREDICATES];
    int projection_column[CSV_SCAN_MAX_COLUMNS];
    csv_scan_field_t predicate_fields[CSV_SCAN_MAX_PREDICATES];
    csv_scan_field_t projection_fields[CSV_SCAN_MAX_COLUMNS];
    csv_scan_decode_buffer_t decode_buffers[CSV_SCAN_MAX_COLUMNS] = {{0}};
    csv_scan_value_t values[CSV_SCAN_MAX_COLUMNS];
    csv_lexer_t lexer;
    csv_token_t token;
    size_t column = 0;
    size_t row_index = 0;
    size_t matches = 0;
    size_t max_selected_column = 0;
    size_t index;
    int has_field = 0;
    int has_selected_columns;
    int lexer_rc;
    int skip_header = opts && opts->has_header;

    if (matched_count) *matched_count = 0;
    if (!content || !plan || (plan->predicate_count && !plan->predicates) ||
        (plan->projection_count && !plan->projections) ||
        plan->predicate_count > CSV_SCAN_MAX_PREDICATES ||
        plan->projection_count > CSV_SCAN_MAX_COLUMNS ||
        (opts && ((opts->delimiter && opts->delimiter != ',') ||
                  (opts->quote && opts->quote != '"')))) {
        return csv_scan_fail("invalid plan or unsupported parser options");
    }

    for (index = 0; index < CSV_SCAN_MAX_COLUMNS; ++index) {
        predicate_columns[index] = -1;
        projection_column[index] = -1;
    }
    for (index = 0; index < plan->predicate_count; ++index) {
        const csv_scan_predicate_t *predicate = &plan->predicates[index];
        if (predicate->column >= CSV_SCAN_MAX_COLUMNS ||
            (predicate->type == CSV_SCAN_VALUE_TEXT && !predicate->text.data) ||
            (predicate->type != CSV_SCAN_VALUE_TEXT &&
             predicate->type != CSV_SCAN_VALUE_INT64 &&
             predicate->type != CSV_SCAN_VALUE_DOUBLE) ||
            predicate->op < CSV_SCAN_OP_EQ || predicate->op > CSV_SCAN_OP_LE) {
            return csv_scan_fail("invalid predicate");
        }
        predicate_next[index] = predicate_columns[predicate->column];
        predicate_columns[predicate->column] = (int)index;
        if (predicate->column > max_selected_column) max_selected_column = predicate->column;
    }
    for (index = 0; index < plan->projection_count; ++index) {
        const csv_scan_projection_t *projection = &plan->projections[index];
        if (projection->column >= CSV_SCAN_MAX_COLUMNS ||
            projection_column[projection->column] != -1 ||
            (projection->type != CSV_SCAN_VALUE_TEXT &&
             projection->type != CSV_SCAN_VALUE_INT64 &&
             projection->type != CSV_SCAN_VALUE_DOUBLE)) {
            return csv_scan_fail("invalid projection");
        }
        projection_column[projection->column] = (int)index;
        if (projection->column > max_selected_column) max_selected_column = projection->column;
    }
    has_selected_columns = plan->predicate_count != 0 || plan->projection_count != 0;

    csv_lexer_init(&lexer, content, len);
    csv_lexer_reset_state();
    memset(predicate_fields, 0, sizeof(predicate_fields));
    memset(projection_fields, 0, sizeof(projection_fields));

    while ((lexer_rc = csv_lexer_next(&lexer, &token)) > 0) {
        if (token.type == CSV_TOKEN_FIELD) {
            if (has_selected_columns && column <= max_selected_column) {
                int predicate_index = predicate_columns[column];
                int projection_index = projection_column[column];
                if (predicate_index >= 0 || projection_index >= 0) {
                    csv_scan_field_t field = {
                        .value = vstr_from_buf(token.value, token.length),
                        .needs_unescape = token.needs_unescape,
                        .present = 1,
                    };
                    while (predicate_index >= 0) {
                        predicate_fields[predicate_index] = field;
                        predicate_index = predicate_next[predicate_index];
                    }
                    if (projection_index >= 0) projection_fields[projection_index] = field;
                }
            }
            ++column;
            has_field = 1;
        } else if (token.type == CSV_TOKEN_NEWLINE && has_field) {
            if (!skip_header) {
                if (csv_scan_finalize_row(plan, row_index, predicate_fields, projection_fields,
                                          decode_buffers, values, &matches) != 0)
                    goto cleanup;
                ++row_index;
            } else {
                skip_header = 0;
            }
            column = 0;
            has_field = 0;
            memset(predicate_fields, 0, plan->predicate_count * sizeof(*predicate_fields));
            memset(projection_fields, 0, plan->projection_count * sizeof(*projection_fields));
        }
    }

    if (lexer_rc < 0) {
        csv_scan_fail(lexer.error);
        goto cleanup;
    }
    if (has_field) {
        if (!skip_header) {
            if (csv_scan_finalize_row(plan, row_index, predicate_fields, projection_fields,
                                      decode_buffers, values, &matches) != 0)
                goto cleanup;
        }
    }

    for (index = 0; index < plan->projection_count; ++index) free(decode_buffers[index].data);
    if (matched_count) *matched_count = matches;
    g_error_msg[0] = '\0';
    return 0;

cleanup:
    for (index = 0; index < plan->projection_count; ++index) free(decode_buffers[index].data);
    return -1;
}

/* ============================================================================
 * Iterator API
 * ============================================================================ */

struct csv_iter_s {
    csv_lexer_t    lexer;
    csv_doc_t     *generic_doc;
    size_t         generic_next_row;
    int            use_generic_doc;
    char         **fields;
    size_t        *field_lens;
    size_t         field_count;
    size_t         field_capacity;
    size_t         row_index;
    char          *field_buffer;
    size_t         buffer_size;
    size_t         buffer_used;
};

csv_iter_t *csv_iter_new(const char *content, size_t len) {
    return csv_iter_new_opts(content, len, NULL);
}

csv_iter_t *csv_iter_new_opts(const char *content, size_t len, const csv_options_t *opts) {
    csv_iter_t *iter = (csv_iter_t *)calloc(1, sizeof(csv_iter_t));
    if (!iter) return NULL;

    iter->field_capacity = 16;
    iter->fields = (char **)calloc(iter->field_capacity, sizeof(char *));
    iter->field_lens = (size_t *)calloc(iter->field_capacity, sizeof(size_t));
    iter->buffer_size = 4096;
    iter->field_buffer = (char *)malloc(iter->buffer_size);
    iter->row_index = (size_t)-1;

    if (!iter->fields || !iter->field_lens || !iter->field_buffer) {
        csv_iter_free(iter);
        return NULL;
    }

    if (!csv_opts_use_fast_lexer(opts)) {
        iter->generic_doc = csv_parse_opts(content, len, opts);
        if (!iter->generic_doc) {
            csv_iter_free(iter);
            return NULL;
        }
        iter->use_generic_doc = 1;
        return iter;
    }

    csv_lexer_init(&iter->lexer, content, len);
    csv_lexer_reset_state();

    return iter;
}

void csv_iter_free(csv_iter_t *iter) {
    if (!iter) return;
    free(iter->fields);
    free(iter->field_lens);
    free(iter->field_buffer);
    csv_free(iter->generic_doc);
    free(iter);
}

static void iter_add_field(csv_iter_t *iter, const char *value, size_t len, int needs_unescape) {
    if (iter->field_count >= iter->field_capacity) {
        size_t new_cap = iter->field_capacity * 2;
        char **new_fields = (char **)realloc(iter->fields, new_cap * sizeof(char *));
        size_t *new_lens = (size_t *)realloc(iter->field_lens, new_cap * sizeof(size_t));
        if (!new_fields || !new_lens) return;
        iter->fields = new_fields;
        iter->field_lens = new_lens;
        iter->field_capacity = new_cap;
    }

    size_t needed = iter->buffer_used + len + 1;
    if (needed > iter->buffer_size) {
        size_t new_size = iter->buffer_size * 2;
        if (new_size < needed) new_size = needed;
        char *new_buf = (char *)realloc(iter->field_buffer, new_size);
        if (!new_buf) return;
        // Update existing pointers
        ptrdiff_t offset = new_buf - iter->field_buffer;
        for (size_t i = 0; i < iter->field_count; i++) {
            iter->fields[i] += offset;
        }
        iter->field_buffer = new_buf;
        iter->buffer_size = new_size;
    }

    char *dst = iter->field_buffer + iter->buffer_used;
    size_t final_len = len;

    if (needs_unescape) {
        size_t j = 0;
        for (size_t i = 0; i < len; i++) {
            if (value[i] == '"' && i + 1 < len && value[i + 1] == '"') {
                dst[j++] = '"';
                i++;
            } else {
                dst[j++] = value[i];
            }
        }
        dst[j] = '\0';
        final_len = j;
    } else {
        memcpy(dst, value, len);
        dst[len] = '\0';
    }

    iter->fields[iter->field_count] = dst;
    iter->field_lens[iter->field_count] = final_len;
    iter->field_count++;
    iter->buffer_used += final_len + 1;
}

bool csv_iter_next(csv_iter_t *iter) {
    if (!iter) return false;

    iter->field_count = 0;
    iter->buffer_used = 0;

    if (iter->use_generic_doc) {
        csv_row_node_t *row = get_row_at(iter->generic_doc, iter->generic_next_row);
        csv_field_node_t *field;
        if (!row) return false;
        for (field = row->fields; field; field = field->next) {
            iter_add_field(iter, field->value ? field->value : "", field->length, 0);
        }
        iter->row_index = iter->generic_next_row++;
        return true;
    }

    csv_token_t token;
    int ret;
    bool has_content = false;

    while ((ret = csv_lexer_next(&iter->lexer, &token)) > 0) {
        switch (token.type) {
            case CSV_TOKEN_FIELD:
                iter_add_field(iter, token.value, token.length, token.needs_unescape);
                has_content = true;
                break;

            case CSV_TOKEN_COMMA:
                if (!has_content) {
                    // Empty field at start
                    iter_add_field(iter, "", 0, 0);
                    has_content = true;
                }
                break;

            case CSV_TOKEN_NEWLINE:
                if (has_content) {
                    iter->row_index++;
                    return true;
                }
                break;
        }
    }

    if (has_content) {
        iter->row_index++;
        return true;
    }

    return false;
}

size_t csv_iter_field_count(const csv_iter_t *iter) {
    return iter ? iter->field_count : 0;
}

const char *csv_iter_field(const csv_iter_t *iter, size_t col) {
    if (!iter || col >= iter->field_count) return NULL;
    return iter->fields[col];
}

size_t csv_iter_field_len(const csv_iter_t *iter, size_t col) {
    if (!iter || col >= iter->field_count) return 0;
    return iter->field_lens[col];
}

vstr csv_iter_field_v(const csv_iter_t *iter, size_t col) {
    if (!iter || col >= iter->field_count) return vstr_from_buf(NULL, 0);
    return vstr_from_buf(iter->fields[col], iter->field_lens[col]);
}

size_t csv_iter_row_index(const csv_iter_t *iter) {
    return iter ? iter->row_index : 0;
}
