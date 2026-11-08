/**
 * @file json_parser.c
 * @brief JSON Parser Implementation with object_pool-backed arenas
 */

#include "json_parser.h"
#include "json_grammar_gen.h"
#include "json_lexer.h"
#include "json_lexer_whitespace.h"
#include "json_types.h"
#include "json_unicode.h"
#include <errno.h>
#include <fmt.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <salts/thread.h>
#include <tstr.h>

#define MAX_ERROR_LEN 512
#define JSON_ARRAY_INDEX_THRESHOLD 8U
#define JSON_OBJECT_INDEX_THRESHOLD 16U
#define JSON_OBJECT_INDEX_MIN_CAPACITY 16U
static SALTS_THREAD_LOCAL char g_error[MAX_ERROR_LEN] = {0};

_Static_assert(sizeof(((json_value_t *)0)->data.object_val) <=
                   sizeof(((json_value_t *)0)->data.array_val),
               "object indexes must not increase json_value_t size");

/* ============================================================================
 * Arena Allocator using object pools + blob chunks
 * ============================================================================ */

static json_blob_chunk_t *json_blob_chunk_create(size_t size) {
  json_blob_chunk_t *chunk = (json_blob_chunk_t *)malloc(sizeof(json_blob_chunk_t));
  if (!chunk) return NULL;

  chunk->data = (unsigned char *)malloc(size);
  if (!chunk->data) {
    free(chunk);
    return NULL;
  }

  chunk->capacity = size;
  chunk->used = 0;
  chunk->next = NULL;
  return chunk;
}

static void json_blob_chunk_destroy(json_blob_chunk_t *chunk) {
  if (!chunk) return;
  free(chunk->data);
  free(chunk);
}

static object_pool_t *json_object_pool_create(size_t object_size, size_t hint_size) {
  size_t initial_capacity = hint_size / object_size;
  if (initial_capacity < 64) initial_capacity = 64;
  if (initial_capacity > 65536) initial_capacity = 65536;

  object_pool_config_t config = {
      .object_size = object_size,
      .initial_capacity = initial_capacity,
      .max_capacity = 0,
      .zero_on_alloc = true,
  };
  return object_pool_create(&config);
}

static void json_arena_destroy_self(json_arena_t *arena) {
  json_blob_chunk_t *chunk = arena->blob_head;
  while (chunk) {
    json_blob_chunk_t *next = chunk->next;
    json_blob_chunk_destroy(chunk);
    chunk = next;
  }

  object_pool_destroy(arena->value_pool);
  object_pool_destroy(arena->pair_pool);
  object_pool_destroy(arena->element_pool);
  free(arena);
}

static bool json_arena_can_adopt(const json_arena_t *dst, const json_arena_t *src) {
  if (!dst || !src) return false;
  if (dst == src || src->parent == dst) return true;
  if (src->parent) return false;

  for (const json_arena_t *parent = dst; parent; parent = parent->parent) {
    if (parent == src) return false;
  }
  return true;
}

static bool json_arena_adopt(json_arena_t *dst, json_arena_t *src) {
  if (!json_arena_can_adopt(dst, src)) return false;
  if (dst == src || src->parent == dst) return true;

  src->adopted_next = dst->adopted_head;
  src->parent = dst;
  dst->adopted_head = src;
  return true;
}

static void *json_blob_alloc(json_arena_t *arena, size_t size) {
  if (!arena || size == 0) return NULL;

  size = (size + 7) & ~((size_t)7);

  if (!arena->blob_current || arena->blob_current->used + size > arena->blob_current->capacity) {
    size_t new_size = arena->initial_size;
    if (new_size < size) new_size = size;
    while (new_size < size && new_size < JSON_POOL_MAX_SIZE / 2) {
      new_size *= 2;
    }
    if (new_size > JSON_POOL_MAX_SIZE) new_size = JSON_POOL_MAX_SIZE;
    if (new_size < size) new_size = size;

    json_blob_chunk_t *chunk = json_blob_chunk_create(new_size);
    if (!chunk) return NULL;

    if (arena->blob_current) {
      arena->blob_current->next = chunk;
    } else {
      arena->blob_head = chunk;
    }
    arena->blob_current = chunk;
    arena->initial_size = new_size;
  }

  void *ptr = arena->blob_current->data + arena->blob_current->used;
  arena->blob_current->used += size;
  arena->blob_used += size;
  if (arena->blob_used > arena->blob_peak) {
    arena->blob_peak = arena->blob_used;
  }
  return ptr;
}

json_arena_t *json_arena_create(void) { return json_arena_create_sized(JSON_POOL_MIN_SIZE); }

json_arena_t *json_arena_create_sized(size_t hint_size) {
  json_arena_t *arena = (json_arena_t *)calloc(1, sizeof(json_arena_t));
  if (!arena) return NULL;

  // Clamp to reasonable range
  if (hint_size < JSON_POOL_MIN_SIZE) hint_size = JSON_POOL_MIN_SIZE;
  if (hint_size > JSON_POOL_MAX_SIZE) hint_size = JSON_POOL_MAX_SIZE;

  arena->value_pool = json_object_pool_create(sizeof(json_value_t), hint_size);
  arena->pair_pool = json_object_pool_create(sizeof(json_pair_t), hint_size);
  arena->element_pool = json_object_pool_create(sizeof(json_element_t), hint_size);
  arena->blob_head = json_blob_chunk_create(hint_size);
  if (!arena->value_pool || !arena->pair_pool || !arena->element_pool || !arena->blob_head) {
    json_arena_destroy_self(arena);
    return NULL;
  }

  arena->blob_current = arena->blob_head;
  arena->initial_size = hint_size;
  return arena;
}

void *json_arena_alloc(json_arena_t *arena, size_t size) {
  if (!arena) return NULL;

  if (size == sizeof(json_value_t)) {
    return object_pool_alloc(arena->value_pool);
  }
  if (size == sizeof(json_pair_t)) {
    return object_pool_alloc(arena->pair_pool);
  }
  if (size == sizeof(json_element_t)) {
    return object_pool_alloc(arena->element_pool);
  }

  return json_blob_alloc(arena, size);
}

char *json_arena_strdup(json_arena_t *arena, const char *str, size_t len) {
  if (!arena || !str || len == SIZE_MAX) return NULL;
  char *dst = (char *)json_arena_alloc(arena, len + 1);
  if (!dst) return NULL;
  memcpy(dst, str, len);
  dst[len] = '\0';
  return dst;
}

void json_arena_free(json_arena_t *arena) {
  if (!arena) return;

  if (arena->parent) {
    json_arena_free(arena->parent);
    return;
  }

  json_arena_t *child = arena->adopted_head;
  while (child) {
    json_arena_t *next = child->adopted_next;
    child->parent = NULL;
    child->adopted_next = NULL;
    json_arena_free(child);
    child = next;
  }

  json_arena_destroy_self(arena);
}

size_t json_arena_used(json_arena_t *arena) {
  if (!arena) return 0;

  size_t total = arena->blob_used;
  total += object_pool_allocated_count(arena->value_pool) * sizeof(json_value_t);
  total += object_pool_allocated_count(arena->pair_pool) * sizeof(json_pair_t);
  total += object_pool_allocated_count(arena->element_pool) * sizeof(json_element_t);

  for (json_arena_t *child = arena->adopted_head; child; child = child->adopted_next) {
    total += json_arena_used(child);
  }
  return total;
}

size_t json_arena_peak(json_arena_t *arena) {
  if (!arena) return 0;

  size_t total = arena->blob_peak;
  total += object_pool_peak_usage(arena->value_pool) * sizeof(json_value_t);
  total += object_pool_peak_usage(arena->pair_pool) * sizeof(json_pair_t);
  total += object_pool_peak_usage(arena->element_pool) * sizeof(json_element_t);

  for (json_arena_t *child = arena->adopted_head; child; child = child->adopted_next) {
    total += json_arena_peak(child);
  }
  return total;
}

/* ============================================================================
 * Value Construction (Arena)
 * ============================================================================ */

json_value_t *json_value_new_arena(json_arena_t *arena, json_type_t type) {
  json_value_t *v = (json_value_t *)json_arena_alloc(arena, sizeof(json_value_t));
  if (!v) return NULL;
  memset(v, 0, sizeof(json_value_t));
  v->type = type;
  v->arena = arena;
  return v;
}

json_value_t *json_value_null_arena(json_arena_t *arena) {
  return json_value_new_arena(arena, JSON_NULL);
}

json_value_t *json_value_bool_arena(json_arena_t *arena, bool val) {
  json_value_t *v = json_value_new_arena(arena, JSON_BOOL);
  if (v) v->data.bool_val = val;
  return v;
}

json_value_t *json_value_number_arena(json_arena_t *arena, double val) {
  json_value_t *v = json_value_new_arena(arena, JSON_NUMBER);
  if (v) v->data.number_val.value = val;
  return v;
}

