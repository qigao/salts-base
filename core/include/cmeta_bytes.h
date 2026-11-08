#ifndef SALTS_BYTES_H
#define SALTS_BYTES_H

#include "cmeta_api.h"
#include <error_codes.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Borrowed contiguous byte view.
 *
 * A view returned by cmeta_bytes_view() remains valid only until the
 * buffer is appended to, consumed, reset, or destroyed. It must not cross a
 * thread, callback, or coroutine suspension boundary unless the caller can
 * prove that the owning buffer cannot be mutated during that interval.
 */
typedef struct cmeta_bytes_view_s {
  const uint8_t *data;
  size_t size;
} cmeta_bytes_view_t;

/**
 * @brief Bounded, single-owner accumulator for stream-oriented binary data.
 *
 * The object owns one contiguous allocation and is not thread-safe. Keep all
 * operations on the same owner thread or event loop. Its allocation never
 * exceeds max_bytes; reset retains the allocation for reuse.
 *
 * The fields are public so the type can be embedded in caller-owned objects,
 * but callers must treat them as read-only implementation state.
 *
 * @code
 * static int consume_input(const void *input, size_t input_size) {
 *   cmeta_bytes_t buffer = cmeta_bytes_INIT;
 *   cmeta_bytes_view_t bytes;
 *   int rc = cmeta_bytes_init(&buffer, 64 * 1024);
 *   if (rc == SALTS_OK) rc = cmeta_bytes_append(&buffer, input, input_size);
 *   if (rc == SALTS_OK) rc = cmeta_bytes_view(&buffer, &bytes);
 *   if (rc == SALTS_OK) rc = cmeta_bytes_consume(&buffer, bytes.size);
 *   cmeta_bytes_destroy(&buffer);
 *   return rc;
 * }
 * @endcode
 */
typedef struct cmeta_bytes_s {
  uint8_t *data;
  size_t read_pos;
  size_t write_pos;
  size_t capacity;
  size_t max_bytes;
} cmeta_bytes_t;

#define cmeta_bytes_INIT {NULL, 0u, 0u, 0u, 0u}

/**
 * @brief Initialize an uninitialized buffer without allocating storage.
 * @param buffer Caller-owned buffer object.
 * @param max_bytes Hard limit for both unread bytes and allocation capacity.
 * @return SALTS_OK, or SALTS_EINVAL for a NULL buffer or zero limit.
 *
 * Destroy an initialized buffer before initializing it again.
 */
SALTS_C_API int cmeta_bytes_init(cmeta_bytes_t *buffer, size_t max_bytes);

/**
 * @brief Release storage and clear the object.
 * @param buffer Initialized buffer, or NULL.
 */
SALTS_C_API void cmeta_bytes_destroy(cmeta_bytes_t *buffer);

/**
 * @brief Append an entire byte range.
 * @param buffer Initialized buffer.
 * @param data Source bytes; NULL is allowed only when size is zero.
 * @param size Number of bytes to append.
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ENOSPC, or SALTS_ENOMEM.
 *
 * Failure leaves all buffered bytes and cursors unchanged. A source range may
 * refer to the buffer's current unread view; other overlap with its allocation
 * is rejected. Time is amortized O(size), with occasional O(unread_size)
 * compaction or growth.
 */
SALTS_C_API int cmeta_bytes_append(cmeta_bytes_t *buffer, const void *data, size_t size);

/**
 * @brief Return a borrowed view of all unread bytes.
 * @param buffer Initialized buffer.
 * @param out Receives {NULL, 0} when the buffer is empty.
 * @return SALTS_OK or SALTS_EINVAL. On failure, out is unchanged.
 * @complexity Time O(1), space O(1).
 */
SALTS_C_API int cmeta_bytes_view(const cmeta_bytes_t *buffer,
                                     cmeta_bytes_view_t *out);

/**
 * @brief Remove bytes from the front of the unread range.
 * @param buffer Initialized buffer.
 * @param size Number of bytes to consume.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_ERANGE.
 *
 * Over-consumption leaves the buffer unchanged. This operation never moves or
 * allocates memory; compaction is deferred until a later append needs space.
 * @complexity Time O(1), space O(1).
 */
SALTS_C_API int cmeta_bytes_consume(cmeta_bytes_t *buffer, size_t size);

/**
 * @brief Discard unread bytes while retaining allocation capacity.
 * @param buffer Initialized buffer. Invalid or NULL objects are ignored.
 * @complexity Time O(1), space O(1).
 */
SALTS_C_API void cmeta_bytes_reset(cmeta_bytes_t *buffer);

/** @brief Return unread byte count, or zero for an invalid object. */
SALTS_C_API size_t cmeta_bytes_size(const cmeta_bytes_t *buffer);

/** @brief Return remaining logical quota, or zero for an invalid object. */
SALTS_C_API size_t cmeta_bytes_available(const cmeta_bytes_t *buffer);

/** @brief Return allocated byte capacity, or zero for an invalid object. */
SALTS_C_API size_t cmeta_bytes_capacity(const cmeta_bytes_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BYTES_H */
