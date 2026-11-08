#include "salts_selector.h"

#include "selector_internal.h"
#include "selector_v1_grammar_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct salts_selector_program_s {
  selector_node_t *nodes;
  selector_string_ref_t *list_values;
  char *strings;
  size_t node_count;
  size_t list_value_count;
  size_t string_size;
  size_t predicate_count;
  uint16_t root;
};

typedef struct selector_writer_s {
  char *output;
  size_t capacity;
  size_t required;
  int overflow;
} selector_writer_t;

typedef struct selector_eval_ctx_s {
  const salts_selector_program_t *program;
  const salts_selector_eval_ops_v1_t *ops;
  void *context;
  salts_selector_diagnostic_v1_t *diagnostic;
  int failed;
} selector_eval_ctx_t;

enum {
  SELECTOR_VALUE_UNKNOWN = -1,
  SELECTOR_VALUE_FALSE = 0,
  SELECTOR_VALUE_TRUE = 1
};

static void selector_diagnostic_set(
    salts_selector_diagnostic_v1_t *diagnostic,
    salts_selector_status_t status, const char *source, size_t source_size,
    size_t offset, const char *message) {
  size_t index;
  uint32_t line = 1u;
  uint32_t column = 1u;
  if (!diagnostic || diagnostic->size < SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE)
    return;
  if (offset > source_size) offset = source_size;
  if (source) {
    for (index = 0u; index < offset; ++index) {
      if (source[index] == '\n') {
        ++line;
        column = 1u;
      } else {
        ++column;
      }
    }
  }
  diagnostic->status = status;
  diagnostic->byte_offset = offset;
  diagnostic->line = line;
  diagnostic->column = column;
  (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
                 message ? message : "selector error");
}

static void selector_diagnostic_clear(
    salts_selector_diagnostic_v1_t *diagnostic) {
  size_t size;
  if (!diagnostic) return;
  size = diagnostic->size;
  if (size < SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE) return;
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->size = size;
  diagnostic->status = SALTS_SELECTOR_OK;
  diagnostic->line = 1u;
  diagnostic->column = 1u;
}

void selector_ctx_fail(selector_parse_ctx_t *ctx,
                       salts_selector_status_t status, size_t offset,
                       const char *message) {
  if (!ctx || ctx->status != SALTS_SELECTOR_OK) return;
  ctx->status = status;
  ctx->current_offset = offset;
  (void)snprintf(ctx->message, sizeof(ctx->message), "%s",
                 message ? message : "selector error");
}

static selector_semantic_t selector_invalid_semantic(void) {
  selector_semantic_t semantic;
  memset(&semantic, 0, sizeof(semantic));
  semantic.node = SELECTOR_NO_NODE;
  return semantic;
}

static selector_semantic_t selector_add_node(selector_parse_ctx_t *ctx,
                                             const selector_node_t *node) {
  selector_semantic_t result = selector_invalid_semantic();
  if (!ctx || !node || ctx->status != SALTS_SELECTOR_OK) return result;
  if (ctx->node_count >= SALTS_SELECTOR_MAX_NODES_V1) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT,
                      ctx->current_offset, "selector AST node limit exceeded");
    return result;
  }
  ctx->nodes[ctx->node_count] = *node;
  result.node = (uint16_t)ctx->node_count;
  result.offset = node->source_offset;
  ++ctx->node_count;
  return result;
}

static int selector_hex_value(unsigned char ch) {
  if (ch >= '0' && ch <= '9') return (int)(ch - '0');
  if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a') + 10;
  if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A') + 10;
  return -1;
}

static uint32_t selector_hex_quad(const char *value) {
  uint32_t result = 0u;
  size_t index;
  for (index = 0u; index < 4u; ++index)
    result = result * 16u + (uint32_t)selector_hex_value((unsigned char)value[index]);
  return result;
}

static size_t selector_utf8_encode(uint32_t codepoint, char output[4]) {
  if (codepoint <= 0x7fu) {
    output[0] = (char)codepoint;
    return 1u;
  }
  if (codepoint <= 0x7ffu) {
    output[0] = (char)(0xc0u | (codepoint >> 6));
    output[1] = (char)(0x80u | (codepoint & 0x3fu));
    return 2u;
  }
  if (codepoint <= 0xffffu) {
    output[0] = (char)(0xe0u | (codepoint >> 12));
    output[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
    output[2] = (char)(0x80u | (codepoint & 0x3fu));
    return 3u;
  }
  output[0] = (char)(0xf0u | (codepoint >> 18));
  output[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
  output[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
  output[3] = (char)(0x80u | (codepoint & 0x3fu));
  return 4u;
}

static int selector_pool_append(selector_parse_ctx_t *ctx, const char *value,
                                size_t value_size,
                                selector_string_ref_t *out_ref,
                                size_t source_offset) {
  if (value_size > SALTS_SELECTOR_MAX_STRING_BYTES_V1) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, source_offset,
                      "selector string limit exceeded");
    return 0;
  }
  if (value_size > sizeof(ctx->strings) - ctx->string_size) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, source_offset,
                      "selector retained-string limit exceeded");
    return 0;
  }
  out_ref->offset = (uint16_t)ctx->string_size;
  out_ref->length = (uint16_t)value_size;
  if (value_size > 0u)
    memcpy(ctx->strings + ctx->string_size, value, value_size);
  ctx->string_size += value_size;
  return 1;
}

