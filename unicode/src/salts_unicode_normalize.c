#include "unicode_normalize.h"
#include <string.h>

typedef struct { uint32_t scalar, offset; uint8_t length; } nfc_decomposition;
typedef struct { uint32_t first, second, composed; } nfc_composition;
typedef struct { uint32_t first, last; uint16_t value; } nfc_property_range;
#include "unicode_normalization_data.h"
#define NFC_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static uint16_t nfc_property(uint32_t cp) {
  size_t lo = 0, hi = NFC_COUNT(nfc_properties);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const nfc_property_range *r = &nfc_properties[mid];
    if (cp < r->first) hi = mid;
    else if (cp > r->last) lo = mid + 1;
    else return r->value;
  }
  return 0;
}

salts_unicode_status salts_unicode_normalization_properties_of(
    uint32_t cp, salts_unicode_normalization_properties *out) {
  if (!out || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  uint16_t prop = nfc_property(cp);
  out->canonical_combining_class = (uint8_t)(prop & 255u);
  out->is_mark = (uint8_t)(prop >> 8);
  return SALTS_UNICODE_OK;
}

static const nfc_decomposition *nfc_decompose(uint32_t cp) {
  size_t lo = 0, hi = NFC_COUNT(nfc_decompositions);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < nfc_decompositions[mid].scalar) hi = mid;
    else if (cp > nfc_decompositions[mid].scalar) lo = mid + 1;
    else return &nfc_decompositions[mid];
  }
  return NULL;
}

/* UAX #15 Hangul constants: SBase, LBase, VBase, TBase and syllable counts. */
enum { NFC_SBASE = 0xac00, NFC_LBASE = 0x1100, NFC_VBASE = 0x1161,
       NFC_TBASE = 0x11a7, NFC_LCOUNT = 19, NFC_VCOUNT = 21,
       NFC_TCOUNT = 28, NFC_NCOUNT = 588, NFC_SCOUNT = 11172 };

static uint32_t nfc_compose(uint32_t a, uint32_t b) {
  if (a >= NFC_LBASE && a < NFC_LBASE + NFC_LCOUNT &&
      b >= NFC_VBASE && b < NFC_VBASE + NFC_VCOUNT)
    return NFC_SBASE + ((a - NFC_LBASE) * NFC_VCOUNT + b - NFC_VBASE) * NFC_TCOUNT;
  if (a >= NFC_SBASE && a < NFC_SBASE + NFC_SCOUNT &&
      (a - NFC_SBASE) % NFC_TCOUNT == 0 && b > NFC_TBASE && b < NFC_TBASE + NFC_TCOUNT)
    return a + b - NFC_TBASE;
  size_t lo = 0, hi = NFC_COUNT(nfc_compositions);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const nfc_composition *c = &nfc_compositions[mid];
    if (a < c->first || (a == c->first && b < c->second)) hi = mid;
    else if (a > c->first || b > c->second) lo = mid + 1;
    else return c->composed;
  }
  return 0;
}

static int nfc_overlap(const void *a, size_t an, const void *b, size_t bn) {
  uintptr_t av = (uintptr_t)a, bv = (uintptr_t)b;
  if (!an || !bn) return 0;
  return av <= bv ? bv - av < an : av - bv < bn;
}

