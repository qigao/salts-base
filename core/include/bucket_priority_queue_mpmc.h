#ifndef SALTS_BUCKET_PRIORITY_QUEUE_MPMC_H
#define SALTS_BUCKET_PRIORITY_QUEUE_MPMC_H

#include <disruptor.h>
#include <thread.h>
#ifdef __cplusplus
  #include <atomic>
  #define ATOMIC_UINT32_T std::atomic<uint32_t>
#else
  #include <stdatomic.h>
  #define ATOMIC_UINT32_T _Atomic uint32_t
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BUCKET_PRIORITY_MPMC_LOW = 0,
  BUCKET_PRIORITY_MPMC_NORMAL = 1,
  BUCKET_PRIORITY_MPMC_HIGH = 2,
  BUCKET_PRIORITY_MPMC_CRITICAL = 3,
  BUCKET_PRIORITY_MPMC_COUNT = 4
} bucket_priority_mpmc_t;

typedef size_t bucket_priority_mpmc_value_t;
typedef struct bucket_priority_queue_mpmc_impl_s bucket_priority_queue_mpmc_impl_t;

typedef struct {
  bucket_priority_queue_mpmc_impl_t *impl;
} bucket_priority_queue_mpmc_t;

/*
 * Initializes a MPMC priority queue.
 * `capacity_per_bucket` is the entry capacity for each priority (must be power of 2).
 * `max_consumers` is unused internally now but kept for API compat.
 *
 * THREAD SAFETY:
 * - Multiple producer threads can call push operations
 * - Multiple consumer threads can call pop operations
 */
bool bucket_priority_queue_mpmc_init(bucket_priority_queue_mpmc_t *queue,
                                               size_t capacity_per_bucket,
                                               uint32_t max_consumers);

/* Releases all memory owned by queue. Safe to call on zero-initialized queue. */
void bucket_priority_queue_mpmc_destroy(bucket_priority_queue_mpmc_t *queue);

/*
 * Try to push (non-blocking). Returns false if all queues are full.
 * Thread-safe for multiple producers.
 */
bool bucket_priority_queue_mpmc_try_push(bucket_priority_queue_mpmc_t *queue,
                                                   bucket_priority_mpmc_t priority,
                                                   bucket_priority_mpmc_value_t value);

/*
 * Push (blocking). Waits if queue is full.
 * Thread-safe for multiple producers.
 */
void bucket_priority_queue_mpmc_push_blocking(bucket_priority_queue_mpmc_t *queue,
                                                        bucket_priority_mpmc_t priority,
                                                        bucket_priority_mpmc_value_t value);

/*
 * Try to pop highest-priority item (non-blocking).
 * Returns false if all queues are empty.
 * Thread-safe for multiple consumers.
 */
bool bucket_priority_queue_mpmc_try_pop(
    bucket_priority_queue_mpmc_t *queue,
    bucket_priority_mpmc_value_t *out_value);

/*
 * Pop highest-priority item (blocking). Waits if all queues are empty.
 * Thread-safe for multiple consumers.
 */
bool bucket_priority_queue_mpmc_pop_blocking(
    bucket_priority_queue_mpmc_t *queue,
    bucket_priority_mpmc_value_t *out_value,
    uint32_t timeout_ms);

/* Query helpers - approximate values in MPMC scenario */
bool bucket_priority_queue_mpmc_empty(const bucket_priority_queue_mpmc_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BUCKET_PRIORITY_QUEUE_MPMC_H */
