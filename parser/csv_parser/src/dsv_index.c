#include "dsv_index.h"

#include "csv_grammar_gen.h"
#include "csv_lexer.h"
#include <salts/fs.h>
#include <cmeta_mmap.h>
#include <tstr.h>
#include <cstl/hash_map.h>
#include <cstl/vec.h>

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    DSV_INDEX_VERSION = 1,
    DSV_INDEX_ENDIAN_MARKER = 0x01020304U,
    DSV_INDEX_FLAG_HAS_COVER = 1U,
};

static const char DSV_INDEX_MAGIC[8] = {'D', 'S', 'V', 'I', 'D', 'X', '0', '1'};

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t endian_marker;
    uint32_t header_size;
    uint32_t entry_size;
    uint64_t source_size;
    uint64_t source_mtime;
    uint64_t source_hash;
    uint64_t entry_count;
    uint32_t text_column;
    uint32_t number_column;
    uint32_t covering_column;
    uint32_t flags;
    uint64_t reserved[3];
} dsv_index_file_header_t;

typedef struct {
    char text_key[DSV_INDEX_MAX_TEXT_KEY];
    uint16_t text_length;
    uint16_t reserved16;
    uint32_t reserved32;
    double number_key;
    uint64_t row_offset;
    uint32_t row_length;
    uint32_t flags;
    int64_t covering_int64;
} dsv_index_file_entry_t;

_Static_assert(sizeof(dsv_index_file_header_t) == 96, "unexpected DSV index header layout");
_Static_assert(sizeof(dsv_index_file_entry_t) == 104, "unexpected DSV index entry layout");

struct dsv_index_s {
    cmeta_mmap_t index_mapping;
    cmeta_mmap_t source_mapping;
    const dsv_index_file_header_t *header;
    const dsv_index_file_entry_t *entries;
    const char *source_data;
    size_t source_length;
    char error[256];
};

typedef struct {
    dsv_index_file_entry_t entry;
    int has_text;
    int has_number;
    int has_cover;
} dsv_index_row_builder_t;

static int dsv_index_fail(dsv_index_t *index, const char *message) {
    if (index) {
        size_t length = message ? strlen(message) : 0;
        if (length >= sizeof(index->error)) length = sizeof(index->error) - 1;
        if (length) memcpy(index->error, message, length);
        index->error[length] = '\0';
    }
    return -1;
}

static int dsv_index_checked_size(uint64_t count, size_t *total) {
    if (!total || count > (uint64_t)SIZE_MAX) return 0;
    if ((size_t)count > (SIZE_MAX - sizeof(dsv_index_file_header_t)) /
                            sizeof(dsv_index_file_entry_t))
        return 0;
    *total = sizeof(dsv_index_file_header_t) +
             (size_t)count * sizeof(dsv_index_file_entry_t);
    return 1;
}

static int dsv_index_parse_int64(vstr text, int64_t *value) {
    uint64_t magnitude = 0;
    uint64_t limit = (uint64_t)INT64_MAX;
    size_t i = 0;
    int negative = 0;

    if (!value || !text.data || text.len == 0) return 0;
    if (text.data[i] == '-' || text.data[i] == '+') {
        negative = text.data[i] == '-';
        ++i;
    }
    if (i == text.len) return 0;
    if (negative) ++limit;
    for (; i < text.len; ++i) {
        unsigned int digit;
        unsigned char ch = (unsigned char)text.data[i];
        if (ch < '0' || ch > '9') return 0;
        digit = (unsigned int)(ch - '0');
        if (magnitude > (limit - digit) / 10U) return 0;
        magnitude = magnitude * 10U + digit;
    }
    if (negative) {
        *value = magnitude == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)magnitude;
    } else {
        *value = (int64_t)magnitude;
    }
    return 1;
}