static int selector_store_field(selector_parse_ctx_t *ctx,
                                selector_semantic_t token,
                                selector_string_ref_t *out_ref) {
  size_t index;
  int allowed = 0;
  if (!ctx || !token.value || token.length == 0u) return 0;
  if (ctx->schema->allow_tag_fields && token.length > 4u &&
      memcmp(token.value, "tag.", 4u) == 0 &&
      memchr(token.value + 4u, '.', token.length - 4u) == NULL)
    allowed = 1;
  for (index = 0u; !allowed && index < ctx->schema->allowed_field_count;
       ++index) {
    const char *candidate = ctx->schema->allowed_fields[index];
    size_t candidate_size = strlen(candidate);
    if (candidate_size == token.length &&
        memcmp(candidate, token.value, token.length) == 0)
      allowed = 1;
  }
  if (!allowed) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR, token.offset,
                      "selector field is not allowed by schema");
    return 0;
  }
  return selector_pool_append(ctx, token.value, token.length, out_ref,
                              token.offset);
}

static int selector_store_string(selector_parse_ctx_t *ctx,
                                 selector_semantic_t token,
                                 selector_string_ref_t *out_ref) {
  char decoded[SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 1u];
  size_t input = 1u;
  size_t output = 0u;
  if (!ctx || !token.value || token.length < 2u || token.value[0] != '"' ||
      token.value[token.length - 1u] != '"')
    return 0;
  while (input + 1u < token.length) {
    unsigned char ch = (unsigned char)token.value[input++];
    if (ch == '\\') {
      unsigned char escaped;
      if (input + 1u > token.length) return 0;
      escaped = (unsigned char)token.value[input++];
      switch (escaped) {
      case '"': ch = '"'; break;
      case '\\': ch = '\\'; break;
      case '/': ch = '/'; break;
      case 'b': ch = '\b'; break;
      case 'f': ch = '\f'; break;
      case 'n': ch = '\n'; break;
      case 'r': ch = '\r'; break;
      case 't': ch = '\t'; break;
      case 'u': {
        uint32_t codepoint;
        char utf8[4];
        size_t utf8_size;
        if (input + 4u > token.length - 1u) return 0;
        codepoint = selector_hex_quad(token.value + input);
        input += 4u;
        if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
          uint32_t low;
          if (input + 6u > token.length - 1u ||
              token.value[input] != '\\' || token.value[input + 1u] != 'u') {
            selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR,
                              token.offset + input,
                              "high surrogate requires a low surrogate");
            return 0;
          }
          low = selector_hex_quad(token.value + input + 2u);
          if (low < 0xdc00u || low > 0xdfffu) {
            selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR,
                              token.offset + input,
                              "invalid Unicode surrogate pair");
            return 0;
          }
          input += 6u;
          codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) +
                      (low - 0xdc00u);
        } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
          selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR,
                            token.offset + input - 4u,
                            "unexpected low Unicode surrogate");
          return 0;
        }
        utf8_size = selector_utf8_encode(codepoint, utf8);
        if (utf8_size > sizeof(decoded) - output) goto string_limit;
        memcpy(decoded + output, utf8, utf8_size);
        output += utf8_size;
        continue;
      }
      default:
        selector_ctx_fail(ctx, SALTS_SELECTOR_SYNTAX_ERROR,
                          token.offset + input - 1u,
                          "invalid selector string escape");
        return 0;
      }
    }
    if (output >= SALTS_SELECTOR_MAX_STRING_BYTES_V1) goto string_limit;
    decoded[output++] = (char)ch;
  }
  return selector_pool_append(ctx, decoded, output, out_ref, token.offset);

string_limit:
  selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, token.offset,
                    "selector string limit exceeded");
  return 0;
}

selector_semantic_t selector_make_binary(selector_parse_ctx_t *ctx,
                                         uint8_t kind,
                                         selector_semantic_t left,
                                         selector_semantic_t right) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = kind;
  node.left = left.node;
  node.right = right.node;
  node.source_offset = (uint16_t)left.offset;
  if (left.node == SELECTOR_NO_NODE || right.node == SELECTOR_NO_NODE)
    return selector_invalid_semantic();
  node.depth = (uint8_t)(1u +
      (ctx->nodes[left.node].depth > ctx->nodes[right.node].depth
           ? ctx->nodes[left.node].depth
           : ctx->nodes[right.node].depth));
  if (node.depth > SALTS_SELECTOR_MAX_DEPTH_V1) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, left.offset,
                      "selector AST depth limit exceeded");
    return selector_invalid_semantic();
  }
  return selector_add_node(ctx, &node);
}

