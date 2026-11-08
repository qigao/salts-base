/**
 * LTV Parser Tests
 * Tests for Length-Type-Value parser with varint encoding
 */

#include "ltv_parser.h"
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>

spec("ltv_parser") {
  describe("Varint Encoding and Decoding") {
    it("should encode and decode small values (0-127) within one byte") {
        uint8_t buf[5];
        uint32_t out;

        /* Single byte values (0-127) */
        for (uint32_t i = 0; i <= 127; i++) {
            int encoded = ltv_encode_varint(i, buf);
            check_equal(encoded, 1);

            int decoded = ltv_decode_varint(buf, sizeof(buf), &out);
            check_equal(decoded, 1);
            check_equal(out, i);
        }
    }

    it("should encode and decode the value 128 using two bytes") {
        uint8_t buf[5];
        uint32_t out;

        int encoded = ltv_encode_varint(128, buf);
        check_equal(encoded, 2);
        check_equal(buf[0], 0x80);
        check_equal(buf[1], 0x01);

        int decoded = ltv_decode_varint(buf, sizeof(buf), &out);
        check_equal(decoded, 2);
        check_equal(out, 128);
    }

    it("should encode and decode large values correctly") {
        uint8_t buf[5];
        uint32_t out;
        uint32_t test_values[] = {255, 256, 16383, 16384, 2097151, 268435455, 0xFFFFFFFF};

        for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
            int encoded = ltv_encode_varint(test_values[i], buf);
            check(encoded > 0);
            check(encoded <= 5);

            int decoded = ltv_decode_varint(buf, sizeof(buf), &out);
            check_equal(decoded, encoded);
            check_equal(out, test_values[i]);
        }
    }

    it("should report the correct size for various varint values") {
        check_equal(ltv_varint_size(0), 1);
        check_equal(ltv_varint_size(127), 1);
        check_equal(ltv_varint_size(128), 2);
        check_equal(ltv_varint_size(16383), 2);
        check_equal(ltv_varint_size(16384), 3);
        check_equal(ltv_varint_size(0xFFFFFFFF), 5);
    }

    it("should return 0 when decoding an incomplete varint") {
        uint8_t buf[] = {0x80};  /* Incomplete: continuation bit set */
        uint32_t out;

        int decoded = ltv_decode_varint(buf, 1, &out);
        check_equal(decoded, 0);  /* Need more data */
    }

    it("should reject overlong and overflowing varints without changing output") {
        const uint8_t overlong[] = {0x81, 0x00};
        const uint8_t high_bits[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
        const uint8_t unterminated[] = {0x80, 0x80, 0x80, 0x80, 0x80};
        uint32_t out = 1234;

        check_equal(ltv_decode_varint(overlong, sizeof(overlong), &out), -1);
        check_equal(out, 1234);
        check_equal(ltv_decode_varint(high_bits, sizeof(high_bits), &out), -1);
        check_equal(out, 1234);
        check_equal(ltv_decode_varint(unterminated, sizeof(unterminated), &out), -1);
        check_equal(out, 1234);
    }

    it("should distinguish an incomplete four-byte prefix from an invalid fifth byte") {
        const uint8_t incomplete[] = {0x80, 0x80, 0x80, 0x80};
        const uint8_t maximum[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
        uint32_t out = 0;

        check_equal(ltv_decode_varint(incomplete, sizeof(incomplete), &out), 0);
        check_equal(ltv_decode_varint(maximum, sizeof(maximum), &out), 5);
        check_equal(out, UINT32_MAX);
    }
  }

  describe("Message Parsing") {
    it("should parse a simple LTV message correctly") {
        /* Length=6 (1 type + 5 value), Type=0x01, Value="Hello" */
        uint8_t buf[] = {0x06, 0x01, 'H', 'e', 'l', 'l', 'o'};

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.length, 6);
        check_equal(msg.type, 0x01);
        check_equal(msg.value_size, 5);
        check(memcmp(msg.value, "Hello", 5) == 0);
        check_equal(msg.consumed, 7);
    }

    it("should handle messages with empty values") {
        /* Length=1 (type only, no value), Type=0x02 */
        uint8_t buf[] = {0x01, 0x02};

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.length, 1);
        check_equal(msg.type, 0x02);
        check_equal(msg.value_size, 0);
        check_null(msg.value);
        check_equal(msg.consumed, 2);
    }

    it("should parse messages with large multi-byte lengths") {
        /* Length=300 (encoded as 2 bytes: 0xAC 0x02) */
        uint8_t buf[302];
        buf[0] = 0xAC;
        buf[1] = 0x02;
        buf[2] = 0x42;  /* Type */
        memset(buf + 3, 'X', 299);

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.length, 300);
        check_equal(msg.type, 0x42);
        check_equal(msg.value_size, 299);
        check_equal(msg.consumed, 302);
    }

    it("should signal need for more data when header is incomplete") {
        uint8_t buf[] = {0x80};  /* Incomplete varint */

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_NEED_MORE);
    }

    it("should signal need for more data when value is truncated") {
        /* Length=10, but only 5 bytes provided */
        uint8_t buf[] = {0x0A, 0x01, 'H', 'e', 'l'};

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_NEED_MORE);
    }

    it("should report invalid varint for zero length messages if disallowed") {
        uint8_t buf[] = {0x00};  /* Length=0 is invalid */

        ltv_message_t msg;
        LtvParseResult result = ltv_parse(buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_INVALID_VARINT);
    }
  }

  describe("Message Building") {
    it("should build a simple LTV message correctly") {
        uint8_t buf[32];
        const uint8_t value[] = {'H', 'e', 'l', 'l', 'o'};

        size_t written = ltv_build(0x01, value, 5, buf, sizeof(buf));

        check_equal(written, 7);
        check_equal(buf[0], 0x06);  /* Length=6 */
        check_equal(buf[1], 0x01);  /* Type */
        check(memcmp(buf + 2, "Hello", 5) == 0);
    }

    it("should build an LTV message with an empty value") {
        uint8_t buf[32];

        size_t written = ltv_build(0x42, NULL, 0, buf, sizeof(buf));

        check_equal(written, 2);
        check_equal(buf[0], 0x01);  /* Length=1 */
        check_equal(buf[1], 0x42);  /* Type */
    }

    it("should return 0 when the output buffer is too small") {
        uint8_t buf[2];
        const uint8_t value[] = {'H', 'e', 'l', 'l', 'o'};

        size_t written = ltv_build(0x01, value, 5, buf, sizeof(buf));

        check_equal(written, 0);
    }

    it("should report the correct wire size for a given value length") {
        check_equal(ltv_wire_size(0), 2);    /* 1 byte length + 1 type */
        check_equal(ltv_wire_size(5), 7);    /* 1 + 1 + 5 */
        check_equal(ltv_wire_size(127), 130); /* 1 + 1 + 127 */
        check_equal(ltv_wire_size(128), 131); /* 2 + 1 + 128 */
    }

    it("should reject invalid builders and oversized values without partial output") {
        uint8_t buf[8];
        const uint8_t value = 0x42;
        memset(buf, 0xA5, sizeof(buf));

        check_equal(ltv_wire_size((size_t)LTV_MAX_PAYLOAD_SIZE + 1), 0);
        check_equal(ltv_wire_size(SIZE_MAX), 0);
        check_equal(ltv_build(1, NULL, 1, buf, sizeof(buf)), 0);
        check_equal(ltv_build(1, &value, 1, NULL, sizeof(buf)), 0);
        check_equal(ltv_build(1, &value, (size_t)LTV_MAX_PAYLOAD_SIZE + 1,
                                buf, sizeof(buf)), 0);
        check_equal(buf[0], 0xA5);
        check_equal(ltv_encode_varint(1, NULL), 0);
    }
  }

  describe("Streaming Parser") {
    it("should successfully parse a complete message fed at once") {
        ltv_stream_t *stream = ltv_stream_create(1024);
        check_not_null(stream);

        uint8_t buf[] = {0x06, 0x01, 'H', 'e', 'l', 'l', 'o'};
        ltv_message_t msg;

        LtvParseResult result = ltv_stream_feed(stream, buf, sizeof(buf), &msg);

        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.type, 0x01);
        check_equal(msg.value_size, 5);

        ltv_stream_destroy(stream);
    }

    it("should successfully parse a message fed in multiple chunks") {
        ltv_stream_t *stream = ltv_stream_create(1024);
        check_not_null(stream);

        /* Message: Length=6, Type=0x01, Value="Hello" */
        uint8_t part1[] = {0x06, 0x01, 'H'};
        uint8_t part2[] = {'e', 'l'};
        uint8_t part3[] = {'l', 'o'};

        ltv_message_t msg;

        LtvParseResult result = ltv_stream_feed(stream, part1, sizeof(part1), &msg);
        check_equal(result, LTV_PARSE_NEED_MORE);

        result = ltv_stream_feed(stream, part2, sizeof(part2), &msg);
        check_equal(result, LTV_PARSE_NEED_MORE);

        result = ltv_stream_feed(stream, part3, sizeof(part3), &msg);
        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.type, 0x01);
        check_equal(msg.value_size, 5);

        ltv_stream_destroy(stream);
    }

    it("should handle multiple messages in sequence") {
        ltv_stream_t *stream = ltv_stream_create(1024);
        check_not_null(stream);

        /* Two messages back-to-back */
        uint8_t buf[] = {
            0x03, 0x01, 'H', 'i',           /* Msg 1: Length=3, Type=1, "Hi" */
            0x03, 0x02, 'O', 'k'            /* Msg 2: Length=3, Type=2, "Ok" */
        };

        ltv_message_t msg;

        LtvParseResult result = ltv_stream_feed(stream, buf, sizeof(buf), &msg);
        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.type, 0x01);
        check_equal(msg.value_size, 2);
        check_equal(msg.value, "Hi", 2);

        const uint8_t *remaining = NULL;
        check_equal(ltv_stream_remaining(stream, &remaining), 4);
        check_equal(remaining, buf + 4, 4);
        check_equal(msg.value, "Hi", 2);

        /* Feed NULL to continue parsing remaining data */
        result = ltv_stream_feed(stream, NULL, 0, &msg);
        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.type, 0x02);
        check_equal(msg.value_size, 2);
        check_equal(msg.value, "Ok", 2);

        ltv_stream_destroy(stream);
    }

    it("should reclaim completed storage before appending the next message") {
        ltv_stream_t *stream = ltv_stream_create(4);
        const uint8_t first[] = {0x03, 0x01, 'H', 'i'};
        const uint8_t second[] = {0x03, 0x02, 'O', 'k'};
        ltv_message_t msg;

        check_not_null(stream);
        check_equal(ltv_stream_feed(stream, first, sizeof(first), &msg), LTV_PARSE_OK);
        check_equal(msg.value, "Hi", 2);
        check_equal(ltv_stream_feed(stream, second, sizeof(second), &msg), LTV_PARSE_OK);
        check_equal(msg.type, 0x02);
        check_equal(msg.value, "Ok", 2);

        ltv_stream_destroy(stream);
    }

    it("should reject a nonzero feed length without input bytes") {
        ltv_stream_t *stream = ltv_stream_create(16);
        ltv_message_t msg;

        check_not_null(stream);
        check_equal(ltv_stream_feed(stream, NULL, 1, &msg), LTV_PARSE_INVALID_VARINT);

        ltv_stream_destroy(stream);
    }

    it("should allow resetting the stream state") {
        ltv_stream_t *stream = ltv_stream_create(1024);
        check_not_null(stream);

        uint8_t partial[] = {0x80, 0x01};  /* Incomplete */
        ltv_message_t msg;

        LtvParseResult result = ltv_stream_feed(stream, partial, sizeof(partial), &msg);
        check_equal(result, LTV_PARSE_NEED_MORE);

        ltv_stream_reset(stream);

        /* After reset, should accept new message from start */
        uint8_t complete[] = {0x03, 0x42, 'A', 'B'};
        result = ltv_stream_feed(stream, complete, sizeof(complete), &msg);
        check_equal(result, LTV_PARSE_OK);
        check_equal(msg.type, 0x42);

        ltv_stream_destroy(stream);
    }
  }

  describe("Utilities") {
    it("should return valid strings for all parse results") {
        check_not_null(ltv_parse_result_string(LTV_PARSE_OK));
        check_not_null(ltv_parse_result_string(LTV_PARSE_NEED_MORE));
        check_not_null(ltv_parse_result_string(LTV_PARSE_INVALID_VARINT));
        check_not_null(ltv_parse_result_string(LTV_PARSE_SIZE_OVERFLOW));
        check_not_null(ltv_parse_result_string(LTV_PARSE_BUFFER_OVERFLOW));
    }

    it("should peek the correct size and header length for short messages") {
        uint8_t buf[] = {0x06, 0x01, 'H', 'e', 'l', 'l', 'o'};
        uint32_t length;
        size_t header;

        LtvParseResult result = ltv_peek_size(buf, sizeof(buf), &length, &header);

        check_equal(result, LTV_PARSE_OK);
        check_equal(length, 6);
        check_equal(header, 1);
    }

    it("should peek the correct size and header length for messages with multi-byte lengths") {
        uint8_t buf[] = {0xAC, 0x02, 0x42};  /* Length=300 */
        uint32_t length;
        size_t header;

        LtvParseResult result = ltv_peek_size(buf, sizeof(buf), &length, &header);

        check_equal(result, LTV_PARSE_OK);
        check_equal(length, 300);
        check_equal(header, 2);
    }
  }
}
