#include "tinytest.h"
#include "salts_selector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TEST_FIELDS[] = {
    "region", "role", "node.id", "platform", "arch"};

typedef struct selector_record_s {
  const char *region;
  const char *role;
  const char *tag_tier;
  const char *const *capabilities;
  size_t capability_count;
} selector_record_t;

static salts_selector_schema_v1_t test_schema(void) {
  salts_selector_schema_v1_t schema;
  memset(&schema, 0, sizeof(schema));
  schema.size = SALTS_SELECTOR_SCHEMA_V1_SIZE;
  schema.allowed_fields = TEST_FIELDS;
  schema.allowed_field_count = sizeof(TEST_FIELDS) / sizeof(TEST_FIELDS[0]);
  schema.allow_tag_fields = 1;
  return schema;
}

static int resolve_field(void *context, const char *field, size_t field_size,
                         const char **out_value, size_t *out_value_size) {
  const selector_record_t *record = (const selector_record_t *)context;
  const char *value = NULL;
  if (field_size == 6u && memcmp(field, "region", 6u) == 0)
    value = record->region;
  else if (field_size == 4u && memcmp(field, "role", 4u) == 0)
    value = record->role;
  else if (field_size == 8u && memcmp(field, "tag.tier", 8u) == 0)
    value = record->tag_tier;
  if (!value) return 0;
  *out_value = value;
  *out_value_size = strlen(value);
  return 1;
}

static int has_capability(void *context, const char *capability,
                          size_t capability_size) {
  const selector_record_t *record = (const selector_record_t *)context;
  size_t index;
  for (index = 0u; index < record->capability_count; ++index) {
    if (strlen(record->capabilities[index]) == capability_size &&
        memcmp(record->capabilities[index], capability, capability_size) == 0)
      return 1;
  }
  return 0;
}

static salts_selector_program_t *compile_selector(
    const char *source, salts_selector_diagnostic_v1_t *diagnostic) {
  salts_selector_schema_v1_t schema = test_schema();
  salts_selector_program_t *program = NULL;
  diagnostic->size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
  check_equal(salts_selector_compile_v1(source, strlen(source), &schema,
                                         &program, diagnostic),
               SALTS_SELECTOR_OK);
  return program;
}

static char *make_membership_selector(size_t item_count) {
  size_t capacity = 32u + item_count * 16u;
  size_t used = 0u;
  char *source = (char *)malloc(capacity);
  if (!source) return NULL;
  used = (size_t)snprintf(source, capacity, "role in [");
  for (size_t index = 0u; index < item_count; ++index) {
    int written = snprintf(source + used, capacity - used,
                           index == 0u ? "\"v%03zu\"" : ",\"v%03zu\"",
                           index);
    if (written < 0 || (size_t)written >= capacity - used) {
      free(source);
      return NULL;
    }
    used += (size_t)written;
  }
  if (used + 2u > capacity) {
    free(source);
    return NULL;
  }
  source[used++] = ']';
  source[used] = '\0';
  return source;
}

