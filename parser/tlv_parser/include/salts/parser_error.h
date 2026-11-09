#ifndef PARSER_ERROR_H
#define PARSER_ERROR_H

#include <stddef.h>
#include <stdint.h>

// Error codes
typedef enum {
  PARSE_OK = 0,
  PARSE_ERR_TRUNCATED = -1,
  PARSE_ERR_INVALID_HEAD = -2,
  PARSE_ERR_INVALID_TAIL = -3,
  PARSE_ERR_CRC_MISMATCH = -4,
  PARSE_ERR_INVALID_VERSION = -5,
  PARSE_ERR_PAYLOAD_TOO_LARGE = -6,
  PARSE_ERR_OUT_OF_MEMORY = -7,
  PARSE_ERR_INVALID_PAYLOAD_TYPE = -8,
  PARSE_ERR_BUFFER_OVERFLOW = -9,
  PARSE_ERR_UNKNOWN = -99,
} ParseError;

// Error context
typedef struct {
  ParseError code;
  size_t offset;     // Where error occurred
  char message[128]; // Human-readable
  uint32_t msg_id;   // Which message failed
} ParseErrorInfo;

// Get error string
const char *parse_error_string(ParseError err);

// Set error info
void parse_error_set(ParseErrorInfo *info, ParseError code, size_t offset, uint32_t msg_id,
                     const char *message);

#endif // PARSER_ERROR_H