selector_semantic_t selector_make_not(selector_parse_ctx_t *ctx,
                                      selector_semantic_t child) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = SELECTOR_NODE_NOT;
  node.left = child.node;
  node.right = SELECTOR_NO_NODE;
  node.source_offset = (uint16_t)child.offset;
  if (child.node == SELECTOR_NO_NODE) return selector_invalid_semantic();
  node.depth = (uint8_t)(ctx->nodes[child.node].depth + 1u);
  if (node.depth > SALTS_SELECTOR_MAX_DEPTH_V1) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, child.offset,
                      "selector AST depth limit exceeded");
    return selector_invalid_semantic();
  }
  return selector_add_node(ctx, &node);
}

selector_semantic_t selector_make_compare(selector_parse_ctx_t *ctx,
                                          uint8_t kind,
                                          selector_semantic_t field,
                                          selector_semantic_t value) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = kind;
  node.left = SELECTOR_NO_NODE;
  node.right = SELECTOR_NO_NODE;
  node.depth = 1u;
  node.source_offset = (uint16_t)field.offset;
  if (!selector_store_field(ctx, field, &node.field) ||
      !selector_store_string(ctx, value, &node.value))
    return selector_invalid_semantic();
  ++ctx->predicate_count;
  return selector_add_node(ctx, &node);
}

static int selector_ref_compare(const selector_parse_ctx_t *ctx,
                                selector_string_ref_t left,
                                selector_string_ref_t right) {
  size_t common = left.length < right.length ? left.length : right.length;
  int compared = memcmp(ctx->strings + left.offset,
                        ctx->strings + right.offset, common);
  if (compared != 0) return compared;
  return left.length < right.length ? -1 : left.length != right.length;
}

static int selector_sort_unique_list(selector_parse_ctx_t *ctx,
                                     selector_semantic_t list) {
  size_t index;
  /* The O(n^2) insertion sort is bounded to 128 entries and avoids an
   * allocator or process-global comparator context. */
  for (index = 1u; index < list.list_count; ++index) {
    selector_string_ref_t value = ctx->list_values[list.list_start + index];
    size_t cursor = index;
    while (cursor > 0u &&
           selector_ref_compare(
               ctx, ctx->list_values[list.list_start + cursor - 1u], value) >
               0) {
      ctx->list_values[list.list_start + cursor] =
          ctx->list_values[list.list_start + cursor - 1u];
      --cursor;
    }
    ctx->list_values[list.list_start + cursor] = value;
  }
  for (index = 1u; index < list.list_count; ++index) {
    if (selector_ref_compare(ctx,
                            ctx->list_values[list.list_start + index - 1u],
                            ctx->list_values[list.list_start + index]) == 0) {
      selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR,
                        ctx->current_offset,
                        "selector list contains a duplicate value");
      return 0;
    }
  }
  return 1;
}

selector_semantic_t selector_make_membership(selector_parse_ctx_t *ctx,
                                             uint8_t kind,
                                             selector_semantic_t field,
                                             selector_semantic_t list) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = kind;
  node.left = SELECTOR_NO_NODE;
  node.right = SELECTOR_NO_NODE;
  node.depth = 1u;
  node.source_offset = (uint16_t)field.offset;
  node.list_start = list.list_start;
  node.list_count = list.list_count;
  if (!selector_store_field(ctx, field, &node.field) ||
      !selector_sort_unique_list(ctx, list))
    return selector_invalid_semantic();
  ++ctx->predicate_count;
  return selector_add_node(ctx, &node);
}

selector_semantic_t selector_make_has(selector_parse_ctx_t *ctx,
                                      selector_semantic_t field) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = SELECTOR_NODE_HAS;
  node.left = SELECTOR_NO_NODE;
  node.right = SELECTOR_NO_NODE;
  node.depth = 1u;
  node.source_offset = (uint16_t)field.offset;
  if (!selector_store_field(ctx, field, &node.field))
    return selector_invalid_semantic();
  ++ctx->predicate_count;
  return selector_add_node(ctx, &node);
}

selector_semantic_t selector_make_capability(selector_parse_ctx_t *ctx,
                                             selector_semantic_t value) {
  selector_node_t node;
  memset(&node, 0, sizeof(node));
  node.kind = SELECTOR_NODE_CAPABILITY;
  node.left = SELECTOR_NO_NODE;
  node.right = SELECTOR_NO_NODE;
  node.depth = 1u;
  node.source_offset = (uint16_t)value.offset;
  if (!selector_store_string(ctx, value, &node.value))
    return selector_invalid_semantic();
  if (node.value.length == 0u) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_SEMANTIC_ERROR, value.offset,
                      "capability name must not be empty");
    return selector_invalid_semantic();
  }
  ++ctx->predicate_count;
  return selector_add_node(ctx, &node);
}

