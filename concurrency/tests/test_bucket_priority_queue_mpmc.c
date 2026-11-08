#include "bucket_priority_queue_mpmc.h"
#include "tinytest.h"
#include <thread.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <stdbool.h>

#define TEST_ITEMS 10000
#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2

typedef struct {
  bucket_priority_queue_mpmc_t *queue;
  _Atomic int start;
  _Atomic int done;
  size_t items_to_process;
  size_t thread_id;
} producer_context_t;

typedef struct {
  bucket_priority_queue_mpmc_t *queue;
  _Atomic int start;
  _Atomic int done;
  _Atomic uint32_t consumed_count;
  size_t target_count;
} consumer_context_t;

static bucket_priority_queue_mpmc_t *test_bucket_priority_queue_create(size_t capacity_per_bucket,
                                                                       uint32_t max_consumers) {
  bucket_priority_queue_mpmc_t *queue =
      (bucket_priority_queue_mpmc_t *)calloc(1, sizeof(*queue));
  if (queue == NULL) {
    return NULL;
  }
  if (!bucket_priority_queue_mpmc_init(queue, capacity_per_bucket, max_consumers)) {
    free(queue);
    return NULL;
  }
  return queue;
}

static void test_bucket_priority_queue_destroy(bucket_priority_queue_mpmc_t *queue) {
  if (queue == NULL) {
    return;
  }
  bucket_priority_queue_mpmc_destroy(queue);
  free(queue);
}

static void producer_thread(void *arg) {
  producer_context_t *ctx = (producer_context_t *)arg;

  while (!(atomic_load(&ctx->start) != 0)) {
    // Wait for start signal
  }

  for (size_t i = 0; i < ctx->items_to_process; ++i) {
    bucket_priority_mpmc_t priority = (bucket_priority_mpmc_t)(i % BUCKET_PRIORITY_MPMC_COUNT);
    size_t value = (ctx->thread_id << 32) | i;  // Encode thread_id in value

    // Retry on failure
    while (!bucket_priority_queue_mpmc_try_push(ctx->queue, priority, value)) {
      // Spin
    }
  }

  atomic_store(&ctx->done, (true) ? 1 : 0);
}

static void consumer_thread(void *arg) {
  consumer_context_t *ctx = (consumer_context_t *)arg;

  while (!(atomic_load(&ctx->start) != 0)) {
    // Wait for start signal
  }

  while (atomic_load(&ctx->consumed_count) < ctx->target_count) {
    bucket_priority_mpmc_value_t value;
    if (bucket_priority_queue_mpmc_try_pop(ctx->queue, &value)) {
      atomic_fetch_add(&ctx->consumed_count, 1);
    }
  }

  atomic_store(&ctx->done, (true) ? 1 : 0);
}

spec("Bucket Priority Queue MPMC") {
  it("starts empty after init") {
    bucket_priority_queue_mpmc_t *queue = test_bucket_priority_queue_create(16, 4);
    check_not_null(queue);
    test_bucket_priority_queue_destroy(queue);
  }

  it("supports single producer single consumer") {
    bucket_priority_queue_mpmc_t *queue = test_bucket_priority_queue_create(16, 1);
    check_not_null(queue);

    // Push items
    check(bucket_priority_queue_mpmc_try_push(queue, BUCKET_PRIORITY_MPMC_LOW, 1));
    check(bucket_priority_queue_mpmc_try_push(queue, BUCKET_PRIORITY_MPMC_CRITICAL, 4));
    check(bucket_priority_queue_mpmc_try_push(queue, BUCKET_PRIORITY_MPMC_NORMAL, 2));
    check(bucket_priority_queue_mpmc_try_push(queue, BUCKET_PRIORITY_MPMC_HIGH, 3));

    // Pop in priority order
    bucket_priority_mpmc_value_t value;
    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 4);  // CRITICAL

    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 3);  // HIGH

    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 2);  // NORMAL

    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 1);  // LOW

    test_bucket_priority_queue_destroy(queue);
  }

  it("handles blocking push") {
    bucket_priority_queue_mpmc_t *queue = test_bucket_priority_queue_create(16, 1);
    check_not_null(queue);

    bucket_priority_queue_mpmc_push_blocking(queue, BUCKET_PRIORITY_MPMC_HIGH, 100);

    bucket_priority_mpmc_value_t value;
    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 100);

    test_bucket_priority_queue_destroy(queue);
  }

  it("handles a blocking push with one slot per bucket") {
    bucket_priority_queue_mpmc_t *queue = test_bucket_priority_queue_create(1, 1);
    bucket_priority_mpmc_value_t value;

    check_not_null(queue);
    bucket_priority_queue_mpmc_push_blocking(queue, BUCKET_PRIORITY_MPMC_HIGH, 101);
    check(bucket_priority_queue_mpmc_try_pop(queue, &value));
    check_equal(value, 101);

    test_bucket_priority_queue_destroy(queue);
  }

  it("works with multiple producers and consumers") {
    bucket_priority_queue_mpmc_t *queue = test_bucket_priority_queue_create(4096, NUM_CONSUMERS);
    check_not_null(queue);

    // Setup producers
    producer_context_t producers[NUM_PRODUCERS];
    cmeta_thread_t producer_threads[NUM_PRODUCERS];

    for (size_t i = 0; i < NUM_PRODUCERS; ++i) {
      producers[i].queue = queue;
      producers[i].start = 0;
      producers[i].done = 0;
      producers[i].items_to_process = TEST_ITEMS / NUM_PRODUCERS;
      producers[i].thread_id = i;

      check(cmeta_thread_create(&producer_threads[i], producer_thread, &producers[i]) == 0);
    }

    // Setup consumers
    consumer_context_t consumers[NUM_CONSUMERS];
    cmeta_thread_t consumer_threads[NUM_CONSUMERS];

    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
      consumers[i].queue = queue;
      consumers[i].start = 0;
      consumers[i].done = 0;
      consumers[i].consumed_count = 0;
      consumers[i].target_count = TEST_ITEMS / NUM_CONSUMERS;

      check(cmeta_thread_create(&consumer_threads[i], consumer_thread, &consumers[i]) == 0);
    }

    // Start all threads
    for (size_t i = 0; i < NUM_PRODUCERS; ++i) {
      atomic_store(&producers[i].start, (true) ? 1 : 0);
    }
    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
      atomic_store(&consumers[i].start, (true) ? 1 : 0);
    }

    // Wait for completion
    for (size_t i = 0; i < NUM_PRODUCERS; ++i) {
      cmeta_thread_join(&producer_threads[i]);
      check((atomic_load(&producers[i].done) != 0));
    }

    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
      cmeta_thread_join(&consumer_threads[i]);
      check((atomic_load(&consumers[i].done) != 0));
    }

    // Verify total consumed
    uint32_t total_consumed = 0;
    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
      total_consumed += atomic_load(&consumers[i].consumed_count);
    }
    check_equal(total_consumed, TEST_ITEMS);

    test_bucket_priority_queue_destroy(queue);
  }
}
