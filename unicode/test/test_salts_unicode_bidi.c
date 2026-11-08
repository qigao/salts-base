#include "unicode.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

spec("salts_unicode Unicode 17 bidi primitives") {
  it("queries Unicode 17 Bidi_Class values") {
    salts_unicode_bidi_class value = SALTS_UNICODE_BIDI_ON;

    check_equal(salts_unicode_bidi_class_of('A', &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_L);

    check_equal(salts_unicode_bidi_class_of(0x05D0u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_R);

    /* U+0590 has no explicit record; Unicode 17 @missing assigns Hebrew R. */
    check_equal(salts_unicode_bidi_class_of(0x0590u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_R);

    check_equal(salts_unicode_bidi_class_of(0x0627u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_AL);

    check_equal(salts_unicode_bidi_class_of('7', &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_EN);

    check_equal(salts_unicode_bidi_class_of(0x2067u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_RLI);

    check_equal(salts_unicode_bidi_class_of(0x2069u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_BIDI_PDI);

    value = SALTS_UNICODE_BIDI_BN;
    check_equal(salts_unicode_bidi_class_of(0xD800u, &value),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(value, SALTS_UNICODE_BIDI_BN);
  }

  it("derives paragraph base level with UAX 9 P2 and P3") {
    static const unsigned char ltr[] = {'1', '2', ' ', 'A'};
    static const unsigned char rtl[] = {'1', '2', ' ', 0xD7, 0x90}; /* Hebrew ALEF */
    static const unsigned char arabic[] = {0xD8, 0xA7}; /* Arabic ALEF */
    static const unsigned char neutral[] = {'1', '2', ' ', '(', ')'};
    uint8_t level = 99u;

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)ltr, sizeof(ltr)), &level),
                SALTS_UNICODE_OK);
    check_equal(level, 0u);

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)rtl, sizeof(rtl)), &level),
                SALTS_UNICODE_OK);
    check_equal(level, 1u);

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)arabic, sizeof(arabic)), &level),
                SALTS_UNICODE_OK);
    check_equal(level, 1u);

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)neutral, sizeof(neutral)), &level),
                SALTS_UNICODE_OK);
    check_equal(level, 0u);
  }

  it("ignores nested isolate contents while determining paragraph level") {
    static const unsigned char input[] = {
        0xE2, 0x81, 0xA7,             /* U+2067 RLI */
        0xD7, 0x90,                   /* Hebrew ALEF */
        0xE2, 0x81, 0xA9,             /* U+2069 PDI */
        ' ', 'A'
    };
    uint8_t level = 77u;

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)input, sizeof(input)), &level),
                SALTS_UNICODE_OK);
    check_equal(level, 0u);
  }

  it("keeps paragraph output unchanged on malformed UTF-8") {
    static const unsigned char malformed[] = {'A', 0xFFu};
    uint8_t level = 42u;

    check_equal(salts_unicode_bidi_paragraph_level(
                    vstr_from_buf((const char *)malformed, sizeof(malformed)),
                    &level),
                SALTS_UNICODE_ERR_INVALID_UTF8);
    check_equal(level, 42u);
  }
}