static int selector_list_add_value(selector_parse_ctx_t *ctx,
                                   selector_semantic_t value,
                                   selector_string_ref_t *out_ref) {
  if (ctx->list_value_count >= SALTS_SELECTOR_MAX_LIST_ITEMS_V1) {
    selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, value.offset,
                      "selector list item limit exceeded");
    return 0;
  }
  if (!selector_store_string(ctx, value, out_ref)) return 0;
  ++ctx->list_value_count;
  return 1;
}

selector_semantic_t selector_list_first(selector_parse_ctx_t *ctx,
                                        selector_semantic_t value) {
  selector_semantic_t list = selector_invalid_semantic();
  selector_string_ref_t ref;
  list.list_start = (uint16_t)ctx->list_value_count;
  list.list_count = 0u;
  list.offset = value.offset;
  if (selector_list_add_value(ctx, value, &ref)) {
    ctx->list_values[list.list_start] = ref;
    list.list_count = 1u;
  }
  return list;
}

selector_semantic_t selector_list_append(selector_parse_ctx_t *ctx,
                                         selector_semantic_t list,
                                         selector_semantic_t value) {
  selector_string_ref_t ref;
  if (list.list_count == 0u ||
      list.list_start + list.list_count != ctx->list_value_count)
    return selector_invalid_semantic();
  if (selector_list_add_value(ctx, value, &ref)) {
    ctx->list_values[list.list_start + list.list_count] = ref;
    ++list.list_count;
  }
  return list;
}

static int selector_utf8_validate(const char *source, size_t source_size,
                                  size_t *error_offset) {
  size_t index = 0u;
  while (index < source_size) {
    unsigned char lead = (unsigned char)source[index];
    size_t width;
    uint32_t codepoint;
    if (lead <= 0x7fu) {
      if (lead == 0u) {
        *error_offset = index;
        return 0;
      }
      ++index;
      continue;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
      width = 2u;
      codepoint = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
      width = 3u;
      codepoint = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
      width = 4u;
      codepoint = lead & 0x07u;
    } else {
      *error_offset = index;
      return 0;
    }
    if (width > source_size - index) {
      *error_offset = index;
      return 0;
    }
    for (size_t part = 1u; part < width; ++part) {
      unsigned char continuation = (unsigned char)source[index + part];
      if ((continuation & 0xc0u) != 0x80u) {
        *error_offset = index + part;
        return 0;
      }
      codepoint = (codepoint << 6) | (continuation & 0x3fu);
    }
    if ((width == 3u && codepoint < 0x800u) ||
        (width == 4u && codepoint < 0x10000u) ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
        codepoint > 0x10ffffu) {
      *error_offset = index;
      return 0;
    }
    index += width;
  }
  return 1;
}

static int selector_schema_valid(const salts_selector_schema_v1_t *schema) {
  size_t index;
  if (!schema || schema->size < SALTS_SELECTOR_SCHEMA_V1_SIZE ||
      !schema->allowed_fields || schema->allowed_field_count == 0u ||
      schema->allowed_field_count > SALTS_SELECTOR_MAX_SCHEMA_FIELDS_V1)
    return 0;
  for (index = 0u; index < schema->allowed_field_count; ++index) {
    size_t length;
    if (!schema->allowed_fields[index]) return 0;
    length = strlen(schema->allowed_fields[index]);
    if (length == 0u || length > SALTS_SELECTOR_MAX_STRING_BYTES_V1)
      return 0;
    if ((length == 2u && strcmp(schema->allowed_fields[index], "in") == 0) ||
        (length == 3u && strcmp(schema->allowed_fields[index], "not") == 0) ||
        (length == 3u && strcmp(schema->allowed_fields[index], "has") == 0) ||
        (length == 10u &&
         strcmp(schema->allowed_fields[index], "capability") == 0))
      return 0;
    for (size_t part = 0u; part < length;) {
      unsigned char ch = (unsigned char)schema->allowed_fields[index][part];
      if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            ch == '_'))
        return 0;
      for (++part; part < length && schema->allowed_fields[index][part] != '.';
           ++part) {
        ch = (unsigned char)schema->allowed_fields[index][part];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
          return 0;
      }
      if (part < length && ++part == length) return 0;
    }
    for (size_t prior = 0u; prior < index; ++prior)
      if (strcmp(schema->allowed_fields[prior], schema->allowed_fields[index]) ==
          0)
        return 0;
  }
  return 1;
}

