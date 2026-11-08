#include "idna.h"
#include <string.h>

typedef struct { uint32_t first, last, offset; uint8_t length, status; } idna_mapping;
typedef struct { uint32_t first, last; uint8_t value; } idna_context_range;
#include "idna_data.h"
#define IDNA_COUNT(a) (sizeof(a) / sizeof((a)[0]))
enum { IDNA_VALID, IDNA_MAPPED, IDNA_IGNORED, IDNA_DEVIATION, IDNA_DISALLOWED };
enum { IDNA_JOIN_U, IDNA_JOIN_L, IDNA_JOIN_R, IDNA_JOIN_D, IDNA_JOIN_T,
       IDNA_GREEK = 8, IDNA_HEBREW = 16, IDNA_JAPANESE = 32 };

static const idna_mapping *idna_map(uint32_t cp) {
  size_t lo = 0, hi = IDNA_COUNT(idna_mappings);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < idna_mappings[mid].first) hi = mid;
    else if (cp > idna_mappings[mid].last) lo = mid + 1;
    else return &idna_mappings[mid];
  }
  return NULL;
}

static uint8_t idna_context(uint32_t cp) {
  size_t lo = 0, hi = IDNA_COUNT(idna_contexts);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < idna_contexts[mid].first) hi = mid;
    else if (cp > idna_contexts[mid].last) lo = mid + 1;
    else return idna_contexts[mid].value;
  }
  return 0;
}

static size_t idna_utf8(uint32_t cp, char bytes[4]) {
  if (cp < 0x80u) { bytes[0] = (char)cp; return 1; }
  unsigned tail = cp < 0x800u ? 1 : cp < 0x10000u ? 2 : 3;
  size_t n = tail + 1, pos = 0;
  bytes[pos++] = (char)((tail == 1 ? 0xc0u : tail == 2 ? 0xe0u : 0xf0u) | (cp >> (6 * tail)));
  while (tail) bytes[pos++] = (char)(0x80u | ((cp >> (6 * --tail)) & 0x3fu));
  return n;
}

/* RFC 3492 Bootstring parameters; all arithmetic is checked before mutation. */
enum { PUNY_BASE = 36, PUNY_TMIN = 1, PUNY_TMAX = 26, PUNY_SKEW = 38,
       PUNY_DAMP = 700, PUNY_INITIAL_BIAS = 72, PUNY_INITIAL_N = 128 };

static uint32_t puny_adapt(uint32_t delta, uint32_t points, int first) {
  delta = first ? delta / PUNY_DAMP : delta / 2;
  delta += delta / points; /* at most UINT32_MAX after the division by >=2 */
  uint32_t k = 0;
  while (delta > ((PUNY_BASE - PUNY_TMIN) * PUNY_TMAX) / 2) {
    delta /= PUNY_BASE - PUNY_TMIN;
    k += PUNY_BASE;
  }
  return k + (PUNY_BASE - PUNY_TMIN + 1) * delta / (delta + PUNY_SKEW);
}

static uint32_t puny_threshold(uint32_t k, uint32_t bias) {
  return k <= bias ? PUNY_TMIN : k - bias >= PUNY_TMAX ? PUNY_TMAX : k - bias;
}

static unsigned puny_digit(unsigned char c) {
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return c - '0' + 26;
  return PUNY_BASE;
}

static char puny_char(uint32_t d) { return (char)(d < 26 ? 'a' + d : '0' + d - 26); }

