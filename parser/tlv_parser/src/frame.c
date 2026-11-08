#include "frame.h"
#include <stdlib.h>
#include <string.h>

void frame_free(frame_t *frame) {
  if (!frame)
    return;

  if (frame->payload) {
    if (frame->payload_pool) {
      pool_rewind(frame->payload_pool, frame->payload_pool_offset);
      frame->payload_pool = NULL;
    } else if (frame->payload_owned) {
      free(frame->payload);
    }
    frame->payload = NULL;
  }
  frame->payload_owned = 0;
  frame->payload_size = 0;
}

size_t frame_pack_header(const frame_t *frame, uint8_t *buf, size_t buf_len) {
  // Wire format: head(1) + msg_id(4) + version(1) + payload_type(1) + payload_size(4) = 11 bytes
  if (buf_len < 11) return 0;
  uint8_t *p = buf;

  *p = frame->head; p += 1;

  uint32_t msg_id = htole32(frame->msg_id);
  memcpy(p, &msg_id, 4); p += 4;

  *p = frame->version; p += 1;
  *p = frame->payload_type; p += 1;

  uint32_t payload_size = htole32((uint32_t)frame->payload_size);
  memcpy(p, &payload_size, 4); p += 4;

  return 11;
}
ParseError frame_validate(const frame_t *frame) {
    if (!frame) {
        return PARSE_ERR_INVALID_HEAD;
    }

    // Check magic bytes
    if (frame->head != FRAME_HEAD) {
        return PARSE_ERR_INVALID_HEAD;
    }

    if (frame->tail != FRAME_TAIL) {
        return PARSE_ERR_INVALID_TAIL;
    }

    // Check version
    if (frame->version != FRAME_VERSION) {
        return PARSE_ERR_INVALID_VERSION;
    }

    // Check payload size
    if (frame->payload_size > MAX_PAYLOAD_SIZE) {
        return PARSE_ERR_PAYLOAD_TOO_LARGE;
    }

    // Check payload type (reserved range check)
    if (frame->payload_type > 0x7F) {
        return PARSE_ERR_INVALID_PAYLOAD_TYPE;
    }

    return PARSE_OK;
}

ParseError frame_validate_header(uint8_t head, uint8_t version) {
    if (head != FRAME_HEAD) {
        return PARSE_ERR_INVALID_HEAD;
    }

    if (version != FRAME_VERSION) {
        return PARSE_ERR_INVALID_VERSION;
    }

    return PARSE_OK;
}

ParseError frame_validate_payload_size(uint32_t size) {
    if (size > MAX_PAYLOAD_SIZE) {
        return PARSE_ERR_PAYLOAD_TOO_LARGE;
    }
    return PARSE_OK;
}

ParseError frame_validate_payload_type(uint8_t type) {
    if (type > 0x7F) {
        return PARSE_ERR_INVALID_PAYLOAD_TYPE;
    }
    return PARSE_OK;
}

size_t frame_calculate_size(const frame_t *frame) {
    if (!frame) return 0;
    // head(1) + msg_id(4) + version(1) + payload_type(1) +
    // payload_size(4) + payload(N) + crc(4) + tail(1)
    return 16 + frame->payload_size;
}
