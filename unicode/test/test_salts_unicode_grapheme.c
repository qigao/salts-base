#include "unicode.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_view(vstr actual, const unsigned char *bytes, size_t length) {
  check_equal(actual.len, length);
  check_equal(memcmp(actual.data, bytes, length), 0);
}

suite("salts_unicode grapheme boundaries") {
  it("reports pinned Extended_Pictographic facts") {
    int result = 7;

    check_equal(salts_unicode_is_extended_pictographic(0x1F469u, &result),
                SALTS_UNICODE_OK);
    check_equal(result, 1);

    check_equal(salts_unicode_is_extended_pictographic(0x00A9u, &result),
                SALTS_UNICODE_OK);
    check_equal(result, 1);

    check_equal(salts_unicode_is_extended_pictographic('A', &result),
                SALTS_UNICODE_OK);
    check_equal(result, 0);

    result = 9;
    check_equal(salts_unicode_is_extended_pictographic(0xD800u, &result),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(result, 9);
    check_equal(salts_unicode_is_extended_pictographic('A', NULL),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
  }

  it("reports pinned grapheme break classes") {
    salts_unicode_grapheme_break value = SALTS_UNICODE_GRAPHEME_OTHER;

    check_equal(salts_unicode_grapheme_break_class(0x000Du, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_CR);
    check_equal(salts_unicode_grapheme_break_class(0x000Au, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_LF);
    check_equal(salts_unicode_grapheme_break_class(0x0301u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_EXTEND);
    check_equal(salts_unicode_grapheme_break_class(0x200Du, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_ZWJ);
    check_equal(salts_unicode_grapheme_break_class(0x1F1FAu, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_REGIONAL_INDICATOR);
    check_equal(salts_unicode_grapheme_break_class(0x1100u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_L);
    check_equal(salts_unicode_grapheme_break_class(0x1161u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_V);
    check_equal(salts_unicode_grapheme_break_class(0x11A8u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_T);
    check_equal(salts_unicode_grapheme_break_class(0xAC00u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_LV);
    check_equal(salts_unicode_grapheme_break_class(0xAC01u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_LVT);

    value = SALTS_UNICODE_GRAPHEME_CONTROL;
    check_equal(salts_unicode_grapheme_break_class('A', &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_GRAPHEME_OTHER);
    check_equal(salts_unicode_grapheme_break_class(0xD800u, &value),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(value, SALTS_UNICODE_GRAPHEME_OTHER);
  }

  it("keeps combining marks CRLF prepend spacing marks and Hangul together") {
    static const unsigned char combining[] = {'e', 0xCC, 0x81};
    static const unsigned char crlf[] = {'\r', '\n'};
    static const unsigned char prepend[] = {0xD8, 0x80, 'A'}; /* U+0600 + A */
    static const unsigned char spacing[] = {0xE0, 0xA4, 0x95, 0xE0, 0xA4, 0xBE}; /* KA + AA */
    static const unsigned char hangul[] = {
        0xE1, 0x84, 0x80, 0xE1, 0x85, 0xA1, 0xE1, 0x86, 0xA8}; /* 1100 1161 11A8 */
    const struct {
      const unsigned char *bytes;
      size_t length;
    } cases[] = {
        {combining, sizeof(combining)}, {crlf, sizeof(crlf)},
        {prepend, sizeof(prepend)}, {spacing, sizeof(spacing)},
        {hangul, sizeof(hangul)}};

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      size_t cursor = 0u;
      vstr cluster = {0};
      const vstr input = vstr_from_buf((const char *)cases[i].bytes, cases[i].length);
      check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster), SALTS_UNICODE_OK);
      check_equal(cursor, cases[i].length);
      check_view(cluster, cases[i].bytes, cases[i].length);
      check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster), SALTS_UNICODE_END);
      check_equal(cursor, cases[i].length);
    }
  }

  it("keeps regional-indicator flags and extended-pictographic ZWJ sequences together") {
    static const unsigned char flag_us[] = {
        0xF0, 0x9F, 0x87, 0xBA, 0xF0, 0x9F, 0x87, 0xB8};
    static const unsigned char woman_technologist[] = {
        0xF0, 0x9F, 0x91, 0xA9, 0xE2, 0x80, 0x8D,
        0xF0, 0x9F, 0x92, 0xBB};
    const struct {
      const unsigned char *bytes;
      size_t length;
    } cases[] = {{flag_us, sizeof(flag_us)},
                 {woman_technologist, sizeof(woman_technologist)}};

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      size_t cursor = 0u;
      vstr cluster = {0};
      const vstr input = vstr_from_buf((const char *)cases[i].bytes, cases[i].length);
      check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster), SALTS_UNICODE_OK);
      check_equal(cursor, cases[i].length);
      check_view(cluster, cases[i].bytes, cases[i].length);
    }
  }

  it("applies Unicode 17 Indic conjunct and RI parity rules") {
    static const unsigned char devanagari_conjunct[] = {
        0xE0, 0xA4, 0x95, /* U+0915 KA */
        0xE0, 0xA5, 0x8D, /* U+094D VIRAMA */
        0xE0, 0xA4, 0x95  /* U+0915 KA */
    };
    static const unsigned char three_ri[] = {
        0xF0, 0x9F, 0x87, 0xA6, /* U+1F1E6 */
        0xF0, 0x9F, 0x87, 0xA7, /* U+1F1E7 */
        0xF0, 0x9F, 0x87, 0xA8  /* U+1F1E8 */
    };
    size_t cursor = 0u;
    vstr cluster = {0};
    vstr input = vstr_from_buf((const char *)devanagari_conjunct,
                               sizeof(devanagari_conjunct));

    check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster),
                SALTS_UNICODE_OK);
    check_equal(cursor, sizeof(devanagari_conjunct));
    check_view(cluster, devanagari_conjunct, sizeof(devanagari_conjunct));

    input = vstr_from_buf((const char *)three_ri, sizeof(three_ri));
    cursor = 0u;
    check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster),
                SALTS_UNICODE_OK);
    check_equal(cursor, 8u);
    check_view(cluster, three_ri, 8u);
    check_equal(salts_unicode_grapheme_next(input, &cursor, &cluster),
                SALTS_UNICODE_OK);
    check_equal(cursor, sizeof(three_ri));
    check_view(cluster, three_ri + 8u, 4u);
  }

  it("iterates backward over the same cluster boundaries") {
    static const unsigned char input_bytes[] = {
        'A',
        'e', 0xCC, 0x81,
        0xF0, 0x9F, 0x87, 0xBA, 0xF0, 0x9F, 0x87, 0xB8};
    const vstr input = vstr_from_buf((const char *)input_bytes, sizeof(input_bytes));
    size_t cursor = input.len;
    vstr cluster = {0};

    check_equal(salts_unicode_grapheme_prev(input, &cursor, &cluster), SALTS_UNICODE_OK);
    check_equal(cursor, 4u);
    check_view(cluster, input_bytes + 4u, sizeof(input_bytes) - 4u);

    check_equal(salts_unicode_grapheme_prev(input, &cursor, &cluster), SALTS_UNICODE_OK);
    check_equal(cursor, 1u);
    check_view(cluster, input_bytes + 1u, 3u);

    check_equal(salts_unicode_grapheme_prev(input, &cursor, &cluster), SALTS_UNICODE_OK);
    check_equal(cursor, 0u);
    check_view(cluster, input_bytes, 1u);

    check_equal(salts_unicode_grapheme_prev(input, &cursor, &cluster), SALTS_UNICODE_END);
    check_equal(cursor, 0u);
  }

  it("fails atomically on malformed UTF-8 and non-boundary reverse cursors") {
    static const unsigned char malformed[] = {'A', 0xFFu};
    const vstr invalid = vstr_from_buf((const char *)malformed, sizeof(malformed));
    size_t cursor = 0u;
    static const char sentinel[] = "sentinel";
    vstr cluster = vstr_from_buf(sentinel, sizeof(sentinel) - 1u);

    check_equal(salts_unicode_grapheme_next(invalid, &cursor, &cluster),
                SALTS_UNICODE_ERR_INVALID_UTF8);
    check_equal(cursor, 0u);
    check_equal(cluster.data, sentinel);
    check_equal(cluster.len, sizeof(sentinel) - 1u);

    static const unsigned char combining[] = {'e', 0xCC, 0x81};
    const vstr input = vstr_from_buf((const char *)combining, sizeof(combining));
    cursor = 1u;
    check_equal(salts_unicode_grapheme_prev(input, &cursor, &cluster),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(cursor, 1u);
    check_equal(cluster.data, sentinel);
    check_equal(cluster.len, sizeof(sentinel) - 1u);
  }
}
