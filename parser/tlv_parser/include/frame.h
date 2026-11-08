#ifndef FRAME_H
#define FRAME_H

#include "crc32.h"  // New
#include "endian.h" // Your provided endianness code
#include "memory_pool.h"
#include "parser_error.h"
#include <stddef.h>
#include <stdint.h>

// Frame constants
#define FRAME_HEAD 0xAA
#define FRAME_TAIL 0x55
#define FRAME_VERSION 0x01
#define MAX_PAYLOAD_SIZE (10 * 1024 * 1024) // 10MB limit

enum {
  FRAME_PAYLOAD_TYPE_BINARY = 0,
  FRAME_PAYLOAD_TYPE_TEXT = 1,
  FRAME_PAYLOAD_TYPE_JSON = 2,
  FRAME_PAYLOAD_TYPE_CONTROL = 3,
};

typedef struct frame_s {
  uint8_t head;
  uint32_t msg_id;
  uint8_t version;
  uint8_t payload_type;
  size_t payload_size;
  char *payload;
  uint32_t crc32;
  uint8_t tail;
  MemoryPool *payload_pool;
  size_t payload_pool_offset;
  uint8_t payload_owned;
} frame_t;

void frame_free(frame_t *frame);

size_t frame_pack_header(const frame_t *frame, uint8_t *buf, size_t buf_len);

// Validation functions
ParseError frame_validate(const frame_t *frame);
ParseError frame_validate_header(uint8_t head, uint8_t version);
ParseError frame_validate_payload_size(uint32_t size);
ParseError frame_validate_payload_type(uint8_t type);

// Calculate frame size
size_t frame_calculate_size(const frame_t *frame);

#endif