json_value_t *json_value_string_arena(json_arena_t *arena, const char *str, size_t len) {
  if (!arena || !str) return NULL;
  json_value_t *v = json_value_new_arena(arena, JSON_STRING);
  if (!v) return NULL;

  v->data.string_val.str = json_arena_strdup(arena, str, len);
  if (!v->data.string_val.str) return NULL;
  v->data.string_val.len = len;
  v->data.string_val.owned = 1;
  return v;
}

json_value_t *json_value_array_arena(json_arena_t *arena) {
  return json_value_new_arena(arena, JSON_ARRAY);
}

json_value_t *json_value_object_arena(json_arena_t *arena) {
  return json_value_new_arena(arena, JSON_OBJECT);
}

static int json_array_index_reserve(json_arena_t *arena, json_value_t *arr, size_t needed) {
  json_value_t **index;
  size_t capacity;

  if (needed <= arr->data.array_val.index_capacity) return 1;

  capacity = arr->data.array_val.index_capacity ? arr->data.array_val.index_capacity : 8;
  while (capacity < needed) {
    if (capacity > SIZE_MAX / 2) {
      capacity = needed;
      break;
    }
    capacity *= 2;
  }
  if (capacity > SIZE_MAX / sizeof(*index)) return 0;

  index = (json_value_t **)json_arena_alloc(arena, capacity * sizeof(*index));
  if (!index) return 0;

  if (arr->data.array_val.index && arr->data.array_val.count > 0) {
    memcpy(index, arr->data.array_val.index, arr->data.array_val.count * sizeof(*index));
  } else {
    json_element_t *element = arr->data.array_val.elements;
    size_t i = 0;
    while (element) {
      index[i++] = element->value;
      element = element->next;
    }
  }
  arr->data.array_val.index = index;
  arr->data.array_val.index_capacity = capacity;
  return 1;
}

bool json_array_append_arena(json_arena_t *arena, json_value_t *arr, json_value_t *val) {
  size_t next_count;
  if (!arena || !arr || arr->type != JSON_ARRAY || !val || arr->data.array_val.count == SIZE_MAX)
    return false;

  next_count = arr->data.array_val.count + 1;
  if ((arr->data.array_val.index || next_count >= JSON_ARRAY_INDEX_THRESHOLD) &&
      !json_array_index_reserve(arena, arr, next_count)) {
    /* The linked list remains authoritative when the optional index cannot grow. */
    arr->data.array_val.index = NULL;
    arr->data.array_val.index_capacity = 0;
  }

  json_element_t *elem = (json_element_t *)json_arena_alloc(arena, sizeof(json_element_t));
  if (!elem) return false;

  elem->value = val;
  elem->next = NULL;

  if (arr->data.array_val.elements_tail) {
    arr->data.array_val.elements_tail->next = elem;
  } else {
    arr->data.array_val.elements = elem;
  }
  arr->data.array_val.elements_tail = elem;
  if (arr->data.array_val.index) {
    arr->data.array_val.index[arr->data.array_val.count] = val;
  }
  arr->data.array_val.count++;
  return true;
}

size_t json_object_key_hash(const char *key, size_t key_len) {
  /* Exact existing object-key fingerprint, formerly pulled incidentally from
   * generic CSTL hash_map.c. JSON owns only this private index; no duplicate
   * container provider or global CMeta runtime is created.
   * Preserve the old offset and overflow semantics for existing JSON indexes. */
  const unsigned char *bytes = (const unsigned char *)key;
  uint64_t hash = UINT64_C(1469598103934665603);
  for (size_t i = 0u; i < key_len; ++i) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return (size_t)(hash == 0u ? 1u : hash);
}

static int json_object_index_capacity_for(size_t count, size_t *capacity) {
  size_t wanted;
  size_t value = JSON_OBJECT_INDEX_MIN_CAPACITY;
  if (!capacity || count > (SIZE_MAX - 1U) / 10U) return 0;
  wanted = (count * 10U) / 7U + 1U;
  while (value < wanted) {
    if (value > SIZE_MAX / 2U) return 0;
    value *= 2U;
  }
  *capacity = value;
  return 1;
}

static json_pair_t *json_object_index_find_hashed(
    const json_object_index_slot_t *slots, size_t capacity, const char *key,
    size_t key_len, size_t hash) {
  size_t mask;
  size_t i;
  if (!slots || capacity == 0 || !key || hash == 0) return NULL;
  mask = capacity - 1U;
  for (i = 0; i < capacity; ++i) {
    const json_object_index_slot_t *slot = &slots[(hash + i) & mask];
    if (!slot->pair) return NULL;
    if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0)
      return slot->pair;
  }
  return NULL;
}

static json_pair_t *json_object_index_find(const json_object_index_slot_t *slots,
                                           size_t capacity, const char *key,
                                           size_t key_len) {
  return json_object_index_find_hashed(slots, capacity, key, key_len,
                                       json_object_key_hash(key, key_len));
}

static int json_object_index_insert(json_object_index_slot_t *slots, size_t capacity,
                                    const char *key, size_t key_len, json_pair_t *pair,
                                    size_t *entry_count) {
  size_t hash;
  size_t mask;
  size_t i;
  if (!slots || capacity == 0 || !key || !pair) return 0;
  hash = json_object_key_hash(key, key_len);
  mask = capacity - 1U;
  for (i = 0; i < capacity; ++i) {
    json_object_index_slot_t *slot = &slots[(hash + i) & mask];
    if (!slot->pair) {
      slot->key = key;
      slot->key_len = key_len;
      slot->pair = pair;
      if (entry_count) ++*entry_count;
      return 1;
    }
    /* Duplicate JSON members retain the first member, matching list lookup. */
    if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0)
      return 1;
  }
  return 0;
}

static void json_object_index_invalidate(json_value_t *obj) {
  obj->data.object_val.index = NULL;
}

static int json_object_index_rehash(json_arena_t *arena, json_value_t *obj,
                                    size_t capacity) {
  json_object_index_t *index = obj->data.object_val.index;
  json_object_index_slot_t *slots;
  size_t i;
  size_t entries = 0;
  if (!arena || !obj || obj->type != JSON_OBJECT || !index || capacity == 0 ||
      capacity > SIZE_MAX / sizeof(*slots))
    return 0;
  slots = (json_object_index_slot_t *)json_arena_alloc(arena,
                                                       capacity * sizeof(*slots));
  if (!slots) return 0;
  memset(slots, 0, capacity * sizeof(*slots));
  for (i = 0; i < obj->data.object_val.count; ++i) {
    json_pair_t *pair = index->members[i];
    if (!pair) return 0;
    if (!json_object_index_insert(slots, capacity, pair->key, pair->key_len,
                                  pair, &entries))
      return 0;
  }
  index->lookup = slots;
  index->lookup_capacity = capacity;
  index->lookup_count = entries;
  return 1;
}

bool json_object_build_index(json_arena_t *arena, json_value_t *obj) {
  json_object_index_t *index;
  json_pair_t **members;
  json_object_index_slot_t *slots;
  size_t capacity;
  size_t i = 0;
  size_t entries = 0;
  json_pair_t *pair;

  if (!arena || !obj || obj->type != JSON_OBJECT) return false;
  if (obj->data.object_val.count < JSON_OBJECT_INDEX_THRESHOLD) return true;
  if (obj->data.object_val.index) return true;
  if (obj->data.object_val.count > SIZE_MAX / sizeof(*members) ||
      !json_object_index_capacity_for(obj->data.object_val.count, &capacity) ||
      capacity > SIZE_MAX / sizeof(*slots))
    return false;

  index = (json_object_index_t *)json_arena_alloc(arena, sizeof(*index));
  members = (json_pair_t **)json_arena_alloc(
      arena, obj->data.object_val.count * sizeof(*members));
  slots = (json_object_index_slot_t *)json_arena_alloc(arena,
                                                       capacity * sizeof(*slots));
  if (!index || !members || !slots) return false;
  memset(index, 0, sizeof(*index));
  memset(slots, 0, capacity * sizeof(*slots));
  for (pair = obj->data.object_val.pairs; pair; pair = pair->next) {
    if (i >= obj->data.object_val.count ||
        !json_object_index_insert(slots, capacity, pair->key, pair->key_len,
                                  pair, &entries))
      return false;
    members[i++] = pair;
  }
  if (i != obj->data.object_val.count) return false;
  index->members = members;
  index->members_capacity = obj->data.object_val.count;
  index->lookup = slots;
  index->lookup_capacity = capacity;
  index->lookup_count = entries;
  obj->data.object_val.index = index;
  return true;
}