static int dsv_index_parse_double(vstr text, double *value) {
    const char *cursor;
    const char *end;
    uint64_t mantissa = 0;
    int fraction_digits = 0;
    int digits = 0;
    int negative = 0;
    double result;
    static const double powers_of_ten[] = {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18,
    };

    if (!value || !text.data || text.len == 0) return 0;
    cursor = text.data;
    end = text.data + text.len;
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
        ++cursor;
    }
    while (cursor < end && *cursor >= '0' && *cursor <= '9') {
        if (mantissa > (UINT64_MAX - (uint64_t)(*cursor - '0')) / 10U) return 0;
        mantissa = mantissa * 10U + (uint64_t)(*cursor++ - '0');
        ++digits;
    }
    if (cursor < end && *cursor == '.') {
        ++cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') {
            if (mantissa > (UINT64_MAX - (uint64_t)(*cursor - '0')) / 10U) return 0;
            mantissa = mantissa * 10U + (uint64_t)(*cursor++ - '0');
            ++fraction_digits;
            ++digits;
        }
    }
    if (digits == 0 || cursor != end) return 0;
    result = (double)mantissa;
    if (fraction_digits > 0) {
        if (fraction_digits < (int)(sizeof(powers_of_ten) / sizeof(powers_of_ten[0]))) {
            result /= powers_of_ten[fraction_digits];
        } else {
            while (fraction_digits-- > 0) result /= 10.0;
        }
    }
    *value = negative ? -result : result;
    return 1;
}

static int dsv_index_copy_text_key(const csv_token_t *token, dsv_index_file_entry_t *entry) {
    size_t source = 0;
    size_t output = 0;
    if (!token || !entry) return 0;
    while (source < token->length) {
        char ch = token->value[source++];
        if (output >= DSV_INDEX_MAX_TEXT_KEY) return 0;
        entry->text_key[output++] = ch;
        if (token->needs_unescape && ch == '"' && source < token->length &&
            token->value[source] == '"') {
            ++source;
        }
    }
    entry->text_length = (uint16_t)output;
    return 1;
}

static int dsv_index_entry_compare(const void *left_ptr, const void *right_ptr) {
    const dsv_index_file_entry_t *left = (const dsv_index_file_entry_t *)left_ptr;
    const dsv_index_file_entry_t *right = (const dsv_index_file_entry_t *)right_ptr;
    size_t shared = left->text_length < right->text_length ? left->text_length : right->text_length;
    int result = memcmp(left->text_key, right->text_key, shared);
    if (result != 0) return result;
    if (left->text_length != right->text_length)
        return left->text_length < right->text_length ? -1 : 1;
    if (left->number_key != right->number_key)
        return left->number_key < right->number_key ? -1 : 1;
    if (left->row_offset == right->row_offset) return 0;
    return left->row_offset < right->row_offset ? -1 : 1;
}

static int dsv_index_finish_row(dsv_index_t *index, vec_t *entries,
                                dsv_index_row_builder_t *row, const char *content,
                                const char *row_start, const char *row_end,
                                const dsv_index_config_t *config, int *skip_header) {
    size_t row_length;
    if (*skip_header) {
        *skip_header = 0;
        return 0;
    }
    if (!row->has_text || !row->has_number ||
        (config->covering_int64_column != DSV_INDEX_NO_COLUMN && !row->has_cover))
        return dsv_index_fail(index, "indexed row is missing a configured column");
    if (vec_size(entries) >=
        (config->max_entries ? config->max_entries : DSV_INDEX_DEFAULT_MAX_ENTRIES))
        return dsv_index_fail(index, "DSV index entry capacity exceeded");
    if (row_start < content || row_end < row_start) return dsv_index_fail(index, "invalid row bounds");
    row_length = (size_t)(row_end - row_start);
    if (row_length > UINT32_MAX)
        return dsv_index_fail(index, "CSV row offset or length exceeds index format");
    row->entry.row_offset = (uint64_t)(row_start - content);
    row->entry.row_length = (uint32_t)row_length;
    if (config->covering_int64_column != DSV_INDEX_NO_COLUMN)
        row->entry.flags |= DSV_INDEX_FLAG_HAS_COVER;
    if (vec_push(entries, &row->entry) != STL_OK)
        return dsv_index_fail(index, "out of memory growing DSV index");
    return 0;
}

