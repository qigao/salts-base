#ifndef SALTS_BYTES_H
#define SALTS_BYTES_H

#include <core/api.h>
#include <core/error_codes.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-owning counted byte view. The caller retains the underlying storage.
 * data/len layout is intended for ABI-preserving migration from legacy vstr,
 * but vstr compatibility has NOT yet been certified or exported. */
typedef struct core_bytes_view {
  const char *data;
  size_t len;
} core_bytes_view;

enum {
  SALTS_CORE_OK = 0,
  SALTS_CORE_EINVAL = -1
};

/* Commit output only after validation. NULL input is valid for zero length. */
SALTS_API int core_bytes_view_init(core_bytes_view *out,
                                   const void *data, size_t length);

/* Borrowed subview. start/length are byte offsets, not Unicode scalars. */
SALTS_API int core_bytes_view_slice(core_bytes_view view,
                                    size_t start, size_t length,
                                    core_bytes_view *out);

/**
 * @brief Borrowed contiguous byte view.
 *
 * A view returned by bytes_view() remains valid only until the
 * buffer is appended to, consumed, reset, or destroyed. It must not cross a
 * thread, callback, or coroutine suspension boundary unless the caller can
 * prove that the owning buffer cannot be mutated during that interval.
 */
typedef struct bytes_view_s {
  const uint8_t *data;
  size_t size;
} bytes_view_t;

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
 *   bytes_t buffer = bytes_INIT;
 *   bytes_view_t bytes;
 *   int rc = bytes_init(&buffer, 64 * 1024);
 *   if (rc == SALTS_OK) rc = bytes_append(&buffer, input, input_size);
 *   if (rc == SALTS_OK) rc = bytes_view(&buffer, &bytes);
 *   if (rc == SALTS_OK) rc = bytes_consume(&buffer, bytes.size);
 *   bytes_destroy(&buffer);
 *   return rc;
 * }
 * @endcode
 */
typedef struct bytes_s {
  uint8_t *data;
  size_t read_pos;
  size_t write_pos;
  size_t capacity;
  size_t max_bytes;
} bytes_t;

#define bytes_INIT {NULL, 0u, 0u, 0u, 0u}

/**
 * @brief Initialize an uninitialized buffer without allocating storage.
 * @param buffer Caller-owned buffer object.
 * @param max_bytes Hard limit for both unread bytes and allocation capacity.
 * @return SALTS_OK, or SALTS_EINVAL for a NULL buffer or zero limit.
 *
 * Destroy an initialized buffer before initializing it again.
 */
SALTS_C_API int bytes_init(bytes_t *buffer, size_t max_bytes);

/**
 * @brief Release storage and clear the object.
 * @param buffer Initialized buffer, or NULL.
 */
SALTS_C_API void bytes_destroy(bytes_t *buffer);

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
SALTS_C_API int bytes_append(bytes_t *buffer, const void *data, size_t size);

/**
 * @brief Return a borrowed view of all unread bytes.
 * @param buffer Initialized buffer.
 * @param out Receives {NULL, 0} when the buffer is empty.
 * @return SALTS_OK or SALTS_EINVAL. On failure, out is unchanged.
 * @complexity Time O(1), space O(1).
 */
SALTS_C_API int bytes_view(const bytes_t *buffer,
                                     bytes_view_t *out);

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
SALTS_C_API int bytes_consume(bytes_t *buffer, size_t size);

/**
 * @brief Discard unread bytes while retaining allocation capacity.
 * @param buffer Initialized buffer. Invalid or NULL objects are ignored.
 * @complexity Time O(1), space O(1).
 */
SALTS_C_API void bytes_reset(bytes_t *buffer);

/** @brief Return unread byte count, or zero for an invalid object. */
SALTS_C_API size_t bytes_size(const bytes_t *buffer);

/** @brief Return remaining logical quota, or zero for an invalid object. */
SALTS_C_API size_t bytes_available(const bytes_t *buffer);

/** @brief Return allocated byte capacity, or zero for an invalid object. */
SALTS_C_API size_t bytes_capacity(const bytes_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BYTES_H */
