#ifndef JSON_UNICODE_H
#define JSON_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static int json_unicode_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool json_unicode_hex4(const char *src, uint32_t *out) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) {
    int digit = json_unicode_hex(src[i]);
    if (digit < 0) return false;
    value = (value << 4) | (uint32_t)digit;
  }
  *out = value;
  return true;
}

/* Decode one JSON \uXXXX escape. offset points at the backslash and is
 * advanced past the complete escape, including a required low surrogate. */
static bool json_unicode_decode_escape(const char *src, size_t len, size_t *offset, uint32_t *out) {
  size_t pos;
  uint32_t high;

  if (!src || !offset || !out) return false;
  pos = *offset;
  if (pos > len || len - pos < 6 || src[pos] != '\\' || src[pos + 1] != 'u' ||
      !json_unicode_hex4(src + pos + 2, &high))
    return false;

  if (high >= 0xD800 && high <= 0xDBFF) {
    uint32_t low;
    if (len - pos < 12 || src[pos + 6] != '\\' || src[pos + 7] != 'u' ||
        !json_unicode_hex4(src + pos + 8, &low) || low < 0xDC00 || low > 0xDFFF)
      return false;
    *out = 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
    *offset = pos + 12;
    return true;
  }

  if (high >= 0xDC00 && high <= 0xDFFF) return false;

  *out = high;
  *offset = pos + 6;
  return true;
}

static size_t json_unicode_append_utf8(char *out, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    out[0] = (char)codepoint;
    return 1;
  }
  if (codepoint <= 0x7FF) {
    out[0] = (char)(0xC0 | (codepoint >> 6));
    out[1] = (char)(0x80 | (codepoint & 0x3F));
    return 2;
  }
  if (codepoint <= 0xFFFF) {
    out[0] = (char)(0xE0 | (codepoint >> 12));
    out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (char)(0x80 | (codepoint & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (codepoint >> 18));
  out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
  out[3] = (char)(0x80 | (codepoint & 0x3F));
  return 4;
}

#endif
