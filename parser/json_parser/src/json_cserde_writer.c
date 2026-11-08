#include "json_cserde_writer.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vstr.h>

typedef struct json_cserde_writer_frame {
  cserde_token_kind begin_kind;
  size_t count;
  bool expect_key;
} json_cserde_writer_frame;

typedef struct json_cserde_writer_context {
  cserde_writer writer;
  cserde_byte_sink_fn sink;
  void *sink_context;
  size_t depth;
  size_t max_depth;
  bool root_written;
  json_cserde_writer_frame frames[];
} json_cserde_writer_context;

static cserde_status json_writer_sink(
    json_cserde_writer_context *context,
    const void *data,
    size_t size) {
  cserde_status status;
  if (context == NULL || context->sink == NULL || (data == NULL && size != 0u))
    return CSERDE_SINK_ERROR;
  if (size == 0u) return CSERDE_OK;
  status = context->sink(context->sink_context, data, size);
  if (status == CSERDE_OK || status == CSERDE_LIMIT_EXCEEDED ||
      status == CSERDE_SINK_ERROR)
    return status;
  return CSERDE_SINK_ERROR;
}

static cserde_status json_writer_literal(
    json_cserde_writer_context *context,
    const char *text) {
  return json_writer_sink(context, text, strlen(text));
}

static cserde_status json_writer_string(
    json_cserde_writer_context *context,
    const cserde_slice *slice) {
  static const char hex[] = "0123456789abcdef";
  size_t start = 0u;
  size_t i;
  cserde_status status;

  if (slice == NULL || (slice->size != 0u && slice->data == NULL))
    return CSERDE_UNSUPPORTED;
  if (!vstr_utf8_valid(vstr_from_buf(
          (const char *)(slice->data != NULL ? slice->data :
                         (const unsigned char *)""),
          slice->size)))
    return CSERDE_UNSUPPORTED;
  status = json_writer_literal(context, "\"");
  if (status != CSERDE_OK) return status;

  for (i = 0u; i < slice->size; ++i) {
    const unsigned char ch = slice->data[i];
    const char *escape = NULL;
    char unicode_escape[6];

    switch (ch) {
    case '"': escape = "\\\""; break;
    case '\\': escape = "\\\\"; break;
    case '\b': escape = "\\b"; break;
    case '\f': escape = "\\f"; break;
    case '\n': escape = "\\n"; break;
    case '\r': escape = "\\r"; break;
    case '\t': escape = "\\t"; break;
    default:
      if (ch < 0x20u) {
        unicode_escape[0] = '\\';
        unicode_escape[1] = 'u';
        unicode_escape[2] = '0';
        unicode_escape[3] = '0';
        unicode_escape[4] = hex[(ch >> 4u) & 0x0fu];
        unicode_escape[5] = hex[ch & 0x0fu];
        if (i > start) {
          status = json_writer_sink(context, slice->data + start, i - start);
          if (status != CSERDE_OK) return status;
        }
        status = json_writer_sink(context, unicode_escape, sizeof(unicode_escape));
        if (status != CSERDE_OK) return status;
        start = i + 1u;
      }
      continue;
    }

    if (i > start) {
      status = json_writer_sink(context, slice->data + start, i - start);
      if (status != CSERDE_OK) return status;
    }
    status = json_writer_literal(context, escape);
    if (status != CSERDE_OK) return status;
    start = i + 1u;
  }

  if (slice->size > start) {
    status = json_writer_sink(
        context, slice->data + start, slice->size - start);
    if (status != CSERDE_OK) return status;
  }
  return json_writer_literal(context, "\"");
}

static cserde_status json_writer_before_value(
    json_cserde_writer_context *context) {
  json_cserde_writer_frame *frame;
  cserde_status status;

  if (context->depth == 0u) {
    if (context->root_written) return CSERDE_UNSUPPORTED;
    context->root_written = true;
    return CSERDE_OK;
  }

  frame = &context->frames[context->depth - 1u];
  if (frame->begin_kind == CSERDE_ARRAY_BEGIN) {
    if (frame->count != 0u) {
      status = json_writer_literal(context, ",");
      if (status != CSERDE_OK) return status;
    }
    ++frame->count;
    return CSERDE_OK;
  }

  if (frame->begin_kind != CSERDE_MAP_BEGIN || frame->expect_key)
    return CSERDE_UNSUPPORTED;
  frame->expect_key = true;
  ++frame->count;
  return CSERDE_OK;
}

static cserde_status json_writer_map_key(
    json_cserde_writer_context *context,
    const cserde_token *token) {
  json_cserde_writer_frame *frame;
  cserde_status status;

  if (context->depth == 0u || token->kind != CSERDE_STRING)
    return CSERDE_UNSUPPORTED;
  frame = &context->frames[context->depth - 1u];
  if (frame->begin_kind != CSERDE_MAP_BEGIN || !frame->expect_key)
    return CSERDE_UNSUPPORTED;

  if (frame->count != 0u) {
    status = json_writer_literal(context, ",");
    if (status != CSERDE_OK) return status;
  }
  status = json_writer_string(context, &token->value.slice);
  if (status != CSERDE_OK) return status;
  status = json_writer_literal(context, ":");
  if (status != CSERDE_OK) return status;
  frame->expect_key = false;
  return CSERDE_OK;
}

