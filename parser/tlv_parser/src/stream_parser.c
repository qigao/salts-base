#include "stream_parser.h"
#include "frame_parser.h"
#include "parser_context.h"
#include "parser_error.h"
#include <stdlib.h>
#include <string.h>

StreamParser *stream_parser_create(size_t buffer_size) {
    if (buffer_size < FRAME_MIN_SIZE) {
        buffer_size = 65536;  /* Default 64KB */
    }

    StreamParser *sp = malloc(sizeof(StreamParser));
    if (!sp)
        return NULL;

    sp->buffer = malloc(buffer_size);
    if (!sp->buffer) {
        free(sp);
        return NULL;
    }

    sp->buffer_size = buffer_size;
    sp->buffered = 0;
    sp->expected = 0;
    memset(&sp->last_error, 0, sizeof(ParseErrorInfo));

    return sp;
}

StreamState stream_parser_feed(StreamParser *sp, const uint8_t *data, size_t len, frame_t *out) {
    if (!sp || !data || !out)
        return STREAM_ERROR;

    /* Append to buffer */
    if (sp->buffered + len > sp->buffer_size) {
        parse_error_set(&sp->last_error, PARSE_ERR_BUFFER_OVERFLOW, sp->buffered, 0,
                        "Stream buffer overflow");
        return STREAM_ERROR;
    }

    memcpy(sp->buffer + sp->buffered, data, len);
    sp->buffered += len;

    /* Peek frame size if not yet known */
    if (sp->expected == 0) {
        uint32_t payload_size;
        FrameParseResult peek = frame_peek_size(sp->buffer, sp->buffered, &payload_size);

        if (peek == FRAME_PARSE_NEED_MORE)
            return STREAM_NEED_MORE_DATA;

        if (peek != FRAME_PARSE_OK) {
            parse_error_set(&sp->last_error, frame_parse_result_to_error(peek), 0, 0,
                            "Invalid frame header");
            return STREAM_ERROR;
        }

        sp->expected = frame_total_size(payload_size);

        /* Check buffer capacity */
        if (sp->expected > sp->buffer_size) {
            parse_error_set(&sp->last_error, PARSE_ERR_BUFFER_OVERFLOW, 0, 0,
                            "Frame too large for buffer");
            return STREAM_ERROR;
        }
    }

    /* Wait for complete frame */
    if (sp->buffered < sp->expected)
        return STREAM_NEED_MORE_DATA;

    /* Parse complete frame */
    ParserContext *ctx = parser_get_context();
    MemoryPool *pool = ctx ? ctx->pool : NULL;

    FrameParseResult result;
    if (pool) {
        result = frame_parse_copy(sp->buffer, sp->expected, out, pool);
    } else {
        result = frame_parse_copy(sp->buffer, sp->expected, out, NULL);
    }

    if (result != FRAME_PARSE_OK) {
        parse_error_set(&sp->last_error, frame_parse_result_to_error(result), 0, 0,
                        "Frame parse failed");
        return STREAM_ERROR;
    }

    /* Shift remaining data */
    size_t remaining = sp->buffered - sp->expected;
    if (remaining > 0) {
        memmove(sp->buffer, sp->buffer + sp->expected, remaining);
    }
    sp->buffered = remaining;
    sp->expected = 0;

    return STREAM_FRAME_COMPLETE;
}

ParseErrorInfo *stream_parser_get_error(StreamParser *sp) {
    return sp ? &sp->last_error : NULL;
}

void stream_parser_reset(StreamParser *sp) {
    if (sp) {
        sp->buffered = 0;
        sp->expected = 0;
        memset(&sp->last_error, 0, sizeof(ParseErrorInfo));
    }
}

void stream_parser_destroy(StreamParser *sp) {
    if (sp) {
        free(sp->buffer);
        free(sp);
    }
}
