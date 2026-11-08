#ifndef SALTS_UNICODE_NORMALIZE_H
#define SALTS_UNICODE_NORMALIZE_H

#include <unicode.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_UNICODE_NFC_17_0_0 0x00110000u

typedef enum salts_unicode_nfc_status {
  SALTS_UNICODE_NFC_OK = 0,
  SALTS_UNICODE_NFC_INVALID_ARGUMENT = -1,
  SALTS_UNICODE_NFC_INVALID_UTF8 = -2,
  SALTS_UNICODE_NFC_UNSUPPORTED_VERSION = -3,
  SALTS_UNICODE_NFC_WORKSPACE = -4,
  SALTS_UNICODE_NFC_OUTPUT_CAPACITY = -5,
  SALTS_UNICODE_NFC_OVERFLOW = -6
} salts_unicode_nfc_status;

typedef struct salts_unicode_normalization_properties {
  uint8_t canonical_combining_class;
  uint8_t is_mark; /* General_Category Mn, Mc or Me, including CCC=0 marks. */
} salts_unicode_normalization_properties;

/** Query Unicode 17.0.0 properties. Invalid scalar/output: INVALID_ARGUMENT;
 * output is unchanged on error. This does not normalize the scalar. */
salts_unicode_status salts_unicode_normalization_properties_of(
    uint32_t scalar, salts_unicode_normalization_properties *out);

/** Explicit Unicode 17 NFC; no compatibility folding or locale processing.
 * Input is a borrowed UTF-8 view (NUL is a valid scalar). All storage, including
 * out_size, must be disjoint and remain alive for this synchronous call.
 * workspace_capacity counts uint32_t elements; output_capacity counts bytes.
 * Four workspace elements per input byte suffice (checked by the caller before
 * allocating); smaller storage is accepted when the decomposition fits.
 * No allocation, retained references, I/O or mutable global state.
 * On success output is NUL-terminated, *out_size excludes that final NUL.
 * On any error output and *out_size are unchanged; workspace is unspecified.
 * A zero-element workspace may be NULL; output and out_size are required.
 * Unsupported version, malformed UTF-8, insufficient workspace/output and
 * arithmetic overflow have distinct statuses. Reordering is stable; worst-case
 * time is quadratic in the longest combining sequence, space linear in the
 * canonical decomposition. Bound input.len and workspace at the application.
 * Example: uint32_t w[8]; char out[8]; size_t n;
 * salts_unicode_nfc(vstr_from_cstr("e\xCC\x81"), SALTS_UNICODE_NFC_17_0_0,
 *                   w, 8, out, sizeof out, &n); // UTF-8 U+00E9, n=2
 */
salts_unicode_nfc_status salts_unicode_nfc(
    vstr input, uint32_t version, uint32_t *workspace, size_t workspace_capacity,
    char *output, size_t output_capacity, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
