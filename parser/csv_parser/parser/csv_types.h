/**
 * @file csv_types.h
 * @brief CSV Parser Internal Types with MemoryPool Integration
 */

#ifndef CSV_TYPES_H
#define CSV_TYPES_H

#include "csv_parser.h"
#include <memory_pool.h>
#include <cstl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CSV_POOL_MIN_SIZE  (4 * 1024)
#define CSV_POOL_MAX_SIZE  (16 * 1024 * 1024)

typedef struct csv_pool_node_s {
    MemoryPool              *pool;
    struct csv_pool_node_s  *next;
} csv_pool_node_t;

typedef struct csv_arena_s {
    csv_pool_node_t *head;
    csv_pool_node_t *current;
    size_t           initial_size;
    int              external;
} csv_arena_t;

typedef struct csv_field_node_s {
    const char              *value;
    size_t                   length;
    int                      owned;
    struct csv_field_node_s *next;
} csv_field_node_t;

typedef struct csv_row_node_s {
    csv_field_node_t        *fields;
    csv_field_node_t        *fields_tail;
    size_t                   field_count;
    struct csv_row_node_s   *next;
} csv_row_node_t;

struct csv_doc_s {
    csv_arena_t    *arena;
    csv_row_node_t *header;
    csv_row_node_t *rows;
    csv_row_node_t *rows_tail;
    vec_t           row_index;
    size_t          row_count;
    size_t          column_count;
};

typedef struct {
    csv_doc_t      *doc;
    csv_arena_t    *arena;
    csv_row_node_t *current_row;
    int             error;
    char            error_msg[256];
} csv_parse_ctx_t;

csv_arena_t *csv_arena_create(void);
csv_arena_t *csv_arena_create_sized(size_t hint_size);
void        *csv_arena_alloc(csv_arena_t *arena, size_t size);
char        *csv_arena_strdup(csv_arena_t *arena, const char *str, size_t len);
void         csv_arena_free(csv_arena_t *arena);

csv_doc_t      *csv_doc_new_arena(csv_arena_t *arena);
csv_row_node_t *csv_row_new_arena(csv_arena_t *arena);
void            csv_row_add_field(csv_arena_t *arena, csv_row_node_t *row,
                                  const char *value, size_t len, int owned);
int             csv_doc_add_row(csv_doc_t *doc, csv_row_node_t *row);

char *csv_unescape_arena(csv_arena_t *arena, const char *src, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CSV_TYPES_H */
