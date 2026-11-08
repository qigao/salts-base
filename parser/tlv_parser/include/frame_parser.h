#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include "frame.h"
#include "parser_error.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @file frame_parser.h
 * @brief Zero-copy binary frame parser for fixed-format protocol
 *
 * Wire format (16 + N bytes):
 *   Offset 0:    HEAD         (1 byte,  0xAA)
 *   Offset 1:    MSG_ID       (4 bytes, LE)
 *   Offset 5:    VERSION      (1 byte)
 *   Offset 6:    PAYLOAD_TYPE (1 byte)
 *   Offset 7:    PAYLOAD_SIZE (4 bytes, LE)
 *   Offset 11:   PAYLOAD      (N bytes)
 *   Offset 11+N: CRC32        (4 bytes, LE)
 *   Offset 15+N: TAIL         (1 byte,  0x55)
 */

#define FRAME_HEADER_SIZE   11  /* bytes before payload */
#define FRAME_TRAILER_SIZE   5  /* bytes after payload (crc + tail) */
#define FRAME_MIN_SIZE      16  /* header + trailer, no payload */

/* Fixed offsets - compile-time constants */
#define FRAME_OFF_HEAD           0
#define FRAME_OFF_MSG_ID         1
#define FRAME_OFF_VERSION        5
#define FRAME_OFF_PAYLOAD_TYPE   6
#define FRAME_OFF_PAYLOAD_SIZE   7
#define FRAME_OFF_PAYLOAD       11

/* Result codes */
typedef enum {
    FRAME_PARSE_OK = 0,
    FRAME_PARSE_NEED_MORE,      /* incomplete data, need more bytes */
    FRAME_PARSE_INVALID_HEAD,
    FRAME_PARSE_INVALID_TAIL,
    FRAME_PARSE_INVALID_SIZE,
    FRAME_PARSE_CRC_MISMATCH,
} FrameParseResult;

/**
 * @brief Peek payload size from buffer without full parse
 * @param data   Input buffer (must have at least FRAME_HEADER_SIZE bytes)
 * @param len    Buffer length
 * @param out_size Output: payload size (only valid if returns FRAME_PARSE_OK)
 * @return FRAME_PARSE_OK, FRAME_PARSE_NEED_MORE, or FRAME_PARSE_INVALID_HEAD
 *
 * Use this to determine how many bytes to wait for before calling frame_parse().
 */
FrameParseResult frame_peek_size(const uint8_t *data, size_t len, uint32_t *out_size);

/**
 * @brief Calculate total frame size from payload size
 */
static inline size_t frame_total_size(uint32_t payload_size) {
    return FRAME_MIN_SIZE + payload_size;
}

/**
 * @brief Parse complete frame - zero-copy
 * @param data   Input buffer containing complete frame
 * @param len    Buffer length (must be >= frame_total_size(payload_size))
 * @param out    Output frame structure
 * @param flags  Parse flags (FRAME_PARSE_FLAG_*)
 * @return FRAME_PARSE_OK on success, error code otherwise
 *
 * IMPORTANT: out->payload points directly into data buffer.
 * Caller must ensure data outlives frame usage, or copy payload if needed.
 * out->payload_owned will be set to 0 (not owned).
 */
#define FRAME_PARSE_FLAG_NONE       0
#define FRAME_PARSE_FLAG_SKIP_CRC   (1 << 0)  /* skip CRC verification */

FrameParseResult frame_parse(const uint8_t *data, size_t len, frame_t *out, int flags);

/**
 * @brief Parse and copy payload to pool
 * @param data   Input buffer
 * @param len    Buffer length
 * @param out    Output frame
 * @param pool   Memory pool for payload allocation (NULL = malloc)
 * @return FRAME_PARSE_OK on success
 *
 * Same as frame_parse() but copies payload to pool/heap.
 * out->payload_owned will be set appropriately.
 */
FrameParseResult frame_parse_copy(const uint8_t *data, size_t len, frame_t *out,
                                  MemoryPool *pool);

/**
 * @brief Convert FrameParseResult to ParseError for compatibility
 */
ParseError frame_parse_result_to_error(FrameParseResult result);

#endif /* FRAME_PARSER_H */
