#ifndef SALTS_SELECTOR_INTERNAL_H
#define SALTS_SELECTOR_INTERNAL_H

#include "salts_selector.h"

#include <stddef.h>
#include <stdint.h>

enum {
  SELECTOR_NODE_AND = 1,
  SELECTOR_NODE_OR,
  SELECTOR_NODE_NOT,
  SELECTOR_NODE_EQ,
  SELECTOR_NODE_NE,
  SELECTOR_NODE_IN,
  SELECTOR_NODE_NOT_IN,
  SELECTOR_NODE_HAS,
  SELECTOR_NODE_CAPABILITY
};

#define SELECTOR_NO_NODE UINT16_MAX

typedef struct selector_string_ref_s {
  uint16_t offset;
  uint16_t length;
} selector_string_ref_t;

typedef struct selector_node_s {
  uint8_t kind;
  uint8_t depth;
  uint16_t left;
  uint16_t right;
  uint16_t source_offset;
  selector_string_ref_t field;
  selector_string_ref_t value;
  uint16_t list_start;
  uint16_t list_count;
} selector_node_t;

typedef struct selector_semantic_s {
  uint16_t node;
  uint16_t list_start;
  uint16_t list_count;
  uint16_t reserved;
  const char *value;
  size_t length;
  size_t offset;
} selector_semantic_t;

typedef struct selector_parse_ctx_s {
  const char *source;
  size_t source_size;
  const salts_selector_schema_v1_t *schema;
  selector_node_t nodes[SALTS_SELECTOR_MAX_NODES_V1];
  size_t node_count;
  selector_string_ref_t list_values[SALTS_SELECTOR_MAX_LIST_ITEMS_V1];
  size_t list_value_count;
  char strings[SALTS_SELECTOR_MAX_SOURCE_BYTES_V1 + 1u];
  size_t string_size;
  size_t predicate_count;
  uint16_t root;
  size_t current_offset;
  salts_selector_status_t status;
  char message[SALTS_SELECTOR_DIAGNOSTIC_BYTES_V1];
} selector_parse_ctx_t;

typedef struct selector_lexer_s {
  const char *input;
  const char *cursor;
  const char *limit;
  const char *marker;
  size_t parenthesis_depth;
  salts_selector_status_t status;
  size_t error_offset;
  char message[SALTS_SELECTOR_DIAGNOSTIC_BYTES_V1];
} selector_lexer_t;

void selector_lexer_init(selector_lexer_t *lexer, const char *input,
                         size_t input_size);
int selector_lexer_next(selector_lexer_t *lexer,
                        selector_semantic_t *token);

void selector_ctx_fail(selector_parse_ctx_t *ctx,
                       salts_selector_status_t status, size_t offset,
                       const char *message);
selector_semantic_t selector_make_binary(selector_parse_ctx_t *ctx,
                                         uint8_t kind,
                                         selector_semantic_t left,
                                         selector_semantic_t right);
selector_semantic_t selector_make_not(selector_parse_ctx_t *ctx,
                                      selector_semantic_t child);
selector_semantic_t selector_make_compare(selector_parse_ctx_t *ctx,
                                          uint8_t kind,
                                          selector_semantic_t field,
                                          selector_semantic_t value);
selector_semantic_t selector_make_membership(selector_parse_ctx_t *ctx,
                                             uint8_t kind,
                                             selector_semantic_t field,
                                             selector_semantic_t list);
selector_semantic_t selector_make_has(selector_parse_ctx_t *ctx,
                                      selector_semantic_t field);
selector_semantic_t selector_make_capability(selector_parse_ctx_t *ctx,
                                             selector_semantic_t value);
selector_semantic_t selector_list_first(selector_parse_ctx_t *ctx,
                                        selector_semantic_t value);
selector_semantic_t selector_list_append(selector_parse_ctx_t *ctx,
                                         selector_semantic_t list,
                                         selector_semantic_t value);

/* Lemon's generated header contains token IDs only. */
void *SelectorV1ParseAlloc(void *(*malloc_proc)(size_t));
void SelectorV1Parse(void *parser, int token_id,
                     selector_semantic_t token,
                     selector_parse_ctx_t *ctx);
void SelectorV1ParseFree(void *parser, void (*free_proc)(void *));

#endif