int salts_selector_compile_v1(
    const char *source, size_t source_size,
    const salts_selector_schema_v1_t *schema,
    salts_selector_program_t **out_program,
    salts_selector_diagnostic_v1_t *diagnostic) {
  selector_parse_ctx_t *ctx = NULL;
  selector_lexer_t lexer;
  selector_semantic_t token;
  salts_selector_program_t *program = NULL;
  void *parser = NULL;
  size_t token_count = 0u;
  size_t utf8_error = 0u;
  int token_id;
  int result = SALTS_SELECTOR_OK;
  if (out_program) *out_program = NULL;
  selector_diagnostic_clear(diagnostic);
  if (!source || source_size == 0u || !selector_schema_valid(schema) ||
      !out_program ||
      (diagnostic && diagnostic->size < SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE)) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_INVALID_ARGUMENT,
                            source, source_size, 0u,
                            "invalid selector compile arguments");
    return SALTS_SELECTOR_INVALID_ARGUMENT;
  }
  if (source_size > SALTS_SELECTOR_MAX_SOURCE_BYTES_V1) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_RESOURCE_LIMIT, source,
                            source_size, SALTS_SELECTOR_MAX_SOURCE_BYTES_V1,
                            "selector source limit exceeded");
    return SALTS_SELECTOR_RESOURCE_LIMIT;
  }
  if (!selector_utf8_validate(source, source_size, &utf8_error)) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_INVALID_UTF8, source,
                            source_size, utf8_error,
                            "selector source is not valid UTF-8");
    return SALTS_SELECTOR_INVALID_UTF8;
  }
  ctx = (selector_parse_ctx_t *)calloc(1u, sizeof(*ctx));
  if (!ctx) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_NO_MEMORY, source,
                            source_size, 0u,
                            "cannot allocate selector compiler state");
    return SALTS_SELECTOR_NO_MEMORY;
  }
  ctx->source = source;
  ctx->source_size = source_size;
  ctx->schema = schema;
  ctx->root = SELECTOR_NO_NODE;
  ctx->status = SALTS_SELECTOR_OK;
  parser = SelectorV1ParseAlloc(malloc);
  if (!parser) {
    result = SALTS_SELECTOR_NO_MEMORY;
    selector_ctx_fail(ctx, result, 0u,
                      "cannot allocate selector parser state");
    goto cleanup;
  }
  selector_lexer_init(&lexer, source, source_size);
  while ((token_id = selector_lexer_next(&lexer, &token)) > 0) {
    if (++token_count > SALTS_SELECTOR_MAX_TOKENS_V1) {
      selector_ctx_fail(ctx, SALTS_SELECTOR_RESOURCE_LIMIT, token.offset,
                        "selector token limit exceeded");
      break;
    }
    ctx->current_offset = token.offset;
    SelectorV1Parse(parser, token_id, token, ctx);
    if (ctx->status != SALTS_SELECTOR_OK) break;
  }
  if (token_id < 0 && ctx->status == SALTS_SELECTOR_OK)
    selector_ctx_fail(ctx, lexer.status, lexer.error_offset, lexer.message);
  if (ctx->status == SALTS_SELECTOR_OK) {
    memset(&token, 0, sizeof(token));
    token.node = SELECTOR_NO_NODE;
    token.offset = source_size;
    ctx->current_offset = source_size;
    SelectorV1Parse(parser, 0, token, ctx);
  }
  if (ctx->status != SALTS_SELECTOR_OK || ctx->root == SELECTOR_NO_NODE ||
      ctx->root >= ctx->node_count) {
    result = ctx->status == SALTS_SELECTOR_OK ? SALTS_SELECTOR_SYNTAX_ERROR
                                              : ctx->status;
    if (ctx->status == SALTS_SELECTOR_OK)
      selector_ctx_fail(ctx, result, source_size,
                        "selector did not produce a complete expression");
    goto cleanup;
  }
  program = (salts_selector_program_t *)calloc(1u, sizeof(*program));
  if (!program) {
    result = SALTS_SELECTOR_NO_MEMORY;
    selector_ctx_fail(ctx, result, 0u,
                      "cannot allocate selector program");
    goto cleanup;
  }
  program->nodes = (selector_node_t *)malloc(ctx->node_count *
                                             sizeof(*program->nodes));
  if (ctx->list_value_count > 0u)
    program->list_values = (selector_string_ref_t *)malloc(
        ctx->list_value_count * sizeof(*program->list_values));
  program->strings = (char *)malloc(ctx->string_size > 0u
                                        ? ctx->string_size
                                        : 1u);
  if (!program->nodes ||
      (ctx->list_value_count > 0u && !program->list_values) ||
      !program->strings) {
    result = SALTS_SELECTOR_NO_MEMORY;
    selector_ctx_fail(ctx, result, 0u,
                      "cannot allocate selector program storage");
    goto cleanup;
  }
  memcpy(program->nodes, ctx->nodes,
         ctx->node_count * sizeof(*program->nodes));
  if (ctx->list_value_count > 0u)
    memcpy(program->list_values, ctx->list_values,
           ctx->list_value_count * sizeof(*program->list_values));
  if (ctx->string_size > 0u)
    memcpy(program->strings, ctx->strings, ctx->string_size);
  program->node_count = ctx->node_count;
  program->list_value_count = ctx->list_value_count;
  program->string_size = ctx->string_size;
  program->predicate_count = ctx->predicate_count;
  program->root = ctx->root;
  *out_program = program;
  program = NULL;

