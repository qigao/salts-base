#ifndef UNICODE_ASCII_INTERNAL_H
#define UNICODE_ASCII_INTERNAL_H

#include <unicode.h>
#include <simd.h>

/* Read only complete vectors from the caller's counted, borrowed input.
 * A high bit hands the remaining bytes back to the strict UTF-8 decoder. */
static inline unicode_status unicode_ascii_prefix(
    const char *data, size_t length, size_t *out_length) {
  size_t offset = 0u;
  while (length - offset >= sizeof(v128)) {
    v128 bytes;
    uint32_t high_bits;
    simd_v128_load(&bytes, data + offset);
    if (!simd_reduce(&simd_u8x16, SALTS_SIMD_REDUCE_BITMASK,
                     &bytes, &high_bits))
      return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
    if (high_bits != 0u) break;
    offset += sizeof(v128);
  }
  while (offset < length && (unsigned char)data[offset] < 0x80u) ++offset;
  *out_length = offset;
  return SALTS_UNICODE_OK;
}

static inline int unicode_ascii_whitespace(unsigned char byte) {
  /* Unicode White_Space includes VT and FF, unlike JSON/XML whitespace. */
  return byte == 0x20u || (byte >= 0x09u && byte <= 0x0du);
}

static inline int unicode_ascii_xid_start(unsigned char byte) {
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static inline int unicode_ascii_xid_continue(unsigned char byte) {
  return unicode_ascii_xid_start(byte) || byte == '_' ||
         (byte >= '0' && byte <= '9');
}

#endif