static int dsv_index_build_internal(dsv_index_t *index, const char *index_path,
                                    const char *content, size_t len,
                                    const dsv_index_config_t *config,
                                    uint64_t source_mtime) {
    vec_t entries = {0};
    csv_lexer_t lexer;
    csv_token_t token;
    dsv_index_row_builder_t row;
    const char *row_start = content;
    int skip_header;
    size_t column = 0;
    int has_field = 0;
    int lexer_rc;
    size_t file_size;
    char *file_data = NULL;
    salts_fs_buf_t file_buffer;
    dsv_index_file_header_t header;
    tstr temporary_path = NULL;
    int rc = -1;

    if (!index || !index_path || !content || !config ||
        config->text_column > UINT32_MAX || config->number_column > UINT32_MAX ||
        config->text_column == config->number_column ||
        (config->covering_int64_column != DSV_INDEX_NO_COLUMN &&
         config->covering_int64_column > UINT32_MAX))
        return dsv_index_fail(index, "invalid DSV index build configuration");
    if (cmeta_mmap_is_open(&index->index_mapping) ||
        cmeta_mmap_is_open(&index->source_mapping))
        return dsv_index_fail(index, "close the DSV index before rebuilding it");
    if (vec_init_bytes(
            &entries, sizeof(dsv_index_file_entry_t), _Alignof(dsv_index_file_entry_t),
            config->max_entries ? config->max_entries : DSV_INDEX_DEFAULT_MAX_ENTRIES) !=
        STL_OK)
        return dsv_index_fail(index, "out of memory creating DSV index");

    memset(&row, 0, sizeof(row));
    skip_header = config->has_header ? 1 : 0;
    csv_lexer_init(&lexer, content, len);
    csv_lexer_reset_state();
    while ((lexer_rc = csv_lexer_next(&lexer, &token)) > 0) {
        if (token.type == CSV_TOKEN_FIELD) {
            vstr field = vstr_from_buf(token.value, token.length);
            if (!skip_header && column == config->text_column) {
                if (!dsv_index_copy_text_key(&token, &row.entry)) {
                    dsv_index_fail(index, "text index key exceeds DSV_INDEX_MAX_TEXT_KEY");
                    goto cleanup;
                }
                row.has_text = 1;
            }
            if (!skip_header && column == config->number_column) {
                if (token.needs_unescape || !dsv_index_parse_double(field, &row.entry.number_key)) {
                    dsv_index_fail(index, "numeric index key is invalid");
                    goto cleanup;
                }
                row.has_number = 1;
            }
            if (!skip_header && config->covering_int64_column != DSV_INDEX_NO_COLUMN &&
                column == config->covering_int64_column) {
                if (token.needs_unescape ||
                    !dsv_index_parse_int64(field, &row.entry.covering_int64)) {
                    dsv_index_fail(index, "covering integer value is invalid");
                    goto cleanup;
                }
                row.has_cover = 1;
            }
            ++column;
            has_field = 1;
        } else if (token.type == CSV_TOKEN_NEWLINE && has_field) {
            if (dsv_index_finish_row(index, &entries, &row, content, row_start,
                                     lexer.cursor, config, &skip_header) != 0)
                goto cleanup;
            memset(&row, 0, sizeof(row));
            column = 0;
            has_field = 0;
            row_start = lexer.cursor;
        }
    }
    if (lexer_rc < 0) {
        dsv_index_fail(index, lexer.error);
        goto cleanup;
    }
    if (has_field && dsv_index_finish_row(index, &entries, &row, content, row_start,
                                          content + len, config, &skip_header) != 0)
        goto cleanup;

    if (vec_size(&entries) > 1) {
        qsort(vec_data(&entries), vec_size(&entries),
              sizeof(dsv_index_file_entry_t), dsv_index_entry_compare);
    }
    if (!dsv_index_checked_size((uint64_t)vec_size(&entries), &file_size)) {
        dsv_index_fail(index, "DSV index file size overflow");
        goto cleanup;
    }
    file_data = (char *)malloc(file_size);
    if (!file_data) {
        dsv_index_fail(index, "out of memory serializing DSV index");
        goto cleanup;
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, DSV_INDEX_MAGIC, sizeof(header.magic));
    header.version = DSV_INDEX_VERSION;
    header.endian_marker = DSV_INDEX_ENDIAN_MARKER;
    header.header_size = (uint32_t)sizeof(header);
    header.entry_size = (uint32_t)sizeof(dsv_index_file_entry_t);
    header.source_size = (uint64_t)len;
    header.source_mtime = source_mtime;
    header.source_hash = (uint64_t)hash_bytes(content, len, NULL);
    header.entry_count = (uint64_t)vec_size(&entries);
    header.text_column = (uint32_t)config->text_column;
    header.number_column = (uint32_t)config->number_column;
    header.covering_column = config->covering_int64_column == DSV_INDEX_NO_COLUMN
                                 ? UINT32_MAX
                                 : (uint32_t)config->covering_int64_column;
    if (config->covering_int64_column != DSV_INDEX_NO_COLUMN)
        header.flags |= DSV_INDEX_FLAG_HAS_COVER;
    memcpy(file_data, &header, sizeof(header));
    if (vec_size(&entries) > 0) {
        memcpy(file_data + sizeof(header), vec_data(&entries),
               vec_size(&entries) * sizeof(dsv_index_file_entry_t));
    }
    file_buffer.base = file_data;
    file_buffer.len = file_size;
    temporary_path = tstr_dup(index_path);
    if (temporary_path) {
        tstr next_path = tstr_cat(temporary_path, ".tmp");
        if (next_path) temporary_path = next_path;
        else tstr_freep(&temporary_path);
    }
    if (!temporary_path) {
        dsv_index_fail(index, "out of memory creating temporary index path");
        goto cleanup;
    }
    if (salts_fs_write_file(temporary_path, &file_buffer) != 0) {
        dsv_index_fail(index, "failed writing DSV sidecar index");
        goto cleanup;
    }
    if (salts_fs_rename(temporary_path, index_path) != 0) {
        dsv_index_fail(index, "failed replacing DSV sidecar index");
        goto cleanup;
    }
    index->error[0] = '\0';
    rc = 0;

cleanup:
    if (rc != 0 && temporary_path) (void)salts_fs_unlink(temporary_path);
    tstr_free(temporary_path);
    free(file_data);
    vec_destroy(&entries);
    return rc;
}

