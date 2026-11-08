#include <tinytest.h>
#include "frame.h"
#include "parser_error.h"
#include "parser_stats.h"
#include "memory_pool.h"
#include "stream_parser.h"
#include "parser_context.h"
#include "crc32.h"
#include "endian.h"
#include <string.h>
#include <stdlib.h>

static uint32_t crc_table[256];

// Helper to create test frame
static size_t create_test_frame(uint8_t *buf, uint32_t msg_id,
                                const char *payload, size_t payload_len) {
    uint8_t *p = buf;

    // head
    *p = 0xAA;
    p += 1;

    // msg_id (little-endian)
    uint32_t msg_id_le = htole32(msg_id);
    memcpy(p, &msg_id_le, 4);
    p += 4;

    // version
    *p = 0x01;
    p += 1;

    // payload_type
    *p = FRAME_PAYLOAD_TYPE_TEXT;
    p += 1;

    // payload_size (little-endian)
    uint32_t payload_size_le = htole32((uint32_t)payload_len);
    memcpy(p, &payload_size_le, 4);
    p += 4;

    // payload
    if (payload && payload_len > 0) {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }

    // CRC (compute over header + payload)
    uint32_t crc = crc32_compute(crc_table, buf, 11 + payload_len);
    uint32_t crc_le = htole32(crc);
    memcpy(p, &crc_le, 4);
    p += 4;

    // tail
    *p = 0x55;
    p += 1;

    return (size_t)(p - buf);
}