cleanup:
  if (parser) SelectorV1ParseFree(parser, free);
  if (result != SALTS_SELECTOR_OK)
    selector_diagnostic_set(diagnostic, result, source, source_size,
                            ctx ? ctx->current_offset : 0u,
                            ctx && ctx->message[0] ? ctx->message
                                                   : "selector compile failed");
  salts_selector_program_destroy(program);
  free(ctx);
  return result;
}

void salts_selector_program_destroy(salts_selector_program_t *program) {
  if (!program) return;
  free(program->nodes);
  free(program->list_values);
  free(program->strings);
  memset(program, 0, sizeof(*program));
  free(program);
}

uint32_t salts_selector_program_language_version(
    const salts_selector_program_t *program) {
  return program ? SALTS_SELECTOR_LANGUAGE_VERSION_V1 : 0u;
}

size_t salts_selector_program_predicate_count(
    const salts_selector_program_t *program) {
  return program ? program->predicate_count : 0u;
}

static void selector_writer_append(selector_writer_t *writer,
                                   const char *value, size_t value_size) {
  size_t copied = 0u;
  if (writer->required > SIZE_MAX - value_size) {
    writer->overflow = 1;
    return;
  }
  if (writer->output && writer->capacity > 0u &&
      writer->required < writer->capacity - 1u) {
    size_t available = writer->capacity - 1u - writer->required;
    copied = value_size < available ? value_size : available;
    memcpy(writer->output + writer->required, value, copied);
  }
  writer->required += value_size;
}

static void selector_writer_literal(selector_writer_t *writer,
                                    const char *literal) {
  selector_writer_append(writer, literal, strlen(literal));
}

static void selector_writer_string(selector_writer_t *writer,
                                   const salts_selector_program_t *program,
                                   selector_string_ref_t ref) {
  static const char hex[] = "0123456789abcdef";
  size_t index;
  selector_writer_literal(writer, "\"");
  for (index = 0u; index < ref.length; ++index) {
    unsigned char ch = (unsigned char)program->strings[ref.offset + index];
    switch (ch) {
    case '"': selector_writer_literal(writer, "\\\""); break;
    case '\\': selector_writer_literal(writer, "\\\\"); break;
    case '\b': selector_writer_literal(writer, "\\b"); break;
    case '\f': selector_writer_literal(writer, "\\f"); break;
    case '\n': selector_writer_literal(writer, "\\n"); break;
    case '\r': selector_writer_literal(writer, "\\r"); break;
    case '\t': selector_writer_literal(writer, "\\t"); break;
    default:
      if (ch < 0x20u) {
        char escape[] = {'\\', 'u', '0', '0', hex[ch >> 4],
                         hex[ch & 0x0fu]};
        selector_writer_append(writer, escape, sizeof(escape));
      } else {
        selector_writer_append(writer,
                               program->strings + ref.offset + index, 1u);
      }
      break;
    }
  }
  selector_writer_literal(writer, "\"");
}