static void json_object_index_append(json_arena_t *arena, json_value_t *obj,
                                     json_pair_t *pair) {
  json_object_index_t *index = obj->data.object_val.index;
  json_pair_t **members;
  size_t capacity;
  if (!arena || !obj || !pair) return;
  if (!index) {
    (void)json_object_build_index(arena, obj);
    return;
  }
  if (obj->data.object_val.count > index->members_capacity) {
    if (index->members_capacity > SIZE_MAX / 2U) {
      json_object_index_invalidate(obj);
      return;
    }
    capacity = index->members_capacity
                   ? index->members_capacity * 2U
                   : JSON_OBJECT_INDEX_THRESHOLD;
    if (capacity < obj->data.object_val.count ||
        capacity > SIZE_MAX / sizeof(*members)) {
      json_object_index_invalidate(obj);
      return;
    }
    members = (json_pair_t **)json_arena_alloc(arena, capacity * sizeof(*members));
    if (!members) {
      json_object_index_invalidate(obj);
      return;
    }
    memcpy(members, index->members,
           (obj->data.object_val.count - 1U) * sizeof(*members));
    index->members = members;
    index->members_capacity = capacity;
  }
  index->members[obj->data.object_val.count - 1U] = pair;

  if (index->lookup_count == SIZE_MAX ||
      index->lookup_count + 1U >
          index->lookup_capacity - index->lookup_capacity / 4U) {
    if (index->lookup_capacity > SIZE_MAX / 2U ||
        !json_object_index_rehash(arena, obj,
                                  index->lookup_capacity * 2U)) {
      json_object_index_invalidate(obj);
      return;
    }
  } else if (!json_object_index_insert(index->lookup,
                                       index->lookup_capacity,
                                       pair->key, pair->key_len, pair,
                                       &index->lookup_count)) {
    json_object_index_invalidate(obj);
  }
}

bool json_object_set_arena_ex(json_arena_t *arena, json_value_t *obj, const char *key,
                              size_t key_len, int key_owned, json_value_t *val) {
  json_pair_t *existing;

  if (!arena || !obj || obj->type != JSON_OBJECT || !key || !val) return false;

  existing = obj->data.object_val.index
                 ? json_object_index_find(obj->data.object_val.index->lookup,
                                          obj->data.object_val.index->lookup_capacity,
                                          key, key_len)
                 : NULL;
  if (!existing) {
    for (existing = obj->data.object_val.pairs; existing; existing = existing->next) {
      if (existing->key_len == key_len && memcmp(existing->key, key, key_len) == 0)
        break;
    }
  }
  if (existing) {
    existing->value = val;
    return true;
  }

  json_pair_t *pair = (json_pair_t *)json_arena_alloc(arena, sizeof(json_pair_t));
  if (!pair) return false;

  pair->key = key;
  pair->key_len = key_len;
  pair->key_owned = key_owned;
  pair->value = val;
  pair->next = NULL;

  if (obj->data.object_val.pairs_tail) {
    obj->data.object_val.pairs_tail->next = pair;
  } else {
    obj->data.object_val.pairs = pair;
  }
  obj->data.object_val.pairs_tail = pair;
  obj->data.object_val.count++;
  if (obj->data.object_val.count >= JSON_OBJECT_INDEX_THRESHOLD)
    json_object_index_append(arena, obj, pair);
  return true;
}

bool json_object_set_arena(json_arena_t *arena, json_value_t *obj, const char *key, size_t key_len,
                           json_value_t *val) {
  // Legacy API - always copy key
  char *key_copy = json_arena_strdup(arena, key, key_len);
  if (!key_copy) return false;
  return json_object_set_arena_ex(arena, obj, key_copy, key_len, 1, val);
}

/* ============================================================================
 * Parser Entry Point
 * ============================================================================ */

void *JsonParseAlloc(void *(*mallocProc)(size_t));
void JsonParseFree(void *parser, void (*freeProc)(void *));
void JsonParse(void *parser, int tokenType, json_token_t token, json_parse_ctx_t *ctx);

json_value_t *json_parse(const char *content, size_t len) {
  if (!content || len == 0) {
    fmt_text(g_error, sizeof(g_error), "Empty input");
    return NULL;
  }

  // Estimate memory: JSON trees typically use 0.5-2x input size
  // Use 1x as initial estimate, will grow if needed
  size_t estimated = len < JSON_POOL_MIN_SIZE ? JSON_POOL_MIN_SIZE : len;
  json_arena_t *arena = json_arena_create_sized(estimated);
  if (!arena) {
    fmt_text(g_error, sizeof(g_error), "Failed to create arena");
    return NULL;
  }

  json_lexer_t lexer;
  char *terminated_content = json_arena_alloc(arena, len + 1);
  if (!terminated_content) {
    fmt_text(g_error, sizeof(g_error), "Failed to allocate buffer");
    json_arena_free(arena);
    return NULL;
  }
  memcpy(terminated_content, content, len);
  terminated_content[len] = '\0';

  json_lexer_init(&lexer, terminated_content, len);

  void *parser = JsonParseAlloc(malloc);
  if (!parser) {
    fmt_text(g_error, sizeof(g_error), "Failed to allocate parser");
    json_arena_free(arena);
    return NULL;
  }

  json_parse_ctx_t ctx = {0};
  ctx.arena = arena;

  json_token_t token;
  int result;

  while ((result = json_lexer_next(&lexer, &token)) > 0) {
    JsonParse(parser, token.type, token, &ctx);
    if (ctx.error) {
      fmt(g_error, sizeof(g_error), "{}", ctx.error_msg);
      JsonParseFree(parser, free);
      json_arena_free(arena);
      return NULL;
    }
  }

  if (result < 0) {
    fmt(g_error, sizeof(g_error), "{}", lexer.error);
    JsonParseFree(parser, free);
    json_arena_free(arena);
    return NULL;
  }

  JsonParse(parser, 0, token, &ctx);
  JsonParseFree(parser, free);

  if (ctx.error) {
    fmt(g_error, sizeof(g_error), "{}", ctx.error_msg);
    json_arena_free(arena);
    return NULL;
  }

  g_error[0] = '\0';
  return ctx.root;
}

json_value_t *json_parse_file(const char *filename) {
  if (!filename) {
    fmt_text(g_error, sizeof(g_error), "NULL filename");
    return NULL;
  }

  FILE *f = fopen(filename, "rb");
  if (!f) {
    fmt(g_error, sizeof(g_error), "Cannot open file: {}", filename);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0 || size > 100 * 1024 * 1024) {
    fclose(f);
    fmt(g_error, sizeof(g_error), "Invalid file size: {}", size);
    return NULL;
  }

  char *buf = (char *)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    fmt_text(g_error, sizeof(g_error), "Memory allocation failed");
    return NULL;
  }

  size_t read = fread(buf, 1, (size_t)size, f);
  fclose(f);

  if (read != (size_t)size) {
    free(buf);
    fmt_text(g_error, sizeof(g_error), "Failed to read file");
    return NULL;
  }

  buf[size] = '\0';
  json_value_t *result = json_parse(buf, (size_t)size);
  free(buf);

  return result;
}

void json_free(json_value_t *value) {
  if (!value) return;
  json_arena_free(value->arena);
}

/* ============================================================================
 * Accessor Functions
 * ============================================================================ */

json_type_t json_type(const json_value_t *value) { return value ? value->type : JSON_NULL; }

bool json_is_null(const json_value_t *value) { return !value || value->type == JSON_NULL; }

bool json_bool(const json_value_t *value) {
  return value && value->type == JSON_BOOL ? value->data.bool_val : false;
}

double json_number(const json_value_t *value) {
  return value && value->type == JSON_NUMBER ? value->data.number_val.value : 0.0;
}

const char *json_number_text(const json_value_t *value, size_t *len) {
  if (len) *len = 0;
  if (!value || value->type != JSON_NUMBER || !value->data.number_val.lexeme) return NULL;
  if (len) *len = value->data.number_val.lexeme_len;
  return value->data.number_val.lexeme;
}

const char *json_string(const json_value_t *value) {
  return value && value->type == JSON_STRING ? value->data.string_val.str : NULL;
}

size_t json_string_len(const json_value_t *value) {
  return value && value->type == JSON_STRING ? value->data.string_val.len : 0;
}

vstr json_string_v(const json_value_t *value) {
  if (!value || value->type != JSON_STRING) return vstr_from_buf(NULL, 0);
  return vstr_from_buf(value->data.string_val.str, value->data.string_val.len);
}

size_t json_object_size(const json_value_t *obj) {
  return obj && obj->type == JSON_OBJECT ? obj->data.object_val.count : 0;
}

const char *json_object_key(const json_value_t *obj, size_t index) {
  if (!obj || obj->type != JSON_OBJECT) return NULL;
  if (index >= obj->data.object_val.count) return NULL;
  if (obj->data.object_val.index)
    return obj->data.object_val.index->members[index]->key;

  json_pair_t *pair = obj->data.object_val.pairs;
  for (size_t i = 0; pair && i < index; i++) {
    pair = pair->next;
  }
  return pair ? pair->key : NULL;
}

size_t json_object_key_len(const json_value_t *obj, size_t index) {
  if (!obj || obj->type != JSON_OBJECT) return 0;
  if (index >= obj->data.object_val.count) return 0;
  if (obj->data.object_val.index)
    return obj->data.object_val.index->members[index]->key_len;

  json_pair_t *pair = obj->data.object_val.pairs;
  for (size_t i = 0; pair && i < index; i++) {
    pair = pair->next;
  }
  return pair ? pair->key_len : 0;
}

