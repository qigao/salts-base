#include "uri_parser.h"
#include "uri_parser_internal.h"
#include <stdlib.h>
#include <string.h>

// Forward declaration of re2c generated function
extern int uri_parse_internal(const char *url_str, uri_t *uri);

int uri_copy_substring_checked(uri_t *uri, const char *src, size_t start, size_t len, char *dest,
                               size_t dest_size) {
  if (!uri || !src || !dest || dest_size == 0u || start > SIZE_MAX - len) return 0;
  if (len >= dest_size) {
    uri->overflow_flags |= URI_OVERFLOW_COMPONENT;
    return 0;
  }
  if (len != 0u) memcpy(dest, src + start, len);
  dest[len] = '\0';
  return 1;
}

// Compatibility helper retains truncating behavior for existing direct callers.
void uri_copy_substring(const char *src, int start, int len, char *dest, int dest_size) {
  size_t copy_len;
  if (!src || !dest || start < 0 || len < 0 || dest_size <= 0) return;
  copy_len = (size_t)len < (size_t)(dest_size - 1) ? (size_t)len : (size_t)(dest_size - 1);
  if (copy_len != 0u) memcpy(dest, src + (size_t)start, copy_len);
  dest[copy_len] = '\0';
}

// Wrapper for re2c parse function - simplified interface
int uri_parse(const char *url_string, uri_t *result) {
  if (!url_string || !result) return 0;

  // Clear the result structure - no malloc cleanup needed!
  memset(result, 0, sizeof(uri_t));

  // Call the re2c generated parser
  return uri_parse_internal(url_string, result);
}