static cserde_status json_writer_number(
    json_cserde_writer_context *context,
    const cserde_token *token) {
  char buffer[64];
  int length;

  if (token->kind == CSERDE_SINT)
    length = snprintf(buffer, sizeof(buffer), "%" PRId64, token->value.sint);
  else if (token->kind == CSERDE_UINT)
    length = snprintf(buffer, sizeof(buffer), "%" PRIu64, token->value.uint);
  else {
    if (!isfinite(token->value.floating)) return CSERDE_UNSUPPORTED;
    length = snprintf(buffer, sizeof(buffer), "%.17g", token->value.floating);
  }
  if (length <= 0 || (size_t)length >= sizeof(buffer))
    return CSERDE_UNSUPPORTED;
  return json_writer_sink(context, buffer, (size_t)length);
}

static cserde_status json_cserde_write(
    void *opaque,
    const cserde_token *token) {
  json_cserde_writer_context *context =
      (json_cserde_writer_context *)opaque;
  json_cserde_writer_frame *frame;
  cserde_status status;

  if (context == NULL || token == NULL) return CSERDE_UNSUPPORTED;

  if (context->depth != 0u) {
    frame = &context->frames[context->depth - 1u];
    if (frame->begin_kind == CSERDE_MAP_BEGIN && frame->expect_key &&
        token->kind != CSERDE_MAP_END)
      return json_writer_map_key(context, token);
  }

  if (token->kind == CSERDE_ARRAY_END || token->kind == CSERDE_MAP_END) {
    if (context->depth == 0u) return CSERDE_UNSUPPORTED;
    frame = &context->frames[context->depth - 1u];
    if ((token->kind == CSERDE_ARRAY_END &&
         frame->begin_kind != CSERDE_ARRAY_BEGIN) ||
        (token->kind == CSERDE_MAP_END &&
         (frame->begin_kind != CSERDE_MAP_BEGIN || !frame->expect_key)))
      return CSERDE_UNSUPPORTED;
    --context->depth;
    return json_writer_literal(
        context, token->kind == CSERDE_ARRAY_END ? "]" : "}");
  }

  if (token->kind == CSERDE_ARRAY_BEGIN || token->kind == CSERDE_MAP_BEGIN) {
    if (context->depth >= context->max_depth)
      return CSERDE_LIMIT_EXCEEDED;
    status = json_writer_before_value(context);
    if (status != CSERDE_OK) return status;
    status = json_writer_literal(
        context, token->kind == CSERDE_ARRAY_BEGIN ? "[" : "{");
    if (status != CSERDE_OK) return status;
    frame = &context->frames[context->depth++];
    frame->begin_kind = token->kind;
    frame->count = 0u;
    frame->expect_key = token->kind == CSERDE_MAP_BEGIN;
    return CSERDE_OK;
  }

  if (token->kind == CSERDE_BYTES) return CSERDE_UNSUPPORTED;
  status = json_writer_before_value(context);
  if (status != CSERDE_OK) return status;

  switch (token->kind) {
  case CSERDE_NULL:
    return json_writer_literal(context, "null");
  case CSERDE_BOOL:
    return json_writer_literal(context, token->value.boolean ? "true" : "false");
  case CSERDE_SINT:
  case CSERDE_UINT:
  case CSERDE_FLOAT:
    return json_writer_number(context, token);
  case CSERDE_STRING:
    return json_writer_string(context, &token->value.slice);
  default:
    return CSERDE_UNSUPPORTED;
  }
}

static cserde_status json_cserde_finish(void *opaque) {
  const json_cserde_writer_context *context =
      (const json_cserde_writer_context *)opaque;
  if (context == NULL || !context->root_written || context->depth != 0u)
    return CSERDE_UNSUPPORTED;
  return CSERDE_OK;
}

static const cserde_writer_ops JSON_CSERDE_WRITER_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    json_cserde_write,
    json_cserde_finish};

cserde_writer *json_cserde_writer_create(
    cserde_byte_sink_fn sink,
    void *sink_context,
    size_t max_depth) {
  json_cserde_writer_context *context;
  size_t bytes;

  if (sink == NULL ||
      max_depth > (SIZE_MAX - sizeof(json_cserde_writer_context)) /
                      sizeof(json_cserde_writer_frame))
    return NULL;
  bytes = sizeof(json_cserde_writer_context) +
      max_depth * sizeof(json_cserde_writer_frame);
  context = (json_cserde_writer_context *)calloc(1u, bytes);
  if (context == NULL) return NULL;
  context->sink = sink;
  context->sink_context = sink_context;
  context->max_depth = max_depth;
  if (cserde_writer_init(
          &context->writer, &JSON_CSERDE_WRITER_OPS, context) != CSERDE_OK) {
    free(context);
    return NULL;
  }
  return &context->writer;
}

void json_cserde_writer_destroy(cserde_writer *writer) {
  if (writer != NULL) free(writer->context);
}