vstr json_object_key_v(const json_value_t *obj, size_t index) {
  if (!obj || obj->type != JSON_OBJECT) return vstr_from_buf(NULL, 0);
  if (index >= obj->data.object_val.count) return vstr_from_buf(NULL, 0);
  if (obj->data.object_val.index) {
    const json_pair_t *pair = obj->data.object_val.index->members[index];
    return vstr_from_buf(pair->key, pair->key_len);
  }

  json_pair_t *pair = obj->data.object_val.pairs;
  for (size_t i = 0; pair && i < index; i++) {
    pair = pair->next;
  }
  return pair ? vstr_from_buf(pair->key, pair->key_len) : vstr_from_buf(NULL, 0);
}

json_value_t *json_object_value(const json_value_t *obj, size_t index) {
  if (!obj || obj->type != JSON_OBJECT) return NULL;
  if (index >= obj->data.object_val.count) return NULL;
  if (obj->data.object_val.index)
    return obj->data.object_val.index->members[index]->value;

  json_pair_t *pair = obj->data.object_val.pairs;
  for (size_t i = 0; pair && i < index; i++) {
    pair = pair->next;
  }
  return pair ? pair->value : NULL;
}

json_value_t *json_object_get(const json_value_t *obj, const char *key) {
  if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
  return json_object_get_v(obj, vstr_from_cstr(key));
}

json_value_t *json_object_get_v(const json_value_t *obj, vstr key) {
  if (!obj || obj->type != JSON_OBJECT || !key.data) return NULL;
  if (obj->data.object_val.index)
    return json_object_get_hashed_v(obj, key,
                                    json_object_key_hash(key.data, key.len));
  for (json_pair_t *pair = obj->data.object_val.pairs; pair; pair = pair->next) {
    if (pair->key_len == key.len && memcmp(pair->key, key.data, key.len) == 0)
      return pair->value;
  }
  return NULL;
}

json_value_t *json_object_get_hashed_v(const json_value_t *obj, vstr key,
                                       size_t key_hash) {
  json_pair_t *indexed;
  if (!obj || obj->type != JSON_OBJECT || !key.data) return NULL;

  if (obj->data.object_val.index) {
    indexed = json_object_index_find_hashed(
        obj->data.object_val.index->lookup,
        obj->data.object_val.index->lookup_capacity, key.data, key.len,
        key_hash);
    return indexed ? indexed->value : NULL;
  }

  for (json_pair_t *pair = obj->data.object_val.pairs; pair; pair = pair->next) {
    if (pair->key_len == key.len && memcmp(pair->key, key.data, key.len) == 0) {
      return pair->value;
    }
  }
  return NULL;
}

size_t json_array_size(const json_value_t *arr) {
  return arr && arr->type == JSON_ARRAY ? arr->data.array_val.count : 0;
}

json_value_t *json_array_get(const json_value_t *arr, size_t index) {
  if (!arr || arr->type != JSON_ARRAY) return NULL;
  if (index >= arr->data.array_val.count) return NULL;
  if (arr->data.array_val.index) return arr->data.array_val.index[index];

  json_element_t *elem = arr->data.array_val.elements;
  for (size_t i = 0; elem && i < index; i++) {
    elem = elem->next;
  }
  return elem ? elem->value : NULL;
}

int json_get_int(const json_value_t *obj, const char *key, int def) {
  json_value_t *v = json_object_get(obj, key);
  return v && v->type == JSON_NUMBER ? (int)v->data.number_val.value : def;
}

bool json_get_bool(const json_value_t *obj, const char *key, bool def) {
  json_value_t *v = json_object_get(obj, key);
  return v && v->type == JSON_BOOL ? v->data.bool_val : def;
}

double json_get_double(const json_value_t *obj, const char *key, double def) {
  json_value_t *v = json_object_get(obj, key);
  return v && v->type == JSON_NUMBER ? v->data.number_val.value : def;
}

const char *json_get_string(const json_value_t *obj, const char *key) {
  json_value_t *v = json_object_get(obj, key);
  return v && v->type == JSON_STRING ? v->data.string_val.str : NULL;
}

vstr json_get_string_v(const json_value_t *obj, const char *key) {
  json_value_t *v = json_object_get(obj, key);
  return json_string_v(v);
}

int json_get_int_v(const json_value_t *obj, vstr key, int def) {
  json_value_t *v = json_object_get_v(obj, key);
  return v && v->type == JSON_NUMBER ? (int)v->data.number_val.value : def;
}

bool json_get_bool_v(const json_value_t *obj, vstr key, bool def) {
  json_value_t *v = json_object_get_v(obj, key);
  return v && v->type == JSON_BOOL ? v->data.bool_val : def;
}

double json_get_double_v(const json_value_t *obj, vstr key, double def) {
  json_value_t *v = json_object_get_v(obj, key);
  return v && v->type == JSON_NUMBER ? v->data.number_val.value : def;
}

vstr json_get_string_vv(const json_value_t *obj, vstr key) {
  json_value_t *v = json_object_get_v(obj, key);
  return json_string_v(v);
}

const char *json_get_error(void) { return g_error[0] ? g_error : NULL; }

/* ============================================================================
 * Serializer
 * ============================================================================ */

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} json_buffer_t;

static bool json_buffer_init(json_buffer_t *buf) {
  buf->capacity = 1024;
  buf->size = 0;
  buf->data = (char *)malloc(buf->capacity);
  return buf->data != NULL;
}

static bool json_buffer_append(json_buffer_t *buf, const char *str, size_t len) {
  if (!buf || !str || len > SIZE_MAX - buf->size - 1) return false;
  size_t needed = buf->size + len + 1;
  if (needed > buf->capacity) {
    size_t new_cap = buf->capacity;
    while (new_cap < needed) {
      if (new_cap > SIZE_MAX / 2) {
        new_cap = needed;
        break;
      }
      new_cap *= 2;
    }
    char *new_data = (char *)realloc(buf->data, new_cap);
    if (!new_data) return false;
    buf->data = new_data;
    buf->capacity = new_cap;
  }
  memcpy(buf->data + buf->size, str, len);
  buf->size += len;
  buf->data[buf->size] = '\0';
  return true;
}

static bool json_serialize_string(const char *str, size_t len, json_buffer_t *buf) {
  if (!str || !json_buffer_append(buf, "\"", 1)) return false;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    switch (c) {
    case '"':
      if (!json_buffer_append(buf, "\\\"", 2)) return false;
      break;
    case '\\':
      if (!json_buffer_append(buf, "\\\\", 2)) return false;
      break;
    case '\b':
      if (!json_buffer_append(buf, "\\b", 2)) return false;
      break;
    case '\f':
      if (!json_buffer_append(buf, "\\f", 2)) return false;
      break;
    case '\n':
      if (!json_buffer_append(buf, "\\n", 2)) return false;
      break;
    case '\r':
      if (!json_buffer_append(buf, "\\r", 2)) return false;
      break;
    case '\t':
      if (!json_buffer_append(buf, "\\t", 2)) return false;
      break;
    default:
      if (c < 32) {
        char tmp[8];
        int tlen = fmt(tmp, sizeof(tmp), "\\u{:04x}", (int)c);
        if (tlen < 0 || !json_buffer_append(buf, tmp, (size_t)tlen)) return false;
      } else if (!json_buffer_append(buf, (const char *)&c, 1)) {
        return false;
      }
    }
  }
  return json_buffer_append(buf, "\"", 1);
}

static bool json_serialize_value(const json_value_t *v, json_buffer_t *buf) {
  if (!v) return json_buffer_append(buf, "null", 4);

  switch (v->type) {
  case JSON_NULL:
    return json_buffer_append(buf, "null", 4);
  case JSON_BOOL:
    return v->data.bool_val ? json_buffer_append(buf, "true", 4)
                            : json_buffer_append(buf, "false", 5);
  case JSON_NUMBER: {
    if (v->data.number_val.lexeme)
      return json_buffer_append(buf, v->data.number_val.lexeme, v->data.number_val.lexeme_len);
    char tmp[64];
    int len = fmt(tmp, sizeof(tmp), "{}", v->data.number_val.value);
    return json_buffer_append(buf, tmp, len);
  }
  case JSON_STRING: {
    if (!json_buffer_append(buf, "\"", 1)) return false;
    // Simple escaping
    const char *s = v->data.string_val.str;
    size_t len = v->data.string_val.len;
    for (size_t i = 0; i < len; i++) {
      char c = s[i];
      switch (c) {
      case '\"':
        if (!json_buffer_append(buf, "\\\"", 2)) return false;
        break;
      case '\\':
        if (!json_buffer_append(buf, "\\\\", 2)) return false;
        break;
      case '\b':
        if (!json_buffer_append(buf, "\\b", 2)) return false;
        break;
      case '\f':
        if (!json_buffer_append(buf, "\\f", 2)) return false;
        break;
      case '\n':
        if (!json_buffer_append(buf, "\\n", 2)) return false;
        break;
      case '\r':
        if (!json_buffer_append(buf, "\\r", 2)) return false;
        break;
      case '\t':
        if (!json_buffer_append(buf, "\\t", 2)) return false;
        break;
      default:
        if ((unsigned char)c < 32) {
          char tmp[8];
          int tlen = fmt(tmp, sizeof(tmp), "\\u{:04x}", (int)c);
          if (!json_buffer_append(buf, tmp, tlen)) return false;
        } else {
          if (!json_buffer_append(buf, &c, 1)) return false;
        }
      }
    }
    return json_buffer_append(buf, "\"", 1);
  }
  case JSON_ARRAY: {
    if (!json_buffer_append(buf, "[", 1)) return false;
    json_element_t *e = v->data.array_val.elements;
    while (e) {
      if (!json_serialize_value(e->value, buf)) return false;
      e = e->next;
      if (e) {
        if (!json_buffer_append(buf, ",", 1)) return false;
      }
    }
    return json_buffer_append(buf, "]", 1);
  }
  case JSON_OBJECT: {
    if (!json_buffer_append(buf, "{", 1)) return false;
    json_pair_t *p = v->data.object_val.pairs;
    while (p) {
      if (!json_serialize_string(p->key, p->key_len, buf)) return false;
      if (!json_buffer_append(buf, ":", 1)) return false;
      if (!json_serialize_value(p->value, buf)) return false;
      p = p->next;
      if (p) {
        if (!json_buffer_append(buf, ",", 1)) return false;
      }
    }
    return json_buffer_append(buf, "}", 1);
  }
  default:
    return false;
  }
}

