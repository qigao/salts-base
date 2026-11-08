/* Sole Base64 implementation for the Salts native module family.
 * Portable scalar C11, no libbase64/GmSSL/CMeta vendor copy, no implicit
 * fallback. The only exported ABI is base64_utils.h / tn_base64_*.
 */
#include <base64_utils.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char tn_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static tn_base64_error_t tn_base64_encoded_size(size_t length, size_t *size) {
  size_t quartets;
  if (!size) return TN_BASE64_ERR_INVALID_ARG;
  quartets = length / 3u + (length % 3u != 0u);
  if (quartets > (SIZE_MAX - 1u) / 4u) return TN_BASE64_ERR_OVERFLOW;
  *size = quartets * 4u;
  return TN_BASE64_OK;
}

tn_base64_error_t tn_base64_encode_buf_ex(const uint8_t *data, size_t length,
                                           char *output, size_t capacity) {
  size_t size, offset = 0u;
  tn_base64_error_t status;
  if ((!data && length != 0u) || !output) return TN_BASE64_ERR_INVALID_ARG;
  status = tn_base64_encoded_size(length, &size);
  if (status != TN_BASE64_OK) return status;
  if (capacity == 0u || capacity - 1u < size) return TN_BASE64_ERR_BUFFER_TOO_SMALL;
  for (size_t i = 0u; i < length;) {
    size_t remaining = length - i;
    unsigned first = data[i++];
    unsigned second = remaining > 1u ? data[i++] : 0u;
    unsigned third = remaining > 2u ? data[i++] : 0u;
    output[offset++] = tn_base64_alphabet[first >> 2];
    output[offset++] = tn_base64_alphabet[((first & 3u) << 4) | (second >> 4)];
    output[offset++] = remaining > 1u ?
      tn_base64_alphabet[((second & 15u) << 2) | (third >> 6)] : '=';
    output[offset++] = remaining > 2u ? tn_base64_alphabet[third & 63u] : '=';
  }
  output[offset] = '\0';
  return TN_BASE64_OK;
}

tn_base64_string_result_t tn_base64_encode_ex(const uint8_t *data, size_t len) {
  tn_base64_string_result_t result = {.ok = false, .error = TN_BASE64_ERR_INVALID_ARG};
  size_t size;
  tn_base64_error_t status;
  char *output;
  if (!data && len != 0u) return result;
  status = tn_base64_encoded_size(len, &size);
  if (status != TN_BASE64_OK) { result.error = status; return result; }
  output = (char *)malloc(size + 1u);
  if (!output) { result.error = TN_BASE64_ERR_NO_MEMORY; return result; }
  status = tn_base64_encode_buf_ex(data, len, output, size + 1u);
  if (status != TN_BASE64_OK) {
    free(output);
    result.error = status;
    return result;
  }
  result.ok = true;
  result.value = output;
  return result;
}

int tn_base64_encode(const uint8_t *data, size_t len, char **output) {
  tn_base64_string_result_t result;
  if (!output) return -1;
  *output = NULL;
  result = tn_base64_encode_ex(data, len);
  if (!result.ok) return -1;
  *output = result.value;
  return 0;
}

int tn_base64_encode_buf(const uint8_t *data, size_t len,
                         char *output, size_t capacity) {
  return tn_base64_encode_buf_ex(data, len, output, capacity) == TN_BASE64_OK ?
      0 : -1;
}

