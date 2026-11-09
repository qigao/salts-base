#include "tinytest.h"
#include <salts/error_codes.h>
#include <salts/platform.h>
#include <salts/uuid.h>

#include <string.h>

suite("Salts UUID") {
  group("Secure random") {
    it("validates destination buffers") {
      check_equal(secure_random(NULL, 0U), SALTS_OK);
      check_equal(secure_random(NULL, 1U), SALTS_EINVAL);
    }

    it("fills independent buffers") {
      uint8_t first[32];
      uint8_t second[32];

      memset(first, 0, sizeof(first));
      memset(second, 0, sizeof(second));
      check_equal(secure_random(first, sizeof(first)), SALTS_OK);
      check_equal(secure_random(second, sizeof(second)), SALTS_OK);
      check_not_equal(first, second, sizeof(first));
    }
  }

  group("UUID version 4") {
    it("rejects a missing destination") {
      check_equal(uuid_v4_generate(NULL), SALTS_EINVAL);
    }

    it("generates a fixed-size RFC variant value") {
      uuid_t uuid;

      check_equal(sizeof(uuid), SALTS_UUID_SIZE);
      check_equal(uuid_v4_generate(&uuid), SALTS_OK);
      check_equal(uuid.bytes[6] & 0xf0U, 0x40U);
      check_equal(uuid.bytes[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values") {
      uuid_t first;
      uuid_t second;

      check_equal(uuid_v4_generate(&first), SALTS_OK);
      check_equal(uuid_v4_generate(&second), SALTS_OK);
      check_false(uuid_equal(&first, &second));
    }
  }

  group("UUID version 7") {
    it("rejects a missing destination") {
      check_equal(uuid_v7_generate(NULL), SALTS_EINVAL);
    }

    it("sets the RFC version and variant bits") {
      uuid_t uuid;

      check_equal(uuid_v7_generate(&uuid), SALTS_OK);
      check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
      check_equal(uuid.bytes[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values within one process") {
      uuid_t first;
      uuid_t second;

      check_equal(uuid_v7_generate(&first), SALTS_OK);
      check_equal(uuid_v7_generate(&second), SALTS_OK);
      check_false(uuid_equal(&first, &second));
    }
  }

  group("Text conversion") {
    it("round-trips a canonical UUID") {
      const char *text = "00112233-4455-4677-8899-aabbccddeeff";
      uuid_t uuid;
      uuid_t reparsed;
      char formatted[SALTS_UUID_STRING_SIZE];

      check_equal(uuid_parse(text, &uuid), SALTS_OK);
      check_equal(uuid_format(&uuid, formatted, sizeof(formatted)), SALTS_OK);
      check_equal(formatted, text);
      check_equal(uuid_parse(formatted, &reparsed), SALTS_OK);
      check_true(uuid_equal(&uuid, &reparsed));
    }

    it("rejects malformed and undersized text buffers") {
      uuid_t uuid;
      char short_buffer[SALTS_UUID_STRING_LENGTH];

      check_equal(uuid_parse("00112233-4455-4677-8899-aabbccddeefg", &uuid), SALTS_EINVAL);
      check_equal(uuid_parse("001122334455-4677-8899-aabbccddeeff", &uuid), SALTS_EINVAL);
      check_equal(uuid_parse("00112233-4455-4677-8899-aabbccddeeff", &uuid), SALTS_OK);
      check_equal(uuid_format(&uuid, short_buffer, sizeof(short_buffer)), SALTS_ENOSPC);
    }
  }
}
