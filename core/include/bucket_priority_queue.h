#ifndef SALTS_BUCKET_PRIORITY_QUEUE_H
#define SALTS_BUCKET_PRIORITY_QUEUE_H

#include "cmeta_api.h"
#include "ring_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BUCKET_PRIORITY_LOW = 0,
  BUCKET_PRIORITY_NORMAL = 1,
  BUCKET_PRIORITY_HIGH = 2,
  BUCKET_PRIORITY_CRITICAL = 3,
  BUCKET_PRIORITY_COUNT = 4
} bucket_priority_t;

typedef size_t bucket_priority_value_t;

typedef struct {
  ring_data_type ring;
  uint8_t *storage;
  size_t storage_bytes;
  size_t capacity_entries;
  size_t count;
} bucket_priority_bucket_t;

typedef struct {
  bucket_priority_bucket_t buckets[BUCKET_PRIORITY_COUNT];
  size_t total_size;
  uint8_t non_empty_mask;
} bucket_priority_queue_t;

/*
 * Initializes a priority queue.
 * `capacity_per_bucket` is the initial entry capacity for each priority.
 * 0 means lazy allocation on first push.
 */
SALTS_C_API bool bucket_priority_queue_init(bucket_priority_queue_t *queue,
                                          size_t capacity_per_bucket);

/* Releases all memory owned by queue. Safe to call on zero-initialized queue. */
SALTS_C_API void bucket_priority_queue_destroy(bucket_priority_queue_t *queue);

/* Removes all items but keeps allocated memory. */
SALTS_C_API void bucket_priority_queue_clear(bucket_priority_queue_t *queue);

/* Ensures each bucket has at least `capacity_per_bucket` entries of capacity. */
SALTS_C_API bool bucket_priority_queue_reserve(bucket_priority_queue_t *queue,
                                             size_t capacity_per_bucket);

/* FIFO push in selected priority bucket. Returns false on invalid input or OOM. */
SALTS_C_API bool bucket_priority_queue_push(bucket_priority_queue_t *queue,
                                          bucket_priority_t priority,
                                          bucket_priority_value_t value);

/*
 * Pops highest-priority available item into `out_value`.
 * Returns false when queue is empty or arguments are invalid.
 */
SALTS_C_API bool bucket_priority_queue_pop(bucket_priority_queue_t *queue,
                                         bucket_priority_value_t *out_value);

/*
 * Reads highest-priority available item without removing it.
 * Returns false when queue is empty or arguments are invalid.
 */
SALTS_C_API bool bucket_priority_queue_peek(const bucket_priority_queue_t *queue,
                                          bucket_priority_value_t *out_value);

/*
 * Pops up to `max_items` values into `out_values`.
 * Returns actual popped count.
 */
SALTS_C_API size_t bucket_priority_queue_pop_batch(bucket_priority_queue_t *queue,
                                                 size_t max_items,
                                                 bucket_priority_value_t *out_values);

/* Query helpers */
SALTS_C_API bool bucket_priority_queue_empty(const bucket_priority_queue_t *queue);
SALTS_C_API size_t bucket_priority_queue_size(const bucket_priority_queue_t *queue);
SALTS_C_API size_t bucket_priority_queue_size_at(const bucket_priority_queue_t *queue,
                                               bucket_priority_t priority);
SALTS_C_API size_t bucket_priority_queue_capacity_at(const bucket_priority_queue_t *queue,
                                                   bucket_priority_t priority);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BUCKET_PRIORITY_QUEUE_H */
