#include "tinytest.hpp"
#include <core/error_codes.h>
#if defined(__APPLE__)
#include <uuid/uuid.h>
#endif
#include <core/uuid.h>
#include <type_traits>

static_assert(sizeof(Salts::UUID) == SALTS_UUID_SIZE,
              "Salts::UUID must preserve the C ABI size");
static_assert(std::is_same<uuid_t, unsigned char[SALTS_UUID_SIZE]>::value,
              "UUID values must be byte arrays");
static_assert(std::is_same<Salts::UUID, uuid_t>::value,
              "The C++ alias must share the public C type");

suite("Salts UUID C++") {
  it("uses the installed C type through the Salts namespace") {
    Salts::UUID uuid{};
    char text[SALTS_UUID_STRING_SIZE]{};

    check_equal(uuid_v7_generate(uuid), SALTS_OK);
    check_equal(uuid_format(uuid, text, sizeof(text)), SALTS_OK);
    check_equal(uuid[6] & 0xf0U, 0x70U);
  }
}