dsv_index_t *dsv_index_create(void) {
    dsv_index_t *index = (dsv_index_t *)calloc(1, sizeof(*index));
    if (!index) return NULL;
    cmeta_mmap_init(&index->index_mapping);
    cmeta_mmap_init(&index->source_mapping);
    return index;
}

void dsv_index_close(dsv_index_t *index) {
    if (!index) return;
    cmeta_mmap_close(&index->index_mapping);
    cmeta_mmap_close(&index->source_mapping);
    index->header = NULL;
    index->entries = NULL;
    index->source_data = NULL;
    index->source_length = 0;
}

void dsv_index_destroy(dsv_index_t *index) {
    if (!index) return;
    dsv_index_close(index);
    free(index);
}

const char *dsv_index_error(const dsv_index_t *index) {
    return index ? index->error : "";
}

int dsv_index_build_memory(dsv_index_t *index, const char *index_path,
                           const char *content, size_t len,
                           const dsv_index_config_t *config) {
    return dsv_index_build_internal(index, index_path, content, len, config, 0);
}

int dsv_index_build_file(dsv_index_t *index, const char *csv_path,
                         const char *index_path, const dsv_index_config_t *config) {
    cmeta_mmap_t source;
    salts_fs_stat_t stat;
    int rc;
    if (!index || !csv_path || !index_path || !config)
        return dsv_index_fail(index, "invalid DSV index file build arguments");
    if (salts_fs_stat(csv_path, &stat) != 0 || !stat.is_file)
        return dsv_index_fail(index, "failed to stat source CSV");
    cmeta_mmap_init(&source);
    if (cmeta_mmap_open(&source, csv_path, SALTS_MMAP_READ) != 0)
        return dsv_index_fail(index, "failed to map source CSV");
    rc = dsv_index_build_internal(index, index_path,
                                  (const char *)cmeta_mmap_data(&source),
                                  cmeta_mmap_size(&source), config, stat.mtime);
    cmeta_mmap_close(&source);
    return rc;
}

