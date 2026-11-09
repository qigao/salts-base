/* Incremental lexical parsing relocated from salts-utils into its parser owner. */
#include <xml_parser/xml_sax.h>
#include <salts/tstr.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SALTS_XML_SAX_MAX_DEPTH 256
#define SALTS_XML_SAX_ERROR_CAP 256

struct xml_sax_parser_s {
  xml_sax_handler_t handler;
  void *ctx;
  tstr buffer;
  size_t pos;
  size_t max_buffer_bytes;
  tstr stack[SALTS_XML_SAX_MAX_DEPTH];
  size_t depth;
  bool started;
  bool finished;
  bool failed;
  bool root_seen;
  bool root_closed;
  char error[SALTS_XML_SAX_ERROR_CAP];
};

/* Diagnostics belong to the parser instance, never process-global state. */
static void xml_sax_set_error(xml_sax_parser_t *parser, const char *format, ...) {
  va_list ap;
  if (!parser || parser->failed) return;
  va_start(ap, format);
  vsnprintf(parser->error, sizeof(parser->error), format, ap);
  va_end(ap);
  parser->failed = true;
}

static bool xml_sax_is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void xml_sax_skip_ws(const char *data, size_t len, size_t *pos) {
  while (*pos < len && xml_sax_is_ws(data[*pos]))
    ++*pos;
}

static bool xml_sax_is_name_start(char c) {
  unsigned char u = (unsigned char)c;
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || c == '_' || c == ':';
}

static bool xml_sax_is_name_char(char c) {
  unsigned char u = (unsigned char)c;
  return xml_sax_is_name_start(c) || (u >= '0' && u <= '9') || c == '-' || c == '.';
}

static int xml_sax_call_failed(xml_sax_parser_t *parser) {
  xml_sax_set_error(parser, "SAX callback failed");
  return -1;
}

