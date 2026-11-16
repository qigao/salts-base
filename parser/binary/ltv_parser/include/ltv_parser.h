#ifndef LTV_PARSER_H
#define LTV_PARSER_H

/**
 * @file ltv_parser.h
 * @brief Length-Type-Value parser with varint length encoding
 *
 * Wire format:
 *   +----------+------+----------+
 *   | Length   | Type | Value    |
 *   | (varint) | (1B) | (N bytes)|
 *   +----------+------+----------+
 *
 * Length field encodes (type_size + value_size), so minimum length is 1.
 * Varint encoding: canonical unsigned LEB128, 7 bits per byte, MSB is the
 * continuation flag. Overlong encodings are rejected.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define LTV_MAX_VARINT_BYTES   5
#define LTV_MAX_PAYLOAD_SIZE   (10 * 1024 * 1024)  /* 10MB */

/* Parse result codes */
typedef enum {
    LTV_PARSE_OK = 0,
    LTV_PARSE_NEED_MORE,        /* Incomplete data */
    LTV_PARSE_INVALID_VARINT,   /* Malformed varint */
    LTV_PARSE_SIZE_OVERFLOW,    /* Length exceeds max */
    LTV_PARSE_BUFFER_OVERFLOW,  /* Stream buffer full */
} LtvParseResult;

/**
 * @brief Parsed LTV message (zero-copy)
 */
typedef struct {
    uint32_t       length;      /* Total length (type + value) */
    uint8_t        type;        /* Message type */
    const uint8_t *value;       /* Pointer to value data (zero-copy) */
    size_t         value_size;  /* Value size (length - 1) */
    size_t         consumed;    /* Total bytes consumed from input */
} ltv_message_t;

/* ============================================================================
 * Varint encoding/decoding
 * ============================================================================ */

/**
 * @brief Decode varint from buffer
 * @param data   Input buffer
 * @param len    Buffer length
 * @param out    Output value
 * @return Bytes consumed (1-5), 0 if more bytes are needed, or -1 for a
 *         malformed, overlong, or overflowing encoding. out is unchanged
 *         unless a canonical value is decoded.
 */
int ltv_decode_varint(const uint8_t *data, size_t len, uint32_t *out);

/**
 * @brief Encode value as varint
 * @param value  Value to encode
 * @param out    Output buffer (must have at least LTV_MAX_VARINT_BYTES space)
 * @return Bytes written (1-5), or 0 when out is NULL.
 */
int ltv_encode_varint(uint32_t value, uint8_t *out);

/**
 * @brief Calculate varint encoded size
 * @param value  Value to encode
 * @return Bytes needed (1-5)
 */
int ltv_varint_size(uint32_t value);

/* ============================================================================
 * Message parsing
 * ============================================================================ */

/**
 * @brief Peek message length without full parse
 * @param data          Input buffer
 * @param len           Buffer length
 * @param out_length    Output: message length (type + value size)
 * @param out_header    Output: varint header size (bytes consumed for length)
 * @return LTV_PARSE_OK on success
 */
LtvParseResult ltv_peek_size(const uint8_t *data, size_t len,
                             uint32_t *out_length, size_t *out_header);

/**
 * @brief Calculate total wire size from length field
 */
static inline size_t ltv_total_size(uint32_t length, size_t header_size) {
    return header_size + length;
}

/**
 * @brief Parse single message (zero-copy)
 * @param data   Input buffer
 * @param len    Buffer length
 * @param out    Output message structure
 * @return LTV_PARSE_OK on success
 *
 * IMPORTANT: out->value points directly into data buffer.
 * Caller must ensure data outlives message usage.
 */
LtvParseResult ltv_parse(const uint8_t *data, size_t len, ltv_message_t *out);

/* ============================================================================
 * Message building
 * ============================================================================ */

/**
 * @brief Calculate wire size for message
 * @param value_size  Size of value data
 * @return Total wire size including header, or 0 when value_size exceeds
 *         LTV_MAX_PAYLOAD_SIZE.
 */
size_t ltv_wire_size(size_t value_size);

/**
 * @brief Build LTV message into buffer
 * @param type        Message type
 * @param value       Value data
 * @param value_size  Value data size
 * @param out         Output buffer
 * @param out_len     Output buffer size
 * @return Bytes written, or 0 for an invalid value pointer, oversized value,
 *         NULL output, or insufficient output capacity. No partial message is
 *         written on failure.
 */
size_t ltv_build(uint8_t type, const uint8_t *value, size_t value_size,
                 uint8_t *out, size_t out_len);

/* ============================================================================
 * Stream parser
 * ============================================================================ */

typedef struct ltv_stream_s ltv_stream_t;

/**
 * @brief Create stream parser
 * @param buffer_size  Internal buffer size (0 = default 64KB)
 * @return Stream parser or NULL on allocation failure
 */
ltv_stream_t *ltv_stream_create(size_t buffer_size);

/**
 * @brief Feed data to stream parser
 * @param stream  Stream parser
 * @param data    Input data
 * @param len     Input data length
 * @param out     Output message (valid only if returns LTV_PARSE_OK)
 * @return LTV_PARSE_OK when complete message parsed
 *
 * Call repeatedly with new data until LTV_PARSE_OK.
 * After LTV_PARSE_OK, out->value remains valid until the next feed, reset, or
 * destroy call on this stream. Call feed again to parse the next message.
 */
LtvParseResult ltv_stream_feed(ltv_stream_t *stream, const uint8_t *data,
                               size_t len, ltv_message_t *out);

/**
 * @brief Get unconsumed input bytes
 * @param stream  Stream parser
 * @param out_remaining  Output: pointer to unconsumed data
 * @return Number of bytes after the most recently returned message. The view
 *         remains valid until the next feed, reset, or destroy call.
 */
size_t ltv_stream_remaining(ltv_stream_t *stream, const uint8_t **out_remaining);

/**
 * @brief Reset stream parser state
 */
void ltv_stream_reset(ltv_stream_t *stream);

/**
 * @brief Destroy stream parser
 */
void ltv_stream_destroy(ltv_stream_t *stream);

/**
 * @brief Get error string
 */
const char *ltv_parse_result_string(LtvParseResult result);

#ifdef __cplusplus
}
#endif

#endif /* LTV_PARSER_H */
