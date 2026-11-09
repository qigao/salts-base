#ifndef SALTS_DEADLINE_QUEUE_H
#define SALTS_DEADLINE_QUEUE_H

#include <error_codes.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cmeta_deadline_id;

typedef struct cmeta_deadline_event {
  cmeta_deadline_id id;
  uint64_t deadline_ms;
  uint64_t token;
} cmeta_deadline_event;

typedef struct cmeta_deadline_queue {
  void *impl;
} cmeta_deadline_queue;

/**
 * Initializes fixed-capacity single-owner storage for absolute millisecond
 * deadlines. The caller supplies one monotonic clock domain consistently.
 * This type provides ordering only: it starts no thread, reads no clock, and
 * invokes no callback. Init, schedule, cancel, take, and destroy must all run
 * on the same owner thread after external synchronization has quiesced users.
 */
int cmeta_deadline_queue_init(cmeta_deadline_queue *queue, size_t capacity);

/**
 * Adds one absolute millisecond deadline. Equal deadlines preserve successful
 * schedule order. The returned id is unique only for the live queue instance.
 * Full storage returns SALTS_ENOBUFS. Failure clears out_id; token is copied
 * integer metadata and never transfers resource ownership.
 */
int cmeta_deadline_queue_schedule(cmeta_deadline_queue *queue, uint64_t deadline_ms, uint64_t token,
                                  cmeta_deadline_id *out_id);

/** Removes one live generation-checked id and returns its copied event. O(log n). */
int cmeta_deadline_queue_cancel(cmeta_deadline_queue *queue, cmeta_deadline_id id,
                                cmeta_deadline_event *out_event);

/** Copies the earliest event without removing it; empty returns SALTS_ETIMEDOUT. O(1). */
int cmeta_deadline_queue_peek(const cmeta_deadline_queue *queue, cmeta_deadline_event *out_event);

/** Removes the earliest due event; no event due returns SALTS_ETIMEDOUT. O(log n). */
int cmeta_deadline_queue_take_ready(cmeta_deadline_queue *queue, uint64_t now_ms,
                                    cmeta_deadline_event *out_event);

size_t cmeta_deadline_queue_size(const cmeta_deadline_queue *queue);

/** Destroys storage and invalidates every id; pending integer events are discarded. */
int cmeta_deadline_queue_destroy(cmeta_deadline_queue *queue);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_DEADLINE_QUEUE_H */