spec("bounded resource selector DSL") {
  describe("re2c and Lemon compilation") {
    it("compiles precedence and renders a deterministic canonical form") {
      static const char source[] =
          "region==\"eu-west\" && role in [\"cache\",\"edge\"] || "
          "capability(\"m3-cache\")";
      static const char expected[] =
          "((region == \"eu-west\" && role in [\"cache\", \"edge\"]) || "
          "capability(\"m3-cache\"))";
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program = compile_selector(source, &diagnostic);
      char canonical[256];
      size_t required = 0u;

      check_not_null(program);
      check_equal(salts_selector_program_language_version(program), 1u);
      check_equal(salts_selector_program_predicate_count(program), 3u);
      check_equal(salts_selector_program_canonical_v1(
                       program, canonical, sizeof(canonical), &required,
                       &diagnostic),
                   SALTS_SELECTOR_OK);
      check_equal(canonical, expected);
      check_equal(required, strlen(expected));

      salts_selector_program_destroy(program);
    }

    it("canonicalizes Unicode escapes and list order") {
      static const char source[] =
          "tag.tier in [\"z\", \"\\u8fb9\\u7f18\", \"a\"]";
      static const char expected[] =
          "tag.tier in [\"a\", \"z\", \"\xe8\xbe\xb9\xe7\xbc\x98\"]";
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program = compile_selector(source, &diagnostic);
      char canonical[256];
      size_t required = 0u;

      check_not_null(program);
      check_equal(salts_selector_program_canonical_v1(
                       program, canonical, sizeof(canonical), &required,
                       &diagnostic),
                   SALTS_SELECTOR_OK);
      check_equal(canonical, expected);

      salts_selector_program_destroy(program);
    }

    it("keeps decoded control bytes in a reparsable canonical form") {
      static const char source[] = "role == \"\\u0000\\u001f\"";
      static const char expected[] = "role == \"\\u0000\\u001f\"";
      salts_selector_schema_v1_t schema = test_schema();
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program = compile_selector(source, &diagnostic);
      salts_selector_program_t *round_trip = NULL;
      char canonical[64];
      size_t required = 0u;

      check_equal(salts_selector_program_canonical_v1(
                       program, canonical, sizeof(canonical), &required,
                       &diagnostic),
                   SALTS_SELECTOR_OK);
      check_equal(canonical, expected);
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_compile_v1(
                       canonical, required, &schema, &round_trip, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_not_null(round_trip);
      salts_selector_program_destroy(round_trip);
      salts_selector_program_destroy(program);
    }

    it("rejects duplicate set values instead of changing their meaning") {
      salts_selector_schema_v1_t schema = test_schema();
      salts_selector_diagnostic_v1_t diagnostic = {
          SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE};
      salts_selector_program_t *program = NULL;
      const char *source = "role in [\"edge\", \"edge\"]";

      check_equal(salts_selector_compile_v1(
                       source, strlen(source), &schema, &program, &diagnostic),
                   SALTS_SELECTOR_SEMANTIC_ERROR);
      check_null(program);
      check_contains(diagnostic.message, "duplicate");
    }

    it("enforces source and decoded-string byte limits exactly") {
      salts_selector_schema_v1_t schema = test_schema();
      salts_selector_diagnostic_v1_t diagnostic = {
          SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE};
      salts_selector_program_t *program = NULL;
      char *source = (char *)malloc(SALTS_SELECTOR_MAX_SOURCE_BYTES_V1 + 2u);
      char string_source[SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 16u];
      size_t prefix_size = strlen("role == \"");

      check_not_null(source);
      memcpy(source, "role == \"x\"", strlen("role == \"x\""));
      memset(source + strlen("role == \"x\""), ' ',
             SALTS_SELECTOR_MAX_SOURCE_BYTES_V1 - strlen("role == \"x\""));
      source[SALTS_SELECTOR_MAX_SOURCE_BYTES_V1] = '\0';
      check_equal(salts_selector_compile_v1(
                       source, SALTS_SELECTOR_MAX_SOURCE_BYTES_V1, &schema,
                       &program, &diagnostic),
                   SALTS_SELECTOR_OK);
      salts_selector_program_destroy(program);
      program = NULL;
      source[SALTS_SELECTOR_MAX_SOURCE_BYTES_V1] = ' ';
      check_equal(salts_selector_compile_v1(
                       source, SALTS_SELECTOR_MAX_SOURCE_BYTES_V1 + 1u,
                       &schema, &program, &diagnostic),
                   SALTS_SELECTOR_RESOURCE_LIMIT);
      free(source);

      memcpy(string_source, "role == \"", prefix_size);
      memset(string_source + prefix_size, 'a',
             SALTS_SELECTOR_MAX_STRING_BYTES_V1);
      string_source[prefix_size + SALTS_SELECTOR_MAX_STRING_BYTES_V1] = '"';
      string_source[prefix_size + SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 1u] =
          '\0';
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_compile_v1(
                       string_source, strlen(string_source), &schema, &program,
                       &diagnostic),
                   SALTS_SELECTOR_OK);
      salts_selector_program_destroy(program);
      program = NULL;
      string_source[prefix_size + SALTS_SELECTOR_MAX_STRING_BYTES_V1] = 'a';
      string_source[prefix_size + SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 1u] =
          '"';
      string_source[prefix_size + SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 2u] =
          '\0';
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_compile_v1(
                       string_source, strlen(string_source), &schema, &program,
                       &diagnostic),
                   SALTS_SELECTOR_RESOURCE_LIMIT);
    }

    it("accepts 128 list items and rejects item 129") {
      salts_selector_schema_v1_t schema = test_schema();
      salts_selector_diagnostic_v1_t diagnostic = {
          SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE};
      salts_selector_program_t *program = NULL;
      char *source = make_membership_selector(
          SALTS_SELECTOR_MAX_LIST_ITEMS_V1);

      check_not_null(source);
      check_equal(salts_selector_compile_v1(source, strlen(source), &schema,
                                             &program, &diagnostic),
                   SALTS_SELECTOR_OK);
      salts_selector_program_destroy(program);
      free(source);
      program = NULL;
      source = make_membership_selector(SALTS_SELECTOR_MAX_LIST_ITEMS_V1 + 1u);
      check_not_null(source);
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_compile_v1(source, strlen(source), &schema,
                                             &program, &diagnostic),
                   SALTS_SELECTOR_RESOURCE_LIMIT);
      check_contains(diagnostic.message, "list item");
      free(source);
    }

    it("supports canonical size queries and reports a short buffer") {
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program =
          compile_selector("role not in [\"edge\"]", &diagnostic);
      size_t required = 0u;
      char output[8];

      check_equal(salts_selector_program_canonical_v1(
                       program, NULL, 0u, &required, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_true(required > sizeof(output));
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_program_canonical_v1(
                       program, output, sizeof(output), &required,
                       &diagnostic),
                   SALTS_SELECTOR_BUFFER_TOO_SMALL);
      check_equal(output[sizeof(output) - 1u], 0);
      salts_selector_program_destroy(program);
    }

    it("rejects schema field names that the lexer cannot produce") {
      const char *const bad_fields[] = {"node..id"};
      const char *const reserved_fields[] = {"has"};
      salts_selector_schema_v1_t schema = {
          SALTS_SELECTOR_SCHEMA_V1_SIZE, bad_fields, 1u, 0};
      salts_selector_diagnostic_v1_t diagnostic = {
          SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE};
      salts_selector_program_t *program = NULL;

      check_equal(salts_selector_compile_v1(
                       "node.id == \"n1\"", strlen("node.id == \"n1\""),
                       &schema, &program, &diagnostic),
                   SALTS_SELECTOR_INVALID_ARGUMENT);
      check_null(program);
      schema.allowed_fields = reserved_fields;
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_compile_v1(
                       "has == \"x\"", strlen("has == \"x\""), &schema,
                       &program, &diagnostic),
                   SALTS_SELECTOR_INVALID_ARGUMENT);
    }
  }

  describe("fail-closed immutable evaluation") {
    it("evaluates fields tags capabilities and boolean precedence") {
      const char *capabilities[] = {"m3-cache", "stream-v1"};
      selector_record_t record = {"eu-west", "edge", "gold", capabilities,
                                  2u};
      salts_selector_eval_ops_v1_t ops = {
          SALTS_SELECTOR_EVAL_OPS_V1_SIZE, resolve_field, has_capability};
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program = compile_selector(
          "region == \"eu-west\" && role in [\"cache\", \"edge\"] && "
          "tag.tier != \"bronze\" && capability(\"m3-cache\")",
          &diagnostic);
      int matched = 0;

      check_not_null(program);
      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_program_evaluate_v1(
                       program, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_true(matched);

      record.role = "origin";
      check_equal(salts_selector_program_evaluate_v1(
                       program, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_false(matched);

      salts_selector_program_destroy(program);
    }

    it("keeps missing comparisons unknown through negation") {
      selector_record_t record = {0};
      salts_selector_eval_ops_v1_t ops = {
          SALTS_SELECTOR_EVAL_OPS_V1_SIZE, resolve_field, has_capability};
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *comparison =
          compile_selector("!(region == \"eu\")", &diagnostic);
      salts_selector_program_t *presence =
          compile_selector("!has(region)", &diagnostic);
      int matched = 1;

      check_equal(salts_selector_program_evaluate_v1(
                       comparison, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_false(matched);
      check_equal(salts_selector_program_evaluate_v1(
                       presence, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_true(matched);

      salts_selector_program_destroy(presence);
      salts_selector_program_destroy(comparison);
    }

    it("applies Kleene OR and membership negation deterministically") {
      selector_record_t record = {NULL, "edge", NULL, NULL, 0u};
      salts_selector_eval_ops_v1_t ops = {
          SALTS_SELECTOR_EVAL_OPS_V1_SIZE, resolve_field, has_capability};
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program = compile_selector(
          "region == \"eu\" || role not in [\"origin\"]", &diagnostic);
      int matched = 0;

      check_equal(salts_selector_program_evaluate_v1(
                       program, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_true(matched);
      record.role = "origin";
      check_equal(salts_selector_program_evaluate_v1(
                       program, &ops, &record, &matched, &diagnostic),
                   SALTS_SELECTOR_OK);
      check_false(matched);
      salts_selector_program_destroy(program);
    }

    it("reports resolver failure without a partial match") {
      salts_selector_eval_ops_v1_t ops = {
          SALTS_SELECTOR_EVAL_OPS_V1_SIZE, NULL, has_capability};
      salts_selector_diagnostic_v1_t diagnostic = {0};
      salts_selector_program_t *program =
          compile_selector("has(region)", &diagnostic);
      int matched = 1;

      diagnostic.size = SALTS_SELECTOR_DIAGNOSTIC_V1_SIZE;
      check_equal(salts_selector_program_evaluate_v1(
                       program, &ops, NULL, &matched, &diagnostic),
                   SALTS_SELECTOR_EVALUATION_ERROR);
      check_false(matched);
      check_contains(diagnostic.message, "not configured");

      salts_selector_program_destroy(program);
    }
  }
}
