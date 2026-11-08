#include "json_cserde_reader.h"

#include "json_types.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JSON_CSERDE_NUMBER_TEXT_CAPACITY = 64 };

typedef struct json_cserde_frame {
  json_type_t type;
  union {
    const json_element_t *element;
    const json_pair_t *pair;
  } next;
  bool object_value_pending;
} json_cserde_frame;

typedef struct json_cserde_context {
  cserde_reader reader;
  const json_value_t *pending;
  size_t depth;
  size_t max_depth;
  json_cserde_frame frames[];
} json_cserde_context;

static cserde_status json_cserde_integer_token(const char *text, size_t len, cserde_token *out) {
  const bool negative = len > 0u && text[0] == '-';
  const uint64_t signed_limit = (uint64_t)INT64_MAX + UINT64_C(1);
  const uint64_t limit = negative ? signed_limit : UINT64_MAX;
  uint64_t magnitude = 0u;
  size_t i = negative ? 1u : 0u;

  if (text == NULL || i == len) return CSERDE_SOURCE_ERROR;
  for (; i < len; ++i) {
    const unsigned char ch = (unsigned char)text[i];
    const uint64_t digit = (uint64_t)(ch - (unsigned char)'0');
    if (ch < (unsigned char)'0' || ch > (unsigned char)'9') return CSERDE_SOURCE_ERROR;
    if (magnitude > (limit - digit) / UINT64_C(10)) return CSERDE_VALUE_OUT_OF_RANGE;
    magnitude = magnitude * UINT64_C(10) + digit;
  }

  if (!negative) {
    out->kind = CSERDE_UINT;
    out->value.uint = magnitude;
  } else {
    out->kind = CSERDE_SINT;
    out->value.sint = magnitude == signed_limit ? INT64_MIN : -(int64_t)magnitude;
  }
  return CSERDE_OK;
}

static cserde_status json_cserde_number_token(const json_value_t *value, cserde_token *out) {
  char generated[JSON_CSERDE_NUMBER_TEXT_CAPACITY];
  const char *text;
  size_t len = 0u;

  text = json_number_text(value, &len);
  out->value.floating = json_number(value);
  if (!isfinite(out->value.floating)) return CSERDE_VALUE_OUT_OF_RANGE;
  if (text == NULL || len == 0u) {
    int generated_len;

    generated_len = snprintf(generated, sizeof(generated), "%.17g", out->value.floating);
    if (generated_len <= 0 || (size_t)generated_len >= sizeof(generated))
      return CSERDE_SOURCE_ERROR;
    text = generated;
    len = (size_t)generated_len;
  }

  if (memchr(text, '.', len) == NULL && memchr(text, 'e', len) == NULL &&
      memchr(text, 'E', len) == NULL)
    return json_cserde_integer_token(text, len, out);

  out->kind = CSERDE_FLOAT;
  return CSERDE_OK;
}

static cserde_status json_cserde_push_container(json_cserde_context *context,
                                                const json_value_t *value, cserde_token *out) {
  json_cserde_frame *frame;

  if (context->depth >= context->max_depth) return CSERDE_LIMIT_EXCEEDED;
  frame = &context->frames[context->depth++];
  frame->type = value->type;
  frame->object_value_pending = false;
  if (value->type == JSON_ARRAY) {
    frame->next.element = value->data.array_val.elements;
    out->kind = CSERDE_ARRAY_BEGIN;
  } else {
    frame->next.pair = value->data.object_val.pairs;
    out->kind = CSERDE_MAP_BEGIN;
  }
  return CSERDE_OK;
}

static cserde_status json_cserde_emit_value(json_cserde_context *context, const json_value_t *value,
                                            cserde_token *out) {
  if (value == NULL) return CSERDE_SOURCE_ERROR;
  switch (value->type) {
  case JSON_NULL:
    out->kind = CSERDE_NULL;
    return CSERDE_OK;
  case JSON_BOOL:
    out->kind = CSERDE_BOOL;
    out->value.boolean = value->data.bool_val;
    return CSERDE_OK;
  case JSON_NUMBER:
    return json_cserde_number_token(value, out);
  case JSON_STRING:
    if (value->data.string_val.len != 0u && value->data.string_val.str == NULL)
      return CSERDE_SOURCE_ERROR;
    out->kind = CSERDE_STRING;
    out->value.slice.data = (const unsigned char *)value->data.string_val.str;
    out->value.slice.size = value->data.string_val.len;
    out->value.slice.lifetime = CSERDE_VIEW_STABLE;
    return CSERDE_OK;
  case JSON_ARRAY:
  case JSON_OBJECT:
    return json_cserde_push_container(context, value, out);
  default:
    return CSERDE_SOURCE_ERROR;
  }
}

static cserde_status json_cserde_next(void *opaque, cserde_token *out) {
  json_cserde_context *context = (json_cserde_context *)opaque;

  for (;;) {
    if (context->pending != NULL) {
      const json_value_t *value = context->pending;
      context->pending = NULL;
      return json_cserde_emit_value(context, value, out);
    }

    if (context->depth == 0u) return CSERDE_DONE;

    {
      json_cserde_frame *frame = &context->frames[context->depth - 1u];
      if (frame->type == JSON_ARRAY) {
        const json_element_t *element = frame->next.element;
        if (element == NULL) {
          --context->depth;
          out->kind = CSERDE_ARRAY_END;
          return CSERDE_OK;
        }
        frame->next.element = element->next;
        context->pending = element->value;
        continue;
      }

      if (frame->type == JSON_OBJECT) {
        const json_pair_t *pair = frame->next.pair;
        if (pair == NULL) {
          --context->depth;
          out->kind = CSERDE_MAP_END;
          return CSERDE_OK;
        }
        if (frame->object_value_pending) {
          frame->next.pair = pair->next;
          frame->object_value_pending = false;
          context->pending = pair->value;
          continue;
        }
        if (pair->key_len != 0u && pair->key == NULL) return CSERDE_SOURCE_ERROR;
        frame->object_value_pending = true;
        out->kind = CSERDE_STRING;
        out->value.slice.data = (const unsigned char *)pair->key;
        out->value.slice.size = pair->key_len;
        out->value.slice.lifetime = CSERDE_VIEW_STABLE;
        return CSERDE_OK;
      }

      return CSERDE_SOURCE_ERROR;
    }
  }
}

static const cserde_reader_ops json_cserde_ops = {sizeof(cserde_reader_ops),
                                                  CSERDE_READER_OPS_ABI_VERSION, json_cserde_next};

cserde_reader *json_cserde_reader_create(const json_value_t *root, size_t max_depth) {
  json_cserde_context *context;
  size_t bytes;

  if (root == NULL ||
      max_depth > (SIZE_MAX - sizeof(json_cserde_context)) / sizeof(json_cserde_frame))
    return NULL;
  bytes = sizeof(json_cserde_context) + max_depth * sizeof(json_cserde_frame);
  context = (json_cserde_context *)calloc(1u, bytes);
  if (context == NULL) return NULL;

  context->pending = root;
  context->max_depth = max_depth;
  if (cserde_reader_init(&context->reader, &json_cserde_ops, context) != CSERDE_OK) {
    free(context);
    return NULL;
  }
  return &context->reader;
}

void json_cserde_reader_destroy(cserde_reader *reader) {
  if (reader != NULL) free(reader->context);
}