static int xml_sax_start_document(xml_sax_parser_t *parser) {
  if (parser->started) return 0;
  parser->started = true;
  if (parser->handler.on_start_document && parser->handler.on_start_document(parser->ctx) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_emit_text(xml_sax_parser_t *parser, const char *text, size_t len) {
  if (len == 0) return 0;
  if (parser->depth == 0) {
    for (size_t i = 0; i < len; ++i) {
      if (!xml_sax_is_ws(text[i])) {
        xml_sax_set_error(parser, "Text outside root element");
        return -1;
      }
    }
    return 0;
  }
  if (parser->handler.on_text && parser->handler.on_text(parser->ctx, text, len) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_emit_comment(xml_sax_parser_t *parser, const char *text,
                                      size_t len) {
  if (parser->handler.on_comment && parser->handler.on_comment(parser->ctx, text, len) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_emit_cdata(xml_sax_parser_t *parser, const char *text, size_t len) {
  if (parser->handler.on_cdata && parser->handler.on_cdata(parser->ctx, text, len) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_emit_doctype(xml_sax_parser_t *parser, const char *text,
                                      size_t len) {
  if (parser->root_seen) {
    xml_sax_set_error(parser, "DOCTYPE after root element");
    return -1;
  }
  if (parser->handler.on_doctype && parser->handler.on_doctype(parser->ctx, text, len) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_emit_pi(xml_sax_parser_t *parser, const char *target,
                                 size_t target_len, const char *data, size_t data_len) {
  if (parser->handler.on_processing_instruction &&
      parser->handler.on_processing_instruction(parser->ctx, target, target_len, data, data_len) !=
          0)
    return xml_sax_call_failed(parser);
  return 0;
}

static int xml_sax_push(xml_sax_parser_t *parser, const char *name, size_t name_len) {
  if (parser->depth >= SALTS_XML_SAX_MAX_DEPTH) {
    xml_sax_set_error(parser, "Max XML depth exceeded");
    return -1;
  }
  tstr owned = tstr_dup_len(name, name_len);
  if (!owned) {
    xml_sax_set_error(parser, "Out of memory");
    return -1;
  }
  parser->stack[parser->depth++] = owned;
  return 0;
}

static int xml_sax_pop(xml_sax_parser_t *parser, const char *name, size_t name_len) {
  if (parser->depth == 0) {
    xml_sax_set_error(parser, "Unexpected closing element");
    return -1;
  }

  tstr top = parser->stack[parser->depth - 1];
  if (tstr_len(top) != name_len || memcmp(top, name, name_len) != 0) {
    xml_sax_set_error(parser, "Mismatched closing element");
    return -1;
  }

  tstr_free(top);
  parser->stack[--parser->depth] = NULL;
  if (parser->depth == 0) parser->root_closed = true;
  return 0;
}

static int xml_sax_parse_name(xml_sax_parser_t *parser, const char *data, size_t len,
                                    size_t *pos, size_t *name_start, size_t *name_len, bool final) {
  if (*pos >= len) return final ? (xml_sax_set_error(parser, "Expected XML name"), -1) : 0;
  if (!xml_sax_is_name_start(data[*pos])) {
    xml_sax_set_error(parser, "Expected XML name");
    return -1;
  }

  *name_start = *pos;
  ++*pos;
  while (*pos < len && xml_sax_is_name_char(data[*pos]))
    ++*pos;
  *name_len = *pos - *name_start;
  return 1;
}

static int xml_sax_find_until(xml_sax_parser_t *parser, const char *data, size_t len,
                                    size_t start, const char *needle, size_t needle_len,
                                    size_t *end, bool final, const char *err) {
  for (size_t i = start; i + needle_len <= len; ++i) {
    if (memcmp(data + i, needle, needle_len) == 0) {
      *end = i;
      return 1;
    }
  }
  if (final) {
    xml_sax_set_error(parser, "%s", err);
    return -1;
  }
  return 0;
}

static int xml_sax_scan_markup_end(xml_sax_parser_t *parser, const char *data,
                                         size_t len, size_t start, size_t *end, bool final,
                                         const char *err) {
  char quote = '\0';
  size_t bracket_depth = 0;
  for (size_t i = start; i < len; ++i) {
    char c = data[i];
    if (quote) {
      if (c == quote) quote = '\0';
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '[') {
      ++bracket_depth;
    } else if (c == ']' && bracket_depth > 0) {
      --bracket_depth;
    } else if (c == '>' && bracket_depth == 0) {
      *end = i;
      return 1;
    }
  }
  if (final) {
    xml_sax_set_error(parser, "%s", err);
    return -1;
  }
  return 0;
}

static int xml_sax_parse_pi(xml_sax_parser_t *parser, const char *data, size_t len,
                                  bool final) {
  size_t end = 0;
  int found = xml_sax_find_until(parser, data, len, parser->pos + 2, "?>", 2, &end, final,
                                       "Unterminated processing instruction");
  if (found <= 0) return found;

  size_t target_start = parser->pos + 2;
  size_t target_pos = target_start;
  size_t target_len = 0;
  int name_rc =
      xml_sax_parse_name(parser, data, end, &target_pos, &target_start, &target_len, true);
  if (name_rc <= 0) return -1;

  size_t body_start = target_pos;
  while (body_start < end && xml_sax_is_ws(data[body_start]))
    ++body_start;
  if (xml_sax_emit_pi(parser, data + target_start, target_len, data + body_start,
                            end - body_start) != 0)
    return -1;
  parser->pos = end + 2;
  return 1;
}

static int xml_sax_parse_comment(xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t end = 0;
  int found = xml_sax_find_until(parser, data, len, parser->pos + 4, "-->", 3, &end, final,
                                       "Unterminated comment");
  if (found <= 0) return found;
  if (xml_sax_emit_comment(parser, data + parser->pos + 4, end - parser->pos - 4) != 0)
    return -1;
  parser->pos = end + 3;
  return 1;
}

static int xml_sax_parse_cdata(xml_sax_parser_t *parser, const char *data, size_t len,
                                     bool final) {
  size_t end = 0;
  int found = xml_sax_find_until(parser, data, len, parser->pos + 9, "]]>", 3, &end, final,
                                       "Unterminated CDATA");
  if (found <= 0) return found;
  if (xml_sax_emit_cdata(parser, data + parser->pos + 9, end - parser->pos - 9) != 0)
    return -1;
  parser->pos = end + 3;
  return 1;
}

static int xml_sax_parse_doctype(xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t end = 0;
  int found = xml_sax_scan_markup_end(parser, data, len, parser->pos + 2, &end, final,
                                            "Unterminated DOCTYPE");
  if (found <= 0) return found;
  if (xml_sax_emit_doctype(parser, data + parser->pos + 2, end - parser->pos - 2) != 0)
    return -1;
  parser->pos = end + 1;
  return 1;
}

static int xml_sax_emit_start_tag(xml_sax_parser_t *parser, const char *data,
                                        size_t tag_start, size_t tag_end, size_t name_start,
                                        size_t name_len, bool self_closing) {
  if (parser->root_closed) {
    xml_sax_set_error(parser, "Multiple root elements");
    return -1;
  }
  if (parser->depth == 0) parser->root_seen = true;

  if (parser->handler.on_element_start &&
      parser->handler.on_element_start(parser->ctx, data + name_start, name_len) != 0)
    return xml_sax_call_failed(parser);

  size_t attr_pos = name_start + name_len;
  while (attr_pos < tag_end) {
    xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end) break;

    size_t attr_name_start = 0;
    size_t attr_name_len = 0;
    int name_rc = xml_sax_parse_name(parser, data, tag_end, &attr_pos, &attr_name_start,
                                           &attr_name_len, true);
    if (name_rc <= 0) return -1;
    xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end || data[attr_pos] != '=') {
      xml_sax_set_error(parser, "Expected = after XML attribute");
      return -1;
    }
    ++attr_pos;
    xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end || (data[attr_pos] != '"' && data[attr_pos] != '\'')) {
      xml_sax_set_error(parser, "Expected quoted XML attribute value");
      return -1;
    }

    char quote = data[attr_pos++];
    size_t value_start = attr_pos;
    while (attr_pos < tag_end && data[attr_pos] != quote) {
      if (data[attr_pos] == '<') {
        xml_sax_set_error(parser, "Invalid < in XML attribute value");
        return -1;
      }
      ++attr_pos;
    }
    if (attr_pos >= tag_end) {
      xml_sax_set_error(parser, "Unterminated XML attribute value");
      return -1;
    }
    if (parser->handler.on_attribute &&
        parser->handler.on_attribute(parser->ctx, data + attr_name_start, attr_name_len,
                                     data + value_start, attr_pos - value_start) != 0)
      return xml_sax_call_failed(parser);
    ++attr_pos;
  }

  if (!self_closing) {
    return xml_sax_push(parser, data + name_start, name_len);
  }

  if (parser->handler.on_element_end &&
      parser->handler.on_element_end(parser->ctx, data + name_start, name_len) != 0)
    return xml_sax_call_failed(parser);
  if (parser->depth == 0) parser->root_closed = true;
  (void)tag_start;
  return 0;
}

static int xml_sax_parse_start_tag(xml_sax_parser_t *parser, const char *data,
                                         size_t len, bool final) {
  size_t pos = parser->pos + 1;
  size_t name_start = 0;
  size_t name_len = 0;
  int name_rc = xml_sax_parse_name(parser, data, len, &pos, &name_start, &name_len, final);
  if (name_rc <= 0) return name_rc;

  while (true) {
    xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (xml_sax_set_error(parser, "Unterminated start tag"), -1) : 0;
    if (data[pos] == '>') {
      if (xml_sax_emit_start_tag(parser, data, parser->pos, pos, name_start, name_len,
                                       false) != 0)
        return -1;
      parser->pos = pos + 1;
      return 1;
    }
    if (data[pos] == '/' && pos + 1 < len && data[pos + 1] == '>') {
      if (xml_sax_emit_start_tag(parser, data, parser->pos, pos, name_start, name_len,
                                       true) != 0)
        return -1;
      parser->pos = pos + 2;
      return 1;
    }
    if (data[pos] == '/' && pos + 1 >= len)
      return final ? (xml_sax_set_error(parser, "Unterminated start tag"), -1) : 0;

    size_t attr_name_start = 0;
    size_t attr_name_len = 0;
    name_rc =
        xml_sax_parse_name(parser, data, len, &pos, &attr_name_start, &attr_name_len, final);
    if (name_rc <= 0) return name_rc;
    xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (xml_sax_set_error(parser, "Expected = after XML attribute"), -1) : 0;
    if (data[pos++] != '=') {
      xml_sax_set_error(parser, "Expected = after XML attribute");
      return -1;
    }
    xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (xml_sax_set_error(parser, "Expected quoted XML attribute value"), -1)
                   : 0;
    if (data[pos] != '"' && data[pos] != '\'') {
      xml_sax_set_error(parser, "Expected quoted XML attribute value");
      return -1;
    }
    char quote = data[pos++];
    while (pos < len && data[pos] != quote) {
      if (data[pos] == '<') {
        xml_sax_set_error(parser, "Invalid < in XML attribute value");
        return -1;
      }
      ++pos;
    }
    if (pos >= len)
      return final ? (xml_sax_set_error(parser, "Unterminated XML attribute value"), -1) : 0;
    ++pos;
  }
}

static int xml_sax_parse_end_tag(xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t pos = parser->pos + 2;
  size_t name_start = 0;
  size_t name_len = 0;
  int name_rc = xml_sax_parse_name(parser, data, len, &pos, &name_start, &name_len, final);
  if (name_rc <= 0) return name_rc;
  xml_sax_skip_ws(data, len, &pos);
  if (pos >= len)
    return final ? (xml_sax_set_error(parser, "Unterminated closing tag"), -1) : 0;
  if (data[pos] != '>') {
    xml_sax_set_error(parser, "Expected > after closing element");
    return -1;
  }
  if (xml_sax_pop(parser, data + name_start, name_len) != 0) return -1;
  if (parser->handler.on_element_end &&
      parser->handler.on_element_end(parser->ctx, data + name_start, name_len) != 0)
    return xml_sax_call_failed(parser);
  parser->pos = pos + 1;
  return 1;
}

static int xml_sax_parse_markup(xml_sax_parser_t *parser, const char *data, size_t len,
                                      bool final) {
  if (parser->pos + 1 >= len)
    return final ? (xml_sax_set_error(parser, "Unterminated markup"), -1) : 0;

  if (memcmp(data + parser->pos, "<!--", (len - parser->pos >= 4) ? 4 : len - parser->pos) == 0) {
    if (len - parser->pos < 4)
      return final ? (xml_sax_set_error(parser, "Unterminated comment"), -1) : 0;
    return xml_sax_parse_comment(parser, data, len, final);
  }
  if (len - parser->pos >= 9 && memcmp(data + parser->pos, "<![CDATA[", 9) == 0)
    return xml_sax_parse_cdata(parser, data, len, final);
  if (len - parser->pos < 9 && memcmp(data + parser->pos, "<![CDATA[", len - parser->pos) == 0)
    return final ? (xml_sax_set_error(parser, "Unterminated CDATA"), -1) : 0;
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '!')
    return xml_sax_parse_doctype(parser, data, len, final);
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '?')
    return xml_sax_parse_pi(parser, data, len, final);
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '/')
    return xml_sax_parse_end_tag(parser, data, len, final);
  return xml_sax_parse_start_tag(parser, data, len, final);
}

static void xml_sax_compact(xml_sax_parser_t *parser) {
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

static int xml_sax_run(xml_sax_parser_t *parser, bool final) {
  if (xml_sax_start_document(parser) != 0) return -1;

  const char *data = parser->buffer ? parser->buffer : "";
  size_t len = tstr_len(parser->buffer);

  while (parser->pos < len) {
    char *lt = memchr(data + parser->pos, '<', len - parser->pos);
    if (!lt) {
      if (parser->depth == 0 && !final) break;
      if (xml_sax_emit_text(parser, data + parser->pos, len - parser->pos) != 0) return -1;
      parser->pos = len;
      break;
    }

    size_t lt_pos = (size_t)(lt - data);
    if (lt_pos > parser->pos) {
      if (xml_sax_emit_text(parser, data + parser->pos, lt_pos - parser->pos) != 0) return -1;
      parser->pos = lt_pos;
    }

    int rc = xml_sax_parse_markup(parser, data, len, final);
    if (rc <= 0) {
      if (rc < 0) return -1;
      break;
    }
  }

  xml_sax_compact(parser);
  return 0;
}

xml_sax_parser_t *xml_sax_parser_create(
    const xml_sax_handler_t *handler, void *ctx, size_t max_buffer_bytes) {
  xml_sax_parser_t *parser;
  if (!handler || max_buffer_bytes == 0 || max_buffer_bytes == SIZE_MAX) return NULL;
  parser = (xml_sax_parser_t *)calloc(1, sizeof(*parser));
  if (!parser) return NULL;
  parser->handler = *handler;
  parser->ctx = ctx;
  parser->max_buffer_bytes = max_buffer_bytes;
  parser->buffer = tstr_new();
  if (!parser->buffer) {
    free(parser);
    return NULL;
  }
  return parser;
}

int xml_sax_parser_set_buffer_limit(xml_sax_parser_t *parser, size_t limit) {
  if (!parser || parser->started || parser->finished || parser->failed ||
      limit == 0 || limit == SIZE_MAX) return -1;
  parser->max_buffer_bytes = limit;
  return 0;
}

int xml_sax_parser_feed(xml_sax_parser_t *parser, const char *data, size_t len) {
  if (!parser || (!data && len > 0)) {
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    xml_sax_set_error(parser, "Parser already finished");
    return -1;
  }
  if (len == 0) return 0;
  if (memchr(data, '\0', len) != NULL) {
    xml_sax_set_error(parser, "Embedded NUL in XML input");
    return -1;
  }
  if (len > parser->max_buffer_bytes - tstr_len(parser->buffer)) {
    xml_sax_set_error(parser, "XML SAX pending buffer limit exceeded");
    return -1;
  }

  tstr next = tstr_cat_len(parser->buffer, data, len);
  if (!next) {
    xml_sax_set_error(parser, "Out of memory");
    return -1;
  }
  parser->buffer = next;
  return xml_sax_run(parser, false);
}

int xml_sax_parser_finish(xml_sax_parser_t *parser) {
  if (!parser) {
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    xml_sax_set_error(parser, "Parser already finished");
    return -1;
  }

  parser->finished = true;
  if (xml_sax_run(parser, true) != 0) return -1;
  if (parser->depth != 0) {
    xml_sax_set_error(parser, "Unclosed XML element");
    return -1;
  }
  if (!parser->root_seen) {
    xml_sax_set_error(parser, "Expected XML root element");
    return -1;
  }
  if (parser->handler.on_end_document && parser->handler.on_end_document(parser->ctx) != 0)
    return xml_sax_call_failed(parser);
  return 0;
}

const char *xml_sax_parser_error(const xml_sax_parser_t *parser) {
  if (!parser) return "Invalid XML SAX parser";
  return parser->error[0] ? parser->error : NULL;
}

void xml_sax_parser_destroy(xml_sax_parser_t *parser) {
  if (!parser) return;
  for (size_t i = 0; i < parser->depth; ++i)
    tstr_free(parser->stack[i]);
  tstr_free(parser->buffer);
  free(parser);
}