static int dsv_index_validate_entries(dsv_index_t *index) {
    uint64_t i;
    for (i = 0; i < index->header->entry_count; ++i) {
        const dsv_index_file_entry_t *entry = &index->entries[i];
        if (entry->text_length > DSV_INDEX_MAX_TEXT_KEY ||
            entry->row_offset > index->source_length ||
            entry->row_length > index->source_length - (size_t)entry->row_offset)
            return dsv_index_fail(index, "DSV index contains invalid entry bounds");
        if (i > 0 && dsv_index_entry_compare(&index->entries[i - 1], entry) > 0)
            return dsv_index_fail(index, "DSV index entries are not sorted");
    }
    return 0;
}

static int dsv_index_open_internal(dsv_index_t *index, const char *index_path,
                                   const char *content, size_t len,
                                   uint64_t expected_mtime) {
    const dsv_index_file_header_t *header;
    size_t expected_size;

    if (!index || !index_path || !content) return dsv_index_fail(index, "invalid DSV index open arguments");
    if (cmeta_mmap_open(&index->index_mapping, index_path, SALTS_MMAP_READ) != 0)
        return dsv_index_fail(index, "failed to map DSV sidecar index");
    if (cmeta_mmap_size(&index->index_mapping) < sizeof(*header))
        return dsv_index_fail(index, "DSV index is truncated");
    header = (const dsv_index_file_header_t *)cmeta_mmap_data(&index->index_mapping);
    if (memcmp(header->magic, DSV_INDEX_MAGIC, sizeof(header->magic)) != 0 ||
        header->version != DSV_INDEX_VERSION ||
        header->endian_marker != DSV_INDEX_ENDIAN_MARKER ||
        header->header_size != sizeof(*header) ||
        header->entry_size != sizeof(dsv_index_file_entry_t) ||
        !dsv_index_checked_size(header->entry_count, &expected_size) ||
        expected_size != cmeta_mmap_size(&index->index_mapping))
        return dsv_index_fail(index, "DSV index format validation failed");
    if (header->source_size != (uint64_t)len)
        return dsv_index_fail(index, "DSV index source size mismatch");
    if (expected_mtime != 0) {
        if (header->source_mtime != expected_mtime)
            return dsv_index_fail(index, "DSV index source mtime mismatch");
    } else if (header->source_hash != (uint64_t)hash_bytes(content, len, NULL)) {
        return dsv_index_fail(index, "DSV index source hash mismatch");
    }

    index->header = header;
    index->entries = (const dsv_index_file_entry_t *)((const char *)header + sizeof(*header));
    index->source_data = content;
    index->source_length = len;
    if (dsv_index_validate_entries(index) != 0) return -1;
    (void)cmeta_mmap_advise(&index->index_mapping, SALTS_MMAP_RANDOM);
    index->error[0] = '\0';
    return 0;
}

int dsv_index_open_memory(dsv_index_t *index, const char *index_path,
                          const char *content, size_t len) {
    if (!index) return -1;
    dsv_index_close(index);
    if (dsv_index_open_internal(index, index_path, content, len, 0) != 0) {
        cmeta_mmap_close(&index->index_mapping);
        return -1;
    }
    return 0;
}

int dsv_index_open_file(dsv_index_t *index, const char *csv_path,
                        const char *index_path) {
    salts_fs_stat_t stat;
    if (!index || !csv_path || !index_path)
        return dsv_index_fail(index, "invalid DSV index file open arguments");
    dsv_index_close(index);
    if (salts_fs_stat(csv_path, &stat) != 0 || !stat.is_file)
        return dsv_index_fail(index, "failed to stat source CSV");
    if (cmeta_mmap_open(&index->source_mapping, csv_path, SALTS_MMAP_READ) != 0)
        return dsv_index_fail(index, "failed to map source CSV");
    if (dsv_index_open_internal(index, index_path,
                                (const char *)cmeta_mmap_data(&index->source_mapping),
                                cmeta_mmap_size(&index->source_mapping), stat.mtime) != 0) {
        cmeta_mmap_close(&index->index_mapping);
        cmeta_mmap_close(&index->source_mapping);
        return -1;
    }
    (void)cmeta_mmap_advise(&index->source_mapping, SALTS_MMAP_RANDOM);
    return 0;
}

size_t dsv_index_count(const dsv_index_t *index) {
    return index && index->header ? (size_t)index->header->entry_count : 0;
}

size_t dsv_index_text_column(const dsv_index_t *index) {
    return index && index->header ? (size_t)index->header->text_column : DSV_INDEX_NO_COLUMN;
}

