#ifndef SALTS_BUCKET_PRIORITY_QUEUE_SPSC_H
#define SALTS_BUCKET_PRIORITY_QUEUE_SPSC_H

#include "ring_buffer_spsc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BUCKET_PRIORITY_SPSC_LOW = 0,
  BUCKET_PRIORITY_SPSC_NORMAL = 1,
  BUCKET_PRIORITY_SPSC_HIGH = 2,
  BUCKET_PRIORITY_SPSC_CRITICAL = 3,
  BUCKET_PRIORITY_SPSC_COUNT = 4
} bucket_priority_spsc_t;

typedef size_t bucket_priority_spsc_value_t;

typedef struct {
  ring_spsc_t ring;
  uint8_t *storage;
  size_t storage_bytes;
  size_t capacity_entries;
} bucket_priority_bucket_spsc_t;

typedef struct {
  bucket_priority_bucket_spsc_t buckets[BUCKET_PRIORITY_SPSC_COUNT];
} bucket_priority_queue_spsc_t;

/*
 * Initializes a SPSC priority queue.
 * `capacity_per_bucket` is the initial entry capacity for each priority.
 * Must be power of 2. 0 means use the default initial capacity (16).
 *
 * IMPORTANT: This queue does NOT auto-grow at runtime for thread-safety.
 * Ensure capacity_per_bucket is large enough for your workload.
 *
 * THREAD SAFETY:
 * - ONE producer thread calls push operations
 * - ONE consumer thread calls pop/peek operations
 * - Lock-free implementation using ring_buffer_spsc
 */
bool bucket_priority_queue_spsc_init(bucket_priority_queue_spsc_t *queue,
                                               size_t capacity_per_bucket);

/* Releases all memory owned by queue. Safe to call on zero-initialized queue. */
void bucket_priority_queue_spsc_destroy(bucket_priority_queue_spsc_t *queue);

/* Removes all items but keeps allocated memory. NOT thread-safe. */
void bucket_priority_queue_spsc_clear(bucket_priority_queue_spsc_t *queue);

/* Ensures each bucket has at least `capacity_per_bucket` entries of capacity. NOT thread-safe. */
bool bucket_priority_queue_spsc_reserve(bucket_priority_queue_spsc_t *queue,
                                                  size_t capacity_per_bucket);

/* FIFO push in selected priority bucket. Returns false on invalid input or OOM. Thread-safe for producer. */
bool bucket_priority_queue_spsc_push(bucket_priority_queue_spsc_t *queue,
                                               bucket_priority_spsc_t priority,
                                               bucket_priority_spsc_value_t value);

/*
 * Pops highest-priority available item into `out_value`.
 * Returns false when queue is empty or arguments are invalid.
 * Thread-safe for consumer.
 */
bool bucket_priority_queue_spsc_pop(bucket_priority_queue_spsc_t *queue,
                                              bucket_priority_spsc_value_t *out_value);

/*
 * Reads highest-priority available item without removing it.
 * Returns false when queue is empty or arguments are invalid.
 * Thread-safe for consumer.
 */
bool bucket_priority_queue_spsc_peek(const bucket_priority_queue_spsc_t *queue,
                                               bucket_priority_spsc_value_t *out_value);

/*
 * Pops up to `max_items` values into `out_values`.
 * Returns actual popped count.
 * Thread-safe for consumer.
 */
size_t bucket_priority_queue_spsc_pop_batch(bucket_priority_queue_spsc_t *queue,
                                                      size_t max_items,
                                                      bucket_priority_spsc_value_t *out_values);

/* Query helpers - thread-safe */
bool bucket_priority_queue_spsc_empty(const bucket_priority_queue_spsc_t *queue);
size_t bucket_priority_queue_spsc_size(const bucket_priority_queue_spsc_t *queue);
size_t bucket_priority_queue_spsc_size_at(const bucket_priority_queue_spsc_t *queue,
                                                    bucket_priority_spsc_t priority);
size_t bucket_priority_queue_spsc_capacity_at(const bucket_priority_queue_spsc_t *queue,
                                                        bucket_priority_spsc_t priority);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BUCKET_PRIORITY_QUEUE_SPSC_H */
