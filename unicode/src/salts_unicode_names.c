#include "unicode.h"
#include <string.h>

typedef struct salts_unicode_name_entry {
  const char *name;
  uint32_t scalar;
} salts_unicode_name_entry;

typedef struct salts_unicode_name_range {
  const char *prefix;
  uint32_t first;
  uint32_t last;
} salts_unicode_name_range;

#include "unicode_names_data.h"

enum { NAME_HEX_RADIX = 16, NAME_MIN_HEX_DIGITS = 4, NAME_MAX_HEX_DIGITS = 6,
       CJK_MAX_HEX_DIGITS = 5 };
static const char hangul_prefix[] = "HANGUL SYLLABLE ";
static const char cjk_prefix[] = "CJK UNIFIED IDEOGRAPH-";

static int salts_unicode_derived_name(vstr original, const char *folded, uint32_t *scalar) {
  for (size_t i = 0u; i < sizeof(salts_unicode_name_ranges) / sizeof(salts_unicode_name_ranges[0]); ++i) {
    const salts_unicode_name_range *range = &salts_unicode_name_ranges[i];
    size_t prefix_length = strlen(range->prefix);
    uint32_t value = 0u;
    int cjk = strcmp(range->prefix, cjk_prefix) == 0;
    if (original.len < prefix_length + NAME_MIN_HEX_DIGITS ||
        original.len > prefix_length + NAME_MAX_HEX_DIGITS ||
        memcmp(folded, range->prefix, prefix_length) != 0) continue;
    if (cjk && (original.len > prefix_length + CJK_MAX_HEX_DIGITS ||
                memcmp(original.data, folded, original.len) != 0)) continue;
    for (size_t j = prefix_length; j < original.len; ++j) {
      unsigned char ch = (unsigned char)folded[j];
      if (ch >= '0' && ch <= '9') value = value * NAME_HEX_RADIX + (uint32_t)(ch - '0');
      else if (ch >= 'A' && ch <= 'F') value = value * NAME_HEX_RADIX + (uint32_t)(ch - 'A' + 10);
      else return 0;
    }
    if (value < range->first || value > range->last) continue;
    /* Only Python's algorithmic CJK names permit a leading zero. Other derived
     * families use the canonical UCD hexadecimal spelling. Six digits bound arithmetic. */
    if (!cjk && original.len > prefix_length + NAME_MIN_HEX_DIGITS && folded[prefix_length] == '0')
      return 0;
    *scalar = value;
    return 1;
  }
  return 0;
}

salts_unicode_status salts_unicode_name_lookup(vstr name, uint32_t *out_scalar) {
  char folded[SALTS_UNICODE_MAX_NAME_BYTES + 1u];
  size_t first = 0u, last = sizeof(salts_unicode_names) / sizeof(salts_unicode_names[0]);
  if (out_scalar == NULL || (name.data == NULL && name.len != 0u))
    return SALTS_UNICODE_ERR_INVALID_ARGUMENT;
  if (name.len == 0u || name.len > SALTS_UNICODE_MAX_NAME_BYTES) return SALTS_UNICODE_NO_MATCH;
  for (size_t i = 0u; i < name.len; ++i) {
    unsigned char ch = (unsigned char)name.data[i];
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-'))
      return SALTS_UNICODE_NO_MATCH;
    folded[i] = (char)ch;
  }
  folded[name.len] = '\0';
  if (name.len >= sizeof(hangul_prefix) - 1u &&
      memcmp(folded, hangul_prefix, sizeof(hangul_prefix) - 1u) == 0 &&
      memcmp(name.data, folded, name.len) != 0) return SALTS_UNICODE_NO_MATCH;
  /* O(L log N + R L) time, O(Lmax) stack; all data immutable, no retained views. */
  while (first < last) {
    size_t middle = first + (last - first) / 2u;
    int order = strcmp(folded, salts_unicode_names[middle].name);
    if (order < 0) last = middle;
    else if (order > 0) first = middle + 1u;
    else {
      *out_scalar = salts_unicode_names[middle].scalar;
      return SALTS_UNICODE_OK;
    }
  }
  return salts_unicode_derived_name(name, folded, out_scalar) ? SALTS_UNICODE_OK : SALTS_UNICODE_NO_MATCH;
}
