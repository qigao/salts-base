/**
 * @file json_types.h
 * @brief JSON Parser Internal Types with object_pool-backed arenas
 */

#ifndef JSON_TYPES_H
#define JSON_TYPES_H

#include "json_parser.h"
#include "object_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_POOL_MIN_SIZE (4 * 1024)
#define JSON_POOL_MAX_SIZE (16 * 1024 * 1024)

typedef struct json_blob_chunk_s {
  unsigned char *data;
  size_t capacity;
  size_t used;
  struct json_blob_chunk_s *next;
} json_blob_chunk_t;

typedef struct json_arena_s {
  object_pool_t *value_pool;
  object_pool_t *pair_pool;
  object_pool_t *element_pool;
  json_blob_chunk_t *blob_head;
  json_blob_chunk_t *blob_current;
  size_t initial_size;
  size_t blob_used;
  size_t blob_peak;
  struct json_arena_s *adopted_head;
  struct json_arena_s *adopted_next;
  struct json_arena_s *parent;
} json_arena_t;

typedef struct json_pair_s {
  const char *key;
  size_t key_len;
  int key_owned; // 0 = zero-copy, 1 = arena-allocated
  struct json_value_s *value;
  struct json_pair_s *next;
} json_pair_t;

typedef struct json_object_index_slot_s {
  const char *key;
  size_t key_len;
  json_pair_t *pair;
} json_object_index_slot_t;

typedef struct json_object_index_s {
  json_pair_t **members;
  size_t members_capacity;
  json_object_index_slot_t *lookup;
  size_t lookup_capacity;
  size_t lookup_count;
} json_object_index_t;

typedef struct json_element_s {
  struct json_value_s *value;
  struct json_element_s *next;
} json_element_t;

struct json_value_s {
  json_type_t type;
  json_arena_t *arena;
  union {
    bool bool_val;
    struct {
      double value;
      const char *lexeme;
      size_t lexeme_len;
    } number_val;
    struct {
      const char *str;
      size_t len;
      int owned; // 0 = zero-copy (points to input), 1 = arena-allocated
    } string_val;
    struct {
      json_pair_t *pairs;
      json_pair_t *pairs_tail;
      json_object_index_t *index;
      size_t count;
    } object_val;
    struct {
      json_element_t *elements;
      json_element_t *elements_tail;
      struct json_value_s **index;
      size_t index_capacity;
      size_t count;
    } array_val;
  } data;
};

typedef struct json_value_s json_value_t;

typedef struct {
  json_value_t *root;
  json_arena_t *arena;
  int error;
  char error_msg[256];
} json_parse_ctx_t;

json_arena_t *json_arena_create(void);
json_arena_t *json_arena_create_sized(size_t hint_size);
void *json_arena_alloc(json_arena_t *arena, size_t size);
char *json_arena_strdup(json_arena_t *arena, const char *str, size_t len);
void json_arena_free(json_arena_t *arena);
size_t json_arena_used(json_arena_t *arena);
size_t json_arena_peak(json_arena_t *arena);

json_value_t *json_value_new_arena(json_arena_t *arena, json_type_t type);
json_value_t *json_value_null_arena(json_arena_t *arena);
json_value_t *json_value_bool_arena(json_arena_t *arena, bool val);
json_value_t *json_value_number_arena(json_arena_t *arena, double val);
json_value_t *json_value_string_arena(json_arena_t *arena, const char *str, size_t len);
json_value_t *json_value_array_arena(json_arena_t *arena);
json_value_t *json_value_object_arena(json_arena_t *arena);

bool json_array_append_arena(json_arena_t *arena, json_value_t *arr, json_value_t *val);
bool json_object_set_arena(json_arena_t *arena, json_value_t *obj, const char *key, size_t key_len,
                           json_value_t *val);
bool json_object_set_arena_ex(json_arena_t *arena, json_value_t *obj, const char *key,
                              size_t key_len, int key_owned, json_value_t *val);
bool json_object_build_index(json_arena_t *arena, json_value_t *obj);
size_t json_object_key_hash(const char *key, size_t key_len);
json_value_t *json_object_get_hashed_v(const json_value_t *obj, vstr key,
                                       size_t key_hash);

#ifdef __cplusplus
}
#endif

#endif /* JSON_TYPES_H */
