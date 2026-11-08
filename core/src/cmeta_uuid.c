#include "cmeta_uuid.h"
#include <random.h>
#include <clock.h>

#include <string.h>

#define SALTS_UUID_V7_TIMESTAMP_MAX UINT64_C(0x0000ffffffffffff)

static int cmeta_uuid_hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

int cmeta_uuid_v4_generate(cmeta_uuid_t *out) {
  cmeta_uuid_t generated;
  int rc;

  if (!out) return SALTS_EINVAL;

  rc = cmeta_platform_secure_random(generated.bytes, sizeof(generated.bytes));
  if (rc != SALTS_OK) return rc;

  generated.bytes[6] = (uint8_t)((generated.bytes[6] & 0x0fU) | 0x40U);
  generated.bytes[8] = (uint8_t)((generated.bytes[8] & 0x3fU) | 0x80U);
  *out = generated;
  return SALTS_OK;
}

int cmeta_uuid_v7_generate(cmeta_uuid_t *out) {
  cmeta_uuid_t generated;
  uint64_t timestamp;
  int rc;

  if (!out) return SALTS_EINVAL;

  rc = cmeta_platform_secure_random(generated.bytes, sizeof(generated.bytes));
  if (rc != SALTS_OK) return rc;

  timestamp = cmeta_realtime_ms();
  if (timestamp > SALTS_UUID_V7_TIMESTAMP_MAX) return SALTS_ERANGE;

  generated.bytes[0] = (uint8_t)(timestamp >> 40);
  generated.bytes[1] = (uint8_t)(timestamp >> 32);
  generated.bytes[2] = (uint8_t)(timestamp >> 24);
  generated.bytes[3] = (uint8_t)(timestamp >> 16);
  generated.bytes[4] = (uint8_t)(timestamp >> 8);
  generated.bytes[5] = (uint8_t)timestamp;
  generated.bytes[6] = (uint8_t)((generated.bytes[6] & 0x0fU) | 0x70U);
  generated.bytes[8] = (uint8_t)((generated.bytes[8] & 0x3fU) | 0x80U);
  *out = generated;
  return SALTS_OK;
}

int cmeta_uuid_parse(const char *text, cmeta_uuid_t *out) {
  static const size_t group_ends[] = {4U, 6U, 8U, 10U};
  cmeta_uuid_t parsed;
  size_t text_index = 0U;
  size_t group_index = 0U;
  size_t byte_index;

  if (!text || !out || strlen(text) != SALTS_UUID_STRING_LENGTH) return SALTS_EINVAL;

  for (byte_index = 0U; byte_index < SALTS_UUID_SIZE; ++byte_index) {
    int high;
    int low;

    if (group_index < sizeof(group_ends) / sizeof(group_ends[0]) &&
        byte_index == group_ends[group_index]) {
      if (text[text_index] != '-') return SALTS_EINVAL;
      ++text_index;
      ++group_index;
    }

    high = cmeta_uuid_hex_value(text[text_index++]);
    low = cmeta_uuid_hex_value(text[text_index++]);
    if (high < 0 || low < 0) return SALTS_EINVAL;
    parsed.bytes[byte_index] = (uint8_t)((high << 4) | low);
  }

  *out = parsed;
  return SALTS_OK;
}

int cmeta_uuid_format(const cmeta_uuid_t *uuid, char *out, size_t out_size) {
  static const char hex[] = "0123456789abcdef";
  static const size_t group_ends[] = {4U, 6U, 8U, 10U};
  cmeta_uuid_t value;
  size_t text_index = 0U;
  size_t group_index = 0U;
  size_t byte_index;

  if (!uuid || !out) return SALTS_EINVAL;
  if (out_size < SALTS_UUID_STRING_SIZE) return SALTS_ENOSPC;

  value = *uuid;
  for (byte_index = 0U; byte_index < SALTS_UUID_SIZE; ++byte_index) {
    if (group_index < sizeof(group_ends) / sizeof(group_ends[0]) &&
        byte_index == group_ends[group_index]) {
      out[text_index++] = '-';
      ++group_index;
    }
    out[text_index++] = hex[value.bytes[byte_index] >> 4];
    out[text_index++] = hex[value.bytes[byte_index] & 0x0fU];
  }
  out[text_index] = '\0';
  return SALTS_OK;
}

bool cmeta_uuid_equal(const cmeta_uuid_t *left, const cmeta_uuid_t *right) {
  if (!left || !right) return false;
  return memcmp(left->bytes, right->bytes, SALTS_UUID_SIZE) == 0;
}