salts_unicode_nfc_status salts_unicode_nfc(
    vstr input, uint32_t version, uint32_t *work, size_t work_capacity,
    char *output, size_t capacity, size_t *out_size) {
  size_t length = 0, cursor = 0, bytes = 0;
  if (!output || !out_size || (!input.data && input.len) || (!work && work_capacity))
    return SALTS_UNICODE_NFC_INVALID_ARGUMENT;
  if (version != SALTS_UNICODE_NFC_17_0_0) return SALTS_UNICODE_NFC_UNSUPPORTED_VERSION;
  if (input.len > PTRDIFF_MAX || capacity > PTRDIFF_MAX ||
      work_capacity > (size_t)PTRDIFF_MAX / sizeof(*work)) return SALTS_UNICODE_NFC_OVERFLOW;
  const void *regions[] = {input.data, work, output, out_size};
  const size_t sizes[] = {input.len, work_capacity * sizeof(*work), capacity, sizeof(*out_size)};
  for (size_t i = 0; i < NFC_COUNT(regions); ++i)
    for (size_t j = i + 1; j < NFC_COUNT(regions); ++j)
      if (nfc_overlap(regions[i], sizes[i], regions[j], sizes[j]))
        return SALTS_UNICODE_NFC_INVALID_ARGUMENT;
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK)
      return SALTS_UNICODE_NFC_INVALID_UTF8;
    uint32_t hangul[3], cp = scalar.value;
    const uint32_t *seq = &cp;
    size_t n = 1;
    if (cp >= NFC_SBASE && cp < NFC_SBASE + NFC_SCOUNT) {
      uint32_t index = cp - NFC_SBASE;
      hangul[0] = NFC_LBASE + index / NFC_NCOUNT;
      hangul[1] = NFC_VBASE + (index % NFC_NCOUNT) / NFC_TCOUNT;
      hangul[2] = NFC_TBASE + index % NFC_TCOUNT;
      seq = hangul;
      n = hangul[2] == NFC_TBASE ? 2 : 3;
    } else {
      const nfc_decomposition *d = nfc_decompose(cp);
      if (d) { seq = nfc_decomposition_values + d->offset; n = d->length; }
    }
    if (n > work_capacity - length) return SALTS_UNICODE_NFC_WORKSPACE;
    for (size_t i = 0; i < n; ++i) {
      uint8_t ccc = (uint8_t)nfc_property(seq[i]);
      size_t pos = length++;
      /* Stable insertion: CCC=0 is a boundary, equal CCC never crosses. */
      while (ccc && pos && (uint8_t)nfc_property(work[pos - 1]) > ccc) {
        work[pos] = work[pos - 1];
        --pos;
      }
      work[pos] = seq[i];
    }
  }
  size_t written = 0, starter = SIZE_MAX;
  uint8_t previous_ccc = 0;
  for (size_t i = 0; i < length; ++i) {
    uint32_t cp = work[i], composite = 0;
    uint8_t ccc = (uint8_t)nfc_property(cp);
    if (starter != SIZE_MAX && (previous_ccc == 0 || previous_ccc < ccc))
      composite = nfc_compose(work[starter], cp);
    if (composite) work[starter] = composite;
    else {
      if (ccc == 0) starter = written;
      work[written++] = cp;
      previous_ccc = ccc;
    }
  }
  for (size_t i = 0; i < written; ++i) {
    uint32_t cp = work[i];
    size_t n = cp < 0x80u ? 1 : cp < 0x800u ? 2 : cp < 0x10000u ? 3 : 4;
    if (bytes > SIZE_MAX - n - 1) return SALTS_UNICODE_NFC_OVERFLOW;
    bytes += n;
  }
  if (capacity <= bytes) return SALTS_UNICODE_NFC_OUTPUT_CAPACITY;
  size_t pos = 0;
  for (size_t i = 0; i < written; ++i) {
    uint32_t cp = work[i];
    if (cp < 0x80u) output[pos++] = (char)cp;
    else {
      unsigned tail = cp < 0x800u ? 1 : cp < 0x10000u ? 2 : 3;
      output[pos++] = (char)((tail == 1 ? 0xc0u : tail == 2 ? 0xe0u : 0xf0u) | (cp >> (6 * tail)));
      while (tail) output[pos++] = (char)(0x80u | ((cp >> (6 * --tail)) & 0x3fu));
    }
  }
  output[pos] = '\0';
  *out_size = bytes;
  return SALTS_UNICODE_NFC_OK;
}
