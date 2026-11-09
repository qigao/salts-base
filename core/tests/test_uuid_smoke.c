#include <core/uuid.h>
#include <tinytest.h>
#include <string.h>

spec("Native UUID versions and canonical text") {
  it("generates v4 and v7, round-trips canonical text and rejects malformed input") {
    uuid_t a = {0}, b = {0};
    char text[SALTS_UUID_STRING_SIZE] = {0};
    check(uuid_v4_generate(a) == SALTS_OK && (a[6] & 0xf0u) == 0x40u);
    check(uuid_v7_generate(b) == SALTS_OK && (b[6] & 0xf0u) == 0x70u);
    check(uuid_format(a, text, sizeof(text)) == SALTS_OK);
    check(uuid_parse_text(text, b) == SALTS_OK && uuid_equal(a, b));
    check(uuid_parse_text("malformed", b) == SALTS_EINVAL);
  }
}
