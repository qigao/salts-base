#include "bucket_priority_queue_spsc.h"
#include "tinytest.h"
#include <thread.h>
#include <stdatomic.h>

#include <stdbool.h>

#define TEST_ITEMS 10000

typedef struct {
  bucket_priority_queue_spsc_t *queue;
  _Atomic int start;
  _Atomic int done;
  size_t items_to_process;
} thread_context_t;

static void producer_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;

  while (!(atomic_load(&ctx->start) != 0)) {
    // Wait for start signal
  }

  for (size_t i = 0; i < ctx->items_to_process; ++i) {
    bucket_priority_spsc_t priority = (bucket_priority_spsc_t)(i % BUCKET_PRIORITY_SPSC_COUNT);

    // Retry on failure (queue full or wrapping)
    while (!bucket_priority_queue_spsc_push(ctx->queue, priority, i)) {
      // Spin - consumer will make space
    }
  }

  atomic_store(&ctx->done, (true) ? 1 : 0);
}

static void consumer_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  size_t consumed = 0;

  while (!(atomic_load(&ctx->start) != 0)) {
    // Wait for start signal
  }

  while (consumed < ctx->items_to_process) {
    bucket_priority_spsc_value_t value;
    if (bucket_priority_queue_spsc_pop(ctx->queue, &value)) {
      consumed++;
    }
  }

  atomic_store(&ctx->done, (true) ? 1 : 0);
}

spec("Bucket Priority Queue SPSC") {
  it("starts empty after init") {
    bucket_priority_queue_spsc_t queue;
    check(bucket_priority_queue_spsc_init(&queue, 0));
    check(bucket_priority_queue_spsc_empty(&queue));
    check_equal(bucket_priority_queue_spsc_size(&queue), 0);
    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("allows empty reads after init with default capacity") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t value = 123;
    bucket_priority_spsc_value_t out[4] = {0};

    check(bucket_priority_queue_spsc_init(&queue, 0));
    check(!bucket_priority_queue_spsc_pop(&queue, &value));
    check(!bucket_priority_queue_spsc_peek(&queue, &value));
    check_equal(bucket_priority_queue_spsc_pop_batch(&queue, 4, out), 0);
    check(bucket_priority_queue_spsc_empty(&queue));
    check_equal(bucket_priority_queue_spsc_size(&queue), 0);

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("is FIFO within the same priority") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t value = 0;

    check(bucket_priority_queue_spsc_init(&queue, 16));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 10));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 11));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 12));

    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 10);
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 11);
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 12);
    check(!bucket_priority_queue_spsc_pop(&queue, &value));

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("pops in priority order (CRITICAL > HIGH > NORMAL > LOW)") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t value = 0;

    check(bucket_priority_queue_spsc_init(&queue, 16));

    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_LOW, 1));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_CRITICAL, 4));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_NORMAL, 2));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 3));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_LOW, 5));

    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 4);  // CRITICAL
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 3);  // HIGH
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 2);  // NORMAL
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 1);  // LOW (first)
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 5);  // LOW (second)

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("peek returns highest priority without removing") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t value = 0;

    check(bucket_priority_queue_spsc_init(&queue, 16));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_LOW, 1));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 100));

    check(bucket_priority_queue_spsc_peek(&queue, &value));
    check_equal(value, 100);
    check_equal(bucket_priority_queue_spsc_size(&queue), 2);  // Still 2 items

    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 100);

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("pop_batch respects priority order") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t out[10];

    check(bucket_priority_queue_spsc_init(&queue, 16));

    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_LOW, 1));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 10));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 11));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_CRITICAL, 20));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_NORMAL, 5));

    size_t n = bucket_priority_queue_spsc_pop_batch(&queue, 10, out);
    check_equal(n, 5);
    check_equal(out[0], 20);  // CRITICAL
    check_equal(out[1], 10);  // HIGH (first)
    check_equal(out[2], 11);  // HIGH (second)
    check_equal(out[3], 5);   // NORMAL
    check_equal(out[4], 1);   // LOW

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("respects capacity limits (no auto-grow in SPSC)") {
    bucket_priority_queue_spsc_t queue;
    bucket_priority_spsc_value_t value = 0;

    // Init with small capacity (but min is 16 due to BUCKET_MIN_CAPACITY)
    check(bucket_priority_queue_spsc_init(&queue, 4));

    // Ring buffer with 16 entries can hold 15 items (needs 1 empty slot)
    for (size_t i = 0; i < 15; ++i) {
      check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_NORMAL, i));
    }

    // Next push should fail (queue full)
    check(!bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_NORMAL, 999));

    // Pop one item to make space
    check(bucket_priority_queue_spsc_pop(&queue, &value));
    check_equal(value, 0);

    // Now push should succeed
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_NORMAL, 100));

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("handles clear operation") {
    bucket_priority_queue_spsc_t queue;

    check(bucket_priority_queue_spsc_init(&queue, 16));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_HIGH, 1));
    check(bucket_priority_queue_spsc_push(&queue, BUCKET_PRIORITY_SPSC_LOW, 2));
    check_equal(bucket_priority_queue_spsc_size(&queue), 2);

    bucket_priority_queue_spsc_clear(&queue);
    check(bucket_priority_queue_spsc_empty(&queue));
    check_equal(bucket_priority_queue_spsc_size(&queue), 0);

    bucket_priority_queue_spsc_destroy(&queue);
  }

  it("works with single producer single consumer threads") {
    bucket_priority_queue_spsc_t queue;
    // 10000 items / 4 priorities = ~2500 per bucket, use 4096 for safety
    check(bucket_priority_queue_spsc_init(&queue, 4096));

    thread_context_t producer_ctx = {
      .queue = &queue,
      .start = 0,
      .done = 0,
      .items_to_process = TEST_ITEMS
    };

    thread_context_t consumer_ctx = {
      .queue = &queue,
      .start = 0,
      .done = 0,
      .items_to_process = TEST_ITEMS
    };

    cmeta_thread_t producer, consumer;
    check(cmeta_thread_create(&producer, producer_thread, &producer_ctx) == 0);
    check(cmeta_thread_create(&consumer, consumer_thread, &consumer_ctx) == 0);

    // Start both threads
    atomic_store(&producer_ctx.start, (true) ? 1 : 0);
    atomic_store(&consumer_ctx.start, (true) ? 1 : 0);

    // Wait for completion
    cmeta_thread_join(&producer);
    cmeta_thread_join(&consumer);

    check((atomic_load(&producer_ctx.done) != 0));
    check((atomic_load(&consumer_ctx.done) != 0));
    check(bucket_priority_queue_spsc_empty(&queue));

    bucket_priority_queue_spsc_destroy(&queue);
  }
}
