#ifndef SALTS_SPSC_RING_H
#define SALTS_SPSC_RING_H

#include <core/api.h>

#ifdef __cplusplus
  #include <atomic>
  #define SALTS_SPSC_ATOMIC_SIZE_T std::atomic<size_t>
#else
  #include <stdalign.h>
  #include <atomic/atomic.h>
  #define SALTS_SPSC_ATOMIC_SIZE_T _Atomic size_t
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A borrowed-storage byte ring for exactly one producer and one consumer.
 * The producer owns write acquire/release; the consumer owns read
 * acquire/release. Control-plane reset or storage replacement requires both
 * roles to be quiescent.
 */
typedef struct spsc_ring {
  size_t size;
  size_t mask;
  uint8_t *data;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T write_pos;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T read_pos;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T wrap_pos;
  SALTS_SPSC_ATOMIC_SIZE_T wrap_len;
  size_t pending_wrap_len;
} spsc_ring;

SALTS_API bool spsc_ring_init(spsc_ring *ring, uint8_t *storage, size_t size);
SALTS_API uint8_t *spsc_ring_write_acquire(spsc_ring *ring, size_t required);
SALTS_API void spsc_ring_write_release(spsc_ring *ring, size_t written);
SALTS_API uint8_t *spsc_ring_read_acquire(spsc_ring *ring, size_t *available);
SALTS_API void spsc_ring_read_release(spsc_ring *ring, size_t read);
SALTS_API size_t spsc_ring_write_available(const spsc_ring *ring);
SALTS_API size_t spsc_ring_read_available(const spsc_ring *ring);

#ifdef __cplusplus
}
#endif

#undef SALTS_SPSC_ATOMIC_SIZE_T

#endif /* SALTS_SPSC_RING_H */
