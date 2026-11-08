#include "bucket_priority_queue.h"
#include "tinytest.h"

#include <stdbool.h>

spec("Bucket Priority Queue (C11 + FIFO)") {
  it("starts empty after init") {
    bucket_priority_queue_t queue;
    check(bucket_priority_queue_init(&queue, 0));
    check(bucket_priority_queue_empty(&queue));
    check_equal(bucket_priority_queue_size(&queue), 0);
    bucket_priority_queue_destroy(&queue);
  }

  it("is FIFO within the same priority") {
    bucket_priority_queue_t queue;
    bucket_priority_value_t value = 0;

    check(bucket_priority_queue_init(&queue, 2));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 10));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 11));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 12));

    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 10);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 11);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 12);
    check(!bucket_priority_queue_pop(&queue, &value));

    bucket_priority_queue_destroy(&queue);
  }

  it("pops in priority order and keeps FIFO per bucket") {
    bucket_priority_queue_t queue;
    bucket_priority_value_t value = 0;

    check(bucket_priority_queue_init(&queue, 1));

    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_LOW, 1));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 100));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 101));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_CRITICAL, 200));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_NORMAL, 50));

    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 200);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 100);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 101);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 50);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 1);

    bucket_priority_queue_destroy(&queue);
  }

  it("pop_batch keeps global priority order and per-bucket FIFO") {
    bucket_priority_queue_t queue;
    bucket_priority_value_t out[8] = {0};
    size_t n = 0;

    check(bucket_priority_queue_init(&queue, 1));

    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_LOW, 1));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 10));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 11));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_CRITICAL, 20));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_NORMAL, 5));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_LOW, 2));

    n = bucket_priority_queue_pop_batch(&queue, 6, out);
    check_equal(n, 6);
    check_equal(out[0], 20);
    check_equal(out[1], 10);
    check_equal(out[2], 11);
    check_equal(out[3], 5);
    check_equal(out[4], 1);
    check_equal(out[5], 2);
    check(bucket_priority_queue_empty(&queue));

    bucket_priority_queue_destroy(&queue);
  }

  it("auto-grows while keeping FIFO") {
    bucket_priority_queue_t queue;
    bucket_priority_value_t value = 0;
    size_t i = 0;

    check(bucket_priority_queue_init(&queue, 1));
    for (i = 0; i < 128; ++i) {
      check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_NORMAL, i));
    }
    check_equal(bucket_priority_queue_size_at(&queue, BUCKET_PRIORITY_NORMAL), 128);

    for (i = 0; i < 128; ++i) {
      check(bucket_priority_queue_pop(&queue, &value));
      check_equal(value, i);
    }
    check(bucket_priority_queue_empty(&queue));

    bucket_priority_queue_destroy(&queue);
  }

  it("peek does not remove item") {
    bucket_priority_queue_t queue;
    bucket_priority_value_t value = 0;

    check(bucket_priority_queue_init(&queue, 0));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_LOW, 3));
    check(bucket_priority_queue_push(&queue, BUCKET_PRIORITY_HIGH, 9));

    check(bucket_priority_queue_peek(&queue, &value));
    check_equal(value, 9);
    check_equal(bucket_priority_queue_size(&queue), 2);

    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 9);
    check(bucket_priority_queue_pop(&queue, &value));
    check_equal(value, 3);

    bucket_priority_queue_destroy(&queue);
  }

  it("rejects invalid priority") {
    bucket_priority_queue_t queue;
    check(bucket_priority_queue_init(&queue, 0));
    check(!bucket_priority_queue_push(&queue, (bucket_priority_t)99, 1));
    bucket_priority_queue_destroy(&queue);
  }
}
