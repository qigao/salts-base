/* Canonical Base64 C11 ABI, originally owned by Salts utils.
 * Restored to the single SaltsCore native owner after Salts 3.0 extraction.
 * No other repository should compile or export a Base64 encoder/decoder.
 */
#ifndef SALTSCORE_BASE64_UTILS_H
#define SALTSCORE_BASE64_UTILS_H

#include <cmeta_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TN_BASE64_OK = 0,
  TN_BASE64_ERR_INVALID_ARG = -1,
  TN_BASE64_ERR_NO_MEMORY = -2,
  TN_BASE64_ERR_BUFFER_TOO_SMALL = -3,
  TN_BASE64_ERR_INVALID_INPUT = -4,
  TN_BASE64_ERR_OVERFLOW = -5
} tn_base64_error_t;

typedef struct {
  bool ok;
  union {
    char *value;
    tn_base64_error_t error;
  };
} tn_base64_string_result_t;

typedef struct {
  uint8_t *data;
  size_t len;
} tn_base64_bytes_t;

typedef struct {
  bool ok;
  union {
    tn_base64_bytes_t value;
    tn_base64_error_t error;
  };
} tn_base64_bytes_result_t;

/* Original owned result ABI: successful allocations must be freed with free().
 * CSPRNG and cryptography are NOT implemented by this encoding module. */
SALTS_C_API tn_base64_string_result_t tn_base64_encode_ex(const uint8_t *data,
                                                            size_t len);
SALTS_C_API tn_base64_error_t tn_base64_encode_buf_ex(const uint8_t *data,
                                                      size_t len, char *out,
                                                      size_t out_cap);
SALTS_C_API tn_base64_bytes_result_t tn_base64_decode_ex(const char *input);
SALTS_C_API int tn_base64_encode(const uint8_t *data, size_t len, char **output);
SALTS_C_API int tn_base64_encode_buf(const uint8_t *data, size_t len,
                                     char *out, size_t out_cap);
SALTS_C_API int tn_base64_decode(const char *input, uint8_t **output,
                                 size_t *output_len);

/* Counted, strict RFC 4648 Base64 for MIME/SMTP/websocket and binary fields.
 * Accepts RFC 2045 folding SP/HT/CR/LF, canonical '=' padding and zero tail
 * bits only. Invalid, truncated and overlong input is rejected without
 * touching caller output. output=NULL, capacity=0 queries decoded byte count
 * after full validation; written must be non-NULL in every case.
 * Non-NULL output must hold exactly the decoded count; no NUL is appended.
 * Does not allocate or own either buffer. Overlapping ranges are forbidden. */
SALTS_C_API tn_base64_error_t tn_base64_decode_buf_ex(const char *input,
                                                       size_t input_len,
                                                       uint8_t *output,
                                                       size_t output_capacity,
                                                       size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* SALTSCORE_BASE64_UTILS_H */
