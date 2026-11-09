#ifndef SALTS_URI_PARSER_H
#define SALTS_URI_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host types
typedef enum {
  URI_HOST_UNKNOWN = 0,
  URI_HOST_REGNAME,
  URI_HOST_IPV6ADDR,
  URI_HOST_IPV4ADDR,
  URI_HOST_IPVFUTURE
} uri_host_type_t;

typedef enum {
  URI_COMPONENT_PORT = 1u << 0u,
  URI_COMPONENT_USERINFO = 1u << 1u,
  URI_COMPONENT_QUERY = 1u << 2u,
  URI_COMPONENT_FRAGMENT = 1u << 3u
} uri_component_flag_t;

typedef enum {
  URI_OVERFLOW_PORT = 1u << 0u,
  URI_OVERFLOW_COMPONENT = 1u << 1u
} uri_overflow_flag_t;

// Simplified URL structure - all stack allocated, no malloc!
typedef struct uri_s {
  char scheme[32];
  char userinfo[256];
  char host[256];
  char path[1024];
  char query[1024];
  char fragment[256];
  int port; // INT_MAX when the syntactically valid numeric value overflows.
  uint8_t host_type;
  uint8_t valid;
  uint8_t component_flags; // uri_component_flag_t bits; preserves empty components.
  uint8_t overflow_flags;  // uri_overflow_flag_t bits; the corresponding value is bounded.
} uri_t;

/**
 * Parse a URL string into a uri_t structure.
 *
 * @param url_string The URL string to parse (null-terminated)
 * @param result     Pointer to uri_t structure to fill
 * @return 1 on success, 0 on failure
 */
int uri_parse(const char *url_string, uri_t *result);

/**
 * Helper function for copying substrings safely.
 *
 * @param src       Source string
 * @param start     Start offset in source
 * @param len       Length to copy
 * @param dest      Destination buffer
 * @param dest_size Size of destination buffer
 */
void uri_copy_substring(const char *src, int start, int len, char *dest, int dest_size);

#ifdef __cplusplus
}
#endif

#endif // SALTS_URI_PARSER_H
