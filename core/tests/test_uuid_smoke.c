#include <salts/uuid.h>
#include <tinytest.h>
#include <string.h>

spec("Native UUID versions and canonical text") {
  it("generates v4 and v7, round-trips canonical text and rejects malformed input") {
    uuid128_t a = {{0}}, b = {{0}};
    char text[SALTS_UUID_STRING_SIZE] = {0};
    check(uuid128_v4_generate(&a) == SALTS_OK && (a.bytes[6] & 0xf0u) == 0x40u);
    check(uuid128_v7_generate(&b) == SALTS_OK && (b.bytes[6] & 0xf0u) == 0x70u);
    check(uuid128_format(&a, text, sizeof(text)) == SALTS_OK);
    check(uuid128_parse(text, &b) == SALTS_OK && uuid128_equal(&a, &b));
    check(uuid128_parse("malformed", &b) == SALTS_EINVAL);
  }
}
