/**
 * Production-Ready Parser Example
 *
 * Demonstrates:
 * 1. Error handling with detailed error codes
 * 2. Streaming parser for network I/O
 * 3. Memory pool for zero-allocation parsing
 * 4. Performance monitoring with statistics
 * 5. Thread-safe parsing with context
 */

#include "crc32.h"
#include "endian.h"
#include "frame.h"
#include "memory_pool.h"
#include "parser_context.h"
#include "parser_error.h"
#include "parser_stats.h"
#include "stream_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <time.h>
#endif

static uint32_t crc_table[256];

// Cross-platform high-resolution timer
static uint64_t get_time_ns(void) {
#ifdef _WIN32
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000000000ULL) / frequency.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)(ts.tv_sec * 1000000000ULL + ts.tv_nsec);
#endif
}

// Helper to create test frame
static size_t create_frame(uint8_t *buf, uint32_t msg_id, const char *payload, size_t payload_len) {
  uint8_t *p = buf;

  *p++ = 0xAA;

  uint32_t msg_id_le = htole32(msg_id);
  memcpy(p, &msg_id_le, 4);
  p += 4;

  *p++ = 0x01;
  *p++ = FRAME_PAYLOAD_TYPE_TEXT;

  uint32_t payload_size_le = htole32((uint32_t)payload_len);
  memcpy(p, &payload_size_le, 4);
  p += 4;

  if (payload && payload_len > 0) {
    memcpy(p, payload, payload_len);
    p += payload_len;
  }

  uint32_t crc = crc32_compute(crc_table, buf, 11 + payload_len);
  uint32_t crc_le = htole32(crc);
  memcpy(p, &crc_le, 4);
  p += 4;

  *p++ = 0x55;

  return (size_t)(p - buf);
}

// Example 1: Basic error handling
void example_error_handling(void) {
  printf("\n=== Example 1: Error Handling ===\n");

  frame_t frame;
  frame.head = 0xFF; // Invalid
  frame.tail = FRAME_TAIL;
  frame.version = FRAME_VERSION;
  frame.payload_type = FRAME_PAYLOAD_TYPE_TEXT;
  frame.payload_size = 100;

  ParseError err = frame_validate(&frame);
  if (err != PARSE_OK) {
    printf("Validation failed: %s\n", parse_error_string(err));
  }

  // Fix and retry
  frame.head = FRAME_HEAD;
  err = frame_validate(&frame);
  printf("After fix: %s\n", parse_error_string(err));
}

// Example 2: Memory pool for high-performance parsing
void example_memory_pool(void) {
  printf("\n=== Example 2: Memory Pool ===\n");

  MemoryPool *pool = pool_create(1024 * 1024); // 1MB pool

  // Parse multiple frames without malloc/free
  for (int i = 0; i < 1000; i++) {
    void *frame_mem = pool_alloc(pool, sizeof(frame_t));
    void *payload_mem = pool_alloc(pool, 100);

    if (!frame_mem || !payload_mem) {
      printf("Pool exhausted at frame %d\n", i);
      break;
    }
  }

  printf("Pool used: %zu bytes\n", pool_get_used(pool));
  printf("Pool peak: %zu bytes\n", pool_get_peak(pool));

  // Reset and reuse
  pool_reset(pool);
  printf("After reset: %zu bytes\n", pool_get_used(pool));

  pool_destroy(pool);
}

// Example 3: Stream parser for network I/O
void example_stream_parser(void) {
  printf("\n=== Example 3: Stream Parser ===\n");

  StreamParser *sp = stream_parser_create(65536);

  // Simulate receiving data in chunks (like from network)
  uint8_t buffer[256];
  const char *messages[] = {"Hello", "World", "From", "Network"};

  for (int i = 0; i < 4; i++) {
    size_t frame_len = create_frame(buffer, i + 1, messages[i], strlen(messages[i]));

    // Simulate partial receives
    size_t chunk1 = frame_len / 2;
    size_t chunk2 = frame_len - chunk1;

    frame_t frame;
    StreamState state;

    // First chunk
    state = stream_parser_feed(sp, buffer, chunk1, &frame);
    printf("Chunk 1: %s\n", state == STREAM_NEED_MORE_DATA ? "Need more data" : "Complete");

    // Second chunk
    state = stream_parser_feed(sp, buffer + chunk1, chunk2, &frame);
    if (state == STREAM_FRAME_COMPLETE) {
      printf("frame_t %u complete: payload_size=%zu\n", frame.msg_id, frame.payload_size);
      if (frame.payload) {
        printf("  Payload: %.*s\n", (int)frame.payload_size, frame.payload);
      }
      frame_free(&frame);
    } else if (state == STREAM_ERROR) {
      ParseErrorInfo *err = stream_parser_get_error(sp);
      printf("Error: %s\n", err->message);
    }
  }

  stream_parser_destroy(sp);
}

