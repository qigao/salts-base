#include "tinytest.hpp"
#include <salts/error_codes.h>
#if defined(__APPLE__)
#include <uuid/uuid.h>
#endif
#include <salts/uuid.h>

static_assert(sizeof(Salts::UUID) == SALTS_UUID_SIZE,
              "Salts::UUID must preserve the C ABI size");

suite("Salts UUID C++") {
  it("uses the installed C type through the Salts namespace") {
    Salts::UUID uuid{};
    char text[SALTS_UUID_STRING_SIZE]{};

    check_equal(uuid128_v7_generate(&uuid), SALTS_OK);
    check_equal(uuid128_format(&uuid, text, sizeof(text)), SALTS_OK);
    check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
  }
}
