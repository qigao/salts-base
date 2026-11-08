#ifndef STREAM_PARSER_H
#define STREAM_PARSER_H

#include "frame.h"
#include "parser_error.h"
#include <stddef.h>
#include <stdint.h>

// Stream parser state
typedef enum { STREAM_NEED_MORE_DATA, STREAM_FRAME_COMPLETE, STREAM_ERROR } StreamState;

// Stream parser for handling partial/incomplete frames
typedef struct {
  uint8_t *buffer;    // Internal buffer
  size_t buffer_size; // Buffer capacity
  size_t buffered;    // Bytes currently in buffer
  size_t expected;    // Expected frame size
  ParseErrorInfo last_error;
} StreamParser;

// Create stream parser
StreamParser *stream_parser_create(size_t buffer_size);

// Feed data to stream parser
StreamState stream_parser_feed(StreamParser *sp, const uint8_t *data, size_t len, frame_t *out);

// Get last error
ParseErrorInfo *stream_parser_get_error(StreamParser *sp);

// Reset stream parser
void stream_parser_reset(StreamParser *sp);

// Destroy stream parser
void stream_parser_destroy(StreamParser *sp);

#endif // STREAM_PARSER_H
