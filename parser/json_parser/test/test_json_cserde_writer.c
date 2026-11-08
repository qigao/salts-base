#include "json_cserde_writer.h"
#include "tinytest.h"

#include <string.h>

typedef struct json_writer_sink {
  char data[256];
  size_t length;
  size_t capacity;
} json_writer_sink;

static cserde_status sink_write(void *opaque, const void *data, size_t size) {
  json_writer_sink *sink = (json_writer_sink *)opaque;
  if (sink == NULL || (data == NULL && size != 0u)) return CSERDE_SINK_ERROR;
  if (size > sink->capacity - sink->length) return CSERDE_LIMIT_EXCEEDED;
  if (size != 0u) memcpy(sink->data + sink->length, data, size);
  sink->length += size;
  return CSERDE_OK;
}

static cserde_token string_token(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

spec("JSON CSerde writer") {
  it("streams compact object and array JSON with escaping") {
    json_writer_sink sink = {{0}, 0u, sizeof(sink.data)};
    cserde_writer *writer = json_cserde_writer_create(sink_write, &sink, 4u);
    cserde_token token = {0};

    check_not_null(writer);
    if (writer == NULL) return;

    token.kind = CSERDE_MAP_BEGIN;
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = string_token("a");
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = string_token("x\"\n");
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = string_token("items");
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_ARRAY_BEGIN};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_BOOL, .value.boolean = true};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_NULL};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_ARRAY_END};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_MAP_END};
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    check_equal(cserde_writer_finish(writer), CSERDE_OK);

    check_equal(sink.length, strlen("{\"a\":\"x\\\"\\n\",\"items\":[true,null]}"));
    check(memcmp(sink.data, "{\"a\":\"x\\\"\\n\",\"items\":[true,null]}", sink.length) == 0);
    json_cserde_writer_destroy(writer);
  }

  it("rejects non-string map keys and bytes") {
    json_writer_sink sink = {{0}, 0u, sizeof(sink.data)};
    cserde_writer *writer = json_cserde_writer_create(sink_write, &sink, 2u);
    cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    check_not_null(writer);
    if (writer == NULL) return;
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_UINT, .value.uint = 1u};
    check_equal(cserde_writer_write(writer, &token), CSERDE_UNSUPPORTED);
    json_cserde_writer_destroy(writer);

    sink = (json_writer_sink){{0}, 0u, sizeof(sink.data)};
    writer = json_cserde_writer_create(sink_write, &sink, 0u);
    token = (cserde_token){.kind = CSERDE_BYTES};
    token.value.slice.data = (const unsigned char *)"x";
    token.value.slice.size = 1u;
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    check_equal(cserde_writer_write(writer, &token), CSERDE_UNSUPPORTED);
    json_cserde_writer_destroy(writer);
  }

  it("rejects invalid UTF-8 string tokens") {
    static const unsigned char invalid_utf8[] = {0xc3u, 0x28u};
    json_writer_sink sink = {{0}, 0u, sizeof(sink.data)};
    cserde_writer *writer = json_cserde_writer_create(sink_write, &sink, 0u);
    cserde_token token = {0};

    check_not_null(writer);
    if (writer == NULL) return;
    token.kind = CSERDE_STRING;
    token.value.slice.data = invalid_utf8;
    token.value.slice.size = sizeof(invalid_utf8);
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    check_equal(cserde_writer_write(writer, &token), CSERDE_UNSUPPORTED);
    check_equal(sink.length, (size_t)0u);
    json_cserde_writer_destroy(writer);
  }

  it("enforces depth and caller sink capacity") {
    json_writer_sink sink = {{0}, 0u, sizeof(sink.data)};
    cserde_writer *writer = json_cserde_writer_create(sink_write, &sink, 1u);
    cserde_token token = {.kind = CSERDE_ARRAY_BEGIN};
    check_not_null(writer);
    if (writer == NULL) return;
    check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    check_equal(cserde_writer_write(writer, &token), CSERDE_LIMIT_EXCEEDED);
    json_cserde_writer_destroy(writer);

    sink = (json_writer_sink){{0}, 0u, 2u};
    writer = json_cserde_writer_create(sink_write, &sink, 0u);
    token = string_token("abc");
    check_equal(cserde_writer_write(writer, &token), CSERDE_LIMIT_EXCEEDED);
    json_cserde_writer_destroy(writer);
  }
}
