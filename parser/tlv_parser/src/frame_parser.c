#include "frame_parser.h"
#include "crc32.h"
#include "endian.h"
#include <stdlib.h>
#include <string.h>

/* Global CRC table - initialized on first use */
static uint32_t g_crc_table[256];
static int g_crc_table_init = 0;

static void ensure_crc_table(void) {
    if (!g_crc_table_init) {
        crc32_generate_table(g_crc_table);
        g_crc_table_init = 1;
    }
}

FrameParseResult frame_peek_size(const uint8_t *data, size_t len, uint32_t *out_size) {
    if (!data || !out_size)
        return FRAME_PARSE_INVALID_HEAD;

    /* Need header to read payload size */
    if (len < FRAME_HEADER_SIZE)
        return FRAME_PARSE_NEED_MORE;

    /* Validate head marker */
    if (data[FRAME_OFF_HEAD] != FRAME_HEAD)
        return FRAME_PARSE_INVALID_HEAD;

    /* Read payload size at fixed offset */
    uint32_t payload_size;
    memcpy(&payload_size, data + FRAME_OFF_PAYLOAD_SIZE, 4);
    payload_size = le32toh(payload_size);

    /* Sanity check */
    if (payload_size > MAX_PAYLOAD_SIZE)
        return FRAME_PARSE_INVALID_SIZE;

    *out_size = payload_size;
    return FRAME_PARSE_OK;
}

FrameParseResult frame_parse(const uint8_t *data, size_t len, frame_t *out, int flags) {
    if (!data || !out)
        return FRAME_PARSE_INVALID_HEAD;

    /* Peek size first */
    uint32_t payload_size;
    FrameParseResult peek_result = frame_peek_size(data, len, &payload_size);
    if (peek_result != FRAME_PARSE_OK)
        return peek_result;

    /* Check we have complete frame */
    size_t total = frame_total_size(payload_size);
    if (len < total)
        return FRAME_PARSE_NEED_MORE;

    /* Validate tail marker */
    if (data[total - 1] != FRAME_TAIL)
        return FRAME_PARSE_INVALID_TAIL;

    /* Read multi-byte fields (memcpy required for unaligned access) */
    uint32_t msg_id, wire_crc;
    memcpy(&msg_id, data + FRAME_OFF_MSG_ID, 4);
    memcpy(&wire_crc, data + FRAME_OFF_PAYLOAD + payload_size, 4);

    /* Fill output - all fields assigned, no memset needed */
    out->head = data[FRAME_OFF_HEAD];
    out->msg_id = le32toh(msg_id);
    out->version = data[FRAME_OFF_VERSION];
    out->payload_type = data[FRAME_OFF_PAYLOAD_TYPE];
    out->payload_size = payload_size;
    out->payload = payload_size > 0 ? (char *)(data + FRAME_OFF_PAYLOAD) : NULL;
    out->crc32 = le32toh(wire_crc);
    out->tail = data[total - 1];
    out->payload_pool = NULL;
    out->payload_pool_offset = 0;
    out->payload_owned = 0;

    /* Verify CRC if requested */
    if (!(flags & FRAME_PARSE_FLAG_SKIP_CRC)) {
        ensure_crc_table();
        /* CRC covers header + payload (first 11 + N bytes) */
        uint32_t computed = crc32_compute(g_crc_table, data, FRAME_HEADER_SIZE + payload_size);
        if (computed != out->crc32)
            return FRAME_PARSE_CRC_MISMATCH;
    }

    return FRAME_PARSE_OK;
}

FrameParseResult frame_parse_copy(const uint8_t *data, size_t len, frame_t *out,
                                  MemoryPool *pool) {
    /* Parse with zero-copy first */
    FrameParseResult result = frame_parse(data, len, out, FRAME_PARSE_FLAG_NONE);
    if (result != FRAME_PARSE_OK)
        return result;

    /* Copy payload if present */
    if (out->payload_size > 0) {
        char *copy = NULL;
        size_t mark = 0;

        if (pool) {
            mark = pool_mark(pool);
            copy = pool_alloc(pool, out->payload_size);
        }

        if (!copy) {
            copy = malloc(out->payload_size);
            if (!copy)
                return FRAME_PARSE_INVALID_SIZE;  /* allocation failed */
            out->payload_owned = 1;
            out->payload_pool = NULL;
        } else {
            out->payload_owned = 0;
            out->payload_pool = pool;
            out->payload_pool_offset = mark;
        }

        memcpy(copy, out->payload, out->payload_size);
        out->payload = copy;
    }

    return FRAME_PARSE_OK;
}

ParseError frame_parse_result_to_error(FrameParseResult result) {
    switch (result) {
    case FRAME_PARSE_OK:
        return PARSE_OK;
    case FRAME_PARSE_NEED_MORE:
        return PARSE_ERR_TRUNCATED;
    case FRAME_PARSE_INVALID_HEAD:
        return PARSE_ERR_INVALID_HEAD;
    case FRAME_PARSE_INVALID_TAIL:
        return PARSE_ERR_INVALID_TAIL;
    case FRAME_PARSE_INVALID_SIZE:
        return PARSE_ERR_PAYLOAD_TOO_LARGE;
    case FRAME_PARSE_CRC_MISMATCH:
        return PARSE_ERR_CRC_MISMATCH;
    default:
        return PARSE_ERR_UNKNOWN;
    }
}