char *json_serialize(const json_value_t *value, size_t *out_len) {
  json_buffer_t buf;
  if (!json_buffer_init(&buf)) return NULL;

  if (!json_serialize_value(value, &buf)) {
    free(buf.data);
    return NULL;
  }

  if (out_len) *out_len = buf.size;
  return buf.data;
}

static bool json_serialize_indent(json_buffer_t *buf, int level) {
  for (int i = 0; i < level; i++) {
    if (!json_buffer_append(buf, "  ", 2)) return false;
  }
  return true;
}

static bool json_serialize_pretty_value_ex(const json_value_t *v, json_buffer_t *buf, int level,
                                           const char *newline, size_t newline_len) {
  if (!v) return json_buffer_append(buf, "null", 4);

  switch (v->type) {
  case JSON_NULL:
    return json_buffer_append(buf, "null", 4);
  case JSON_BOOL:
    return v->data.bool_val ? json_buffer_append(buf, "true", 4)
                            : json_buffer_append(buf, "false", 5);
  case JSON_NUMBER: {
    if (v->data.number_val.lexeme)
      return json_buffer_append(buf, v->data.number_val.lexeme, v->data.number_val.lexeme_len);
    char tmp[64];
    int len = fmt(tmp, sizeof(tmp), "{}", v->data.number_val.value);
    return json_buffer_append(buf, tmp, len);
  }
  case JSON_STRING: {
    // Re-use logic from compact serializer
    return json_serialize_value(v, buf);
  }
  case JSON_ARRAY: {
    if (v->data.array_val.count == 0) return json_buffer_append(buf, "[]", 2);

    if (!json_buffer_append(buf, "[", 1)) return false;
    if (!json_buffer_append(buf, newline, newline_len)) return false;
    json_element_t *e = v->data.array_val.elements;
    while (e) {
      if (!json_serialize_indent(buf, level + 1)) return false;
      if (!json_serialize_pretty_value_ex(e->value, buf, level + 1, newline, newline_len))
        return false;
      e = e->next;
      if (e) {
        if (!json_buffer_append(buf, ",", 1)) return false;
        if (!json_buffer_append(buf, newline, newline_len)) return false;
      } else {
        if (!json_buffer_append(buf, newline, newline_len)) return false;
      }
    }
    if (!json_serialize_indent(buf, level)) return false;
    return json_buffer_append(buf, "]", 1);
  }
  case JSON_OBJECT: {
    if (v->data.object_val.count == 0) return json_buffer_append(buf, "{}", 2);

    if (!json_buffer_append(buf, "{", 1)) return false;
    if (!json_buffer_append(buf, newline, newline_len)) return false;
    json_pair_t *p = v->data.object_val.pairs;
    while (p) {
      if (!json_serialize_indent(buf, level + 1)) return false;
      if (!json_serialize_string(p->key, p->key_len, buf)) return false;
      if (!json_buffer_append(buf, ": ", 2)) return false;
      if (!json_serialize_pretty_value_ex(p->value, buf, level + 1, newline, newline_len))
        return false;
      p = p->next;
      if (p) {
        if (!json_buffer_append(buf, ",", 1)) return false;
        if (!json_buffer_append(buf, newline, newline_len)) return false;
      } else {
        if (!json_buffer_append(buf, newline, newline_len)) return false;
      }
    }
    if (!json_serialize_indent(buf, level)) return false;
    return json_buffer_append(buf, "}", 1);
  }
  default:
    return false;
  }
}

static bool json_serialize_pretty_value(const json_value_t *v, json_buffer_t *buf, int level) {
  return json_serialize_pretty_value_ex(v, buf, level, "\n", 1);
}

char *json_serialize_pretty(const json_value_t *value, size_t *out_len) {
  json_buffer_t buf;
  if (!json_buffer_init(&buf)) return NULL;

  if (!json_serialize_pretty_value(value, &buf, 0)) {
    free(buf.data);
    return NULL;
  }

  if (out_len) *out_len = buf.size;
  return buf.data;
}

char *json_serialize_pretty_crlf(const json_value_t *value, size_t *out_len) {
  json_buffer_t buf;
  if (!json_buffer_init(&buf)) return NULL;

  if (!json_serialize_pretty_value_ex(value, &buf, 0, "\r\n", 2)) {
    free(buf.data);
    return NULL;
  }

  if (out_len) *out_len = buf.size;
  return buf.data;
}

void json_serialize_free(char *str) { free(str); }

/* ============================================================================
 * Builder Implementation
 * ============================================================================ */

static json_value_t *json_create_root(json_type_t type) {
  json_arena_t *arena = json_arena_create();
  if (!arena) return NULL;

  json_value_t *value = json_value_new_arena(arena, type);
  if (!value) json_arena_free(arena);
  return value;
}

json_value_t *json_create_object(void) { return json_create_root(JSON_OBJECT); }

json_value_t *json_create_array(void) { return json_create_root(JSON_ARRAY); }

json_value_t *json_create_string(const char *str) {
  return str ? json_create_string_n(str, strlen(str)) : NULL;
}

json_value_t *json_create_string_n(const char *str, size_t len) {
  if (!str) return NULL;
  json_arena_t *arena = json_arena_create();
  if (!arena) return NULL;
  json_value_t *value = json_value_string_arena(arena, str, len);
  if (!value) json_arena_free(arena);
  return value;
}

json_value_t *json_create_number(double num) {
  char text[64];
  int len = snprintf(text, sizeof(text), "%.17g", num);
  json_value_t *value = json_create_root(JSON_NUMBER);
  if (!value || len <= 0 || (size_t)len >= sizeof(text)) {
    json_free(value);
    return NULL;
  }
  value->data.number_val.value = num;
  value->data.number_val.lexeme = json_arena_strdup(value->arena, text, (size_t)len);
  value->data.number_val.lexeme_len = (size_t)len;
  if (!value->data.number_val.lexeme) {
    json_free(value);
    return NULL;
  }
  return value;
}

json_value_t *json_create_int64(int64_t num) {
  char text[32];
  int len = snprintf(text, sizeof(text), "%lld", (long long)num);
  json_value_t *value = json_create_root(JSON_NUMBER);
  if (!value) return NULL;
  value->data.number_val.value = (double)num;
  value->data.number_val.lexeme = json_arena_strdup(value->arena, text, (size_t)len);
  value->data.number_val.lexeme_len = (size_t)len;
  if (!value->data.number_val.lexeme) {
    json_free(value);
    return NULL;
  }
  return value;
}

json_value_t *json_create_uint64(uint64_t num) {
  char text[32];
  int len = snprintf(text, sizeof(text), "%llu", (unsigned long long)num);
  json_value_t *value = json_create_root(JSON_NUMBER);
  if (!value || len <= 0 || (size_t)len >= sizeof(text)) {
    json_free(value);
    return NULL;
  }
  value->data.number_val.value = (double)num;
  value->data.number_val.lexeme = json_arena_strdup(value->arena, text, (size_t)len);
  value->data.number_val.lexeme_len = (size_t)len;
  if (!value->data.number_val.lexeme) {
    json_free(value);
    return NULL;
  }
  return value;
}

json_value_t *json_create_bool(bool val) {
  json_value_t *value = json_create_root(JSON_BOOL);
  if (value) value->data.bool_val = val;
  return value;
}

json_value_t *json_create_null(void) { return json_create_root(JSON_NULL); }

