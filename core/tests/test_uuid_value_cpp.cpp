#include "tinytest.hpp"
#include <salts/error_codes.h>
#include <salts/uuid_value.h>

static_assert(sizeof(Salts::UUID) == SALTS_UUID_SIZE,
              "Salts::UUID must preserve the C ABI size");

suite("Salts UUID C++") {
  it("uses the installed C type through the Salts namespace") {
    Salts::UUID uuid{};
    char text[SALTS_UUID_STRING_SIZE]{};

    check_equal(uuid_value_v7_generate(&uuid), SALTS_OK);
    check_equal(uuid_value_format(&uuid, text, sizeof(text)), SALTS_OK);
    check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
  }
}
