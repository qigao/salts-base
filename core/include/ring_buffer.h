/************************** INCLUDE ***************************/
#ifndef ring_H
#define ring_H
#include <cmeta_api.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef __cplusplus
  #include <stdbool.h>
#else
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*************************** TYPES ****************************/

/**
 * @brief Single-threaded ring buffer (bipartite buffer)
 *
 * THREAD SAFETY: NONE
 * - This implementation is for SINGLE-THREADED use only
 * - NO atomic operations, NO memory barriers
 * - Maximum performance for non-threaded environments
 *
 * For thread-safe versions, use:
 * - ring_buffer_spsc.h for single-producer single-consumer
 * - disruptor.h for multi-producer multi-consumer
 */
typedef struct {
  size_t size;   /**< Size of the data array */
  uint8_t *data; /**< Pointer to the data array */
  size_t r;      /**< Read index */
  size_t w;      /**< Write index */
  size_t i;      /**< Invalidated space index */
  bool write_wrapped; /**< Write wrapped flag */
  bool read_wrapped;  /**< Read wrapped flag */
} ring_data_type;

/******************** FUNCTION PROTOTYPES *********************/

/**
 * @brief Initializes a bipartite buffer instance
 * @param[in] Instance pointer
 * @param[in] Data array pointer
 * @param[in] Size of data array
 * @retval None
 */
SALTS_C_API void ring_init(ring_data_type *inst, uint8_t *data_array, size_t size);

/**
 * @brief Acquires a linear region in the bipartite buffer for writing
 * @param[in] Instance pointer
 * @param[in] Free linear space in the buffer required
 * @retval Pointer to the beginning of the linear space
 */
SALTS_C_API uint8_t *ring_write_acquire(ring_data_type *inst, size_t free_required);

/**
 * @brief Releases the bipartite buffer after a write
 * @param[in] Instance pointer
 * @param[in] Bytes written to the linear space
 * @retval None
 */
SALTS_C_API void ring_write_release(ring_data_type *inst, size_t written);

/**
 * @brief Acquires a linear region in the bipartite buffer for reading
 * @param[in] Instance pointer
 * @param[out] Available linear data in the buffer
 * @retval Pointer to the beginning of the data
 */
SALTS_C_API uint8_t *ring_read_acquire(ring_data_type *inst, size_t *available);

/**
 * @brief Releases the bipartite buffer after a read
 * @param[in] Instance pointer
 * @param[in] Bytes read from the linear region
 * @retval None
 */
SALTS_C_API void ring_read_release(ring_data_type *inst, size_t read);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ring_H */
