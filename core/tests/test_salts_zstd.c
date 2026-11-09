#include "tinytest.h"

#include "salts_error.h"
#include "salts_zstd.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

spec("Salts zstd adapter") {
  static const uint8_t payload[] = {
      0x00U, 0x01U, 0x02U, 0x03U, 0x00U, 0xffU, 0xfeU, 0xfdU,
      't',   'u',   'r',   'b',   'o',   '-',   'z',   's',
      't',   'd',   '-',   'r',   'o',   'u',   'n',   'd',
      't',   'r',   'i',   'p'};

  group("one-shot frames") {
    it("round-trips binary data") {
      size_t bound = 0U;
      size_t compressed_size = 0U;
      size_t content_size = 0U;
      size_t decompressed_size = 0U;
      uint8_t decompressed[sizeof(payload)];
      uint8_t *compressed;

      check_equal(salts_zstd_compress_bound(sizeof(payload), &bound), SALTS_OK);
      check_greater_equal(bound, sizeof(payload));
      compressed = (uint8_t *)malloc(bound);
      check_not_null(compressed);

      check_equal(salts_zstd_compress(compressed, bound, &compressed_size,
                                       payload, sizeof(payload), 0),
                   SALTS_OK);
      check_greater(compressed_size, 0U);
      check_equal(salts_zstd_frame_content_size(
                       compressed, compressed_size, &content_size),
                   SALTS_OK);
      check_equal(content_size, sizeof(payload));
      check_equal(salts_zstd_decompress(
                       decompressed, sizeof(decompressed), &decompressed_size,
                       compressed, compressed_size),
                   SALTS_OK);
      check_equal(decompressed_size, sizeof(payload));
      check_equal(decompressed, payload, sizeof(payload));

      free(compressed);
    }

    it("round-trips empty content") {
      size_t bound = 0U;
      size_t compressed_size = 0U;
      size_t content_size = 1U;
      size_t decompressed_size = 1U;
      uint8_t *compressed;

      check_equal(salts_zstd_compress_bound(0U, &bound), SALTS_OK);
      compressed = (uint8_t *)malloc(bound);
      check_not_null(compressed);
      check_equal(salts_zstd_compress(compressed, bound, &compressed_size,
                                       NULL, 0U, 0),
                   SALTS_OK);
      check_equal(salts_zstd_frame_content_size(
                       compressed, compressed_size, &content_size),
                   SALTS_OK);
      check_equal(content_size, 0U);
      check_equal(salts_zstd_decompress(NULL, 0U, &decompressed_size,
                                         compressed, compressed_size),
                   SALTS_OK);
      check_equal(decompressed_size, 0U);

      free(compressed);
    }
  }

  group("errors") {
    it("reports insufficient destination capacity") {
      uint8_t compressed[128];
      uint8_t too_small[sizeof(payload) - 1U];
      size_t compressed_size = 0U;
      size_t output_size = 17U;

      check_equal(salts_zstd_compress(compressed, 1U, &output_size,
                                       payload, sizeof(payload), 0),
                   SALTS_ENOSPC);
      check_equal(output_size, 17U);

      check_equal(salts_zstd_compress(compressed, sizeof(compressed),
                                       &compressed_size, payload,
                                       sizeof(payload), 0),
                   SALTS_OK);
      check_equal(salts_zstd_decompress(
                       too_small, sizeof(too_small), &output_size, compressed,
                       compressed_size),
                   SALTS_ENOSPC);
      check_equal(output_size, 17U);
    }

    it("maps malformed frames to protocol errors") {
      static const uint8_t malformed[] = {0x00U, 0x01U, 0x02U, 0x03U};
      uint8_t output[16];
      size_t output_size = 23U;

      check_equal(salts_zstd_decompress(output, sizeof(output), &output_size,
                                         malformed, sizeof(malformed)),
                   SALTS_EPROTO);
      check_equal(output_size, 23U);
      check_equal(salts_zstd_frame_content_size(
                       malformed, sizeof(malformed), &output_size),
                   SALTS_EPROTO);
      check_equal(output_size, 23U);
    }

    it("rejects invalid arguments without changing outputs") {
      size_t value = 29U;

      check_equal(salts_zstd_compress_bound(1U, NULL), SALTS_EINVAL);
      check_equal(salts_zstd_compress_bound(SIZE_MAX, &value), SALTS_ERANGE);
      check_equal(value, 29U);
      check_equal(salts_zstd_compress(NULL, 0U, &value, payload,
                                       sizeof(payload), 0),
                   SALTS_EINVAL);
      check_equal(value, 29U);
      check_equal(salts_zstd_decompress(NULL, 1U, &value, payload,
                                         sizeof(payload)),
                   SALTS_EINVAL);
      check_equal(value, 29U);
      check_equal(salts_zstd_frame_content_size(NULL, 0U, &value),
                   SALTS_EINVAL);
      check_equal(value, 29U);
      check_equal(salts_zstd_compress((uint8_t[64]){0}, 64U, &value, payload,
                                       sizeof(payload),
                                       salts_zstd_max_level() + 1),
                   SALTS_EINVAL);
      check_equal(value, 29U);
    }
  }

  group("levels") {
    it("exposes a valid default and supported range") {
      check_less_equal(salts_zstd_min_level(), salts_zstd_default_level());
      check_greater_equal(salts_zstd_max_level(), salts_zstd_default_level());
      check_less(salts_zstd_min_level(), salts_zstd_max_level());
    }
  }
}
