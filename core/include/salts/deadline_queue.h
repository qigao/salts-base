#ifndef SALTS_DEADLINE_QUEUE_H
#define SALTS_DEADLINE_QUEUE_H

#include <salts/api.h>

#include <salts/error_codes.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t deadline_id;

typedef struct deadline_event {
  deadline_id id;
  uint64_t deadline_ms;
  uint64_t token;
} deadline_event;

typedef struct deadline_queue {
  void *impl;
} deadline_queue;

/**
 * Initializes fixed-capacity single-owner storage for absolute millisecond
 * deadlines. The caller supplies one monotonic clock domain consistently.
 * This type provides ordering only: it starts no thread, reads no clock, and
 * invokes no callback. Init, schedule, cancel, take, and destroy must all run
 * on the same owner thread after external synchronization has quiesced users.
 */
SALTS_API int deadline_queue_init(deadline_queue *queue, size_t capacity);

/**
 * Adds one absolute millisecond deadline. Equal deadlines preserve successful
 * schedule order. The returned id is unique only for the live queue instance.
 * Full storage returns SALTS_ENOBUFS. Failure clears out_id; token is copied
 * integer metadata and never transfers resource ownership.
 */
SALTS_API int deadline_queue_schedule(deadline_queue *queue, uint64_t deadline_ms, uint64_t token,
                                  deadline_id *out_id);

/** Removes one live generation-checked id and returns its copied event. O(log n). */
SALTS_API int deadline_queue_cancel(deadline_queue *queue, deadline_id id,
                                deadline_event *out_event);

/** Copies the earliest event without removing it; empty returns SALTS_ETIMEDOUT. O(1). */
SALTS_API int deadline_queue_peek(const deadline_queue *queue, deadline_event *out_event);

/** Removes the earliest due event; no event due returns SALTS_ETIMEDOUT. O(log n). */
SALTS_API int deadline_queue_take_ready(deadline_queue *queue, uint64_t now_ms,
                                    deadline_event *out_event);

SALTS_API size_t deadline_queue_size(const deadline_queue *queue);

/** Destroys storage and invalidates every id; pending integer events are discarded. */
SALTS_API int deadline_queue_destroy(deadline_queue *queue);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_DEADLINE_QUEUE_H */