// Example 4: Performance monitoring
void example_statistics(void) {
  printf("\n=== Example 4: Performance Statistics ===\n");

  ParserStats stats;
  parser_stats_init(&stats);

  uint8_t buffer[256];

  // Parse 1000 frames and measure performance
  uint64_t start = get_time_ns();

  for (int i = 0; i < 1000; i++) {
    char payload[64];
    snprintf(payload, sizeof(payload), "Message %d", i);
    size_t frame_len = create_frame(buffer, i, payload, strlen(payload));

    uint64_t parse_start = get_time_ns();

    // Simulate parsing (in real code, call actual parser)
    ParseError result = PARSE_OK;

    uint64_t parse_end = get_time_ns();
    uint64_t elapsed = parse_end - parse_start;

    parser_stats_update(&stats, result, frame_len, elapsed);
  }

  uint64_t end = get_time_ns();

  // Report statistics
  parser_stats_report(&stats, stdout);

  uint64_t total_time = end - start;
  printf("Total time: %.2f ms\n", total_time / 1e6);
}

// Example 5: Thread-safe parsing
void example_thread_safe(void) {
  printf("\n=== Example 5: Thread-Safe Context ===\n");

  // Get thread-local context (automatically created)
  ParserContext *ctx = parser_get_context();

  printf("Context created with pool size: %zu bytes\n", pool_get_available(ctx->pool));

  // Use context for parsing
  uint8_t buffer[256];
  size_t frame_len = create_frame(buffer, 1, "Thread-safe", 11);

  // Parse and update stats
  parser_stats_update(&ctx->stats, PARSE_OK, frame_len, 1000);

  // Get statistics
  ParserStats *stats = parser_context_get_stats(ctx);
  printf("frame_ts parsed: %llu\n", (unsigned long long)stats->frames_parsed);

  // Context is automatically cleaned up on thread exit
}

// Example 6: Complete production workflow
void example_production_workflow(void) {
  printf("\n=== Example 6: Production Workflow ===\n");

  // 1. Create stream parser for network I/O
  StreamParser *sp = stream_parser_create(65536);

  // 2. Get thread-local context
  ParserContext *ctx = parser_get_context();

  // 3. Simulate receiving data
  uint8_t buffer[256];
  const char *payload = "Production data";
  size_t frame_len = create_frame(buffer, 100, payload, strlen(payload));

  // 4. Parse with error handling
  frame_t frame;
  StreamState state = stream_parser_feed(sp, buffer, frame_len, &frame);

  if (state == STREAM_FRAME_COMPLETE) {
    // 5. Validate frame
    ParseError err = frame_validate(&frame);
    if (err == PARSE_OK) {
      printf("  frame_t validated successfully\n");
      printf("  msg_id: %u\n", frame.msg_id);
      printf("  payload_size: %zu\n", frame.payload_size);

      // 6. Update statistics
      parser_stats_update(&ctx->stats, PARSE_OK, frame_len, 1000);

      // 7. Process frame
      if (frame.payload) {
        printf("  payload: %.*s\n", (int)frame.payload_size, frame.payload);
      }
      frame_free(&frame);
    } else {
      printf("  Validation failed: %s\n", parse_error_string(err));
      parser_stats_update(&ctx->stats, err, 0, 1000);
    }
  } else if (state == STREAM_ERROR) {
    ParseErrorInfo *err = stream_parser_get_error(sp);
    printf("  Parse error: %s (offset: %zu)\n", err->message, err->offset);
  }

  // 8. Report statistics
  printf("\nFinal statistics:\n");
  parser_stats_report(&ctx->stats, stdout);

  stream_parser_destroy(sp);
}

int main(void) {
  printf("Production-Ready Parser Examples\n");
  printf("=================================\n");

  // Initialize CRC table
  crc32_generate_table(crc_table);

  example_error_handling();
  example_memory_pool();
  example_stream_parser();
  example_statistics();
  example_thread_safe();
  example_production_workflow();

  printf("\n  All examples completed successfully\n");
  return 0;
}
