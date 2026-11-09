#include "tinytest.h"
#include <core/error_codes.h>
#include <core/platform.h>
#include <core/uuid.h>
#if defined(__APPLE__)
#include <uuid/uuid.h>
#endif

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
      check_equal(uuid_v4_generate(uuid), SALTS_OK);
      check_equal(uuid[6] & 0xf0U, 0x40U);
      check_equal(uuid[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values") {
      uuid_t first;
      uuid_t second;

      check_equal(uuid_v4_generate(first), SALTS_OK);
      check_equal(uuid_v4_generate(second), SALTS_OK);
      check_false(uuid_equal(first, second));
    }
  }

  group("UUID version 7") {
    it("rejects a missing destination") {
      check_equal(uuid_v7_generate(NULL), SALTS_EINVAL);
    }

    it("sets the RFC version and variant bits") {
      uuid_t uuid;

      check_equal(uuid_v7_generate(uuid), SALTS_OK);
      check_equal(uuid[6] & 0xf0U, 0x70U);
      check_equal(uuid[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values within one process") {
      uuid_t first;
      uuid_t second;

      check_equal(uuid_v7_generate(first), SALTS_OK);
      check_equal(uuid_v7_generate(second), SALTS_OK);
      check_false(uuid_equal(first, second));
    }
  }

  group("Text conversion") {
    it("uses canonical byte order without a wrapper") {
      const uuid_t expected = {
          0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x46, 0x77,
          0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
      uuid_t uuid;
      uuid_t copied;
      unsigned char *data = uuid;
      char formatted[SALTS_UUID_STRING_SIZE];

      check_equal(uuid_parse_text("00112233-4455-4677-8899-AABBCCDDEEFF", data), SALTS_OK);
      check_equal(uuid, expected, sizeof(uuid));
      memcpy(copied, uuid, sizeof(copied));
      check_true(uuid_equal(uuid, copied));
      check_equal(uuid_format(copied, formatted, sizeof(formatted)), SALTS_OK);
      check_equal(formatted, "00112233-4455-4677-8899-aabbccddeeff");
    }

    it("round-trips a canonical UUID") {
      const char *text = "00112233-4455-4677-8899-aabbccddeeff";
      uuid_t uuid;
      uuid_t reparsed;
      char formatted[SALTS_UUID_STRING_SIZE];

      check_equal(uuid_parse_text(text, uuid), SALTS_OK);
      check_equal(uuid_format(uuid, formatted, sizeof(formatted)), SALTS_OK);
      check_equal(formatted, text);
      check_equal(uuid_parse_text(formatted, reparsed), SALTS_OK);
      check_true(uuid_equal(uuid, reparsed));
    }

    it("rejects malformed and undersized text buffers") {
      uuid_t uuid;
      char short_buffer[SALTS_UUID_STRING_LENGTH];

      check_equal(uuid_parse_text("00112233-4455-4677-8899-aabbccddeefg", uuid), SALTS_EINVAL);
      check_equal(uuid_parse_text("001122334455-4677-8899-aabbccddeeff", uuid), SALTS_EINVAL);
      check_equal(uuid_parse_text("00112233-4455-4677-8899-aabbccddeeff", uuid), SALTS_OK);
      check_equal(uuid_format(uuid, short_buffer, sizeof(short_buffer)), SALTS_ENOSPC);
    }

    it("preserves destination bytes when parsing fails") {
      uuid_t uuid;
      uuid_t original;

      memset(uuid, 0xa5, sizeof(uuid));
      memcpy(original, uuid, sizeof(original));
      check_equal(uuid_parse_text("00112233-4455-4677-8899-aabbccddeefg", uuid), SALTS_EINVAL);
      check_equal(uuid, original, sizeof(uuid));
      check_equal(uuid_parse_text(NULL, uuid), SALTS_EINVAL);
      check_equal(uuid, original, sizeof(uuid));
      check_equal(uuid_parse_text("00112233-4455-4677-8899-aabbccddeeff", NULL), SALTS_EINVAL);
    }

    it("validates formatting and comparison pointers") {
      uuid_t uuid = {0};
      char text[SALTS_UUID_STRING_SIZE];
      char original[SALTS_UUID_STRING_SIZE];

      memset(text, 'x', sizeof(text));
      memcpy(original, text, sizeof(original));
      check_equal(uuid_format(NULL, text, sizeof(text)), SALTS_EINVAL);
      check_equal(text, original, sizeof(text));
      check_equal(uuid_format(uuid, NULL, sizeof(text)), SALTS_EINVAL);
      check_equal(uuid_format(uuid, text, SALTS_UUID_STRING_LENGTH), SALTS_ENOSPC);
      check_equal(text, original, sizeof(text));
      check_false(uuid_equal(NULL, uuid));
      check_false(uuid_equal(uuid, NULL));
    }

    it("converts overlapping byte and text storage") {
      char storage[SALTS_UUID_STRING_SIZE] = "00112233-4455-4677-8899-aabbccddeeff";
      unsigned char *uuid = (unsigned char *)storage;

      check_equal(uuid_parse_text(storage, uuid), SALTS_OK);
      check_equal(uuid[6], 0x46);
      check_equal(uuid[15], 0xff);
      check_equal(uuid_format(uuid, storage, sizeof(storage)), SALTS_OK);
      check_equal(storage, "00112233-4455-4677-8899-aabbccddeeff");
    }
  }
}
