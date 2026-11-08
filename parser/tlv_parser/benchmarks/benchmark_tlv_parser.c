/**
 * @file benchmark_tlv_parser.c
 * @brief Performance benchmark for TLV Parser (frame parser)
 *
 * Tests fixed-format frame parsing performance:
 * - Parse complete frame (zero-copy)
 * - Build frame
 * - Round-trip (build + parse)
 * - Stream parser (chunked data)
 */

#include "crc32.h"
#include "endian.h"
#include "frame.h"
#include "frame_parser.h"
#include "memory_pool.h"
#include "stream_parser.h"
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

/**
 * @brief Helper to build a complete frame
 */
static size_t build_frame(uint8_t *buf, uint32_t msg_id, const char *payload, size_t payload_len) {
  uint8_t *p = buf;

  /* HEAD */
  *p++ = FRAME_HEAD;

  /* MSG_ID (little-endian) */
  uint32_t msg_id_le = htole32(msg_id);
  memcpy(p, &msg_id_le, 4);
  p += 4;

  /* VERSION */
  *p++ = FRAME_VERSION;

  /* PAYLOAD_TYPE */
  *p++ = FRAME_PAYLOAD_TYPE_TEXT;

  /* PAYLOAD_SIZE (little-endian) */
  uint32_t payload_size_le = htole32((uint32_t)payload_len);
  memcpy(p, &payload_size_le, 4);
  p += 4;

  /* PAYLOAD */
  if (payload && payload_len > 0) {
    memcpy(p, payload, payload_len);
    p += payload_len;
  }

  /* CRC32 (little-endian) */
  uint32_t crc = crc32_compute(crc_table, buf, 11 + payload_len);
  uint32_t crc_le = htole32(crc);
  memcpy(p, &crc_le, 4);
  p += 4;

  /* TAIL */
  *p++ = FRAME_TAIL;

  return (size_t)(p - buf);
}
suite("benchmark") {
  static uint8_t frame_buf[256];
  static size_t frame_len;
  static const char *payload = "AAPL";
  static size_t payload_len = 4;

  before_each() {
    /* Initialize CRC table */
    crc32_generate_table(crc_table);

    /* Pre-build frame for parse benchmarks */
    frame_len = build_frame(frame_buf, 12345, payload, payload_len);
  }

  after_each() { /* Cleanup if needed */ }
  /* Benchmark suite */
  bench("TLV Parser Performance") {
      benchmark_titles("benchmark", "input", "iters", "avg(us)", NULL, "min(us)", "max(us)", "ops/s", NULL, NULL);

    /* Frame Parser benchmarks */
    benchmark("TLV Parser: parse frame (4 bytes payload)", 100000, 1) {
      frame_t frame;
      FrameParseResult result = frame_parse(frame_buf, frame_len, &frame, FRAME_PARSE_FLAG_NONE);
      check(result == FRAME_PARSE_OK);
      /* Note: frame.payload points to frame_buf, no need to free */
    }

    benchmark("TLV Parser: build frame (4 bytes payload)", 100000, 1) {
      uint8_t buf[256];
      size_t len = build_frame(buf, 12345, payload, payload_len);
      check_greater(len, 0);
    }

    benchmark("TLV Parser: round-trip frame", 50000, 1) {
      /* Build */
      uint8_t buf[256];
      size_t len = build_frame(buf, 12345, payload, payload_len);

      /* Parse */
      frame_t frame;
      FrameParseResult result = frame_parse(buf, len, &frame, FRAME_PARSE_FLAG_NONE);
      check(result == FRAME_PARSE_OK);
    }

    benchmark("TLV Parser: stream parser (chunked)", 50000, 1) {
      StreamParser *sp = stream_parser_create(1024);

      /* Simulate chunked receive (split frame in half) */
      size_t chunk1 = frame_len / 2;
      size_t chunk2 = frame_len - chunk1;

      frame_t frame;
      StreamState state;

      /* First chunk */
      state = stream_parser_feed(sp, frame_buf, chunk1, &frame);
      check(state == STREAM_NEED_MORE_DATA);

      /* Second chunk */
      state = stream_parser_feed(sp, frame_buf + chunk1, chunk2, &frame);
      check(state == STREAM_FRAME_COMPLETE);

      frame_free(&frame);
      stream_parser_destroy(sp);
    }

    benchmark("TLV Parser: parse with CRC skip", 100000, 1) {
      frame_t frame;
      FrameParseResult result =
          frame_parse(frame_buf, frame_len, &frame, FRAME_PARSE_FLAG_SKIP_CRC);
      check(result == FRAME_PARSE_OK);
    }

    benchmark("TLV Parser: parse large payload (1KB)", 10000, 1) {
      /* Build large frame */
      uint8_t large_buf[2048];
      char large_payload[1024];
      memset(large_payload, 'A', sizeof(large_payload));
      size_t large_len = build_frame(large_buf, 99999, large_payload, sizeof(large_payload));

      /* Parse */
      frame_t frame;
      FrameParseResult result = frame_parse(large_buf, large_len, &frame, FRAME_PARSE_FLAG_NONE);
      check(result == FRAME_PARSE_OK);
    }
  }
}