static int tn_base64_digit(unsigned char value) {
  if (value >= 'A' && value <= 'Z') return (int)(value - 'A');
  if (value >= 'a' && value <= 'z') return (int)(value - 'a') + 26;
  if (value >= '0' && value <= '9') return (int)(value - '0') + 52;
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

static int tn_base64_whitespace(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static tn_base64_error_t tn_base64_validate(const char *input, size_t length,
                                             size_t *required) {
  unsigned char quad[4];
  size_t count = 0u, size = 0u;
  int terminal = 0;
  for (size_t i = 0u; i < length; ++i) {
    unsigned char ch = (unsigned char)input[i];
    int digit;
    size_t chunk;
    if (tn_base64_whitespace(ch)) continue;
    if (terminal) return TN_BASE64_ERR_INVALID_INPUT;
    digit = ch == '=' ? 64 : tn_base64_digit(ch);
    if (digit < 0) return TN_BASE64_ERR_INVALID_INPUT;
    quad[count++] = (unsigned char)digit;
    if (count != 4u) continue;
    if (quad[0] == 64 || quad[1] == 64) return TN_BASE64_ERR_INVALID_INPUT;
    if (quad[2] == 64) {
      if (quad[3] != 64 || (quad[1] & 15u) != 0u)
        return TN_BASE64_ERR_INVALID_INPUT;
      chunk = 1u;
      terminal = 1;
    } else if (quad[3] == 64) {
      if ((quad[2] & 3u) != 0u) return TN_BASE64_ERR_INVALID_INPUT;
      chunk = 2u;
      terminal = 1;
    } else {
      chunk = 3u;
    }
    if (size > SIZE_MAX - chunk) return TN_BASE64_ERR_OVERFLOW;
    size += chunk;
    count = 0u;
  }
  if (count != 0u) return TN_BASE64_ERR_INVALID_INPUT;
  *required = size;
  return TN_BASE64_OK;
}

tn_base64_error_t tn_base64_decode_buf_ex(const char *input, size_t input_len,
                                           uint8_t *output, size_t capacity,
                                           size_t *written) {
  size_t required = 0u, offset = 0u, count = 0u;
  unsigned char quad[4];
  tn_base64_error_t status;
  if (!written || (!input && input_len != 0u))
    return TN_BASE64_ERR_INVALID_ARG;
  status = tn_base64_validate(input, input_len, &required);
  if (status != TN_BASE64_OK) return status;
  if (!output) {
    if (capacity != 0u) return TN_BASE64_ERR_INVALID_ARG;
    *written = required;
    return TN_BASE64_OK;
  }
  if (capacity < required) return TN_BASE64_ERR_BUFFER_TOO_SMALL;
  /* Complete validation before committing any caller output. */
  for (size_t i = 0u; i < input_len; ++i) {
    unsigned char ch = (unsigned char)input[i];
    if (tn_base64_whitespace(ch)) continue;
    quad[count++] = (unsigned char)(ch == '=' ? 64 : tn_base64_digit(ch));
    if (count != 4u) continue;
    output[offset++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
    if (quad[2] != 64) {
      output[offset++] = (uint8_t)((quad[1] << 4) | (quad[2] >> 2));
      if (quad[3] != 64)
        output[offset++] = (uint8_t)((quad[2] << 6) | quad[3]);
    }
    count = 0u;
  }
  *written = offset;
  return TN_BASE64_OK;
}

tn_base64_bytes_result_t tn_base64_decode_ex(const char *input) {
  tn_base64_bytes_result_t result = {.ok = false, .error = TN_BASE64_ERR_INVALID_ARG};
  size_t needed = 0u, written = 0u;
  tn_base64_error_t status;
  uint8_t *output;
  if (!input) return result;
  /* Original result API rejects empty strings; the counted query supports
   * zero-length MIME bodies without allocating. */
  size_t len = strlen(input);
  if (len == 0u) return result;
  status = tn_base64_decode_buf_ex(input, len, NULL, 0u, &needed);
  if (status != TN_BASE64_OK) { result.error = status; return result; }
  output = (uint8_t *)malloc(needed != 0u ? needed : 1u);
  if (!output) { result.error = TN_BASE64_ERR_NO_MEMORY; return result; }
  status = tn_base64_decode_buf_ex(input, len, output, needed, &written);
  if (status != TN_BASE64_OK || written != needed) {
    free(output);
    result.error = status != TN_BASE64_OK ? status : TN_BASE64_ERR_INVALID_INPUT;
    return result;
  }
  result.ok = true;
  result.value.data = output;
  result.value.len = written;
  return result;
}

int tn_base64_decode(const char *input, uint8_t **output, size_t *length) {
  tn_base64_bytes_result_t result;
  if (!output || !length) return -1;
  *output = NULL;
  *length = 0u;
  result = tn_base64_decode_ex(input);
  if (!result.ok) return -1;
  *output = result.value.data;
  *length = result.value.len;
  return 0;
}