static int selector_render_node(const salts_selector_program_t *program,
                                uint16_t node_index, size_t depth,
                                selector_writer_t *writer) {
  const selector_node_t *node;
  size_t index;
  if (depth > SALTS_SELECTOR_MAX_DEPTH_V1 ||
      node_index >= program->node_count)
    return 0;
  node = &program->nodes[node_index];
  if (node->kind == SELECTOR_NODE_AND || node->kind == SELECTOR_NODE_OR) {
    selector_writer_literal(writer, "(");
    if (!selector_render_node(program, node->left, depth + 1u, writer))
      return 0;
    selector_writer_literal(writer,
                            node->kind == SELECTOR_NODE_AND ? " && " : " || ");
    if (!selector_render_node(program, node->right, depth + 1u, writer))
      return 0;
    selector_writer_literal(writer, ")");
    return 1;
  }
  if (node->kind == SELECTOR_NODE_NOT) {
    selector_writer_literal(writer, "!(");
    if (!selector_render_node(program, node->left, depth + 1u, writer))
      return 0;
    selector_writer_literal(writer, ")");
    return 1;
  }
  if (node->kind == SELECTOR_NODE_CAPABILITY) {
    selector_writer_literal(writer, "capability(");
    selector_writer_string(writer, program, node->value);
    selector_writer_literal(writer, ")");
    return 1;
  }
  if (node->field.offset + node->field.length > program->string_size)
    return 0;
  if (node->kind == SELECTOR_NODE_HAS) {
    selector_writer_literal(writer, "has(");
    selector_writer_append(writer, program->strings + node->field.offset,
                           node->field.length);
    selector_writer_literal(writer, ")");
    return 1;
  }
  selector_writer_append(writer, program->strings + node->field.offset,
                         node->field.length);
  if (node->kind == SELECTOR_NODE_EQ || node->kind == SELECTOR_NODE_NE) {
    selector_writer_literal(writer,
                            node->kind == SELECTOR_NODE_EQ ? " == " : " != ");
    selector_writer_string(writer, program, node->value);
    return 1;
  }
  if (node->kind != SELECTOR_NODE_IN && node->kind != SELECTOR_NODE_NOT_IN)
    return 0;
  selector_writer_literal(writer,
                          node->kind == SELECTOR_NODE_IN ? " in [" : " not in [");
  if ((size_t)node->list_start + node->list_count >
      program->list_value_count)
    return 0;
  for (index = 0u; index < node->list_count; ++index) {
    if (index > 0u) selector_writer_literal(writer, ", ");
    selector_writer_string(
        writer, program, program->list_values[node->list_start + index]);
  }
  selector_writer_literal(writer, "]");
  return 1;
}

int salts_selector_program_canonical_v1(
    const salts_selector_program_t *program, char *output,
    size_t output_capacity, size_t *required,
    salts_selector_diagnostic_v1_t *diagnostic) {
  selector_writer_t writer;
  selector_diagnostic_clear(diagnostic);
  if (required) *required = 0u;
  if (!program || !program->nodes || program->root >= program->node_count ||
      !required || (!output && output_capacity != 0u) ||
      (diagnostic && diagnostic->size < SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE)) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_INVALID_ARGUMENT, NULL,
                            0u, 0u, "invalid canonicalization arguments");
    return SALTS_SELECTOR_INVALID_ARGUMENT;
  }
  memset(&writer, 0, sizeof(writer));
  writer.output = output;
  writer.capacity = output_capacity;
  if (!selector_render_node(program, program->root, 0u, &writer) ||
      writer.overflow ||
      writer.required > SALTS_SELECTOR_MAX_CANONICAL_BYTES_V1) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_RESOURCE_LIMIT, NULL,
                            0u, 0u, "canonical selector limit exceeded");
    return SALTS_SELECTOR_RESOURCE_LIMIT;
  }
  *required = writer.required;
  if (output && output_capacity > 0u)
    output[writer.required < output_capacity ? writer.required
                                             : output_capacity - 1u] = '\0';
  if (!output && output_capacity == 0u) return SALTS_SELECTOR_OK;
  if (output_capacity <= writer.required) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_BUFFER_TOO_SMALL, NULL,
                            0u, 0u, "canonical output buffer is too small");
    return SALTS_SELECTOR_BUFFER_TOO_SMALL;
  }
  return SALTS_SELECTOR_OK;
}

static int selector_bytes_equal(const char *left, size_t left_size,
                                const char *right, size_t right_size) {
  return left_size == right_size &&
         (left_size == 0u || memcmp(left, right, left_size) == 0);
}

static void selector_eval_fail(selector_eval_ctx_t *ctx,
                               const selector_node_t *node,
                               const char *message) {
  if (ctx->failed) return;
  ctx->failed = 1;
  selector_diagnostic_set(ctx->diagnostic, SALTS_SELECTOR_EVALUATION_ERROR,
                          NULL, 0u, node ? node->source_offset : 0u, message);
}

