#include <asn1_value.h>
#include <tinytest.h>
#include <string.h>

static uint8_t input_storage[65536u + 32u];
static uint8_t encoded[65536u + 32u];
static volatile size_t observed_size;

#define OID_BENCH(title_, length_, multibyte_) \
  bench(title_) { \
    uint8_t *input = input_storage + 1u; \
    const size_t length = (length_); \
    memset(input, 0x2a, length); \
    if (multibyte_) { \
      for (size_t i = (multibyte_) == 2 ? 1u : 0u; i + 1u < length; i += 2u) { \
        input[i] = 0x81; input[i + 1u] = 1u; \
      } \
    } \
    size_t written = 0u, failures = 0u; \
    check_equal(asn1_encode_tlv(0u, 0u, 6u, input, length, SALTS_ASN1_DER, \
                                0u, encoded, sizeof(encoded), &written), SALTS_ASN1_OK); \
    const size_t operations = length < 256u ? 1024u : 4u; \
    benchmark_ops("complete DER OID validation", 32, operations) { \
      for (size_t i = 0u; i < operations; ++i) { \
        const asn1_status status = asn1_validate(encoded, written, SALTS_ASN1_DER); \
        if (status != SALTS_ASN1_OK) ++failures; \
        observed_size = (size_t)status; \
      } \
    } \
    check_equal(failures, 0u); \
  }

#define MAGNITUDE_BENCH(title_, length_, padded_) \
  bench(title_) { \
    uint8_t *input = input_storage + 1u; \
    const size_t length = (length_); \
    memset(input, (padded_) ? 0 : 0x7f, length); \
    input[length - 1u] = 0x7f; \
    asn1_value *value = NULL; \
    check_equal(asn1_value_create(0u, 0u, 2u, input, length, &value), SALTS_ASN1_OK); \
    const uint8_t *result = NULL; \
    size_t size = 0u, failures = 0u; \
    const size_t operations = length < 256u ? 1024u : 4u; \
    benchmark_ops("borrow minimal integer magnitude", 32, operations) { \
      for (size_t i = 0u; i < operations; ++i) { \
        if (asn1_value_get_unsigned_integer(value, &result, &size) != SALTS_ASN1_OK) ++failures; \
        observed_size = size; \
      } \
    } \
    check_equal(failures, 0u); \
    check_equal(size, (padded_) ? 1u : length); \
    check_equal(result[0], 0x7fu); \
    asn1_value_free(value); \
  }

suite("ASN.1 counted scan workloads") {
  OID_BENCH("8-byte short-arc OID", 8u, 0)
  OID_BENCH("16-byte short-arc OID", 16u, 0)
  OID_BENCH("17-byte short-arc OID", 17u, 0)
  OID_BENCH("256-byte short-arc OID", 256u, 0)
  OID_BENCH("64-KiB short-arc OID", 65536u, 0)
  OID_BENCH("64-KiB multibyte-arc OID", 65536u, 1)
  OID_BENCH("64-KiB mixed short and multibyte OID", 65536u, 2)
  MAGNITUDE_BENCH("8-byte padded magnitude", 8u, 1)
  MAGNITUDE_BENCH("16-byte padded magnitude", 16u, 1)
  MAGNITUDE_BENCH("17-byte padded magnitude", 17u, 1)
  MAGNITUDE_BENCH("256-byte padded magnitude", 256u, 1)
  MAGNITUDE_BENCH("64-KiB padded magnitude", 65536u, 1)
  MAGNITUDE_BENCH("64-KiB already minimal magnitude", 65536u, 0)
}
