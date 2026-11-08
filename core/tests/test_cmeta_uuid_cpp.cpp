#include "tinytest.hpp"
#include <error_codes.h>
#include "cmeta_uuid.h"

static_assert(sizeof(Salts::UUID) == SALTS_UUID_SIZE,
              "Salts::UUID must preserve the C ABI size");

suite("Salts UUID C++") {
  it("uses the installed C type through the Salts namespace") {
    Salts::UUID uuid{};
    char text[SALTS_UUID_STRING_SIZE]{};

    check_equal(cmeta_uuid_v7_generate(&uuid), SALTS_OK);
    check_equal(cmeta_uuid_format(&uuid, text, sizeof(text)), SALTS_OK);
    check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
  }
}
