#ifndef SALTS_ZSTD_H
#define SALTS_ZSTD_H

#include "cmeta_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the maximum compressed size for a source buffer.
 *
 * @param source_size Number of source bytes.
 * @param out_bound Receives the required destination capacity on success.
 * @return SALTS_OK on success, otherwise a SALTS_* error code.
 *
 * @note The function is thread-safe and does not allocate memory. On failure,
 *       @p out_bound is unchanged.
 */
SALTS_C_API int cmeta_zstd_compress_bound(size_t source_size, size_t *out_bound);

/**
 * @brief Compress one complete zstd frame into caller-owned storage.
 *
 * A level of 0 selects the zstd default. Other values must be between
 * cmeta_zstd_min_level() and cmeta_zstd_max_level(), inclusive. A NULL source
 * is valid only when @p source_size is zero.
 *
 * @param destination Destination buffer owned by the caller.
 * @param destination_capacity Available destination bytes.
 * @param out_size Receives the compressed byte count on success.
 * @param source Source bytes.
 * @param source_size Number of source bytes.
 * @param level Compression level, or 0 for the default.
 * @return SALTS_OK on success, SALTS_ENOSPC if the destination is too small,
 *         or another SALTS_* error code.
 *
 * @note The function is thread-safe. On failure, @p out_size is unchanged.
 */
SALTS_C_API int cmeta_zstd_compress(void *destination, size_t destination_capacity,
                                  size_t *out_size, const void *source,
                                  size_t source_size, int level);

/**
 * @brief Decompress one complete zstd frame into caller-owned storage.
 *
 * A NULL destination is valid only when @p destination_capacity is zero, which
 * permits decoding a frame whose uncompressed content is empty. A NULL source
 * is never valid because a complete frame is required.
 *
 * @param destination Destination buffer owned by the caller.
 * @param destination_capacity Available destination bytes.
 * @param out_size Receives the decompressed byte count on success.
 * @param source Complete zstd frame.
 * @param source_size Compressed frame size.
 * @return SALTS_OK on success, SALTS_ENOSPC if the destination is too small,
 *         SALTS_EPROTO for malformed or unsupported frames, or another
 *         SALTS_* error code.
 *
 * @note The function is thread-safe. On failure, @p out_size is unchanged.
 */
SALTS_C_API int cmeta_zstd_decompress(void *destination, size_t destination_capacity,
                                    size_t *out_size, const void *source,
                                    size_t source_size);

/**
 * @brief Read the declared uncompressed size from a complete zstd frame.
 *
 * @param source Complete zstd frame.
 * @param source_size Compressed frame size.
 * @param out_size Receives the declared content size on success.
 * @return SALTS_OK on success, SALTS_ENOTSUP when the frame does not declare a
 *         content size, SALTS_ERANGE when it cannot fit in size_t, or another
 *         SALTS_* error code.
 *
 * @note On failure, @p out_size is unchanged.
 */
SALTS_C_API int cmeta_zstd_frame_content_size(const void *source, size_t source_size,
                                            size_t *out_size);

/** @return The vendored zstd default compression level. */
SALTS_C_API int cmeta_zstd_default_level(void);

/** @return The minimum compression level accepted by cmeta_zstd_compress(). */
SALTS_C_API int cmeta_zstd_min_level(void);

/** @return The maximum compression level accepted by cmeta_zstd_compress(). */
SALTS_C_API int cmeta_zstd_max_level(void);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_ZSTD_H */
