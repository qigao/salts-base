#include <utf8.h>

#include <stddef.h>
#include <stdint.h>

static int cont(unsigned char byte) { return (byte & 0xc0u) == 0x80u; }

/* No decoder state crosses calls: PluginRegistry may borrow its candidate
 * path for only this invocation, and must never cache an unowned pointer. */
int salts_core_utf8_valid(const char *data, size_t length) {
  const unsigned char *s = (const unsigned char *)data;
  size_t i = 0u;
  if (s == NULL) return length == 0u;
  while (i < length) {
    const unsigned char lead = s[i];
    const size_t remaining = length - i;
    if (lead < 0x80u) {
      ++i;
    } else if (lead >= 0xc2u && lead <= 0xdfu) {
      if (remaining < 2u || !cont(s[i + 1u])) return 0;
      i += 2u;
    } else if (lead == 0xe0u) {
      if (remaining < 3u || s[i + 1u] < 0xa0u ||
          s[i + 1u] > 0xbfu || !cont(s[i + 2u])) return 0;
      i += 3u;
    } else if ((lead >= 0xe1u && lead <= 0xecu) ||
               (lead >= 0xeeu && lead <= 0xefu)) {
      if (remaining < 3u || !cont(s[i + 1u]) ||
          !cont(s[i + 2u])) return 0;
      i += 3u;
    } else if (lead == 0xedu) {
      if (remaining < 3u || s[i + 1u] < 0x80u ||
          s[i + 1u] > 0x9fu || !cont(s[i + 2u])) return 0;
      i += 3u;
    } else if (lead == 0xf0u) {
      if (remaining < 4u || s[i + 1u] < 0x90u ||
          s[i + 1u] > 0xbfu ||
          !cont(s[i + 2u]) || !cont(s[i + 3u])) return 0;
      i += 4u;
    } else if (lead >= 0xf1u && lead <= 0xf3u) {
      if (remaining < 4u || !cont(s[i + 1u]) ||
          !cont(s[i + 2u]) || !cont(s[i + 3u])) return 0;
      i += 4u;
    } else if (lead == 0xf4u) {
      if (remaining < 4u || s[i + 1u] < 0x80u ||
          s[i + 1u] > 0x8fu ||
          !cont(s[i + 2u]) || !cont(s[i + 3u])) return 0;
      i += 4u;
    } else {
      return 0;
    }
  }
  return 1;
}
