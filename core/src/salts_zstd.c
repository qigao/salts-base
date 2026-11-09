#include "salts_zstd.h"

#include "salts_error.h"

#include <stdint.h>
#include <zstd.h>
#include <zstd_errors.h>

static int salts_zstd_error(size_t result) {
  ZSTD_ErrorCode code = ZSTD_getErrorCode(result);

  switch (code) {
  case ZSTD_error_parameter_unsupported:
  case ZSTD_error_parameter_combination_unsupported:
  case ZSTD_error_parameter_outOfBound:
  case ZSTD_error_dstBuffer_null:
  case ZSTD_error_srcBuffer_wrong:
    return SALTS_EINVAL;
  case ZSTD_error_memory_allocation:
    return SALTS_ENOMEM;
  case ZSTD_error_workSpace_tooSmall:
  case ZSTD_error_dstSize_tooSmall:
  case ZSTD_error_noForwardProgress_destFull:
    return SALTS_ENOSPC;
  case ZSTD_error_prefix_unknown:
  case ZSTD_error_version_unsupported:
  case ZSTD_error_frameParameter_unsupported:
  case ZSTD_error_frameParameter_windowTooLarge:
  case ZSTD_error_corruption_detected:
  case ZSTD_error_checksum_wrong:
  case ZSTD_error_literals_headerWrong:
  case ZSTD_error_dictionary_corrupted:
  case ZSTD_error_dictionary_wrong:
  case ZSTD_error_dictionaryCreation_failed:
  case ZSTD_error_srcSize_wrong:
  case ZSTD_error_noForwardProgress_inputEmpty:
    return SALTS_EPROTO;
  case ZSTD_error_no_error:
    return SALTS_OK;
  default:
    return SALTS_EIO;
  }
}

int salts_zstd_compress_bound(size_t source_size, size_t *out_bound) {
  size_t bound;

  if (out_bound == NULL) {
    return SALTS_EINVAL;
  }

  bound = ZSTD_compressBound(source_size);
  if (ZSTD_isError(bound)) {
    if (ZSTD_getErrorCode(bound) == ZSTD_error_srcSize_wrong) {
      return SALTS_ERANGE;
    }
    return salts_zstd_error(bound);
  }

  *out_bound = bound;
  return SALTS_OK;
}

int salts_zstd_compress(void *destination, size_t destination_capacity,
                        size_t *out_size, const void *source,
                        size_t source_size, int level) {
  static const unsigned char empty_source = 0U;
  size_t result;

  if (destination == NULL || out_size == NULL ||
      (source == NULL && source_size != 0U)) {
    return SALTS_EINVAL;
  }
  if (level != 0 &&
      (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel())) {
    return SALTS_EINVAL;
  }

  result = ZSTD_compress(destination, destination_capacity,
                         source != NULL ? source : &empty_source, source_size,
                         level);
  if (ZSTD_isError(result)) {
    return salts_zstd_error(result);
  }

  *out_size = result;
  return SALTS_OK;
}

int salts_zstd_decompress(void *destination, size_t destination_capacity,
                          size_t *out_size, const void *source,
                          size_t source_size) {
  unsigned char empty_destination;
  size_t result;

  if (out_size == NULL || source == NULL || source_size == 0U ||
      (destination == NULL && destination_capacity != 0U)) {
    return SALTS_EINVAL;
  }

  result = ZSTD_decompress(destination != NULL ? destination : &empty_destination,
                           destination_capacity, source, source_size);
  if (ZSTD_isError(result)) {
    return salts_zstd_error(result);
  }

  *out_size = result;
  return SALTS_OK;
}

int salts_zstd_frame_content_size(const void *source, size_t source_size,
                                  size_t *out_size) {
  unsigned long long content_size;

  if (source == NULL || source_size == 0U || out_size == NULL) {
    return SALTS_EINVAL;
  }

  content_size = ZSTD_getFrameContentSize(source, source_size);
  if (content_size == ZSTD_CONTENTSIZE_ERROR) {
    return SALTS_EPROTO;
  }
  if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return SALTS_ENOTSUP;
  }
  if (content_size > (unsigned long long)SIZE_MAX) {
    return SALTS_ERANGE;
  }

  *out_size = (size_t)content_size;
  return SALTS_OK;
}

int salts_zstd_default_level(void) { return ZSTD_defaultCLevel(); }

int salts_zstd_min_level(void) { return ZSTD_minCLevel(); }

int salts_zstd_max_level(void) { return ZSTD_maxCLevel(); }