size_t dsv_index_number_column(const dsv_index_t *index) {
    return index && index->header ? (size_t)index->header->number_column : DSV_INDEX_NO_COLUMN;
}

size_t dsv_index_covering_column(const dsv_index_t *index) {
    if (!index || !index->header || !(index->header->flags & DSV_INDEX_FLAG_HAS_COVER))
        return DSV_INDEX_NO_COLUMN;
    return (size_t)index->header->covering_column;
}

static int dsv_index_compare_text(const dsv_index_file_entry_t *entry, vstr text) {
    size_t shared = entry->text_length < text.len ? entry->text_length : text.len;
    int result = memcmp(entry->text_key, text.data, shared);
    if (result != 0) return result;
    if (entry->text_length == text.len) return 0;
    return entry->text_length < text.len ? -1 : 1;
}

static int dsv_index_range_compare(const void *left_ptr, const void *right_ptr) {
    const dsv_index_range_cursor_t *left = (const dsv_index_range_cursor_t *)left_ptr;
    const dsv_index_range_cursor_t *right = (const dsv_index_range_cursor_t *)right_ptr;
    size_t shared = left->_text_length < right->_text_length
                        ? left->_text_length
                        : right->_text_length;
    int result = memcmp(left->_text, right->_text, shared);
    if (result != 0) return result;
    if (left->_text_length != right->_text_length)
        return left->_text_length < right->_text_length ? -1 : 1;
    if (left->_has_lower != right->_has_lower) return left->_has_lower ? 1 : -1;
    if (left->_has_lower && left->_lower_number != right->_lower_number)
        return left->_lower_number < right->_lower_number ? -1 : 1;
    if (left->_lower_inclusive != right->_lower_inclusive)
        return left->_lower_inclusive ? -1 : 1;
    return 0;
}

static int dsv_index_ranges_same_text(const dsv_index_range_cursor_t *left,
                                      const dsv_index_range_cursor_t *right) {
    return left->_text_length == right->_text_length &&
           memcmp(left->_text, right->_text, left->_text_length) == 0;
}

static int dsv_index_ranges_overlap(const dsv_index_range_cursor_t *left,
                                    const dsv_index_range_cursor_t *right) {
    if (!dsv_index_ranges_same_text(left, right)) return 0;
    if (!left->_has_upper || !right->_has_lower) return 1;
    if (right->_lower_number < left->_upper_number) return 1;
    return right->_lower_number == left->_upper_number &&
           (right->_lower_inclusive || left->_upper_inclusive);
}

static void dsv_index_merge_range(dsv_index_range_cursor_t *left,
                                  const dsv_index_range_cursor_t *right) {
    if (!left->_has_upper ||
        (right->_has_upper && right->_upper_number < left->_upper_number))
        return;
    if (!right->_has_upper) {
        left->_has_upper = 0;
        left->_upper_inclusive = 0;
        return;
    }
    if (right->_upper_number > left->_upper_number) {
        left->_upper_number = right->_upper_number;
        left->_upper_inclusive = right->_upper_inclusive;
    } else if (right->_upper_number == left->_upper_number) {
        left->_upper_inclusive = left->_upper_inclusive || right->_upper_inclusive;
    }
}

static void dsv_index_range_position(const dsv_index_t *index,
                                     dsv_index_range_cursor_t *range) {
    uint64_t low;
    uint64_t high;

    low = 0;
    high = index->header->entry_count;
    while (low < high) {
        uint64_t mid = low + (high - low) / 2U;
        const dsv_index_file_entry_t *entry = &index->entries[mid];
        vstr text = vstr_from_buf(range->_text, range->_text_length);
        int text_cmp = dsv_index_compare_text(entry, text);
        int before = text_cmp < 0;
        if (text_cmp == 0 && range->_has_lower) {
            before = entry->number_key < range->_lower_number ||
                     (entry->number_key == range->_lower_number &&
                      !range->_lower_inclusive);
        }
        if (before) low = mid + 1U;
        else high = mid;
    }
    range->_position = low;
}