spec("uri_parser_production") {
    before_each() {
        crc32_generate_table(crc_table);
    }

    describe("error_handling") {
        it("should set error info correctly") {
            ParseErrorInfo error;
            parse_error_set(&error, PARSE_ERR_CRC_MISMATCH, 10, 123, NULL);

            check_equal(error.code, PARSE_ERR_CRC_MISMATCH);
            check_equal(error.offset, 10);
            check_equal(error.msg_id, 123);
            check_not_null(error.message);

            const char *err_str = parse_error_string(PARSE_ERR_CRC_MISMATCH);
            check_not_null(err_str);
        }

        it("should validate frames correctly") {
            frame_t frame = {0};

            // Valid frame
            frame.head = FRAME_HEAD;
            frame.tail = FRAME_TAIL;
            frame.version = FRAME_VERSION;
            frame.payload_type = FRAME_PAYLOAD_TYPE_TEXT;
            frame.payload_size = 100;

            check_equal(frame_validate(&frame), PARSE_OK);

            // Invalid head
            frame.head = 0xFF;
            check_equal(frame_validate(&frame), PARSE_ERR_INVALID_HEAD);
            frame.head = FRAME_HEAD;

            // Invalid tail
            frame.tail = 0xFF;
            check_equal(frame_validate(&frame), PARSE_ERR_INVALID_TAIL);
            frame.tail = FRAME_TAIL;

            // Invalid version
            frame.version = 0xFF;
            check_equal(frame_validate(&frame), PARSE_ERR_INVALID_VERSION);
            frame.version = FRAME_VERSION;

            // Payload too large
            frame.payload_size = MAX_PAYLOAD_SIZE + 1;
            check_equal(frame_validate(&frame), PARSE_ERR_PAYLOAD_TOO_LARGE);
        }
    }

    describe("memory_pool") {
        it("should allocate and reset pool") {
            const size_t first_allocation_size = 100;
            const size_t second_allocation_size = 200;
            const size_t alignment = MEMORY_POOL_DEFAULT_ALIGNMENT;
            MemoryPool *pool = pool_create(1024);
            check_not_null(pool);

            void *ptr1 = pool_alloc(pool, first_allocation_size);
            check_not_null(ptr1);
            check_equal(pool_get_used(pool), first_allocation_size);

            void *ptr2 = pool_alloc(pool, second_allocation_size);
            check_not_null(ptr2);
            const size_t second_allocation_offset =
                (first_allocation_size + alignment - 1) & ~(alignment - 1);
            check_equal(pool_get_used(pool),
                        second_allocation_offset + second_allocation_size);

            pool_reset(pool);
            check_equal(pool_get_used(pool), 0);

            void *ptr3 = pool_alloc(pool, 50);
            check_not_null(ptr3);
            check_true(ptr1 == ptr3);  // Reused memory

            pool_destroy(pool);
        }

        it("should handle pool exhaustion") {
            MemoryPool *pool = pool_create(100);
            check_not_null(pool);

            void *ptr1 = pool_alloc(pool, 50);
            check_not_null(ptr1);

            void *ptr2 = pool_alloc(pool, 60);  // Would exceed capacity
            check_null(ptr2);

            pool_destroy(pool);
        }
    }

    describe("stream_parser") {
        it("should parse complete frame") {
            StreamParser *sp = stream_parser_create(1024);
            check_not_null(sp);

            uint8_t buffer[256];
            const char *payload = "Hello";
            size_t frame_len = create_test_frame(buffer, 1, payload, strlen(payload));

            frame_t frame;
            memset(&frame, 0, sizeof(frame));
            StreamState state = stream_parser_feed(sp, buffer, frame_len, &frame);

            check_equal(state, STREAM_FRAME_COMPLETE);
            check_equal(frame.head, 0xAA);
            check_equal(frame.msg_id, 1);
            check_equal(frame.version, 0x01);
            check_equal(frame.payload_size, strlen(payload));
            check_equal(frame.tail, 0x55);

            frame_free(&frame);
            stream_parser_destroy(sp);
        }

        it("should handle partial frames") {
            StreamParser *sp = stream_parser_create(1024);
            check_not_null(sp);

            uint8_t buffer[256];
            const char *payload = "Hello World";
            size_t frame_len = create_test_frame(buffer, 2, payload, strlen(payload));

            frame_t frame;
            memset(&frame, 0, sizeof(frame));

            // Feed first 10 bytes (incomplete header)
            StreamState state = stream_parser_feed(sp, buffer, 10, &frame);
            check_equal(state, STREAM_NEED_MORE_DATA);

            // Feed next 10 bytes (still incomplete)
            state = stream_parser_feed(sp, buffer + 10, 10, &frame);
            check_equal(state, STREAM_NEED_MORE_DATA);

            // Feed remaining bytes
            state = stream_parser_feed(sp, buffer + 20, frame_len - 20, &frame);
            check_equal(state, STREAM_FRAME_COMPLETE);
            check_equal(frame.msg_id, 2);

            frame_free(&frame);
            stream_parser_destroy(sp);
        }

        it("should handle multiple frames") {
            StreamParser *sp = stream_parser_create(1024);
            check_not_null(sp);

            uint8_t buffer1[256], buffer2[256];
            size_t len1 = create_test_frame(buffer1, 1, "First", 5);
            size_t len2 = create_test_frame(buffer2, 2, "Second", 6);

            frame_t frame;

            // Feed first frame
            memset(&frame, 0, sizeof(frame));
            StreamState state = stream_parser_feed(sp, buffer1, len1, &frame);
            check_equal(state, STREAM_FRAME_COMPLETE);
            check_equal(frame.msg_id, 1);
            frame_free(&frame);

            // Feed second frame
            memset(&frame, 0, sizeof(frame));
            state = stream_parser_feed(sp, buffer2, len2, &frame);
            check_equal(state, STREAM_FRAME_COMPLETE);
            check_equal(frame.msg_id, 2);
            frame_free(&frame);

            stream_parser_destroy(sp);
        }
    }

    describe("statistics") {
        it("should track stats correctly") {
            ParserStats stats;
            parser_stats_init(&stats);

            check_equal(stats.frames_parsed, 0);
            check_equal(stats.frames_failed, 0);

            // Simulate successful parse
            parser_stats_update(&stats, PARSE_OK, 100, 1000);
            check_equal(stats.frames_parsed, 1);
            check_equal(stats.bytes_processed, 100);

            // Simulate failed parse
            parser_stats_update(&stats, PARSE_ERR_CRC_MISMATCH, 0, 500);
            check_equal(stats.frames_failed, 1);
            check_equal(stats.crc_errors, 1);

            double error_rate = parser_stats_error_rate(&stats);
            check_within(error_rate, 50.0, 0.001);
        }
    }

    describe("context") {
        it("should handle context creation") {
            ParserContext *ctx = parser_context_create(1024);
            check_not_null(ctx);
            check_not_null(ctx->pool);

            ParserStats *stats = parser_context_get_stats(ctx);
            check_not_null(stats);
            check_equal(stats->frames_parsed, 0);

            ParseErrorInfo *error = parser_context_get_error(ctx);
            check_not_null(error);

            parser_context_destroy(ctx);
        }

        it("should handle thread local context") {
            ParserContext *ctx1 = parser_get_context();
            check_not_null(ctx1);

            ParserContext *ctx2 = parser_get_context();
            check_true(ctx1 == ctx2);  // Same thread, same context
        }
    }
}
