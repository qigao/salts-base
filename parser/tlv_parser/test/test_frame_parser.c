#include <tinytest.h>
#include "crc32.h"
#include "endian.h"
#include "frame.h"
#include "frame_parser.h"
#include "memory_pool.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

/* Helper: create valid test frame */
static size_t create_test_frame(uint8_t *buf, size_t buf_size, uint8_t head, uint32_t msg_id,
                                uint8_t version, uint8_t payload_type, const char *payload,
                                uint8_t tail) {
    if (buf_size < 16)
        return 0;

    size_t payload_len = payload ? strlen(payload) : 0;
    size_t total_len = 16 + payload_len;

    if (buf_size < total_len)
        return 0;

    uint8_t *p = buf;

    /* head */
    *p++ = head;

    /* msg_id (little-endian) */
    uint32_t msg_id_le = htole32(msg_id);
    memcpy(p, &msg_id_le, 4);
    p += 4;

    /* version */
    *p++ = version;

    /* payload_type */
    *p++ = payload_type;

    /* payload_size (little-endian) */
    uint32_t payload_size_le = htole32((uint32_t)payload_len);
    memcpy(p, &payload_size_le, 4);
    p += 4;

    /* payload */
    if (payload && payload_len > 0) {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }

    /* CRC (over header + payload) */
    uint32_t crc = crc32_compute(crc_table, buf, 11 + payload_len);
    uint32_t crc_le = htole32(crc);
    memcpy(p, &crc_le, 4);
    p += 4;

    /* tail */
    *p++ = tail;

    return total_len;
}