static salts_idna_status puny_decode(vstr input, uint32_t *out, size_t *length) {
  size_t pos = 0, count = 0, delimiter = SIZE_MAX;
  uint32_t n = PUNY_INITIAL_N, index = 0, bias = PUNY_INITIAL_BIAS;
  for (size_t j = 0; j < input.len; ++j) if (input.data[j] == '-') delimiter = j;
  if (delimiter != SIZE_MAX) {
    for (size_t j = 0; j < delimiter; ++j) {
      if ((unsigned char)input.data[j] >= 128 || count == SALTS_IDNA_MAX_LABEL_BYTES)
        return SALTS_IDNA_INVALID_ALABEL;
      out[count++] = (unsigned char)input.data[j];
    }
    pos = delimiter + 1;
  }
  while (pos < input.len) {
    uint32_t old = index, weight = 1;
    for (uint32_t k = PUNY_BASE;; k += PUNY_BASE) {
      if (pos == input.len) return SALTS_IDNA_INVALID_ALABEL;
      uint32_t digit = puny_digit((unsigned char)input.data[pos++]);
      if (digit >= PUNY_BASE) return SALTS_IDNA_INVALID_ALABEL;
      if (digit > (UINT32_MAX - index) / weight) return SALTS_IDNA_OVERFLOW;
      index += digit * weight;
      uint32_t t = puny_threshold(k, bias);
      if (digit < t) break;
      if (weight > UINT32_MAX / (PUNY_BASE - t)) return SALTS_IDNA_OVERFLOW;
      weight *= PUNY_BASE - t;
    }
    if (count == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
    uint32_t points = (uint32_t)count + 1;
    bias = puny_adapt(index - old, points, old == 0);
    if (index / points > 0x10ffffu - n) return SALTS_IDNA_INVALID_ALABEL;
    n += index / points;
    index %= points;
    if (n >= 0xd800u && n <= 0xdfffu) return SALTS_IDNA_INVALID_ALABEL;
    memmove(out + index + 1, out + index, (count - index) * sizeof(*out));
    out[index++] = n;
    ++count;
  }
  if (!count) return SALTS_IDNA_INVALID_ALABEL;
  *length = count;
  return SALTS_IDNA_OK;
}

static salts_idna_status puny_encode(const uint32_t *cp, size_t count,
                                    char output[64], size_t *length) {
  size_t written = 4;
  uint32_t n = PUNY_INITIAL_N, delta = 0, bias = PUNY_INITIAL_BIAS, basic = 0;
  memcpy(output, "xn--", 4);
  for (size_t j = 0; j < count; ++j) if (cp[j] < 128) {
    if (written == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
    output[written++] = (char)cp[j];
    ++basic;
  }
  if (basic) {
    if (written == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
    output[written++] = '-';
  }
  uint32_t handled = basic;
  while (handled < count) {
    uint32_t next = UINT32_MAX;
    for (size_t j = 0; j < count; ++j) if (cp[j] >= n && cp[j] < next) next = cp[j];
    if (next - n > (UINT32_MAX - delta) / (handled + 1)) return SALTS_IDNA_OVERFLOW;
    delta += (next - n) * (handled + 1);
    n = next;
    for (size_t j = 0; j < count; ++j) {
      if (cp[j] < n) {
        if (delta == UINT32_MAX) return SALTS_IDNA_OVERFLOW;
        ++delta;
      }
      if (cp[j] == n) {
        uint32_t q = delta;
        for (uint32_t k = PUNY_BASE;; k += PUNY_BASE) {
          uint32_t t = puny_threshold(k, bias);
          if (q < t) break;
          if (written == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
          output[written++] = puny_char(t + (q - t) % (PUNY_BASE - t));
          q = (q - t) / (PUNY_BASE - t);
        }
        if (written == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
        output[written++] = puny_char(q);
        bias = puny_adapt(delta, handled + 1, handled == basic);
        delta = 0;
        ++handled;
      }
    }
    if (delta == UINT32_MAX || n == UINT32_MAX) return SALTS_IDNA_OVERFLOW;
    ++delta;
    ++n;
  }
  output[written] = '\0';
  *length = written;
  return SALTS_IDNA_OK;
}

static salts_unicode_bidi_class idna_bidi(uint32_t cp) {
  salts_unicode_bidi_class result = SALTS_UNICODE_BIDI_L;
  (void)salts_unicode_bidi_class_of(cp, &result); /* validated scalar */
  return result;
}

static uint8_t idna_ccc(uint32_t cp) {
  salts_unicode_normalization_properties p = {0};
  (void)salts_unicode_normalization_properties_of(cp, &p);
  return p.canonical_combining_class;
}

static int idna_bidi_valid(const uint32_t *cp, size_t n) {
  salts_unicode_bidi_class first = idna_bidi(cp[0]), last = first;
  int rtl = first == SALTS_UNICODE_BIDI_R || first == SALTS_UNICODE_BIDI_AL;
  int en = 0, an = 0;
  if (!rtl && first != SALTS_UNICODE_BIDI_L) return 0;
  for (size_t i = 0; i < n; ++i) {
    salts_unicode_bidi_class b = idna_bidi(cp[i]);
    if (b == SALTS_UNICODE_BIDI_EN) en = 1;
    if (b == SALTS_UNICODE_BIDI_AN) an = 1;
    if (b != SALTS_UNICODE_BIDI_NSM) last = b;
    if (b == SALTS_UNICODE_BIDI_EN || b == SALTS_UNICODE_BIDI_ES ||
        b == SALTS_UNICODE_BIDI_CS || b == SALTS_UNICODE_BIDI_ET ||
        b == SALTS_UNICODE_BIDI_ON || b == SALTS_UNICODE_BIDI_BN ||
        b == SALTS_UNICODE_BIDI_NSM) continue;
    if (rtl ? (b != SALTS_UNICODE_BIDI_R && b != SALTS_UNICODE_BIDI_AL && b != SALTS_UNICODE_BIDI_AN)
            : b != SALTS_UNICODE_BIDI_L) return 0;
  }
  if (rtl) return !(en && an) && (last == SALTS_UNICODE_BIDI_R || last == SALTS_UNICODE_BIDI_AL ||
                                last == SALTS_UNICODE_BIDI_EN || last == SALTS_UNICODE_BIDI_AN);
  return last == SALTS_UNICODE_BIDI_L || last == SALTS_UNICODE_BIDI_EN;
}

static salts_idna_status idna_validate_label(const uint32_t *cp, size_t n) {
  if (!n || cp[0] == '-' || cp[n - 1] == '-' || (n >= 4 && cp[2] == '-' && cp[3] == '-'))
    return SALTS_IDNA_INVALID_LABEL;
  salts_unicode_normalization_properties p;
  if (salts_unicode_normalization_properties_of(cp[0], &p) != SALTS_UNICODE_OK || p.is_mark)
    return SALTS_IDNA_INVALID_LABEL;
  int japanese = 0, arabic = 0, extended_arabic = 0;
  for (size_t i = 0; i < n; ++i) {
    if (idna_context(cp[i]) & IDNA_JAPANESE) japanese = 1;
    if (cp[i] >= 0x0660u && cp[i] <= 0x0669u) arabic = 1;
    if (cp[i] >= 0x06f0u && cp[i] <= 0x06f9u) extended_arabic = 1;
  }
  for (size_t i = 0; i < n; ++i) {
    uint32_t c = cp[i];
    const idna_mapping *m = idna_map(c);
    if (!m || (m->status != IDNA_VALID && m->status != IDNA_DEVIATION)) return SALTS_IDNA_DISALLOWED;
    if (c < 128 && !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return SALTS_IDNA_DISALLOWED;
    if (c == 0x200cu || c == 0x200du) {
      if (i && idna_ccc(cp[i - 1]) == 9) continue; /* Virama */
      if (c == 0x200du) return SALTS_IDNA_CONTEXTJ;
      size_t left = i, right = i + 1;
      while (left && (idna_context(cp[left - 1]) & 7) == IDNA_JOIN_T) --left;
      while (right < n && (idna_context(cp[right]) & 7) == IDNA_JOIN_T) ++right;
      unsigned l = left ? idna_context(cp[left - 1]) & 7 : IDNA_JOIN_U;
      unsigned r = right < n ? idna_context(cp[right]) & 7 : IDNA_JOIN_U;
      if ((l != IDNA_JOIN_L && l != IDNA_JOIN_D) || (r != IDNA_JOIN_R && r != IDNA_JOIN_D))
        return SALTS_IDNA_CONTEXTJ;
    }
    if (c == 0x00b7u && !(i && i + 1 < n && cp[i - 1] == 'l' && cp[i + 1] == 'l'))
      return SALTS_IDNA_CONTEXTO;
    if (c == 0x0375u && !(i + 1 < n && (idna_context(cp[i + 1]) & IDNA_GREEK)))
      return SALTS_IDNA_CONTEXTO;
    if ((c == 0x05f3u || c == 0x05f4u) && !(i && (idna_context(cp[i - 1]) & IDNA_HEBREW)))
      return SALTS_IDNA_CONTEXTO;
    if (c == 0x30fbu && !japanese) return SALTS_IDNA_CONTEXTO;
  }
  return arabic && extended_arabic ? SALTS_IDNA_CONTEXTO : SALTS_IDNA_OK;
}

/* A-labels must already be NFC. Never repair a decoded label by mapping/NFC. */
static int idna_label_is_nfc(const uint32_t *cp, size_t n) {
  char input[4 * SALTS_IDNA_MAX_LABEL_BYTES], output[4 * SALTS_IDNA_MAX_LABEL_BYTES + 1];
  uint32_t work[4 * SALTS_IDNA_MAX_LABEL_BYTES];
  size_t bytes = 0, out_size = 0;
  for (size_t i = 0; i < n; ++i) bytes += idna_utf8(cp[i], input + bytes);
  return salts_unicode_nfc(vstr_from_buf(input, bytes), SALTS_UNICODE_NFC_17_0_0,
      work, IDNA_COUNT(work), output, sizeof output, &out_size) == SALTS_UNICODE_NFC_OK &&
      out_size == bytes && memcmp(input, output, bytes) == 0;
}

static int idna_overlap(const void *a, size_t an, const void *b, size_t bn) {
  uintptr_t av = (uintptr_t)a, bv = (uintptr_t)b;
  return an && bn && (av <= bv ? bv - av < an : av - bv < bn);
}

salts_idna_status salts_idna_to_ascii(vstr input, uint32_t profile,
    const salts_idna_workspace *w, char *output, size_t capacity, size_t *out_size) {
  if (!w || !output || !out_size || (!input.data && input.len) ||
      (!w->mapped && w->mapped_capacity) || (!w->normalized && w->normalized_capacity) ||
      (!w->scalars && w->scalar_capacity)) return SALTS_IDNA_INVALID_ARGUMENT;
  if (profile != SALTS_IDNA_UNICODE17_UTS46_35_STRICT &&
      profile != SALTS_IDNA_UNICODE17_UTS46_35_DNS) return SALTS_IDNA_UNSUPPORTED_PROFILE;
  if (input.len > PTRDIFF_MAX || capacity > PTRDIFF_MAX ||
      w->mapped_capacity > PTRDIFF_MAX || w->normalized_capacity > PTRDIFF_MAX ||
      w->scalar_capacity > (size_t)PTRDIFF_MAX / sizeof(uint32_t)) return SALTS_IDNA_OVERFLOW;
  const void *regions[] = {input.data, output, out_size, w, w->mapped, w->normalized, w->scalars};
  const size_t sizes[] = {input.len, capacity, sizeof(*out_size), sizeof(*w),
      w->mapped_capacity, w->normalized_capacity, w->scalar_capacity * sizeof(uint32_t)};
  for (size_t i = 0; i < IDNA_COUNT(regions); ++i)
    for (size_t j = i + 1; j < IDNA_COUNT(regions); ++j)
      if (idna_overlap(regions[i], sizes[i], regions[j], sizes[j])) return SALTS_IDNA_INVALID_ARGUMENT;

  size_t cursor = 0, mapped = 0, normalized = 0;
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) return SALTS_IDNA_INVALID_UTF8;
    if (!scalar.value) return SALTS_IDNA_DISALLOWED;
    const idna_mapping *m = idna_map(scalar.value);
    if (!m) return SALTS_IDNA_DISALLOWED;
    if (m->status == IDNA_IGNORED) continue;
    const uint32_t *seq = m->status == IDNA_MAPPED ? idna_mapping_values + m->offset : &scalar.value;
    size_t n = m->status == IDNA_MAPPED ? m->length : 1;
    for (size_t i = 0; i < n; ++i) {
      char bytes[4];
      size_t count = idna_utf8(seq[i], bytes);
      if (count > w->mapped_capacity - mapped) return SALTS_IDNA_WORKSPACE;
      memcpy(w->mapped + mapped, bytes, count);
      mapped += count;
    }
  }
  if (!mapped) return SALTS_IDNA_INVALID_LABEL;
  if (!w->normalized_capacity) return SALTS_IDNA_WORKSPACE;
  salts_unicode_nfc_status ns = salts_unicode_nfc(vstr_from_buf(w->mapped, mapped),
      SALTS_UNICODE_NFC_17_0_0, w->scalars, w->scalar_capacity,
      w->normalized, w->normalized_capacity, &normalized);
  if (ns == SALTS_UNICODE_NFC_OVERFLOW) return SALTS_IDNA_OVERFLOW;
  if (ns != SALTS_UNICODE_NFC_OK) return SALTS_IDNA_WORKSPACE;

  /* Mapping can expose the root marker by removing ignored suffix scalars.
   * Remove exactly one here; normal label validation still rejects empty names
   * and repeated separators. STRICT keeps its original empty-label behavior. */
  int absolute = profile == SALTS_IDNA_UNICODE17_UTS46_35_DNS &&
      normalized && w->normalized[normalized - 1] == '.';
  if (absolute) --normalized;

  /* Any valid DNS result bounds its decoded scalar count and label count too.
   * Retain decoded labels until domain-wide Bidi activation is known. */
  uint32_t domain[SALTS_IDNA_MAX_DOMAIN_BYTES];
  size_t starts[(SALTS_IDNA_MAX_DOMAIN_BYTES + 1) / 2 + 1];
  size_t total = 0, labels = 0, start = 0, ascii_size = 0;
  char ascii[SALTS_IDNA_MAX_DOMAIN_BYTES + 2];
  int bidi_domain = 0;
  for (size_t end = 0; end <= normalized; ++end) {
    if (end != normalized && w->normalized[end] != '.') continue;
    if (end == start) return SALTS_IDNA_INVALID_LABEL;
    vstr label = vstr_from_buf(w->normalized + start, end - start);
    uint32_t points[SALTS_IDNA_MAX_LABEL_BYTES];
    size_t count = 0;
    int alabel = label.len >= 4 && memcmp(label.data, "xn--", 4) == 0;
    salts_idna_status status;
    if (alabel) {
      if (label.len > SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
      status = puny_decode(vstr_from_buf(label.data + 4, label.len - 4), points, &count);
      if (status != SALTS_IDNA_OK) return status;
      if (!idna_label_is_nfc(points, count)) return SALTS_IDNA_INVALID_ALABEL;
    } else {
      size_t pos = 0;
      while (pos < label.len) {
        salts_unicode_scalar s;
        if (salts_unicode_utf8_next(label, &pos, &s) != SALTS_UNICODE_OK) return SALTS_IDNA_INVALID_UTF8;
        if (count == SALTS_IDNA_MAX_LABEL_BYTES) return SALTS_IDNA_DNS_LENGTH;
        points[count++] = s.value;
      }
    }
    status = idna_validate_label(points, count);
    if (status != SALTS_IDNA_OK) return status;
    int nonascii = 0;
    for (size_t i = 0; i < count; ++i) {
      if (points[i] >= 128) nonascii = 1;
      salts_unicode_bidi_class b = idna_bidi(points[i]);
      if (b == SALTS_UNICODE_BIDI_R || b == SALTS_UNICODE_BIDI_AL || b == SALTS_UNICODE_BIDI_AN) bidi_domain = 1;
    }
    if (alabel && !nonascii) return SALTS_IDNA_INVALID_ALABEL;
    char encoded[SALTS_IDNA_MAX_LABEL_BYTES + 1];
    size_t encoded_size = count;
    if (nonascii) {
      status = puny_encode(points, count, encoded, &encoded_size);
      if (status != SALTS_IDNA_OK) return status;
    } else {
      for (size_t i = 0; i < count; ++i) encoded[i] = (char)points[i];
    }
    if (alabel && (encoded_size != label.len || memcmp(encoded, label.data, encoded_size) != 0))
      return SALTS_IDNA_INVALID_ALABEL;
    size_t separator = labels ? 1 : 0;
    if (separator + encoded_size > SALTS_IDNA_MAX_DOMAIN_BYTES - ascii_size) return SALTS_IDNA_DNS_LENGTH;
    if (count > IDNA_COUNT(domain) - total || labels + 1 >= IDNA_COUNT(starts)) return SALTS_IDNA_DNS_LENGTH;
    starts[labels++] = total;
    memcpy(domain + total, points, count * sizeof(*points));
    total += count;
    if (separator) ascii[ascii_size++] = '.';
    memcpy(ascii + ascii_size, encoded, encoded_size);
    ascii_size += encoded_size;
    start = end + 1;
  }
  starts[labels] = total;
  if (bidi_domain)
    for (size_t i = 0; i < labels; ++i)
      if (!idna_bidi_valid(domain + starts[i], starts[i + 1] - starts[i])) return SALTS_IDNA_BIDI;
  if (absolute) ascii[ascii_size++] = '.';
  if (capacity <= ascii_size) return SALTS_IDNA_OUTPUT_CAPACITY;
  ascii[ascii_size] = '\0';
  memcpy(output, ascii, ascii_size + 1);
  *out_size = ascii_size;
  return SALTS_IDNA_OK;
}
