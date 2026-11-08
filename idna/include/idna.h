#ifndef SALTS_IDNA_H
#define SALTS_IDNA_H

#include <unicode_normalize.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A fixed profile, not a set of independent flags. No implicit version upgrade. */
#define SALTS_IDNA_UNICODE17_UTS46_35_STRICT 1u
#define SALTS_IDNA_UNICODE17_UTS46_35_DNS 2u
#define SALTS_IDNA_MAX_LABEL_BYTES 63u
#define SALTS_IDNA_MAX_DOMAIN_BYTES 253u

typedef enum salts_idna_status {
  SALTS_IDNA_OK = 0,
  SALTS_IDNA_INVALID_ARGUMENT = -1,
  SALTS_IDNA_UNSUPPORTED_PROFILE = -2,
  SALTS_IDNA_INVALID_UTF8 = -3,
  SALTS_IDNA_DISALLOWED = -4,
  SALTS_IDNA_INVALID_LABEL = -5,
  SALTS_IDNA_INVALID_ALABEL = -6,
  SALTS_IDNA_BIDI = -7,
  SALTS_IDNA_CONTEXTJ = -8,
  SALTS_IDNA_CONTEXTO = -9,
  SALTS_IDNA_DNS_LENGTH = -10,
  SALTS_IDNA_WORKSPACE = -11,
  SALTS_IDNA_OUTPUT_CAPACITY = -12,
  SALTS_IDNA_OVERFLOW = -13
} salts_idna_status;

/** Caller-owned scratch regions. Capacities are bytes except scalar_capacity
 * (uint32_t elements). Each region is exclusive and mutable during the call;
 * its contents are unspecified afterward. No allocations or retained pointers.
 * mapped holds UTS #46 mapped UTF-8, normalized holds its NFC plus terminal NUL,
 * scalars holds the canonical decomposition required by salts_unicode_nfc.
 * Capacities are explicit application budgets: mapping/normalization can expand
 * input before DNS length checking. Insufficiency is WORKSPACE, not invalid DNS.
 * Example budgets for interactive hostnames: 4096 mapped bytes, 4096 normalized
 * bytes, 4096 scalars. These are application choices, not universal guarantees.
 */
typedef struct salts_idna_workspace {
  char *mapped;
  size_t mapped_capacity;
  char *normalized;
  size_t normalized_capacity;
  uint32_t *scalars;
  size_t scalar_capacity;
} salts_idna_workspace;

/** Synchronous UTF-8 domain -> lowercase ASCII connection identity.
 * Both SALTS_IDNA_UNICODE17_UTS46_35_STRICT and SALTS_IDNA_UNICODE17_UTS46_35_DNS
 * use the same nontransitional mapping,
 * STD3, hyphen, Bidi, ContextJ, DNS length checks; invalid Punycode rejected.
 * Both additionally enforce RFC 5892 ContextO and reject empty non-root labels.
 * STRICT rejects trailing dots. DNS accepts one optional root separator after
 * mapping/NFC (including ignored suffixes) and preserves it as an ASCII dot.
 * Root-only names and repeated trailing separators are invalid in both profiles.
 * SALTS_IDNA_MAX_DOMAIN_BYTES excludes the optional DNS root separator.
 * This profile is not full IDNA2008 or visual spoof/confusable detection.
 * Not a URL/IP/port parser; callers keep numeric-address handling separate.
 * Existing A-labels are decoded and validated, never repaired by normalization.
 * Input is borrowed for this call; bound input.len at the application boundary.
 * Input, output, scratch regions, workspace descriptor and out_size must all be
 * disjoint. Null scratch is allowed only with zero corresponding capacity.
 * output and out_size are required. All referenced storage remains caller-owned.
 * Success writes NUL-terminated ASCII; *out_size excludes NUL. All errors leave
 * output and *out_size unchanged. Do not submit failed output to DNS or TLS.
 * Use the SAME successful ASCII identity for DNS, TLS verification and SNI.
 * No I/O, cache, global mutable state or hidden allocations. Independent calls
 * with separate output/workspace may run concurrently. Errors name the failed
 * rule/resource; this API does not report source offsets or partial labels.
 * Example: salts_idna_to_ascii(vstr_from_cstr("b\xC3\xBC" "cher.de"),
 *   SALTS_IDNA_UNICODE17_UTS46_35_STRICT, &scratch, ascii, sizeof ascii, &size);
 * yields "xn--bcher-kva.de". A 254-byte output holds any STRICT result;
 * DNS needs 255 bytes for a maximal absolute name, including terminal NUL.
 * With DNS, "b\xC3\xBC" "cher.de.\xC2\xAD" yields "xn--bcher-kva.de.".
 */
salts_idna_status salts_idna_to_ascii(vstr input, uint32_t profile,
    const salts_idna_workspace *workspace, char *output, size_t output_capacity,
    size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