static int selector_eval_node(selector_eval_ctx_t *ctx, uint16_t node_index,
                              size_t depth) {
  const selector_node_t *node;
  const char *field_value = NULL;
  size_t field_value_size = 0u;
  int resolved;
  int left;
  int right;
  size_t index;
  if (depth > SALTS_SELECTOR_MAX_DEPTH_V1 ||
      node_index >= ctx->program->node_count) {
    selector_eval_fail(ctx, NULL, "invalid selector program structure");
    return SELECTOR_VALUE_UNKNOWN;
  }
  node = &ctx->program->nodes[node_index];
  if (node->kind == SELECTOR_NODE_AND) {
    left = selector_eval_node(ctx, node->left, depth + 1u);
    if (ctx->failed || left == SELECTOR_VALUE_FALSE) return left;
    right = selector_eval_node(ctx, node->right, depth + 1u);
    if (ctx->failed || right == SELECTOR_VALUE_FALSE) return right;
    return left == SELECTOR_VALUE_TRUE && right == SELECTOR_VALUE_TRUE
               ? SELECTOR_VALUE_TRUE
               : SELECTOR_VALUE_UNKNOWN;
  }
  if (node->kind == SELECTOR_NODE_OR) {
    left = selector_eval_node(ctx, node->left, depth + 1u);
    if (ctx->failed || left == SELECTOR_VALUE_TRUE) return left;
    right = selector_eval_node(ctx, node->right, depth + 1u);
    if (ctx->failed || right == SELECTOR_VALUE_TRUE) return right;
    return left == SELECTOR_VALUE_FALSE && right == SELECTOR_VALUE_FALSE
               ? SELECTOR_VALUE_FALSE
               : SELECTOR_VALUE_UNKNOWN;
  }
  if (node->kind == SELECTOR_NODE_NOT) {
    left = selector_eval_node(ctx, node->left, depth + 1u);
    return left == SELECTOR_VALUE_UNKNOWN ? SELECTOR_VALUE_UNKNOWN
                                         : !left;
  }
  if (node->kind == SELECTOR_NODE_CAPABILITY) {
    if (!ctx->ops->has_capability) {
      selector_eval_fail(ctx, node, "capability resolver is not configured");
      return SELECTOR_VALUE_UNKNOWN;
    }
    resolved = ctx->ops->has_capability(
        ctx->context, ctx->program->strings + node->value.offset,
        node->value.length);
    if (resolved < 0 || resolved > 1) {
      selector_eval_fail(ctx, node, "capability resolver failed");
      return SELECTOR_VALUE_UNKNOWN;
    }
    return resolved ? SELECTOR_VALUE_TRUE : SELECTOR_VALUE_FALSE;
  }
  if (!ctx->ops->resolve_field) {
    selector_eval_fail(ctx, node, "field resolver is not configured");
    return SELECTOR_VALUE_UNKNOWN;
  }
  resolved = ctx->ops->resolve_field(
      ctx->context, ctx->program->strings + node->field.offset,
      node->field.length, &field_value, &field_value_size);
  if (resolved < 0 || resolved > 1 || (resolved == 1 && !field_value)) {
    selector_eval_fail(ctx, node, "field resolver failed");
    return SELECTOR_VALUE_UNKNOWN;
  }
  if (node->kind == SELECTOR_NODE_HAS)
    return resolved ? SELECTOR_VALUE_TRUE : SELECTOR_VALUE_FALSE;
  if (resolved == 0) return SELECTOR_VALUE_UNKNOWN;
  if (node->kind == SELECTOR_NODE_EQ || node->kind == SELECTOR_NODE_NE) {
    int equal = selector_bytes_equal(
        field_value, field_value_size,
        ctx->program->strings + node->value.offset, node->value.length);
    return (node->kind == SELECTOR_NODE_EQ ? equal : !equal)
               ? SELECTOR_VALUE_TRUE
               : SELECTOR_VALUE_FALSE;
  }
  if (node->kind != SELECTOR_NODE_IN && node->kind != SELECTOR_NODE_NOT_IN) {
    selector_eval_fail(ctx, node, "unknown selector predicate");
    return SELECTOR_VALUE_UNKNOWN;
  }
  for (index = 0u; index < node->list_count; ++index) {
    selector_string_ref_t ref =
        ctx->program->list_values[node->list_start + index];
    if (selector_bytes_equal(field_value, field_value_size,
                             ctx->program->strings + ref.offset,
                             ref.length))
      return node->kind == SELECTOR_NODE_IN ? SELECTOR_VALUE_TRUE
                                            : SELECTOR_VALUE_FALSE;
  }
  return node->kind == SELECTOR_NODE_IN ? SELECTOR_VALUE_FALSE
                                        : SELECTOR_VALUE_TRUE;
}

int salts_selector_program_evaluate_v1(
    const salts_selector_program_t *program,
    const salts_selector_eval_ops_v1_t *ops, void *context, int *out_match,
    salts_selector_diagnostic_v1_t *diagnostic) {
  selector_eval_ctx_t eval;
  int value;
  selector_diagnostic_clear(diagnostic);
  if (out_match) *out_match = 0;
  if (!program || !ops || ops->size < SALTS_SELECTOR_EVAL_OPS_V1_SIZE ||
      !out_match || program->root >= program->node_count ||
      (diagnostic && diagnostic->size < SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE)) {
    selector_diagnostic_set(diagnostic, SALTS_SELECTOR_INVALID_ARGUMENT, NULL,
                            0u, 0u, "invalid selector evaluation arguments");
    return SALTS_SELECTOR_INVALID_ARGUMENT;
  }
  memset(&eval, 0, sizeof(eval));
  eval.program = program;
  eval.ops = ops;
  eval.context = context;
  eval.diagnostic = diagnostic;
  value = selector_eval_node(&eval, program->root, 0u);
  if (eval.failed) return SALTS_SELECTOR_EVALUATION_ERROR;
  *out_match = value == SELECTOR_VALUE_TRUE;
  return SALTS_SELECTOR_OK;
}
