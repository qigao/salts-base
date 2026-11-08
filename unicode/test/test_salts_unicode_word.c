#include "unicode.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_view(vstr view, const unsigned char *bytes, size_t length) {
  check_equal(view.len, length);
  if (view.len == length)
    check_equal(memcmp(view.data, bytes, length), 0);
}

spec("salts_unicode Unicode 17 word boundaries") {
  it("reports representative Word_Break classes") {
    salts_unicode_word_break value = SALTS_UNICODE_WORD_OTHER;

    check_equal(salts_unicode_word_break_class('A', &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_WORD_ALETTER);
    check_equal(salts_unicode_word_break_class('7', &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_WORD_NUMERIC);
    check_equal(salts_unicode_word_break_class(0x05D0u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_WORD_HEBREW_LETTER);
    check_equal(salts_unicode_word_break_class(0x0301u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_WORD_EXTEND);
    check_equal(salts_unicode_word_break_class(0x200Du, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_WORD_ZWJ);

    value = SALTS_UNICODE_WORD_OTHER;
    check_equal(salts_unicode_word_break_class(0xD800u, &value),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(value, SALTS_UNICODE_WORD_OTHER);
  }

  it("keeps contractions decimals and whitespace runs on default boundaries") {
    static const unsigned char contraction[] = "can't";
    static const unsigned char decimal[] = "3.14";
    static const unsigned char spaces[] = "   ";
    const struct {
      const unsigned char *bytes;
      size_t length;
    } cases[] = {
        {contraction, sizeof(contraction) - 1u},
        {decimal, sizeof(decimal) - 1u},
        {spaces, sizeof(spaces) - 1u},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      size_t cursor = 0u;
      vstr segment = {0};
      const vstr input =
          vstr_from_buf((const char *)cases[i].bytes, cases[i].length);

      check_equal(salts_unicode_word_next(input, &cursor, &segment),
                  SALTS_UNICODE_OK);
      check_equal(cursor, cases[i].length);
      check_view(segment, cases[i].bytes, cases[i].length);
      check_equal(salts_unicode_word_next(input, &cursor, &segment),
                  SALTS_UNICODE_END);
      check_equal(cursor, cases[i].length);
    }
  }

  it("applies Hebrew quote emoji ZWJ and RI parity rules") {
    static const unsigned char hebrew_quote[] = {
        0xD7, 0x90, '"', 0xD7, 0x91}; /* ALEF " BET */
    static const unsigned char woman_technologist[] = {
        0xF0, 0x9F, 0x91, 0xA9, 0xE2, 0x80, 0x8D,
        0xF0, 0x9F, 0x92, 0xBB};
    static const unsigned char three_ri[] = {
        0xF0, 0x9F, 0x87, 0xA6,
        0xF0, 0x9F, 0x87, 0xA7,
        0xF0, 0x9F, 0x87, 0xA8};
    size_t cursor = 0u;
    vstr segment = {0};
    vstr input = vstr_from_buf((const char *)hebrew_quote,
                               sizeof(hebrew_quote));

    check_equal(salts_unicode_word_next(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, sizeof(hebrew_quote));
    check_view(segment, hebrew_quote, sizeof(hebrew_quote));

    input = vstr_from_buf((const char *)woman_technologist,
                          sizeof(woman_technologist));
    cursor = 0u;
    check_equal(salts_unicode_word_next(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, sizeof(woman_technologist));
    check_view(segment, woman_technologist, sizeof(woman_technologist));

    input = vstr_from_buf((const char *)three_ri, sizeof(three_ri));
    cursor = 0u;
    check_equal(salts_unicode_word_next(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, 8u);
    check_view(segment, three_ri, 8u);
    check_equal(salts_unicode_word_next(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, sizeof(three_ri));
    check_view(segment, three_ri + 8u, 4u);
  }

  it("iterates backward over the same default word boundaries") {
    static const unsigned char bytes[] = "can't 3.14";
    const vstr input = vstr_from_buf((const char *)bytes, sizeof(bytes) - 1u);
    size_t cursor = input.len;
    vstr segment = {0};

    check_equal(salts_unicode_word_prev(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, 6u);
    check_view(segment, bytes + 6u, 4u);

    check_equal(salts_unicode_word_prev(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, 5u);
    check_view(segment, bytes + 5u, 1u);

    check_equal(salts_unicode_word_prev(input, &cursor, &segment),
                SALTS_UNICODE_OK);
    check_equal(cursor, 0u);
    check_view(segment, bytes, 5u);

    check_equal(salts_unicode_word_prev(input, &cursor, &segment),
                SALTS_UNICODE_END);
    check_equal(cursor, 0u);
  }

  it("fails atomically on malformed UTF-8 and non-boundary cursors") {
    static const unsigned char malformed[] = {'A', 0xFFu};
    const vstr invalid = vstr_from_buf((const char *)malformed,
                                      sizeof(malformed));
    static const char sentinel[] = "sentinel";
    vstr segment = vstr_from_buf(sentinel, sizeof(sentinel) - 1u);
    size_t cursor = 0u;

    check_equal(salts_unicode_word_next(invalid, &cursor, &segment),
                SALTS_UNICODE_ERR_INVALID_UTF8);
    check_equal(cursor, 0u);
    check_equal(segment.data, sentinel);
    check_equal(segment.len, sizeof(sentinel) - 1u);

    {
      static const unsigned char word[] = "can't";
      const vstr input = vstr_from_buf((const char *)word, sizeof(word) - 1u);
      cursor = 1u;
      check_equal(salts_unicode_word_next(input, &cursor, &segment),
                  SALTS_UNICODE_ERR_INVALID_ARGUMENT);
      check_equal(cursor, 1u);
      check_equal(segment.data, sentinel);
      check_equal(segment.len, sizeof(sentinel) - 1u);

      cursor = 4u;
      check_equal(salts_unicode_word_prev(input, &cursor, &segment),
                  SALTS_UNICODE_ERR_INVALID_ARGUMENT);
      check_equal(cursor, 4u);
      check_equal(segment.data, sentinel);
      check_equal(segment.len, sizeof(sentinel) - 1u);
    }
  }
}
