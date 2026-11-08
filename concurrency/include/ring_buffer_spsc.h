/************************** INCLUDE ***************************/
#ifndef RING_BUFFER_SPSC_H
#define RING_BUFFER_SPSC_H

#include <spsc_ring.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************** TYPES ****************************/

/**
 * @brief Single-Producer Single-Consumer (SPSC) thread-safe ring buffer
 *
 * THREAD SAFETY:
 * - ONE producer thread calls ring_spsc_write_acquire/release
 * - ONE consumer thread calls ring_spsc_read_acquire/release
 * - Lock-free implementation using atomic operations
 * - Memory barriers ensure proper visibility between threads
 *
 * PERFORMANCE:
 * - Faster than MPMC (disruptor) due to no CAS loops
 * - Slower than single-threaded ring_buffer due to atomic operations
 *
 * DESIGN PHILOSOPHY (Linus-style "good taste"):
 * - No wrapped flags (state follows operation, not buffer)
 * - No invalidate index (simplified algorithm)
 * - Cache-line alignment to prevent false sharing
 */
typedef cmeta_spsc_ring ring_spsc_t;

/******************** FUNCTION PROTOTYPES *********************/

/**
 * @brief Initialize SPSC ring buffer
 * @param inst Instance pointer
 * @param data_array Data array pointer (must remain valid)
 * @param size Size of data array (MUST be power of 2)
 * @return true on success, false if size is not power of 2
 */
bool ring_spsc_init(ring_spsc_t *inst, uint8_t *data_array, size_t size);

/**
 * @brief Acquire space for writing
 * @param inst Instance pointer
 * @param size_required Bytes required
 * @return Pointer to buffer, or NULL if not enough space
 *
 * NOTE: Returns contiguous space only. If wrapping is needed, returns NULL.
 * Call again after consumer reads to get space from beginning.
 */
uint8_t *ring_spsc_write_acquire(ring_spsc_t *inst, size_t size_required);

/**
 * @brief Release write operation
 * @param inst Instance pointer
 * @param bytes_written Actual bytes written
 *
 * NOTE: Data becomes visible to consumer only after release
 */
void ring_spsc_write_release(ring_spsc_t *inst, size_t bytes_written);

/**
 * @brief Acquire data for reading
 * @param inst Instance pointer
 * @param available Output: bytes available
 * @return Pointer to data, or NULL if buffer empty
 *
 * NOTE: Returns contiguous data only. May need multiple calls to read all data.
 */
uint8_t *ring_spsc_read_acquire(ring_spsc_t *inst, size_t *available);

/**
 * @brief Release read operation
 * @param inst Instance pointer
 * @param bytes_read Actual bytes consumed
 */
void ring_spsc_read_release(ring_spsc_t *inst, size_t bytes_read);

/**
 * @brief Get available space for writing
 * @param inst Instance pointer
 * @return Available bytes
 */
size_t ring_spsc_write_available(const ring_spsc_t *inst);

/**
 * @brief Get available data for reading
 * @param inst Instance pointer
 * @return Available bytes
 */
size_t ring_spsc_read_available(const ring_spsc_t *inst);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_SPSC_H */
