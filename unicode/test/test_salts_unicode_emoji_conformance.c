#include "unicode.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SALTS_UNICODE_EMOJI_TEST_DATA
#error SALTS_UNICODE_EMOJI_TEST_DATA must identify emoji-data-17.0.0.txt
#endif

#define EXPECTED_RECORD_COUNT 1226u
#define EXPECTED_SCALAR_PROPERTY_CHECKS 5790u

static uint32_t property_flag(const char *name) {
  if (strcmp(name, "Emoji") == 0) return SALTS_UNICODE_PROPERTY_EMOJI;
  if (strcmp(name, "Emoji_Presentation") == 0)
    return SALTS_UNICODE_PROPERTY_EMOJI_PRESENTATION;
  if (strcmp(name, "Emoji_Modifier") == 0)
    return SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER;
  if (strcmp(name, "Emoji_Modifier_Base") == 0)
    return SALTS_UNICODE_PROPERTY_EMOJI_MODIFIER_BASE;
  if (strcmp(name, "Emoji_Component") == 0)
    return SALTS_UNICODE_PROPERTY_EMOJI_COMPONENT;
  if (strcmp(name, "Extended_Pictographic") == 0)
    return SALTS_UNICODE_PROPERTY_EXTENDED_PICTOGRAPHIC;
  return 0u;
}

static char *trim(char *text) {
  char *end;
  while (*text == ' ' || *text == '\t') ++text;
  end = text + strlen(text);
  while (end > text &&
         (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    --end;
  *end = '\0';
  return text;
}

spec("salts_unicode Unicode 17 emoji-data conformance") {
  it("matches every vendored emoji property range") {
    FILE *file = fopen(SALTS_UNICODE_EMOJI_TEST_DATA, "rb");
    char line[4096];
    size_t records = 0u;
    size_t scalar_checks = 0u;

    check_not_null(file);
    if (file == NULL) return;

    while (fgets(line, sizeof(line), file) != NULL) {
      char *comment = strchr(line, '#');
      char *semi;
      char *range;
      char *property;
      char *dots;
      char *end = NULL;
      unsigned long first;
      unsigned long last;
      uint32_t flag;

      if (comment != NULL) *comment = '\0';
      range = trim(line);
      if (*range == '\0') continue;

      semi = strchr(range, ';');
      check_not_null(semi);
      if (semi == NULL) continue;
      *semi = '\0';
      property = trim(semi + 1);
      range = trim(range);

      flag = property_flag(property);
      check_true(flag != 0u);
      if (flag == 0u) continue;

      dots = strstr(range, "..");
      if (dots != NULL) {
        *dots = '\0';
        first = strtoul(trim(range), &end, 16);
        check_true(end != trim(range) && *end == '\0');
        last = strtoul(trim(dots + 2), &end, 16);
        check_true(end != trim(dots + 2) && *end == '\0');
      } else {
        first = strtoul(range, &end, 16);
        check_true(end != range && *end == '\0');
        last = first;
      }

      check_true(first <= last && last <= 0x10FFFFul);
      for (unsigned long scalar = first; scalar <= last; ++scalar) {
        uint32_t properties = 0u;
        check_equal(salts_unicode_scalar_properties((uint32_t)scalar, &properties),
                    SALTS_UNICODE_OK);
        check_true((properties & flag) != 0u);
        ++scalar_checks;
      }
      ++records;
    }

    check_equal(ferror(file), 0);
    fclose(file);
    check_equal(records, (size_t)EXPECTED_RECORD_COUNT);
    check_equal(scalar_checks, (size_t)EXPECTED_SCALAR_PROPERTY_CHECKS);
  }
}