json_value_t *json_clone(const json_value_t *value) {
  json_value_t *copy = NULL;
  size_t i;

  if (!value) return NULL;
  switch (value->type) {
  case JSON_NULL:
    return json_create_null();
  case JSON_BOOL:
    return json_create_bool(value->data.bool_val);
  case JSON_NUMBER:
    copy = json_create_number(value->data.number_val.value);
    if (copy && value->data.number_val.lexeme) {
      copy->data.number_val.lexeme = json_arena_strdup(copy->arena, value->data.number_val.lexeme,
                                                       value->data.number_val.lexeme_len);
      copy->data.number_val.lexeme_len = value->data.number_val.lexeme_len;
      if (!copy->data.number_val.lexeme) {
        json_free(copy);
        return NULL;
      }
    }
    return copy;
  case JSON_STRING:
    return json_create_string_n(value->data.string_val.str, value->data.string_val.len);
  case JSON_ARRAY:
    copy = json_create_array();
    for (i = 0; copy && i < value->data.array_val.count; ++i) {
      json_value_t *child = json_clone(json_array_get(value, i));
      if (!child || !json_array_add_checked(copy, child)) {
        json_free(child);
        json_free(copy);
        return NULL;
      }
    }
    return copy;
  case JSON_OBJECT:
    copy = json_create_object();
    for (i = 0; copy && i < value->data.object_val.count; ++i) {
      json_value_t *child = json_clone(json_object_value(value, i));
      if (!child || !json_object_add_n(copy, json_object_key(value, i),
                                       json_object_key_len(value, i), child)) {
        json_free(child);
        json_free(copy);
        return NULL;
      }
    }
    return copy;
  default:
    return NULL;
  }
}

static bool json_can_transfer_to_arena(const json_arena_t *dst, const json_value_t *val) {
  return val && json_arena_can_adopt(dst, val->arena);
}

bool json_object_add_n(json_value_t *obj, const char *key, size_t key_len, json_value_t *val) {
  if (!obj || obj->type != JSON_OBJECT || !key || !val ||
      !json_can_transfer_to_arena(obj->arena, val))
    return false;

  char *key_copy = json_arena_strdup(obj->arena, key, key_len);
  if (!key_copy) return false;

  if (!json_object_set_arena_ex(obj->arena, obj, key_copy, key_len, 1, val)) return false;
  return json_arena_adopt(obj->arena, val->arena);
}

bool json_object_add_checked(json_value_t *obj, const char *key, json_value_t *val) {
  return key ? json_object_add_n(obj, key, strlen(key), val) : false;
}

bool json_array_add_checked(json_value_t *arr, json_value_t *val) {
  if (!arr || arr->type != JSON_ARRAY || !val || !json_can_transfer_to_arena(arr->arena, val))
    return false;

  if (!json_array_append_arena(arr->arena, arr, val)) return false;
  return json_arena_adopt(arr->arena, val->arena);
}

void json_object_add(json_value_t *obj, const char *key, json_value_t *val) {
  (void)json_object_add_checked(obj, key, val);
}

void json_array_add(json_value_t *arr, json_value_t *val) {
  (void)json_array_add_checked(arr, val);
}

void json_object_set_string(json_value_t *obj, const char *key, const char *val) {
  if (!obj || obj->type != JSON_OBJECT || !key || !val) return;
  json_value_t *v = json_value_string_arena(obj->arena, val, strlen(val));
  if (v) json_object_set_arena(obj->arena, obj, key, strlen(key), v);
}

void json_object_set_number(json_value_t *obj, const char *key, double val) {
  if (!obj || obj->type != JSON_OBJECT || !key) return;
  json_value_t *v = json_value_number_arena(obj->arena, val);
  if (v) json_object_set_arena(obj->arena, obj, key, strlen(key), v);
}

void json_object_set_bool(json_value_t *obj, const char *key, bool val) {
  if (!obj || obj->type != JSON_OBJECT || !key) return;
  json_value_t *v = json_value_bool_arena(obj->arena, val);
  if (v) json_object_set_arena(obj->arena, obj, key, strlen(key), v);
}

void json_object_set_null(json_value_t *obj, const char *key) {
  if (!obj || obj->type != JSON_OBJECT || !key) return;
  json_value_t *v = json_value_null_arena(obj->arena);
  if (v) json_object_set_arena(obj->arena, obj, key, strlen(key), v);
}

/* ============================================================================
 * SAX/Stream Parser - no retained DOM; memory follows parser depth and pending
 * token/input size.
 * ============================================================================ */

#include "json_grammar_gen.h"

typedef enum {
  SAX_STATE_VALUE,
  SAX_STATE_OBJECT_KEY,
  SAX_STATE_OBJECT_COLON,
  SAX_STATE_OBJECT_VALUE,
  SAX_STATE_OBJECT_COMMA,
  SAX_STATE_ARRAY_VALUE,
  SAX_STATE_ARRAY_COMMA
} sax_state_t;

#define SAX_MAX_DEPTH 256

struct json_sax_parser_s {
  json_sax_handler_t handler;
  int (*on_number_raw)(void *ctx, const char *val, size_t len);
  void *ctx;
  sax_state_t state_stack[SAX_MAX_DEPTH];
  int depth;
  sax_state_t state;
  bool root_seen;
  bool done;
  bool failed;
  bool finished;
  tstr buffer;
  size_t pos;
  tstr scratch;
  char error[MAX_ERROR_LEN];
};

static void json_sax_set_error(json_sax_parser_t *parser, const char *fmt_str, ...) {
  va_list ap;
  va_start(ap, fmt_str);
  vsnprintf(g_error, sizeof(g_error), fmt_str, ap);
  va_end(ap);

  if (parser) {
    va_start(ap, fmt_str);
    vsnprintf(parser->error, sizeof(parser->error), fmt_str, ap);
    va_end(ap);
    parser->failed = true;
  }
}

static bool json_sax_is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static bool json_sax_is_delim(char c) {
  return json_sax_is_ws(c) || c == ',' || c == ']' || c == '}';
}