spec("frame_parser") {
    before_each() {
        crc32_generate_table(crc_table);
    }

    describe("peek_size") {
        it("should peek valid size") {
            uint8_t buf[256];
            create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, "Hello", 0x55);

            uint32_t size;
            FrameParseResult result = frame_peek_size(buf, sizeof(buf), &size);

            check_equal(result, FRAME_PARSE_OK);
            check_equal(size, 5); /* "Hello" = 5 bytes */
        }

        it("should return need more if header is partial") {
            uint8_t buf[8]; /* Less than FRAME_HEADER_SIZE */
            memset(buf, 0, sizeof(buf));
            buf[0] = 0xAA;

            uint32_t size;
            FrameParseResult result = frame_peek_size(buf, sizeof(buf), &size);

            check_equal(result, FRAME_PARSE_NEED_MORE);
        }

        it("should return invalid head if first byte is wrong") {
            uint8_t buf[256];
            create_test_frame(buf, sizeof(buf), 0xBB, 1, 1, 1, "Hello", 0x55); /* Wrong head */

            uint32_t size;
            FrameParseResult result = frame_peek_size(buf, sizeof(buf), &size);

            check_equal(result, FRAME_PARSE_INVALID_HEAD);
        }
    }

    describe("frame_parse") {
        it("should parse valid frame") {
            uint8_t buf[256];
            const char *payload = "Hello";
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 42, 1, 1, payload, 0x55);

            frame_t frame;
            FrameParseResult result = frame_parse(buf, frame_len, &frame, FRAME_PARSE_FLAG_NONE);

            check_equal(result, FRAME_PARSE_OK);
            check_equal(frame.head, 0xAA);
            check_equal(frame.msg_id, 42);
            check_equal(frame.version, 1);
            check_equal(frame.payload_type, 1);
            check_equal(frame.payload_size, 5);
            check_not_null(frame.payload);
            check_equal(frame.payload, payload, 5);
            check_equal(frame.tail, 0x55);
            check_equal(frame.payload_owned, 0); /* zero-copy */
        }

        it("should parse frame with empty payload") {
            uint8_t buf[256];
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 0, NULL, 0x55);

            frame_t frame;
            FrameParseResult result = frame_parse(buf, frame_len, &frame, FRAME_PARSE_FLAG_NONE);

            check_equal(result, FRAME_PARSE_OK);
            check_equal(frame.payload_size, 0);
            check_null(frame.payload);
        }

        it("should return invalid tail if last byte is wrong") {
            uint8_t buf[256];
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, "Hi", 0xBB); /* Wrong tail */

            frame_t frame;
            FrameParseResult result = frame_parse(buf, frame_len, &frame, FRAME_PARSE_FLAG_NONE);

            check_equal(result, FRAME_PARSE_INVALID_TAIL);
        }

        it("should return crc mismatch if crc is corrupted") {
            uint8_t buf[256];
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, "Test", 0x55);

            /* Corrupt CRC */
            buf[frame_len - 5] ^= 0xFF;

            frame_t frame;
            FrameParseResult result = frame_parse(buf, frame_len, &frame, FRAME_PARSE_FLAG_NONE);

            check_equal(result, FRAME_PARSE_CRC_MISMATCH);
        }

        it("should skip crc if flag is set") {
            uint8_t buf[256];
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, "Test", 0x55);

            /* Corrupt CRC */
            buf[frame_len - 5] ^= 0xFF;

            frame_t frame;
            FrameParseResult result = frame_parse(buf, frame_len, &frame, FRAME_PARSE_FLAG_SKIP_CRC);

            check_equal(result, FRAME_PARSE_OK); /* Should pass with skip flag */
        }

        it("should return need more if data is partial") {
            uint8_t buf[256];
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, "Hello", 0x55);

            frame_t frame;
            /* Only provide partial data */
            FrameParseResult result = frame_parse(buf, frame_len - 5, &frame, FRAME_PARSE_FLAG_NONE);

            check_equal(result, FRAME_PARSE_NEED_MORE);
        }
    }

    describe("frame_parse_copy") {
        it("should copy to heap if no pool provided") {
            uint8_t buf[256];
            const char *payload = "Copied";
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, payload, 0x55);

            frame_t frame;
            FrameParseResult result = frame_parse_copy(buf, frame_len, &frame, NULL);

            check_equal(result, FRAME_PARSE_OK);
            check_not_null(frame.payload);
            check_equal(frame.payload_owned, 1); /* heap-allocated */
            check_equal(frame.payload, payload, strlen(payload));

            frame_free(&frame);
        }

        it("should copy to pool if provided") {
            uint8_t buf[256];
            const char *payload = "Pooled";
            size_t frame_len = create_test_frame(buf, sizeof(buf), 0xAA, 1, 1, 1, payload, 0x55);

            MemoryPool *pool = pool_create(1024);
            check_not_null(pool);

            frame_t frame;
            FrameParseResult result = frame_parse_copy(buf, frame_len, &frame, pool);

            check_equal(result, FRAME_PARSE_OK);
            check_not_null(frame.payload);
            check_equal(frame.payload_owned, 0); /* pool-allocated */
            check_true(frame.payload_pool == pool);
            check_equal(frame.payload, payload, strlen(payload));

            frame_free(&frame);
            pool_destroy(pool);
        }
    }

    describe("utilities") {
        it("should calculate total size") {
            check_equal(frame_total_size(0), 16);
            check_equal(frame_total_size(5), 21);
            check_equal(frame_total_size(100), 116);
        }

        it("should free heap payload") {
            frame_t frame;
            memset(&frame, 0, sizeof(frame));
            frame.payload = malloc(10);
            frame.payload_owned = 1;
            frame.payload_size = 10;

            frame_free(&frame);

            check_null(frame.payload);
            check_equal(frame.payload_size, 0);
        }

        it("should free pool payload") {
            MemoryPool *pool = pool_create(64);
            check_not_null(pool);

            frame_t frame;
            memset(&frame, 0, sizeof(frame));
            frame.payload_pool = pool;
            frame.payload_pool_offset = pool_mark(pool);
            frame.payload = pool_alloc(pool, 32);
            frame.payload_size = 32;

            check_not_null(frame.payload);
            check(pool_get_used(pool) > 0);

            frame_free(&frame);

            check_null(frame.payload);
            check_equal(frame.payload_size, 0);
            check_equal(pool_get_used(pool), 0);

            pool_destroy(pool);
        }
    }

    describe("crc") {
        it("should generate non-zero table") {
            uint32_t table[256];
            crc32_generate_table(table);

            int non_zero_count = 0;
            for (int i = 0; i < 256; i++) {
                if (table[i] != 0)
                    non_zero_count++;
            }

            check(non_zero_count > 200);
        }

        it("should be deterministic") {
            const char *data = "Hello, World!";
            uint32_t crc1 = crc32_compute(crc_table, data, strlen(data));
            uint32_t crc2 = crc32_compute(crc_table, data, strlen(data));

            check_equal(crc1, crc2);
        }

        it("should produce different values for different data") {
            const char *data1 = "Hello, World!";
            const char *data2 = "Hello, World?";

            uint32_t crc1 = crc32_compute(crc_table, data1, strlen(data1));
            uint32_t crc2 = crc32_compute(crc_table, data2, strlen(data2));

            check(crc1 != crc2);
        }
    }

    describe("validation") {
        it("should validate valid frame") {
            frame_t frame = {
                .head = FRAME_HEAD,
                .tail = FRAME_TAIL,
                .version = FRAME_VERSION,
                .payload_type = FRAME_PAYLOAD_TYPE_TEXT,
                .payload_size = 10
            };

            check_equal(frame_validate(&frame), PARSE_OK);
        }

        it("should fail on bad head") {
            frame_t frame = {
                .head = 0xBB,
                .tail = FRAME_TAIL,
                .version = FRAME_VERSION,
                .payload_type = FRAME_PAYLOAD_TYPE_TEXT
            };

            check_equal(frame_validate(&frame), PARSE_ERR_INVALID_HEAD);
        }

        it("should fail on bad tail") {
            frame_t frame = {
                .head = FRAME_HEAD,
                .tail = 0xBB,
                .version = FRAME_VERSION,
                .payload_type = FRAME_PAYLOAD_TYPE_TEXT
            };

            check_equal(frame_validate(&frame), PARSE_ERR_INVALID_TAIL);
        }
    }

    describe("error conversion") {
        it("should convert parse results to errors correctly") {
            check_equal(frame_parse_result_to_error(FRAME_PARSE_OK), PARSE_OK);
            check_equal(frame_parse_result_to_error(FRAME_PARSE_NEED_MORE), PARSE_ERR_TRUNCATED);
            check_equal(frame_parse_result_to_error(FRAME_PARSE_INVALID_HEAD), PARSE_ERR_INVALID_HEAD);
            check_equal(frame_parse_result_to_error(FRAME_PARSE_INVALID_TAIL), PARSE_ERR_INVALID_TAIL);
            check_equal(frame_parse_result_to_error(FRAME_PARSE_CRC_MISMATCH), PARSE_ERR_CRC_MISMATCH);
        }
    }
}
