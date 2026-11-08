#include "unicode.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

spec("salts_unicode Unicode 17 emoji properties") {
  it("exposes versioned scalar emoji property flags") {
    uint32_t props = 0u;

    check_equal(salts_unicode_scalar_properties(0x1F600u, &props), SALTS_UNICODE_OK);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI) != 0u);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI_PRESENTATION) != 0u);
    check_true((props & SALTS_UNICODE_PROPERTY_EXTENDED_PICTOGRAPHIC) != 0u);

    check_equal(salts_unicode_scalar_properties(0x1F3FBu, &props), SALTS_UNICODE_OK);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER) != 0u);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI_COMPONENT) != 0u);

    check_equal(salts_unicode_scalar_properties(0x1F466u, &props), SALTS_UNICODE_OK);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER_BASE) != 0u);

    check_equal(salts_unicode_scalar_properties(0x200Du, &props), SALTS_UNICODE_OK);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI_COMPONENT) != 0u);

    check_equal(salts_unicode_scalar_properties(0x00A9u, &props), SALTS_UNICODE_OK);
    check_true((props & SALTS_UNICODE_PROPERTY_EMOJI) != 0u);
    check_false((props & SALTS_UNICODE_PROPERTY_EMOJI_PRESENTATION) != 0u);

    check_equal(salts_unicode_scalar_properties((uint32_t)'A', &props), SALTS_UNICODE_OK);
    check_equal(props & (SALTS_UNICODE_PROPERTY_EMOJI |
                         SALTS_UNICODE_PROPERTY_EMOJI_PRESENTATION |
                         SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER |
                         SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER_BASE |
                         SALTS_UNICODE_PROPERTY_EMOJI_COMPONENT |
                         SALTS_UNICODE_PROPERTY_EXTENDED_PICTOGRAPHIC),
                0u);
  }

  it("attaches emoji facts to decoded UTF-8 scalars") {
    static const unsigned char input_bytes[] = {0xF0u, 0x9Fu, 0x98u, 0x80u};
    const vstr input = vstr_from_buf((const char *)input_bytes, sizeof(input_bytes));
    salts_unicode_scalar scalar = {0};
    size_t cursor = 0u;

    check_equal(salts_unicode_utf8_next(input, &cursor, &scalar), SALTS_UNICODE_OK);
    check_equal(scalar.value, 0x1F600u);
    check_true((scalar.properties & SALTS_UNICODE_PROPERTY_EMOJI_PRESENTATION) != 0u);
    check_true((scalar.properties & SALTS_UNICODE_PROPERTY_EXTENDED_PICTOGRAPHIC) != 0u);
    check_equal(cursor, sizeof(input_bytes));
  }

  it("keeps output unchanged for invalid scalars") {
    uint32_t props = 0xA5A5A5A5u;
    check_equal(salts_unicode_scalar_properties(0xD800u, &props),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(props, 0xA5A5A5A5u);
  }
}