static bool json_sax_is_hex4(const char *src) {
  for (int i = 0; i < 4; ++i) {
    char c = src[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

static int sax_unescape_to_buffer(json_sax_parser_t *parser, const char *src, size_t len,
                                  const char **out, size_t *out_len) {
  if (!parser || !out || !out_len) return -1;

  if (!parser->scratch) {
    parser->scratch = tstr_new_len(NULL, len);
  } else {
    tstr_clear(parser->scratch);
    tstr next = tstr_reserve(parser->scratch, len);
    if (!next) {
      json_sax_set_error(parser, "Out of memory");
      return -1;
    }
    parser->scratch = next;
  }
  if (!parser->scratch) {
    json_sax_set_error(parser, "Out of memory");
    return -1;
  }

  char *buf = parser->scratch;
  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (src[i] == '\\' && i + 1 < len) {
      i++;
      switch (src[i]) {
      case '"':
        buf[j++] = '"';
        break;
      case '\\':
        buf[j++] = '\\';
        break;
      case '/':
        buf[j++] = '/';
        break;
      case 'b':
        buf[j++] = '\b';
        break;
      case 'f':
        buf[j++] = '\f';
        break;
      case 'n':
        buf[j++] = '\n';
        break;
      case 'r':
        buf[j++] = '\r';
        break;
      case 't':
        buf[j++] = '\t';
        break;
      case 'u': {
        size_t escape = i - 1;
        uint32_t codepoint;
        if (!json_unicode_decode_escape(src, len, &escape, &codepoint)) {
          json_sax_set_error(parser, "Invalid Unicode surrogate pair");
          return -1;
        }
        j += json_unicode_append_utf8(buf + j, codepoint);
        i = escape - 1;
        break;
      }
      default:
        buf[j++] = src[i];
        break;
      }
    } else {
      buf[j++] = src[i];
    }
  }
  if (!tstr_set_len_checked(parser->scratch, j)) {
    json_sax_set_error(parser, "Out of memory");
    return -1;
  }
  *out = parser->scratch;
  *out_len = j;
  return 0;
}

static int json_sax_emit_string(json_sax_parser_t *parser, const json_token_t *token, bool is_key) {
  const char *value = token->value;
  size_t len = token->length;

  if (token->has_escape &&
      sax_unescape_to_buffer(parser, token->value, token->length, &value, &len) != 0) {
    return -1;
  }

  if (is_key) {
    if (parser->handler.on_object_key &&
        parser->handler.on_object_key(parser->ctx, value, len) != 0) {
      json_sax_set_error(parser, "SAX callback failed");
      return -1;
    }
  } else if (parser->handler.on_string && parser->handler.on_string(parser->ctx, value, len) != 0) {
    json_sax_set_error(parser, "SAX callback failed");
    return -1;
  }
  return 0;
}

static void json_sax_mark_value_complete(json_sax_parser_t *parser) {
  if (parser->state == SAX_STATE_OBJECT_VALUE) {
    parser->state = SAX_STATE_OBJECT_COMMA;
  } else if (parser->state == SAX_STATE_ARRAY_VALUE) {
    parser->state = SAX_STATE_ARRAY_COMMA;
  } else {
    parser->root_seen = true;
    parser->done = true;
  }
}

static int json_sax_after_container_end(json_sax_parser_t *parser) {
  parser->state = (parser->depth > 0) ? parser->state_stack[--parser->depth] : SAX_STATE_VALUE;
  if (parser->depth == 0 && parser->state == SAX_STATE_VALUE) {
    parser->root_seen = true;
    parser->done = true;
  }
  return 0;
}

static int json_sax_push_container(json_sax_parser_t *parser, sax_state_t next_state) {
  if (parser->depth >= SAX_MAX_DEPTH) {
    json_sax_set_error(parser, "Max depth exceeded");
    return -1;
  }
  parser->state_stack[parser->depth++] =
      (parser->state == SAX_STATE_OBJECT_VALUE)  ? SAX_STATE_OBJECT_COMMA
      : (parser->state == SAX_STATE_ARRAY_VALUE) ? SAX_STATE_ARRAY_COMMA
                                                 : SAX_STATE_VALUE;
  parser->root_seen = true;
  parser->state = next_state;
  return 0;
}

static int json_sax_process_token(json_sax_parser_t *parser, const json_token_t *token) {
  if (parser->done) {
    json_sax_set_error(parser, "Unexpected data after JSON value");
    return -1;
  }

  switch (parser->state) {
  case SAX_STATE_VALUE:
  case SAX_STATE_OBJECT_VALUE:
  case SAX_STATE_ARRAY_VALUE:
    switch (token->type) {
    case JSON_TOKEN_NULL:
      if (parser->handler.on_null && parser->handler.on_null(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      json_sax_mark_value_complete(parser);
      break;

    case JSON_TOKEN_TRUE:
      if (parser->handler.on_bool && parser->handler.on_bool(parser->ctx, true) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      json_sax_mark_value_complete(parser);
      break;

    case JSON_TOKEN_FALSE:
      if (parser->handler.on_bool && parser->handler.on_bool(parser->ctx, false) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      json_sax_mark_value_complete(parser);
      break;

    case JSON_TOKEN_NUMBER:
      if ((parser->on_number_raw &&
           parser->on_number_raw(parser->ctx, token->value, token->length) != 0) ||
          (!parser->on_number_raw && parser->handler.on_number &&
           parser->handler.on_number(parser->ctx, token->num_value) != 0)) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      json_sax_mark_value_complete(parser);
      break;

    case JSON_TOKEN_STRING:
      if (json_sax_emit_string(parser, token, false) != 0) return -1;
      json_sax_mark_value_complete(parser);
      break;

    case JSON_TOKEN_LBRACE:
      if (parser->handler.on_object_start && parser->handler.on_object_start(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      return json_sax_push_container(parser, SAX_STATE_OBJECT_KEY);

    case JSON_TOKEN_LBRACKET:
      if (parser->handler.on_array_start && parser->handler.on_array_start(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      return json_sax_push_container(parser, SAX_STATE_ARRAY_VALUE);

    case JSON_TOKEN_RBRACKET:
      if (parser->state == SAX_STATE_ARRAY_VALUE) {
        if (parser->handler.on_array_end && parser->handler.on_array_end(parser->ctx) != 0) {
          json_sax_set_error(parser, "SAX callback failed");
          return -1;
        }
        return json_sax_after_container_end(parser);
      }
      json_sax_set_error(parser, "Unexpected ]");
      return -1;

    case JSON_TOKEN_RBRACE:
      if (parser->state == SAX_STATE_OBJECT_VALUE) {
        json_sax_set_error(parser, "Expected value before }");
        return -1;
      }
      json_sax_set_error(parser, "Unexpected }");
      return -1;

    default:
      json_sax_set_error(parser, "Unexpected token in value context");
      return -1;
    }
    break;

  case SAX_STATE_OBJECT_KEY:
    if (token->type == JSON_TOKEN_STRING) {
      if (json_sax_emit_string(parser, token, true) != 0) return -1;
      parser->state = SAX_STATE_OBJECT_COLON;
    } else if (token->type == JSON_TOKEN_RBRACE) {
      if (parser->handler.on_object_end && parser->handler.on_object_end(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      return json_sax_after_container_end(parser);
    } else {
      json_sax_set_error(parser, "Expected string key or }");
      return -1;
    }
    break;

  case SAX_STATE_OBJECT_COLON:
    if (token->type == JSON_TOKEN_COLON) {
      parser->state = SAX_STATE_OBJECT_VALUE;
    } else {
      json_sax_set_error(parser, "Expected :");
      return -1;
    }
    break;

  case SAX_STATE_OBJECT_COMMA:
    if (token->type == JSON_TOKEN_COMMA) {
      parser->state = SAX_STATE_OBJECT_KEY;
    } else if (token->type == JSON_TOKEN_RBRACE) {
      if (parser->handler.on_object_end && parser->handler.on_object_end(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      return json_sax_after_container_end(parser);
    } else {
      json_sax_set_error(parser, "Expected , or }");
      return -1;
    }
    break;

  case SAX_STATE_ARRAY_COMMA:
    if (token->type == JSON_TOKEN_COMMA) {
      parser->state = SAX_STATE_ARRAY_VALUE;
    } else if (token->type == JSON_TOKEN_RBRACKET) {
      if (parser->handler.on_array_end && parser->handler.on_array_end(parser->ctx) != 0) {
        json_sax_set_error(parser, "SAX callback failed");
        return -1;
      }
      return json_sax_after_container_end(parser);
    } else {
      json_sax_set_error(parser, "Expected , or ]");
      return -1;
    }
    break;
  }
  return 0;
}

static int json_sax_parse_number_token(const char *start, size_t len, double *out) {
  char small[128];
  char *buf = small;
  char *endp = NULL;

  if (len >= sizeof(small)) {
    if (len == SIZE_MAX) return -1;
    buf = (char *)malloc(len + 1);
    if (!buf) return -1;
  }

  memcpy(buf, start, len);
  buf[len] = '\0';
  errno = 0;
  *out = strtod(buf, &endp);
  if (endp != buf + len) {
    if (buf != small) free(buf);
    return -1;
  }
  if (buf != small) free(buf);
  return 0;
}

static int json_sax_scan_number(json_sax_parser_t *parser, json_token_t *token, bool final,
                                const char *base, size_t len) {
  size_t start = parser->pos;
  size_t i = start;

  if (i < len && base[i] == '-') {
    ++i;
    if (i == len) return final ? (json_sax_set_error(parser, "Invalid number"), -1) : 0;
  }

  if (i >= len) return final ? (json_sax_set_error(parser, "Invalid number"), -1) : 0;

  if (base[i] == '0') {
    ++i;
    if (i < len && base[i] >= '0' && base[i] <= '9') {
      json_sax_set_error(parser, "Invalid number");
      return -1;
    }
  } else if (base[i] >= '1' && base[i] <= '9') {
    do {
      ++i;
    } while (i < len && base[i] >= '0' && base[i] <= '9');
  } else {
    json_sax_set_error(parser, "Invalid number");
    return -1;
  }

  if (i < len && base[i] == '.') {
    ++i;
    if (i == len) return final ? (json_sax_set_error(parser, "Invalid number"), -1) : 0;
    if (base[i] < '0' || base[i] > '9') {
      json_sax_set_error(parser, "Invalid number");
      return -1;
    }
    do {
      ++i;
    } while (i < len && base[i] >= '0' && base[i] <= '9');
  }

  if (i < len && (base[i] == 'e' || base[i] == 'E')) {
    ++i;
    if (i == len) return final ? (json_sax_set_error(parser, "Invalid number"), -1) : 0;
    if (base[i] == '+' || base[i] == '-') {
      ++i;
      if (i == len) return final ? (json_sax_set_error(parser, "Invalid number"), -1) : 0;
    }
    if (base[i] < '0' || base[i] > '9') {
      json_sax_set_error(parser, "Invalid number");
      return -1;
    }
    do {
      ++i;
    } while (i < len && base[i] >= '0' && base[i] <= '9');
  }

  if (i == len && !final) return 0;
  if (i < len && !json_sax_is_delim(base[i])) {
    json_sax_set_error(parser, "Invalid number terminator");
    return -1;
  }

  token->type = JSON_TOKEN_NUMBER;
  token->value = base + start;
  token->length = i - start;
  token->has_escape = 0;
  if (json_sax_parse_number_token(token->value, token->length, &token->num_value) != 0) {
    json_sax_set_error(parser, "Invalid number");
    return -1;
  }
  parser->pos = i;
  return 1;
}

static int json_sax_scan_string(json_sax_parser_t *parser, json_token_t *token, bool final,
                                const char *base, size_t len) {
  size_t start = parser->pos;
  size_t i = start + 1;
  bool has_escape = false;

  while (i < len) {
    unsigned char c = (unsigned char)base[i];
    if (c == '"') {
      token->type = JSON_TOKEN_STRING;
      token->value = base + start + 1;
      token->length = i - start - 1;
      token->num_value = 0.0;
      token->has_escape = has_escape ? 1 : 0;
      parser->pos = i + 1;
      return 1;
    }
    if (c == '\\') {
      has_escape = true;
      ++i;
      if (i == len)
        return final ? (json_sax_set_error(parser, "Incomplete escape sequence"), -1) : 0;
      switch (base[i]) {
      case '"':
      case '\\':
      case '/':
      case 'b':
      case 'f':
      case 'n':
      case 'r':
      case 't':
        ++i;
        break;
      case 'u':
        if (i + 4 >= len)
          return final ? (json_sax_set_error(parser, "Incomplete unicode escape"), -1) : 0;
        if (!json_sax_is_hex4(base + i + 1)) {
          json_sax_set_error(parser, "Invalid unicode escape");
          return -1;
        }
        i += 5;
        break;
      default:
        json_sax_set_error(parser, "Invalid escape sequence");
        return -1;
      }
    } else {
      if (c < 0x20U) {
        json_sax_set_error(parser, "Invalid control character in string");
        return -1;
      }
      ++i;
    }
  }

  return final ? (json_sax_set_error(parser, "Unterminated string"), -1) : 0;
}

static int json_sax_scan_literal(json_sax_parser_t *parser, json_token_t *token, bool final,
                                 const char *base, size_t len, const char *literal,
                                 size_t literal_len, int token_type) {
  size_t start = parser->pos;
  size_t available = len - start;
  size_t cmp_len = available < literal_len ? available : literal_len;

  if (memcmp(base + start, literal, cmp_len) != 0) {
    json_sax_set_error(parser, "Unexpected character '%c'", base[start]);
    return -1;
  }
  if (available < literal_len)
    return final ? (json_sax_set_error(parser, "Incomplete literal"), -1) : 0;
  if (start + literal_len < len && !json_sax_is_delim(base[start + literal_len])) {
    json_sax_set_error(parser, "Invalid literal terminator");
    return -1;
  }

  token->type = token_type;
  token->value = base + start;
  token->length = literal_len;
  token->num_value = 0.0;
  token->has_escape = 0;
  parser->pos = start + literal_len;
  return 1;
}

static int json_sax_next_token(json_sax_parser_t *parser, json_token_t *token, bool final) {
  const char *base = parser->buffer ? parser->buffer : "";
  size_t len = tstr_len(parser->buffer);
  const char *p = base + parser->pos;
  const char *end = base + len;

  p = json_skip_rfc_whitespace_simde(p, end);
  parser->pos = (size_t)(p - base);
  if (parser->pos >= len) return 0;

  memset(token, 0, sizeof(*token));
  switch (base[parser->pos]) {
  case '{':
    token->type = JSON_TOKEN_LBRACE;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case '}':
    token->type = JSON_TOKEN_RBRACE;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case '[':
    token->type = JSON_TOKEN_LBRACKET;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case ']':
    token->type = JSON_TOKEN_RBRACKET;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case ':':
    token->type = JSON_TOKEN_COLON;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case ',':
    token->type = JSON_TOKEN_COMMA;
    token->value = base + parser->pos;
    token->length = 1;
    ++parser->pos;
    return 1;
  case '"':
    return json_sax_scan_string(parser, token, final, base, len);
  case 't':
    return json_sax_scan_literal(parser, token, final, base, len, "true", 4, JSON_TOKEN_TRUE);
  case 'f':
    return json_sax_scan_literal(parser, token, final, base, len, "false", 5, JSON_TOKEN_FALSE);
  case 'n':
    return json_sax_scan_literal(parser, token, final, base, len, "null", 4, JSON_TOKEN_NULL);
  default:
    if (base[parser->pos] == '-' || (base[parser->pos] >= '0' && base[parser->pos] <= '9'))
      return json_sax_scan_number(parser, token, final, base, len);
    json_sax_set_error(parser, "Unexpected character '%c'", base[parser->pos]);
    return -1;
  }
}

static void json_sax_compact_buffer(json_sax_parser_t *parser) {
  size_t len = tstr_len(parser->buffer);
  if (!parser->buffer || parser->pos == 0) return;
  if (parser->pos >= len) {
    tstr_clear(parser->buffer);
    parser->pos = 0;
    return;
  }

  size_t remaining = len - parser->pos;
  memmove(parser->buffer, parser->buffer + parser->pos, remaining);
  (void)tstr_set_len_checked(parser->buffer, remaining);
  parser->pos = 0;
}

static int json_sax_parser_run(json_sax_parser_t *parser, bool final) {
  json_token_t token;
  int result;

  while ((result = json_sax_next_token(parser, &token, final)) > 0) {
    if (json_sax_process_token(parser, &token) != 0) {
      json_sax_compact_buffer(parser);
      return -1;
    }
  }

  json_sax_compact_buffer(parser);
  return result < 0 ? -1 : 0;
}

static json_sax_parser_t *json_sax_parser_create_common(
    const json_sax_handler_t *handler, const json_sax_handler_raw_t *raw_handler, void *ctx) {
  if ((handler == NULL) == (raw_handler == NULL)) {
    fmt_text(g_error, sizeof(g_error), "Invalid arguments");
    return NULL;
  }

  json_sax_parser_t *parser = (json_sax_parser_t *)calloc(1, sizeof(*parser));
  if (!parser) {
    fmt_text(g_error, sizeof(g_error), "Out of memory");
    return NULL;
  }

  if (handler != NULL) {
    parser->handler = *handler;
  } else {
    parser->handler.on_null = raw_handler->on_null;
    parser->handler.on_bool = raw_handler->on_bool;
    parser->handler.on_string = raw_handler->on_string;
    parser->handler.on_object_start = raw_handler->on_object_start;
    parser->handler.on_object_key = raw_handler->on_object_key;
    parser->handler.on_object_end = raw_handler->on_object_end;
    parser->handler.on_array_start = raw_handler->on_array_start;
    parser->handler.on_array_end = raw_handler->on_array_end;
    parser->on_number_raw = raw_handler->on_number;
  }
  parser->ctx = ctx;
  parser->state = SAX_STATE_VALUE;
  parser->buffer = tstr_new();
  if (!parser->buffer) {
    free(parser);
    fmt_text(g_error, sizeof(g_error), "Out of memory");
    return NULL;
  }
  return parser;
}

json_sax_parser_t *json_sax_parser_create(const json_sax_handler_t *handler, void *ctx) {
  return json_sax_parser_create_common(handler, NULL, ctx);
}

json_sax_parser_t *json_sax_parser_create_raw(const json_sax_handler_raw_t *handler, void *ctx) {
  return json_sax_parser_create_common(NULL, handler, ctx);
}

int json_sax_parser_feed(json_sax_parser_t *parser, const char *data, size_t len) {
  if (!parser || (!data && len > 0)) {
    fmt_text(g_error, sizeof(g_error), "Invalid arguments");
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    json_sax_set_error(parser, "Parser already finished");
    return -1;
  }
  if (len == 0) return 0;

  tstr next = tstr_cat_len(parser->buffer, data, len);
  if (!next) {
    json_sax_set_error(parser, "Out of memory");
    return -1;
  }
  parser->buffer = next;

  return json_sax_parser_run(parser, false);
}

int json_sax_parser_finish(json_sax_parser_t *parser) {
  if (!parser) {
    fmt_text(g_error, sizeof(g_error), "Invalid arguments");
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    json_sax_set_error(parser, "Parser already finished");
    return -1;
  }

  parser->finished = true;
  if (json_sax_parser_run(parser, true) != 0) return -1;
  if (!parser->done) {
    if (!parser->root_seen) {
      json_sax_set_error(parser, "Expected JSON value");
    } else {
      json_sax_set_error(parser, "Unclosed object or array");
    }
    return -1;
  }
  return 0;
}

const char *json_sax_parser_error(const json_sax_parser_t *parser) {
  if (!parser) return g_error;
  return parser->error[0] ? parser->error : g_error;
}

void json_sax_parser_destroy(json_sax_parser_t *parser) {
  if (!parser) return;
  tstr_free(parser->buffer);
  tstr_free(parser->scratch);
  free(parser);
}

int json_parse_sax(const char *content, size_t len, const json_sax_handler_t *handler, void *ctx) {
  if (!content || len == 0 || !handler) {
    fmt_text(g_error, sizeof(g_error), "Invalid arguments");
    return -1;
  }

  json_sax_parser_t *parser = json_sax_parser_create(handler, ctx);
  if (!parser) return -1;

  int ret = json_sax_parser_feed(parser, content, len);
  if (ret == 0) ret = json_sax_parser_finish(parser);
  json_sax_parser_destroy(parser);
  return ret;
}

int json_parse_sax_raw(const char *content, size_t len,
                       const json_sax_handler_raw_t *handler, void *ctx) {
  if (!content || len == 0 || !handler) {
    fmt_text(g_error, sizeof(g_error), "Invalid arguments");
    return -1;
  }

  json_sax_parser_t *parser = json_sax_parser_create_raw(handler, ctx);
  if (!parser) return -1;

  int ret = json_sax_parser_feed(parser, content, len);
  if (ret == 0) ret = json_sax_parser_finish(parser);
  json_sax_parser_destroy(parser);
  return ret;
}