int dsv_index_seek_many(const dsv_index_t *index,
                        const dsv_index_query_t *queries, size_t query_count,
                        dsv_index_cursor_t *cursor) {
    size_t i;
    size_t output_count = 0;
    if (!index || !index->header || !queries || !cursor || query_count == 0 ||
        query_count > DSV_INDEX_MAX_QUERY_RANGES)
        return -1;
    memset(cursor, 0, sizeof(*cursor));
    for (i = 0; i < query_count; ++i) {
        const dsv_index_query_t *query = &queries[i];
        dsv_index_range_cursor_t *range;
        if (!query->text_equals.data || query->text_equals.len > DSV_INDEX_MAX_TEXT_KEY)
            return -1;
        if (query->has_lower_number && query->has_upper_number &&
            (query->lower_number > query->upper_number ||
             (query->lower_number == query->upper_number &&
              (!query->lower_inclusive || !query->upper_inclusive))))
            continue;
        range = &cursor->_ranges[output_count++];
        memcpy(range->_text, query->text_equals.data, query->text_equals.len);
        range->_text_length = (uint16_t)query->text_equals.len;
        range->_has_lower = query->has_lower_number ? 1U : 0U;
        range->_lower_inclusive = query->lower_inclusive ? 1U : 0U;
        range->_lower_number = query->lower_number;
        range->_has_upper = query->has_upper_number ? 1U : 0U;
        range->_upper_inclusive = query->upper_inclusive ? 1U : 0U;
        range->_upper_number = query->upper_number;
    }
    if (output_count == 0) return 0;
    qsort(cursor->_ranges, output_count, sizeof(cursor->_ranges[0]),
          dsv_index_range_compare);
    cursor->_range_count = 1;
    for (i = 1; i < output_count; ++i) {
        dsv_index_range_cursor_t *last = &cursor->_ranges[cursor->_range_count - 1];
        if (dsv_index_ranges_overlap(last, &cursor->_ranges[i])) {
            dsv_index_merge_range(last, &cursor->_ranges[i]);
        } else {
            cursor->_ranges[cursor->_range_count++] = cursor->_ranges[i];
        }
    }
    for (i = 0; i < cursor->_range_count; ++i)
        dsv_index_range_position(index, &cursor->_ranges[i]);
    return 0;
}

int dsv_index_seek(const dsv_index_t *index, const dsv_index_query_t *query,
                   dsv_index_cursor_t *cursor) {
    return dsv_index_seek_many(index, query, 1, cursor);
}

int dsv_index_cursor_next(const dsv_index_t *index, dsv_index_cursor_t *cursor,
                          dsv_index_row_t *row) {
    dsv_index_range_cursor_t *range;
    if (!index || !index->header || !cursor || !row) return -1;
    while (cursor->_range_index < cursor->_range_count) {
        vstr text;
        range = &cursor->_ranges[cursor->_range_index];
        text = vstr_from_buf(range->_text, range->_text_length);
        while (!range->_finished && range->_position < index->header->entry_count) {
            const dsv_index_file_entry_t *entry = &index->entries[range->_position++];
            int text_cmp = dsv_index_compare_text(entry, text);
            if (text_cmp != 0) {
                range->_finished = 1;
                break;
            }
            if (range->_has_lower &&
                (entry->number_key < range->_lower_number ||
                 (entry->number_key == range->_lower_number && !range->_lower_inclusive)))
                continue;
            if (range->_has_upper &&
                (entry->number_key > range->_upper_number ||
                 (entry->number_key == range->_upper_number && !range->_upper_inclusive))) {
                range->_finished = 1;
                break;
            }
            row->row_offset = entry->row_offset;
            row->row_length = entry->row_length;
            row->has_covering_int64 = (entry->flags & DSV_INDEX_FLAG_HAS_COVER) != 0;
            row->covering_int64 = entry->covering_int64;
            return 1;
        }
        ++cursor->_range_index;
    }
    return 0;
}

vstr dsv_index_row_view(const dsv_index_t *index, const dsv_index_row_t *row) {
    if (!index || !row || !index->source_data || row->row_offset > index->source_length ||
        row->row_length > index->source_length - (size_t)row->row_offset)
        return vstr_from_buf(NULL, 0);
    return vstr_from_buf(index->source_data + (size_t)row->row_offset, row->row_length);
}
