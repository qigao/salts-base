#ifndef SALTS_SPSC_RING_H
#define SALTS_SPSC_RING_H

#ifdef __cplusplus
  #include <atomic>
  #define SALTS_SPSC_ATOMIC_SIZE_T std::atomic<size_t>
#else
  #include <stdalign.h>
  #include <stdatomic.h>
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
typedef struct cmeta_spsc_ring {
  size_t size;
  size_t mask;
  uint8_t *data;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T write_pos;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T read_pos;
  alignas(64) SALTS_SPSC_ATOMIC_SIZE_T wrap_pos;
  SALTS_SPSC_ATOMIC_SIZE_T wrap_len;
  size_t pending_wrap_len;
} cmeta_spsc_ring;

bool cmeta_spsc_ring_init(cmeta_spsc_ring *ring, uint8_t *storage, size_t size);
uint8_t *cmeta_spsc_ring_write_acquire(cmeta_spsc_ring *ring, size_t required);
void cmeta_spsc_ring_write_release(cmeta_spsc_ring *ring, size_t written);
uint8_t *cmeta_spsc_ring_read_acquire(cmeta_spsc_ring *ring, size_t *available);
void cmeta_spsc_ring_read_release(cmeta_spsc_ring *ring, size_t read);
size_t cmeta_spsc_ring_write_available(const cmeta_spsc_ring *ring);
size_t cmeta_spsc_ring_read_available(const cmeta_spsc_ring *ring);

#ifdef __cplusplus
}
#endif

#undef SALTS_SPSC_ATOMIC_SIZE_T

#endif /* SALTS_SPSC_RING_H */
