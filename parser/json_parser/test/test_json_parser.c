/**
 * @file test_json_parser.c
 * @brief Unit tests for JSON parser
 */

#include "json_parser.h"
#include "tinytest.h"
#include <salts/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_error_thread_probe {
  int status;
} json_error_thread_probe;

static void json_parse_success_on_thread(void *user) {
  json_error_thread_probe *probe = (json_error_thread_probe *)user;
  json_value_t *value = json_parse("{}", 2u);
  probe->status = value != NULL && json_get_error() == NULL ? 0 : 1;
  json_free(value);
}

typedef struct {
  int null_count;
  int bool_count;
  int number_count;
  int string_count;
  int object_start_count;
  int object_end_count;
  int array_start_count;
  int array_end_count;
  int key_count;
  double last_number;
  char raw_numbers[4][32];
  char last_string[256];
  char last_key[256];
} sax_test_ctx_t;

static int sax_on_null(void *ctx) {
  ((sax_test_ctx_t *)ctx)->null_count++;
  return 0;
}

static int sax_on_bool(void *ctx, bool val) {
  (void)val;
  ((sax_test_ctx_t *)ctx)->bool_count++;
  return 0;
}

static int sax_on_number(void *ctx, double val) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->number_count++;
  c->last_number = val;
  return 0;
}

static int sax_on_number_raw(void *ctx, const char *val, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  if (c->number_count >= 4 || len >= sizeof(c->raw_numbers[0])) return -1;
  memcpy(c->raw_numbers[c->number_count], val, len);
  c->raw_numbers[c->number_count][len] = '\0';
  c->number_count++;
  return 0;
}

static int sax_on_string(void *ctx, const char *val, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->string_count++;
  if (len < sizeof(c->last_string)) {
    memcpy(c->last_string, val, len);
    c->last_string[len] = '\0';
  }
  return 0;
}

static int sax_on_object_start(void *ctx) {
  ((sax_test_ctx_t *)ctx)->object_start_count++;
  return 0;
}

static int sax_on_object_end(void *ctx) {
  ((sax_test_ctx_t *)ctx)->object_end_count++;
  return 0;
}

static int sax_on_object_key(void *ctx, const char *key, size_t len) {
  sax_test_ctx_t *c = (sax_test_ctx_t *)ctx;
  c->key_count++;
  if (len < sizeof(c->last_key)) {
    memcpy(c->last_key, key, len);
    c->last_key[len] = '\0';
  }
  return 0;
}

static int sax_on_array_start(void *ctx) {
  ((sax_test_ctx_t *)ctx)->array_start_count++;
  return 0;
}

static int sax_on_array_end(void *ctx) {
  ((sax_test_ctx_t *)ctx)->array_end_count++;
  return 0;
}

static json_sax_handler_t test_handler = {.on_null = sax_on_null,
                                          .on_bool = sax_on_bool,
                                          .on_number = sax_on_number,
                                          .on_string = sax_on_string,
                                          .on_object_start = sax_on_object_start,
                                          .on_object_key = sax_on_object_key,
                                          .on_object_end = sax_on_object_end,
                                          .on_array_start = sax_on_array_start,
                                          .on_array_end = sax_on_array_end};

static json_sax_handler_raw_t test_raw_handler = {
    .on_null = sax_on_null,
    .on_bool = sax_on_bool,
    .on_number = sax_on_number_raw,
    .on_string = sax_on_string,
    .on_object_start = sax_on_object_start,
    .on_object_key = sax_on_object_key,
    .on_object_end = sax_on_object_end,
    .on_array_start = sax_on_array_start,
    .on_array_end = sax_on_array_end};

static int sax_fail_on_string(void *ctx, const char *val, size_t len) {
  (void)ctx;
  (void)val;
  (void)len;
  return -1;
}

typedef struct {
  size_t match_starts;
  size_t match_ends;
  json_type_t last_match_type;
  size_t strings;
  size_t numbers;
  size_t objects_started;
  size_t objects_ended;
  size_t arrays_started;
  size_t arrays_ended;
  size_t keys;
  char string_values[8][128];
  char number_values[8][128];
} json_path_stream_test_ctx_t;

static int json_path_stream_match_start(void *ctx, json_type_t type) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  capture->match_starts++;
  capture->last_match_type = type;
  return 0;
}

static int json_path_stream_match_end(void *ctx, json_type_t type) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  capture->match_ends++;
  capture->last_match_type = type;
  return 0;
}

static int json_path_stream_string(void *ctx, const char *value, size_t len) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  if (capture->strings >= 8 || len >= sizeof(capture->string_values[0])) return -1;
  memcpy(capture->string_values[capture->strings], value, len);
  capture->string_values[capture->strings][len] = '\0';
  capture->strings++;
  return 0;
}

static int json_path_stream_number(void *ctx, const char *value, size_t len) {
  json_path_stream_test_ctx_t *capture = (json_path_stream_test_ctx_t *)ctx;
  if (capture->numbers >= 8 || len >= sizeof(capture->number_values[0])) return -1;
  memcpy(capture->number_values[capture->numbers], value, len);
  capture->number_values[capture->numbers][len] = '\0';
  capture->numbers++;
  return 0;
}

static int json_path_stream_object_start(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->objects_started++;
  return 0;
}

static int json_path_stream_object_end(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->objects_ended++;
  return 0;
}

static int json_path_stream_array_start(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->arrays_started++;
  return 0;
}

static int json_path_stream_array_end(void *ctx) {
  ((json_path_stream_test_ctx_t *)ctx)->arrays_ended++;
  return 0;
}

static int json_path_stream_key(void *ctx, const char *key, size_t len) {
  (void)key;
  (void)len;
  ((json_path_stream_test_ctx_t *)ctx)->keys++;
  return 0;
}

static json_path_stream_handler_t json_path_stream_test_handler(void) {
  json_path_stream_handler_t handler = {0};
  handler.on_match_start = json_path_stream_match_start;
  handler.on_match_end = json_path_stream_match_end;
  handler.events.on_number = json_path_stream_number;
  handler.events.on_string = json_path_stream_string;
  handler.events.on_object_start = json_path_stream_object_start;
  handler.events.on_object_key = json_path_stream_key;
  handler.events.on_object_end = json_path_stream_object_end;
  handler.events.on_array_start = json_path_stream_array_start;
  handler.events.on_array_end = json_path_stream_array_end;
  return handler;
}

spec("json_parser") {
  describe("Basic Types") {
    it("should parse null correctly") {
      json_value_t *v = json_parse("null", 4);
      check_not_null(v);
      check_equal(json_type(v), JSON_NULL);
      check(json_is_null(v));
      json_free(v);
    }

    it("should parse true correctly") {
      json_value_t *v = json_parse("true", 4);
      check_not_null(v);
      check_equal(json_type(v), JSON_BOOL);
      check(json_bool(v));
      json_free(v);
    }

    it("should parse false correctly") {
      json_value_t *v = json_parse("false", 5);
      check_not_null(v);
      check_equal(json_type(v), JSON_BOOL);
      check(!json_bool(v));
      json_free(v);
    }
  }

  describe("Numbers") {
    it("should parse integers correctly") {
      json_value_t *v = json_parse("42", 2);
      check_not_null(v);
      check_equal(json_type(v), JSON_NUMBER);
      check_within(json_number(v), 42.0, 0.001);
      json_free(v);
    }

    it("should parse negative numbers correctly") {
      json_value_t *v = json_parse("-123", 4);
      check_not_null(v);
      check_equal(json_type(v), JSON_NUMBER);
      check_within(json_number(v), -123.0, 0.001);
      json_free(v);
    }

    it("should parse floating point numbers correctly") {
      json_value_t *v = json_parse("3.14159", 7);
      check_not_null(v);
      check_equal(json_type(v), JSON_NUMBER);
      check_within(json_number(v), 3.14159, 0.00001);
      json_free(v);
    }

    it("should parse scientific notation correctly") {
      json_value_t *v = json_parse("1.5e10", 6);
      check_not_null(v);
      check_equal(json_type(v), JSON_NUMBER);
      check_within(json_number(v), 1.5e10, 0.001);
      json_free(v);
    }

    it("should preserve exact numeric lexemes and serialize uint64 max") {
      const char *number = "18446744073709551615";
      size_t len = 0;
      char *serialized;
      json_value_t *v = json_parse(number, strlen(number));
      check_not_null(v);
      check_equal(json_number_text(v, &len), number);
      check_equal(len, strlen(number));
      json_free(v);

      v = json_create_uint64(UINT64_MAX);
      check_not_null(v);
      serialized = json_serialize(v, NULL);
      check_not_null(serialized);
      check_equal(serialized, number);
      free(serialized);
      json_free(v);
    }
  }

  describe("Strings") {
    it("should parse simple strings correctly") {
      json_value_t *v = json_parse("\"hello\"", 7);
      check_not_null(v);
      check_equal(json_type(v), JSON_STRING);
      check_equal(json_string(v), "hello");
      check_equal(json_string_len(v), 5);
      json_free(v);
    }

    it("should handle escape sequences in strings") {
      json_value_t *v = json_parse("\"hello\\nworld\"", 14);
      check_not_null(v);
      check_equal(json_type(v), JSON_STRING);
      check_equal(json_string(v), "hello\nworld");
      json_free(v);
    }

    it("should handle unicode escape sequences") {
      json_value_t *v = json_parse("\"\\u0041\\u0042\"", 14);
      check_not_null(v);
      check_equal(json_type(v), JSON_STRING);
      check_equal(json_string(v), "AB");
      json_free(v);
    }

    it("should decode UTF-16 surrogate pairs in values and keys") {
      const char *json = "{\"\\uD83D\\uDE00\":\"\\uD83D\\uDE00\"}";
      const char emoji[] = "\xF0\x9F\x98\x80";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_equal(json_object_key_len(v, 0), 4);
      check_equal(json_object_key(v, 0), emoji, 4);
      check_equal(json_string_len(json_object_value(v, 0)), 4);
      check_equal(json_string(json_object_value(v, 0)), emoji, 4);
      json_free(v);
    }

    it("should reject unpaired UTF-16 surrogates") {
      const char *invalid[] = {"\"\\uD83D\"", "\"\\uDE00\"", "\"\\uD83D\\n\""};
      for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        json_value_t *v = json_parse(invalid[i], strlen(invalid[i]));
        check_null(v);
        check_contains(json_get_error(), "surrogate");
      }
    }
  }

  describe("Arrays") {
    it("should parse empty arrays correctly") {
      json_value_t *v = json_parse("[]", 2);
      check_not_null(v);
      check_equal(json_type(v), JSON_ARRAY);
      check_equal(json_array_size(v), 0);
      json_free(v);
    }

    it("should parse simple arrays correctly") {
      json_value_t *v = json_parse("[1, 2, 3]", 9);
      check_not_null(v);
      check_equal(json_type(v), JSON_ARRAY);
      check_equal(json_array_size(v), 3);
      check_within(json_number(json_array_get(v, 0)), 1.0, 0.001);
      check_within(json_number(json_array_get(v, 1)), 2.0, 0.001);
      check_within(json_number(json_array_get(v, 2)), 3.0, 0.001);
      json_free(v);
    }

    it("should handle nested arrays correctly") {
      json_value_t *v = json_parse("[[1, 2], [3, 4]]", 16);
      check_not_null(v);
      check_equal(json_type(v), JSON_ARRAY);
      check_equal(json_array_size(v), 2);

      json_value_t *inner = json_array_get(v, 0);
      check_equal(json_type(inner), JSON_ARRAY);
      check_equal(json_array_size(inner), 2);

      json_free(v);
    }
  }

  describe("Objects") {
    it("should parse empty objects correctly") {
      json_value_t *v = json_parse("{}", 2);
      check_not_null(v);
      check_equal(json_type(v), JSON_OBJECT);
      check_equal(json_object_size(v), 0);
      json_free(v);
    }

    it("should parse simple objects correctly") {
      const char *json = "{\"name\": \"test\", \"value\": 42}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_equal(json_type(v), JSON_OBJECT);
      check_equal(json_object_size(v), 2);

      check_equal(json_get_string(v, "name"), "test");
      check_equal(json_get_int(v, "value", 0), 42);

      json_free(v);
    }

    it("should handle nested objects correctly") {
      const char *json = "{\"outer\": {\"inner\": 123}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *outer = json_object_get(v, "outer");
      check_not_null(outer);
      check_equal(json_type(outer), JSON_OBJECT);

      check_equal(json_get_int(outer, "inner", 0), 123);

      json_free(v);
    }

    it("should handle mixed complex structures") {
      const char *json = "{"
                         "  \"string\": \"hello\","
                         "  \"number\": 3.14,"
                         "  \"bool\": true,"
                         "  \"null\": null,"
                         "  \"array\": [1, 2, 3]"
                         "}";

      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      check_equal(json_get_string(v, "string"), "hello");
      check_within(json_get_double(v, "number", 0), 3.14, 0.01);
      check(json_get_bool(v, "bool", false));
      check(json_is_null(json_object_get(v, "null")));

      json_value_t *arr = json_object_get(v, "array");
      check_equal(json_array_size(arr), 3);

      json_free(v);
    }

    it("should parse large configuration-like JSON correctly") {
      const char *json = "{"
                         "  \"listeners\": ["
                         "    {\"port\": 1883, \"transport\": \"tcp\"},"
                         "    {\"port\": 8883, \"transport\": \"tls\"}"
                         "  ],"
                         "  \"upstreams\": ["
                         "    {\"host\": \"10.0.0.1\", \"port\": 1883, \"weight\": 3}"
                         "  ],"
                         "  \"settings\": {"
                         "    \"max_clients\": 10000,"
                         "    \"connect_timeout_ms\": 5000"
                         "  }"
                         "}";

      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *listeners = json_object_get(v, "listeners");
      check_equal(json_array_size(listeners), 2);

      json_value_t *l0 = json_array_get(listeners, 0);
      check_equal(json_get_int(l0, "port", 0), 1883);
      check_equal(json_get_string(l0, "transport"), "tcp");

      json_value_t *upstreams = json_object_get(v, "upstreams");
      check_equal(json_array_size(upstreams), 1);

      json_value_t *u0 = json_array_get(upstreams, 0);
      check_equal(json_get_string(u0, "host"), "10.0.0.1");
      check_equal(json_get_int(u0, "weight", 0), 3);

      json_value_t *settings = json_object_get(v, "settings");
      check_equal(json_get_int(settings, "max_clients", 0), 10000);

      json_free(v);
    }

    it("should index large parsed objects for key and JSONPath lookup") {
      enum { KEY_COUNT = 128, BUFFER_CAPACITY = 4096 };
      char *json = (char *)malloc(BUFFER_CAPACITY);
      size_t offset = 0;
      json_value_t *v;

      check_not_null(json);
      json[offset++] = '{';
      for (int i = 0; i < KEY_COUNT; ++i) {
        int written = snprintf(json + offset, BUFFER_CAPACITY - offset,
                               "%s\"key_%d\":%d", i == 0 ? "" : ",", i, i);
        check_greater(written, 0);
        check((size_t)written < BUFFER_CAPACITY - offset);
        offset += (size_t)written;
      }
      json[offset++] = '}';
      json[offset] = '\0';

      v = json_parse(json, offset);
      check_not_null(v);
      check_equal(json_object_size(v), KEY_COUNT);
      check_equal(json_get_int(v, "key_0", -1), 0);
      check_equal(json_get_int(v, "key_63", -1), 63);
      check_equal(json_get_int(v, "key_127", -1), 127);
      check_null(json_object_get(v, "missing"));
      check_equal(json_object_key(v, KEY_COUNT - 1), "key_127");
      check_equal((int)json_number(json_path_get(v, "$.key_127")), 127);

      json_free(v);
      free(json);
    }

    it("should keep indexes coherent when builder objects grow and update") {
      enum { INITIAL_KEYS = 40 };
      json_value_t *obj = json_create_object();
      char key[32];

      check_not_null(obj);
      for (int i = 0; i < INITIAL_KEYS; ++i) {
        int written = snprintf(key, sizeof(key), "key_%d", i);
        check_greater(written, 0);
        json_object_set_number(obj, key, (double)i);
      }
      json_object_set_number(obj, "key_31", 999.0);
      json_object_set_number(obj, "key_40", 40.0);

      check_equal(json_object_size(obj), INITIAL_KEYS + 1);
      check_within(json_get_double(obj, "key_31", -1.0), 999.0, 0.001);
      check_within(json_get_double(obj, "key_40", -1.0), 40.0, 0.001);
      check_equal(json_object_key(obj, 31), "key_31");
      check_equal(json_object_key(obj, INITIAL_KEYS), "key_40");
      json_free(obj);
    }

  }

  describe("Auxiliary") {
    it("should build length-delimited strings and object keys") {
      json_value_t *obj = json_create_object();
      json_value_t *value = json_create_string_n("x\0y", 3);

      check_not_null(obj);
      check_not_null(value);
      check_true(json_object_add_n(obj, "a\0b", 3, value));
      check_equal(json_object_size(obj), 1);
      check_equal(json_object_key_len(obj, 0), 3);
      check_equal(json_object_key(obj, 0), "a\0b", 3);
      check_equal(json_string_len(json_object_value(obj, 0)), 3);
      check_equal(json_string(json_object_value(obj, 0)), "x\0y", 3);

      size_t len = 0;
      char *serialized = json_serialize(obj, &len);
      check_not_null(serialized);
      check_equal(serialized, "{\"a\\u0000b\":\"x\\u0000y\"}");
      json_serialize_free(serialized);
      json_free(obj);
    }

    it("should serialize int64 builders without double precision loss") {
      json_value_t *value = json_create_int64(INT64_C(9007199254740993));
      json_value_t *copy = json_clone(value);
      size_t len = 0;
      char *serialized = json_serialize(value, &len);
      check_not_null(serialized);
      check_equal(serialized, "9007199254740993");
      check_equal(len, 16);
      json_serialize_free(serialized);
      serialized = json_serialize(copy, &len);
      check_equal(serialized, "9007199254740993");
      json_serialize_free(serialized);
      json_free(copy);
      json_free(value);
    }

    it("should report checked builder argument failures") {
      json_value_t *array = json_create_array();
      json_value_t *value = json_create_number(1.0);

      check_not_null(array);
      check_not_null(value);
      check_false(json_array_add_checked(NULL, value));
      check_false(json_array_add_checked(array, NULL));
      check_false(json_object_add_checked(array, "key", value));
      check_false(json_object_add_n(array, "key", 3, value));
      check_equal(json_array_size(array), 0);

      check_true(json_array_add_checked(array, value));
      check_equal(json_array_size(array), 1);
      json_free(array);
    }

    it("should reject adopting a child into a second parent") {
      json_value_t *first = json_create_array();
      json_value_t *second = json_create_array();
      json_value_t *value = json_create_string("owned");

      check_not_null(first);
      check_not_null(second);
      check_not_null(value);
      check_true(json_array_add_checked(first, value));
      check_false(json_array_add_checked(second, value));
      check_equal(json_array_size(first), 1);
      check_equal(json_array_size(second), 0);

      json_free(second);
      json_free(first);
    }

    it("should reject an arena ownership cycle") {
      json_value_t *parent = json_create_array();
      json_value_t *child = json_create_array();

      check_not_null(parent);
      check_not_null(child);
      check_true(json_array_add_checked(parent, child));
      check_false(json_array_add_checked(child, parent));
      check_equal(json_array_size(parent), 1);
      check_equal(json_array_size(child), 0);
      json_free(parent);
    }

    it("should keep legacy builder wrappers working") {
      json_value_t *obj = json_create_object();
      json_value_t *array = json_create_array();

      check_not_null(obj);
      check_not_null(array);
      json_array_add(array, json_create_bool(true));
      json_object_add(obj, "items", array);
      check_equal(json_array_size(json_object_get(obj, "items")), 1);
      check_true(json_bool(json_array_get(json_object_get(obj, "items"), 0)));
      json_free(obj);
    }

    it("should handle whitespace around JSON") {
      const char *json = "  \n\t { \"key\" : \"value\" } \n";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_equal(json_get_string(v, "key"), "value");
      json_free(v);
    }

    it("should allow iterating over object keys and values") {
      const char *json = "{\"a\": 1, \"b\": 2, \"c\": 3}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_equal(json_object_size(v), 3);

      check_equal(json_object_key(v, 0), "a");
      check_equal(json_object_key(v, 1), "b");
      check_equal(json_object_key(v, 2), "c");

      check_within(json_number(json_object_value(v, 0)), 1.0, 0.001);
      check_within(json_number(json_object_value(v, 1)), 2.0, 0.001);
      check_within(json_number(json_object_value(v, 2)), 3.0, 0.001);

      json_free(v);
    }

    it("should skip a long RFC whitespace prefix before lexing JSON") {
      enum { WHITESPACE_BYTES = 4096 };
      const char suffix[] = "{\"value\":7}";
      char *json = (char *)malloc(WHITESPACE_BYTES + sizeof(suffix));

      check_not_null(json);
      for (size_t i = 0; i < WHITESPACE_BYTES; ++i) {
        static const char whitespace[] = {' ', '\t', '\r', '\n'};
        json[i] = whitespace[i % sizeof(whitespace)];
      }
      memcpy(json + WHITESPACE_BYTES, suffix, sizeof(suffix));

      json_value_t *v = json_parse(json, WHITESPACE_BYTES + sizeof(suffix) - 1);
      check_not_null(v);
      check_equal(json_get_int(v, "value", 0), 7);

      json_free(v);
      free(json);
    }

    it("should lex a long plain ASCII string without changing its value") {
      enum { STRING_BYTES = 4096 };
      char *json = (char *)malloc(STRING_BYTES + 3);

      check_not_null(json);
      json[0] = '"';
      memset(json + 1, 'x', STRING_BYTES);
      json[STRING_BYTES + 1] = '"';
      json[STRING_BYTES + 2] = '\0';

      json_value_t *v = json_parse(json, STRING_BYTES + 2);
      check_not_null(v);
      check_equal(json_string_len(v), STRING_BYTES);
      check_equal(json_string(v), json + 1, STRING_BYTES);

      json_free(v);
      free(json);
    }
  }

  describe("JSONPath") {
    it("should decode surrogate pairs in bracket property names") {
      const char emoji[] = "\xF0\x9F\x98\x80";
      const char *json = "{\"\\uD83D\\uDE00\":7}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *selected = json_path_get(v, "$['\\uD83D\\uDE00']");
      check_not_null(selected);
      check_equal((int)json_number(selected), 7);
      check_equal(json_object_key(v, 0), emoji, 4);

      check_null(json_path_get(v, "$['\\uD83D']"));
      check_not_null(json_path_get_error());
      json_free(v);
    }

    it("should get a nested object member by path") {
      const char *json = "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
                         "{\"port\":8883,\"transport\":\"tls\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *port = json_path_get(v, "$.listeners[0].port");
      check_not_null(port);
      check_equal((int)json_number(port), 1883);

      json_value_t *last = json_path_get(v, "$.listeners[-1].transport");
      check_not_null(last);
      check_equal(json_string(last), "tls");

      json_free(v);
    }

    it("should return all wildcard matches") {
      const char *json = "{\"listeners\":[{\"transport\":\"tcp\"},{\"transport\":\"tls\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.listeners[*].transport");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "tcp");
      check_equal(json_string(json_path_result_get(result, 1)), "tls");

      json_path_result_free(result);
      json_free(v);
    }

    it("should support bracket key union on objects") {
      const char *json = "{\"settings\":{\"max_clients\":10000,\"connect_timeout_ms\":5000,"
                         "\"name\":\"edge\"}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result =
          json_path_query(v, "$.settings['max_clients','connect_timeout_ms']");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal((int)json_number(json_path_result_get(result, 0)), 10000);
      check_equal((int)json_number(json_path_result_get(result, 1)), 5000);

      json_path_result_free(result);
      json_free(v);
    }

    it("should filter array elements using current-node paths") {
      const char *json = "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
                         "{\"port\":8883,\"transport\":\"tls\"},"
                         "{\"port\":8080,\"transport\":\"ws\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.listeners[@.port >= 8000].transport");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "tls");
      check_equal(json_string(json_path_result_get(result, 1)), "ws");

      json_path_result_free(result);
      json_free(v);
    }

    it("should report invalid JSONPath expressions") {
      json_value_t *v = json_parse("{\"a\":1}", 7);
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.a[");
      check_null(result);
      check_not_null(json_path_get_error());

      json_free(v);
    }

    it("should provide indexed access to distant array elements") {
      enum { ELEMENTS = 4096 };
      const size_t capacity = (size_t)ELEMENTS * 6 + 3;
      char *json = (char *)malloc(capacity);
      size_t offset = 0;

      check_not_null(json);
      json[offset++] = '[';
      for (int i = 0; i < ELEMENTS; ++i) {
        offset += (size_t)snprintf(json + offset, capacity - offset, "%s%d", i == 0 ? "" : ",", i);
      }
      json[offset++] = ']';
      json[offset] = '\0';

      json_value_t *v = json_parse(json, offset);
      check_not_null(v);
      check_equal(json_array_size(v), ELEMENTS);
      check_within(json_number(json_array_get(v, 0)), 0.0, 0.001);
      check_within(json_number(json_array_get(v, 2048)), 2048.0, 0.001);
      check_within(json_number(json_array_get(v, ELEMENTS - 1)), 4095.0, 0.001);

      json_free(v);
      free(json);
    }

    it("should reuse a compiled JSONPath after its source expression changes") {
      char expr[] = "$.listeners[-1].transport";
      const char *first_json =
          "{\"listeners\":[{\"transport\":\"tcp\"},{\"transport\":\"tls\"}]}";
      const char *second_json =
          "{\"listeners\":[{\"transport\":\"quic\"},{\"transport\":\"ws\"}]}";
      json_path_program_t *program = json_path_compile(expr);
      json_value_t *first = json_parse(first_json, strlen(first_json));
      json_value_t *second = json_parse(second_json, strlen(second_json));

      check_not_null(program);
      check_not_null(first);
      check_not_null(second);
      memset(expr, 'x', sizeof(expr) - 1U);
      check_equal(json_string(json_path_get_compiled(first, program)), "tls");
      check_equal(json_string(json_path_get_compiled(second, program)), "ws");

      json_free(second);
      json_free(first);
      json_path_program_free(program);
    }

    it("should execute compiled filters and unions without reparsing") {
      const char *json =
          "{\"listeners\":[{\"port\":1883,\"transport\":\"tcp\"},"
          "{\"port\":8883,\"transport\":\"tls\"},"
          "{\"port\":8080,\"transport\":\"ws\"}],"
          "\"settings\":{\"max_clients\":10000,\"connect_timeout_ms\":5000}}";
      json_value_t *root = json_parse(json, strlen(json));
      json_path_program_t *filter = json_path_compile(
          "$.listeners[@.port >= 8000].transport");
      json_path_program_t *union_program = json_path_compile(
          "$.settings['max_clients','connect_timeout_ms']");
      json_path_result_t *filtered;
      json_path_result_t *union_result;

      check_not_null(root);
      check_not_null(filter);
      check_not_null(union_program);
      filtered = json_path_query_compiled(root, filter);
      union_result = json_path_query_compiled(root, union_program);
      check_not_null(filtered);
      check_not_null(union_result);
      check_equal(json_path_result_size(filtered), 2);
      check_equal(json_string(json_path_result_get(filtered, 0)), "tls");
      check_equal(json_string(json_path_result_get(filtered, 1)), "ws");
      check_equal(json_path_result_size(union_result), 2);
      check_equal((int)json_number(json_path_result_get(union_result, 0)), 10000);
      check_equal((int)json_number(json_path_result_get(union_result, 1)), 5000);

      json_path_result_free(union_result);
      json_path_result_free(filtered);
      json_path_program_free(union_program);
      json_path_program_free(filter);
      json_free(root);
    }

    it("should match filter regex with string patterns") {
      const char *json = "{\"items\":[{\"name\":\"alpha-1\"},{\"name\":\"beta-2\"},"
                         "{\"name\":\"gamma\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result =
          json_path_query(v, "$.items[@.name ~ '^[a-z]+-[0-9]+$'].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "alpha-1");
      check_equal(json_string(json_path_result_get(result, 1)), "beta-2");

      json_path_result_t *anchored =
          json_path_query(v, "$.items[@.name ~ '^alpha'].name");
      check_not_null(anchored);
      check_equal(json_path_result_size(anchored), 1);
      check_equal(json_string(json_path_result_get(anchored, 0)), "alpha-1");

      json_path_result_free(anchored);
      json_path_result_free(result);
      json_free(v);
    }

    it("should match filter regex with slash-delimited patterns") {
      const char *json =
          "{\"items\":[{\"code\":\"A-1\"},{\"code\":\"B-22\"},{\"code\":\"C\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result =
          json_path_query(v, "$.items[@.code ~ /^[A-C]-[0-9]+$/].code");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "A-1");
      check_equal(json_string(json_path_result_get(result, 1)), "B-22");

      json_path_result_free(result);
      json_free(v);
    }

    it("should match filter contains as literal substring") {
      const char *json = "{\"items\":[{\"name\":\"alpha-1\"},{\"name\":\"beta\"},"
                         "{\"name\":\"alphabet\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result =
          json_path_query(v, "$.items[contains(@.name, 'alph')].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "alpha-1");
      check_equal(json_string(json_path_result_get(result, 1)), "alphabet");

      json_path_result_t *missing =
          json_path_query(v, "$.items[contains(@.name, 'xyz')].name");
      check_not_null(missing);
      check_equal(json_path_result_size(missing), 0);

      json_path_result_free(missing);
      json_path_result_free(result);
      json_free(v);
    }
    it("should handle contains edge cases like empty needles") {
      const char *json = "{\"items\":[{\"name\":\"alpha-1\"},{\"name\":\"\"},"
                         "{\"name\":\"beta\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *empty = json_path_query(v, "$.items[contains(@.name, '')].name");
      check_not_null(empty);
      check_equal(json_path_result_size(empty), 3);

      json_path_result_t *whole = json_path_query(v, "$.items[contains(@.name, 'alpha-1')].name");
      check_not_null(whole);
      check_equal(json_path_result_size(whole), 1);
      check_equal(json_string(json_path_result_get(whole, 0)), "alpha-1");

      json_path_result_t *tail = json_path_query(v, "$.items[contains(@.name, 'eta')].name");
      check_not_null(tail);
      check_equal(json_path_result_size(tail), 1);
      check_equal(json_string(json_path_result_get(tail, 0)), "beta");

      json_path_result_free(tail);
      json_path_result_free(whole);
      json_path_result_free(empty);
      json_free(v);
    }

    it("should keep plain contains member names as labels") {
      const char *json = "{\"contains\":\"payload\",\"items\":[1]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      check_equal(json_string(json_path_get(v, "$.contains")), "payload");
      json_free(v);
    }

    it("should treat malformed filter regex as no match") {
      const char *json = "{\"items\":[{\"name\":\"alpha\"},{\"name\":\"beta\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.items[@.name ~ '['].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);

      json_path_result_free(result);
      json_free(v);
    }


    it("should collect descendant member values in document order") {
      const char *json = "{\"store\":{\"book\":[{\"category\":\"reference\","
                         "\"author\":\"Nigel Rees\",\"title\":\"Sayings of the Century\","
                         "\"price\":8.95},"
                         "{\"category\":\"fiction\",\"author\":\"Evelyn Waugh\","
                         "\"title\":\"Sword of Honour\",\"price\":12.99},"
                         "{\"category\":\"fiction\",\"author\":\"Herman Melville\","
                         "\"title\":\"Moby Dick\",\"isbn\":\"0-553-21311-3\",\"price\":8.99},"
                         "{\"category\":\"fiction\",\"author\":\"J. R. R. Tolkien\","
                         "\"title\":\"The Lord of the Rings\",\"isbn\":\"0-395-19395-8\","
                         "\"price\":22.99}],\"bicycle\":{\"color\":\"red\",\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.store..price");
      check_not_null(result);
      check_equal(json_path_result_size(result), 5);
      check_within(json_number(json_path_result_get(result, 0)), 8.95, 0.001);
      check_within(json_number(json_path_result_get(result, 1)), 12.99, 0.001);
      check_within(json_number(json_path_result_get(result, 2)), 8.99, 0.001);
      check_within(json_number(json_path_result_get(result, 3)), 22.99, 0.001);
      check_within(json_number(json_path_result_get(result, 4)), 19.95, 0.001);

      json_path_result_free(result);
      json_free(v);
    }

    it("should collect descendant member values across the whole document") {
      const char *json = "{\"store\":{\"book\":[{\"category\":\"reference\","
                         "\"author\":\"Nigel Rees\",\"title\":\"Sayings of the Century\","
                         "\"price\":8.95},"
                         "{\"category\":\"fiction\",\"author\":\"Evelyn Waugh\","
                         "\"title\":\"Sword of Honour\",\"price\":12.99},"
                         "{\"category\":\"fiction\",\"author\":\"Herman Melville\","
                         "\"title\":\"Moby Dick\",\"isbn\":\"0-553-21311-3\",\"price\":8.99},"
                         "{\"category\":\"fiction\",\"author\":\"J. R. R. Tolkien\","
                         "\"title\":\"The Lord of the Rings\",\"isbn\":\"0-395-19395-8\","
                         "\"price\":22.99}],\"bicycle\":{\"color\":\"red\",\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$..author");
      check_not_null(result);
      check_equal(json_path_result_size(result), 4);
      check_equal(json_string(json_path_result_get(result, 0)), "Nigel Rees");
      check_equal(json_string(json_path_result_get(result, 1)), "Evelyn Waugh");
      check_equal(json_string(json_path_result_get(result, 2)), "Herman Melville");
      check_equal(json_string(json_path_result_get(result, 3)), "J. R. R. Tolkien");

      json_path_result_free(result);
      json_free(v);
    }

    it("should index into descendant arrays") {
      const char *json = "{\"store\":{\"book\":[{\"title\":\"Sayings of the Century\"},"
                         "{\"title\":\"Sword of Honour\"},{\"title\":\"Moby Dick\"},"
                         "{\"title\":\"The Lord of the Rings\"}]}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *by_index = json_path_query(v, "$..book[2]");
      check_not_null(by_index);
      check_equal(json_path_result_size(by_index), 1);
      check_equal(json_string(json_path_get(json_path_result_get(by_index, 0), "$.title")),
                   "Moby Dick");

      json_path_result_t *by_negative = json_path_query(v, "$..book[-1]");
      check_not_null(by_negative);
      check_equal(json_path_result_size(by_negative), 1);
      check_equal(json_string(json_path_get(json_path_result_get(by_negative, 0), "$.title")),
                   "The Lord of the Rings");

      json_path_result_free(by_negative);
      json_path_result_free(by_index);
      json_free(v);
    }

    it("should expand descendant wildcard to all member values and array elements") {
      const char *json = "{\"store\":{\"book\":[{\"category\":\"reference\","
                         "\"author\":\"Nigel Rees\",\"title\":\"Sayings of the Century\","
                         "\"price\":8.95},"
                         "{\"category\":\"fiction\",\"author\":\"Evelyn Waugh\","
                         "\"title\":\"Sword of Honour\",\"price\":12.99},"
                         "{\"category\":\"fiction\",\"author\":\"Herman Melville\","
                         "\"title\":\"Moby Dick\",\"isbn\":\"0-553-21311-3\",\"price\":8.99},"
                         "{\"category\":\"fiction\",\"author\":\"J. R. R. Tolkien\","
                         "\"title\":\"The Lord of the Rings\",\"isbn\":\"0-395-19395-8\","
                         "\"price\":22.99}],\"bicycle\":{\"color\":\"red\",\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$..*");
      check_not_null(result);
      check_equal(json_path_result_size(result), 27);
      check_equal((int)json_type(json_path_result_get(result, 0)), (int)JSON_OBJECT);
      check_equal((int)json_type(json_path_result_get(result, 1)), (int)JSON_ARRAY);
      check_equal(json_string(json_path_result_get(result, 9)), "Sayings of the Century");
      check_within(json_number(json_path_result_get(result, 26)), 19.95, 0.001);

      json_path_result_free(result);
      json_free(v);
    }

    it("should treat descendant shorthand and bracket names as equivalent") {
      const char *json = "{\"store\":{\"book\":[{\"price\":8.95},{\"price\":12.99}],"
                         "\"bicycle\":{\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *shorthand = json_path_query(v, "$..price");
      json_path_result_t *bracket = json_path_query(v, "$..['price']");
      check_not_null(shorthand);
      check_not_null(bracket);
      check_equal(json_path_result_size(shorthand), 3);
      check_equal(json_path_result_size(bracket), 3);
      for (size_t i = 0; i < 3; ++i) {
        check_within(json_number(json_path_result_get(shorthand, i)),
                       json_number(json_path_result_get(bracket, i)), 0.001);
      }

      json_path_result_free(bracket);
      json_path_result_free(shorthand);
      json_free(v);
    }

    it("should apply descendant union selectors per visited node") {
      const char *json = "{\"store\":{\"book\":[{\"price\":8.95,\"author\":\"Nigel Rees\"},"
                         "{\"price\":12.99,\"author\":\"Evelyn Waugh\"}],"
                         "\"bicycle\":{\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$..['price','author']");
      check_not_null(result);
      check_equal(json_path_result_size(result), 5);
      check_within(json_number(json_path_result_get(result, 0)), 8.95, 0.001);
      check_equal(json_string(json_path_result_get(result, 1)), "Nigel Rees");
      check_within(json_number(json_path_result_get(result, 2)), 12.99, 0.001);
      check_equal(json_string(json_path_result_get(result, 3)), "Evelyn Waugh");
      check_within(json_number(json_path_result_get(result, 4)), 19.95, 0.001);

      json_path_result_free(result);
      json_free(v);
    }

    it("should order object union results by selector order") {
      const char *json = "{\"a\":1,\"b\":2}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$['b','a']");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal((int)json_number(json_path_result_get(result, 0)), 2);
      check_equal((int)json_number(json_path_result_get(result, 1)), 1);

      json_path_result_free(result);
      json_free(v);
    }

    it("should order array union results by selector order") {
      const char *json = "{\"items\":[10,20,30]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.items[1,0]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal((int)json_number(json_path_result_get(result, 0)), 20);
      check_equal((int)json_number(json_path_result_get(result, 1)), 10);

      json_path_result_free(result);
      json_free(v);
    }

    it("should keep duplicate union selectors in the result") {
      const char *json = "{\"settings\":{\"price\":19.95,\"name\":\"edge\"}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.settings['price','price']");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_within(json_number(json_path_result_get(result, 0)), 19.95, 0.001);
      check_within(json_number(json_path_result_get(result, 1)), 19.95, 0.001);

      json_path_result_free(result);
      json_free(v);
    }

    it("should order descendant union results by selector per node") {
      const char *json = "{\"store\":{\"book\":[{\"price\":8.95,\"author\":\"Nigel Rees\"},"
                         "{\"price\":12.99,\"author\":\"Evelyn Waugh\"}],"
                         "\"bicycle\":{\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$..['author','price']");
      check_not_null(result);
      check_equal(json_path_result_size(result), 5);
      check_equal(json_string(json_path_result_get(result, 0)), "Nigel Rees");
      check_within(json_number(json_path_result_get(result, 1)), 8.95, 0.001);
      check_equal(json_string(json_path_result_get(result, 2)), "Evelyn Waugh");
      check_within(json_number(json_path_result_get(result, 3)), 12.99, 0.001);
      check_within(json_number(json_path_result_get(result, 4)), 19.95, 0.001);

      json_path_result_free(result);
      json_free(v);
    }

    it("should apply remaining segments per union selector result") {
      const char *json = "{\"store\":{\"book\":{\"title\":\"A\"},"
                         "\"bicycle\":{\"title\":\"B\"}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *result = json_path_query(v, "$.store['bicycle','book'].title");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "B");
      check_equal(json_string(json_path_result_get(result, 1)), "A");

      json_path_result_free(result);
      json_free(v);
    }

    it("should return the first descendant match for single-value lookups") {
      const char *json = "{\"store\":{\"book\":[{\"price\":8.95},{\"price\":12.99}],"
                         "\"bicycle\":{\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_value_t *price = json_path_get(v, "$..price");
      check_not_null(price);
      check_within(json_number(price), 8.95, 0.001);

      json_free(v);
    }

    it("should reject descendant paths in stream matchers") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$..name");

      check_not_null(program);
      check_null(json_path_stream_create(program, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");

      json_path_program_free(program);
    }


    it("should reject invalid compiled JSONPath expressions") {
      json_path_program_t *program;
      check_null(json_path_compile("$.listeners["));
      check_not_null(json_path_get_error());
      program = json_path_compile("$.listeners");
      check_not_null(program);
      check_null(json_path_get_error());
      json_path_program_free(program);
    }

    it("should stream wildcard scalar matches without building a DOM") {
      const char *json =
          "{\"items\":[{\"name\":\"Alice\"},{\"name\":\"Bob\"}]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[*].name");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      for (size_t i = 0; i < strlen(json); ++i)
        check_equal(json_path_stream_feed(stream, json + i, 1), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.match_starts, 2);
      check_equal(capture.match_ends, 2);
      check_equal(capture.strings, 2);
      check_equal(capture.string_values[0], "Alice");
      check_equal(capture.string_values[1], "Bob");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream a selected object as balanced SAX events") {
      const char *json =
          "{\"items\":[{\"name\":\"A\",\"id\":1},{\"name\":\"B\",\"id\":2}]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[1]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 1);
      check_equal(capture.last_match_type, JSON_OBJECT);
      check_equal(capture.objects_started, 1);
      check_equal(capture.objects_ended, 1);
      check_equal(capture.keys, 2);
      check_equal(capture.strings, 1);
      check_equal(capture.numbers, 1);
      check_equal(capture.string_values[0], "B");
      check_equal(capture.number_values[0], "2");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream object key unions with exact number tokens") {
      const char *json = "{\"settings\":{\"a\":100,\"skip\":0,\"b\":200}}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.settings['a','b']");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, 11), 0);
      check_equal(json_path_stream_feed(stream, json + 11, strlen(json) - 11), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.numbers, 2);
      check_equal(capture.number_values[0], "100");
      check_equal(capture.number_values[1], "200");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream array index unions") {
      const char *json = "{\"items\":[10,20,30]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[0,2]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.numbers, 2);
      check_equal(capture.number_values[0], "10");
      check_equal(capture.number_values[1], "30");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should reject non-streamable filters and negative indexes") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *filter =
          json_path_compile("$.items[@.port >= 8000].transport");
      json_path_program_t *negative = json_path_compile("$.items[-1]");

      check_not_null(filter);
      check_not_null(negative);
      check_null(json_path_stream_create(filter, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");
      check_null(json_path_stream_create(negative, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");

      json_path_program_free(negative);
      json_path_program_free(filter);
    }

    it("should stop a JSONPath stream when a selected callback fails") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.name");
      json_path_stream_t *stream;
      handler.events.on_string = sax_fail_on_string;
      stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, "{\"name\":\"stop\"}", 15), -1);
      check_contains(json_path_stream_error(stream), "callback");
      check_equal(json_path_stream_match_count(stream), 0);

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should slice arrays with start and end bounds") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[1:3]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "b");
      check_equal(json_string(json_path_result_get(result, 1)), "c");
      json_path_result_free(result);
      json_free(v);
    }

    it("should slice arrays with an omitted end bound") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[5:]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "f");
      check_equal(json_string(json_path_result_get(result, 1)), "g");
      json_path_result_free(result);
      json_free(v);
    }

    it("should slice arrays with a positive step") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[1:5:2]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "b");
      check_equal(json_string(json_path_result_get(result, 1)), "d");
      json_path_result_free(result);
      json_free(v);
    }

    it("should slice arrays with a negative step in descending order") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[5:1:-2]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "f");
      check_equal(json_string(json_path_result_get(result, 1)), "d");
      json_path_result_free(result);
      json_free(v);
    }

    it("should reverse arrays with an all-omitted negative slice") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[::-1]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 7);
      check_equal(json_string(json_path_result_get(result, 0)), "g");
      check_equal(json_string(json_path_result_get(result, 3)), "d");
      check_equal(json_string(json_path_result_get(result, 6)), "a");
      json_path_result_free(result);
      json_free(v);
    }

    it("should step through a whole array when both bounds are omitted") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[::2]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 4);
      check_equal(json_string(json_path_result_get(result, 0)), "a");
      check_equal(json_string(json_path_result_get(result, 1)), "c");
      check_equal(json_string(json_path_result_get(result, 2)), "e");
      check_equal(json_string(json_path_result_get(result, 3)), "g");
      json_path_result_free(result);
      json_free(v);
    }

    it("should treat a zero step as an empty slice") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[::0]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);
      json_path_result_free(result);
      json_free(v);
    }

    it("should return an empty list when the slice range is inverted") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[3:1]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);
      json_path_result_free(result);
      json_free(v);
    }

    it("should support negative slice bounds") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[-3:-1]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "e");
      check_equal(json_string(json_path_result_get(result, 1)), "f");
      json_path_result_free(result);
      json_free(v);
    }

    it("should support an omitted start with a negative end") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.arr[:-2]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 5);
      check_equal(json_string(json_path_result_get(result, 0)), "a");
      check_equal(json_string(json_path_result_get(result, 4)), "e");
      json_path_result_free(result);
      json_free(v);
    }

    it("should slice the root array") {
      const char *json = "[\"a\",\"b\",\"c\",\"d\"]";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$[1:3]");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "b");
      check_equal(json_string(json_path_result_get(result, 1)), "c");
      json_path_result_free(result);
      json_free(v);
    }

    it("should return the first sliced element for single-value lookups") {
      const char *json = "{\"arr\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_value_t *value = json_path_get(v, "$.arr[2:4]");
      check_not_null(value);
      check_equal(json_string(value), "c");
      json_free(v);
    }

    it("should apply remaining segments per sliced element") {
      const char *json = "{\"store\":{\"book\":[{\"title\":\"a\"},{\"title\":\"b\"},"
                         "{\"title\":\"c\"}]}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.store.book[0:2].title");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "a");
      check_equal(json_string(json_path_result_get(result, 1)), "b");
      json_path_result_free(result);
      json_free(v);
    }

    it("should slice descendants and return nothing for non-arrays") {
      const char *json = "{\"x\":{\"arr\":[1,2,3]},\"obj\":{\"a\":1}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *sliced = json_path_query(v, "$..arr[0:2]");
      check_not_null(sliced);
      check_equal(json_path_result_size(sliced), 2);
      check_equal((int)json_number(json_path_result_get(sliced, 0)), 1);
      check_equal((int)json_number(json_path_result_get(sliced, 1)), 2);
      json_path_result_t *object_slice = json_path_query(v, "$.obj[0:2]");
      check_not_null(object_slice);
      check_equal(json_path_result_size(object_slice), 0);
      json_path_result_free(object_slice);
      json_path_result_free(sliced);
      json_free(v);
    }

    it("should support standard ? filter syntax on arrays") {
      const char *json = "{\"items\":[{\"name\":\"a\",\"price\":5},"
                         "{\"name\":\"b\",\"price\":15},"
                         "{\"name\":\"c\",\"price\":8}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.items[?@.price < 10].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "a");
      check_equal(json_string(json_path_result_get(result, 1)), "c");
      json_path_result_free(result);
      json_free(v);
    }

    it("should keep the parenthesized filter form equivalent to ? syntax") {
      const char *json = "{\"items\":[{\"name\":\"a\",\"price\":5},"
                         "{\"name\":\"b\",\"price\":15},"
                         "{\"name\":\"c\",\"price\":8}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *qmark = json_path_query(v, "$.items[? @.price < 10].name");
      json_path_result_t *paren = json_path_query(v, "$.items[(@.price < 10)].name");
      check_not_null(qmark);
      check_not_null(paren);
      check_equal(json_path_result_size(qmark), 2);
      check_equal(json_path_result_size(paren), 2);
      check_equal(json_string(json_path_result_get(qmark, 0)),
                   json_string(json_path_result_get(paren, 0)));
      check_equal(json_string(json_path_result_get(qmark, 1)),
                   json_string(json_path_result_get(paren, 1)));
      json_path_result_free(paren);
      json_path_result_free(qmark);
      json_free(v);
    }

    it("should filter by member existence with ? syntax") {
      const char *json = "{\"items\":[{\"name\":\"a\",\"isbn\":\"x\"},"
                         "{\"name\":\"b\"},"
                         "{\"name\":\"c\",\"isbn\":\"y\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.items[?@.isbn].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "a");
      check_equal(json_string(json_path_result_get(result, 1)), "c");
      json_path_result_free(result);
      json_free(v);
    }

    it("should support boolean expressions inside ? filters") {
      const char *json = "{\"items\":[{\"name\":\"a\",\"price\":5,\"stock\":0},"
                         "{\"name\":\"b\",\"price\":15,\"stock\":3},"
                         "{\"name\":\"c\",\"price\":8,\"stock\":2}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result =
          json_path_query(v, "$.items[?(@.price < 10 && @.stock > 0)].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 1);
      check_equal(json_string(json_path_result_get(result, 0)), "c");
      json_path_result_free(result);
      json_free(v);
    }

    it("should apply ? filters to descendants") {
      const char *json = "{\"store\":{\"book\":[{\"title\":\"t1\",\"price\":8.95},"
                         "{\"title\":\"t2\",\"price\":12.99},"
                         "{\"title\":\"t3\",\"price\":9.5}],"
                         "\"bicycle\":{\"price\":19.95}}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$..book[?@.price < 10].title");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "t1");
      check_equal(json_string(json_path_result_get(result, 1)), "t3");
      json_path_result_free(result);
      json_free(v);
    }

    it("should support reversed literal comparisons in filters") {
      const char *json = "{\"items\":[{\"port\":1,\"name\":\"a\"},"
                         "{\"port\":5,\"name\":\"b\"},"
                         "{\"port\":9,\"name\":\"c\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.items[?2 < @.port].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "b");
      check_equal(json_string(json_path_result_get(result, 1)), "c");

      json_path_result_t *strings =
          json_path_query(v, "$.items[?'b' == @.name].port");
      check_not_null(strings);
      check_equal(json_path_result_size(strings), 1);
      check_equal((int)json_number(json_path_result_get(strings, 0)), 5);

      json_path_result_free(strings);
      json_path_result_free(result);
      json_free(v);
    }

    it("should filter by member absence with negated existence") {
      const char *json = "{\"items\":[{\"name\":\"a\",\"isbn\":\"x\"},"
                         "{\"name\":\"b\"},"
                         "{\"name\":\"c\",\"isbn\":\"y\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.items[?!@.isbn].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 1);
      check_equal(json_string(json_path_result_get(result, 0)), "b");
      json_path_result_free(result);
      json_free(v);
    }

    it("should compare string members inside ? filters") {
      const char *json = "{\"items\":[{\"name\":\"alpha\",\"price\":5},"
                         "{\"name\":\"beta\",\"price\":15}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result = json_path_query(v, "$.items[?@.name == 'alpha'].price");
      check_not_null(result);
      check_equal(json_path_result_size(result), 1);
      check_equal((int)json_number(json_path_result_get(result, 0)), 5);
      json_path_result_free(result);
      json_free(v);
    }

    it("should reject slice selectors in stream matchers") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[0:2]");

      check_not_null(program);
      check_null(json_path_stream_create(program, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");

      json_path_program_free(program);
    }

    it("should reject standard ? filter syntax in stream matchers") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[?@.price < 10].name");

      check_not_null(program);
      check_null(json_path_stream_create(program, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");

      json_path_program_free(program);
    }

    it("should count duplicate union fanout alternatives in streams") {
      const char *json = "{\"settings\":{\"a\":100}}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.settings['a','a']");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.match_starts, 1);
      check_equal(capture.match_ends, 1);
      check_equal(capture.numbers, 1);
      check_equal(capture.number_values[0], "100");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should filter with length() on array and object members") {
      const char *json = "{\"store\":{\"book\":["
                         "{\"title\":\"abc\",\"authors\":[\"x\"],\"meta\":{\"a\":1,\"b\":2}},"
                         "{\"title\":\"a very long title here\",\"authors\":[\"x\",\"y\"],\"meta\":{\"a\":1}},"
                         "{\"title\":\"xy\",\"authors\":[\"x\",\"y\",\"z\"],\"meta\":{\"a\":1,\"b\":2,\"c\":3}}]}}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *long_title = json_path_query(v, "$.store.book[?length(@.title) > 10].title");
      check_not_null(long_title);
      check_equal(json_path_result_size(long_title), 1);
      check_equal(json_string(json_path_result_get(long_title, 0)), "a very long title here");

      json_path_result_t *two_authors = json_path_query(v, "$.store.book[?count(@.authors[*]) > 1].title");
      check_not_null(two_authors);
      check_equal(json_path_result_size(two_authors), 2);
      check_equal(json_string(json_path_result_get(two_authors, 0)), "a very long title here");
      check_equal(json_string(json_path_result_get(two_authors, 1)), "xy");

      json_path_result_t *two_members = json_path_query(v, "$.store.book[?length(@.meta) == 2].title");
      check_not_null(two_members);
      check_equal(json_path_result_size(two_members), 1);
      check_equal(json_string(json_path_result_get(two_members, 0)), "abc");

      json_path_result_t *exact_count = json_path_query(v, "$.store.book[?count(@.authors[*]) == 3].title");
      check_not_null(exact_count);
      check_equal(json_path_result_size(exact_count), 1);
      check_equal(json_string(json_path_result_get(exact_count, 0)), "xy");

      json_path_result_free(exact_count);
      json_path_result_free(two_members);
      json_path_result_free(two_authors);
      json_path_result_free(long_title);
      json_free(v);
    }

    it("should support no-argument length() on the current node") {
      const char *json = "{\"groups\":[[\"a\",\"b\",\"c\"],[\"a\"],[\"a\",\"b\"]],\"solo\":[[\"a\"]]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *big = json_path_query(v, "$.groups[?length() > 2]");
      check_not_null(big);
      check_equal(json_path_result_size(big), 1);
      check_equal(json_string(json_array_get(json_path_result_get(big, 0), 0)), "a");
      check_equal(json_string(json_array_get(json_path_result_get(big, 0), 2)), "c");

      json_path_result_t *solo = json_path_query(v, "$.solo[?length() == 1]");
      check_not_null(solo);
      check_equal(json_path_result_size(solo), 1);
      check_equal(json_string(json_array_get(json_path_result_get(solo, 0), 0)), "a");

      json_path_result_free(solo);
      json_path_result_free(big);
      json_free(v);
    }

    it("should count UTF-8 characters for string length()") {
      const char *json = "{\"items\":[{\"name\":\"中文\"},{\"name\":\"hello\"},{\"name\":\"é\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      json_path_result_t *two_chars = json_path_query(v, "$.items[?length(@.name) == 2].name");
      check_not_null(two_chars);
      check_equal(json_path_result_size(two_chars), 1);
      check_equal(json_string(json_path_result_get(two_chars, 0)), "中文");

      json_path_result_t *one_char = json_path_query(v, "$.items[?length(@.name) == 1].name");
      check_not_null(one_char);
      check_equal(json_path_result_size(one_char), 1);
      check_equal(json_string(json_path_result_get(one_char, 0)), "é");

      json_path_result_free(one_char);
      json_path_result_free(two_chars);
      json_free(v);
    }

    it("should support match() and search() regex functions") {
      const char *json = "{\"items\":[{\"code\":\"A-1\",\"name\":\"alpha\"},"
                         "{\"code\":\"B-22\",\"name\":\"beta\"},"
                         "{\"code\":\"XA-1\",\"name\":\"gamma\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);

      /* match() requires the whole string to match the pattern. */
      json_path_result_t *full = json_path_query(v, "$.items[?match(@.code, 'A-1')].name");
      check_not_null(full);
      check_equal(json_path_result_size(full), 1);
      check_equal(json_string(json_path_result_get(full, 0)), "alpha");

      /* search() finds the pattern anywhere in the string. */
      json_path_result_t *any = json_path_query(v, "$.items[?search(@.code, 'A-1')].name");
      check_not_null(any);
      check_equal(json_path_result_size(any), 2);
      check_equal(json_string(json_path_result_get(any, 0)), "alpha");
      check_equal(json_string(json_path_result_get(any, 1)), "gamma");

      /* Anchored patterns work with both forms. */
      json_path_result_t *anchored =
          json_path_query(v, "$.items[?match(@.code, '^[A-C]-[0-9]+$')].name");
      check_not_null(anchored);
      check_equal(json_path_result_size(anchored), 2);
      check_equal(json_string(json_path_result_get(anchored, 0)), "alpha");
      check_equal(json_string(json_path_result_get(anchored, 1)), "beta");

      json_path_result_free(anchored);
      json_path_result_free(any);
      json_path_result_free(full);
      json_free(v);
    }

    it("should support case-insensitive contains_ci()") {
      const char *json = "{\"items\":[{\"name\":\"Alpha-1\"},{\"name\":\"beta\"},"
                         "{\"name\":\"ALPHABET\"}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *result =
          json_path_query(v, "$.items[?contains_ci(@.name, 'alph')].name");
      check_not_null(result);
      check_equal(json_path_result_size(result), 2);
      check_equal(json_string(json_path_result_get(result, 0)), "Alpha-1");
      check_equal(json_string(json_path_result_get(result, 1)), "ALPHABET");

      json_path_result_t *missing = json_path_query(v, "$.items[?contains_ci(@.name, 'zzz')].name");
      check_not_null(missing);
      check_equal(json_path_result_size(missing), 0);

      json_path_result_free(missing);
      json_path_result_free(result);
      json_free(v);
    }

    it("should keep contains_ci usable as a plain member name") {
      const char *json = "{\"contains_ci\":42}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_value_t *value = json_path_get(v, "$.contains_ci");
      check_not_null(value);
      check_equal((int)json_number(value), 42);
      json_free(v);
    }

    it("should keep match and search usable as plain member names") {
      const char *json = "{\"match\":1,\"search\":2}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_value_t *m = json_path_get(v, "$.match");
      check_not_null(m);
      check_equal((int)json_number(m), 1);
      json_value_t *s = json_path_get(v, "$.search");
      check_not_null(s);
      check_equal((int)json_number(s), 2);
      json_free(v);
    }

    it("should keep length and count usable as plain member names") {
      const char *json = "{\"length\":5,\"count\":7}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_value_t *length = json_path_get(v, "$.length");
      check_not_null(length);
      check_equal((int)json_number(length), 5);
      json_value_t *count = json_path_get(v, "$.count");
      check_not_null(count);
      check_equal((int)json_number(count), 7);
      json_free(v);
    }

    it("should fall back to recursive evaluation for very deep filters") {
      char expr[4096];
      size_t pos = 0;
      int i;
      pos += (size_t)snprintf(expr + pos, sizeof(expr) - pos, "$.items[?@.a == ");
      for (i = 0; i < 70; ++i)
        pos += (size_t)snprintf(expr + pos, sizeof(expr) - pos, "(@.a == ");
      pos += (size_t)snprintf(expr + pos, sizeof(expr) - pos, "1");
      for (i = 0; i < 70; ++i)
        pos += (size_t)snprintf(expr + pos, sizeof(expr) - pos, ")");
      pos += (size_t)snprintf(expr + pos, sizeof(expr) - pos, "]");

      const char *json = "{\"items\":[{\"a\":1},{\"a\":2}]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_program_t *program = json_path_compile(expr);
      check_not_null(program);
      json_path_result_t *result = json_path_query_compiled(v, program);
      check_not_null(result);
      check_equal(json_path_result_size(result), 0);
      json_path_result_free(result);
      json_path_program_free(program);
      json_free(v);
    }

    it("should treat literal booleans as filter truth values") {
      const char *json = "{\"items\":[1,2,3]}";
      json_value_t *v = json_parse(json, strlen(json));
      check_not_null(v);
      json_path_result_t *all = json_path_query(v, "$.items[?true]");
      check_not_null(all);
      check_equal(json_path_result_size(all), 3);
      json_path_result_t *none = json_path_query(v, "$.items[?false]");
      check_not_null(none);
      check_equal(json_path_result_size(none), 0);
      json_path_result_free(none);
      json_path_result_free(all);
      json_free(v);
    }

    it("should stream scalar array filters with zero buffering") {
      const char *json = "{\"nums\":[1,2,3,4,5]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.nums[?@ > 2]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_equal(json_path_stream_match_count(stream), 3);
      check_equal(capture.match_starts, 3);
      check_equal(capture.match_ends, 3);
      check_equal(capture.numbers, 3);
      check_equal(capture.number_values[0], "3");
      check_equal(capture.number_values[1], "4");
      check_equal(capture.number_values[2], "5");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream scalar equality filters and reversed comparisons") {
      const char *json = "{\"nums\":[1,2,3]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *eq = json_path_compile("$.nums[?@ == 2]");
      json_path_stream_t *stream = json_path_stream_create(eq, &handler, &capture);

      check_not_null(eq);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 1);
      check_equal(capture.number_values[0], "2");

      json_path_stream_destroy(stream);
      json_path_program_free(eq);

      capture = (json_path_stream_test_ctx_t){0};
      json_path_program_t *reversed = json_path_compile("$.nums[?2 < @]");
      json_path_stream_t *rstream = json_path_stream_create(reversed, &handler, &capture);
      check_not_null(reversed);
      check_not_null(rstream);
      check_equal(json_path_stream_feed(rstream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(rstream), 0);
      check_equal(json_path_stream_match_count(rstream), 1);
      check_equal(capture.number_values[0], "3");

      json_path_stream_destroy(rstream);
      json_path_program_free(reversed);
    }

    it("should stream object member value filters and string predicates") {
      const char *json = "{\"cfg\":{\"a\":\"on\",\"b\":\"off\",\"c\":\"on\"}}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.cfg[?@ == 'on']");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.strings, 2);
      check_equal(capture.string_values[0], "on");
      check_equal(capture.string_values[1], "on");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream boolean value filters") {
      const char *json = "{\"flags\":[true,false,true]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.flags[?@ == true]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.match_starts, 2);
      check_equal(capture.match_ends, 2);

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should stream long string filter candidates via the transient buffer") {
      const char *long_value = "a very long string value that exceeds the inline sixty four byte buffer";
      char json[256];
      snprintf(json, sizeof(json), "{\"words\":[\"short\",\"%s\"]}", long_value);
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.words[?@ == 'a very long string value that exceeds the inline sixty four byte buffer']");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_equal(json_path_stream_match_count(stream), 1);
      check_equal(capture.strings, 1);
      check_equal(capture.string_values[0], long_value);

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }

    it("should reject non-terminal and boolean stream filters") {
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *continued = json_path_compile("$.nums[?@ > 2].x");
      json_path_program_t *boolean = json_path_compile("$.nums[?@ > 2 && @ < 5]");

      check_not_null(continued);
      check_not_null(boolean);
      check_null(json_path_stream_create(continued, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");
      check_null(json_path_stream_create(boolean, &handler, &capture));
      check_contains(json_path_get_error(), "not streamable");

      json_path_program_free(boolean);
      json_path_program_free(continued);
    }

    it("should count duplicate array index union alternatives in streams") {
      const char *json = "{\"items\":[10,20]}";
      json_path_stream_test_ctx_t capture = {0};
      json_path_stream_handler_t handler = json_path_stream_test_handler();
      json_path_program_t *program = json_path_compile("$.items[0,0]");
      json_path_stream_t *stream = json_path_stream_create(program, &handler, &capture);

      check_not_null(program);
      check_not_null(stream);
      check_equal(json_path_stream_feed(stream, json, strlen(json)), 0);
      check_equal(json_path_stream_finish(stream), 0);
      check_null(json_path_stream_error(stream));
      check_equal(json_path_stream_match_count(stream), 2);
      check_equal(capture.match_starts, 1);
      check_equal(capture.match_ends, 1);
      check_equal(capture.numbers, 1);
      check_equal(capture.number_values[0], "10");

      json_path_stream_destroy(stream);
      json_path_program_free(program);
    }
  }

  describe("Error Handling") {
    it("should return NULL and set error for invalid JSON") {
      json_value_t *v = json_parse("invalid", 7);
      check_null(v);
      check_not_null(json_get_error());
    }

    it("keeps parser diagnostics local to the calling thread") {
      json_error_thread_probe probe = {0};
      cmeta_thread_t thread = NULL;
      json_value_t *value = json_parse("invalid", 7u);

      check_null(value);
      check_not_null(json_get_error());
      check_equal(cmeta_thread_create(&thread, json_parse_success_on_thread,
                                      &probe),
                  0);
      check_equal(cmeta_thread_join(&thread), 0);
      cmeta_thread_destroy(&thread);
      check_equal(probe.status, 0);
      check_not_null(json_get_error());
    }

    it("should return NULL for unclosed braces") {
      json_value_t *v = json_parse("{\"key\": 1", 9);
      check_null(v);
    }
  }

  describe("SAX Parsing") {
    it("should SAX parse a simple object correctly") {
      const char *json = "{\"name\": \"test\", \"value\": 42}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.object_start_count, 1);
      check_equal(ctx.object_end_count, 1);
      check_equal(ctx.key_count, 2);
      check_equal(ctx.string_count, 1);
      check_equal(ctx.number_count, 1);
      check_within(ctx.last_number, 42.0, 0.001);
    }

    it("should SAX parse an array correctly") {
      const char *json = "[1, 2, 3, 4, 5]";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
      check_equal(ctx.number_count, 5);
      check_within(ctx.last_number, 5.0, 0.001);
    }

    it("should SAX parse nested structures correctly") {
      const char *json = "{\"arr\": [1, 2], \"obj\": {\"x\": true}}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.object_start_count, 2);
      check_equal(ctx.object_end_count, 2);
      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
      check_equal(ctx.key_count, 3);
      check_equal(ctx.number_count, 2);
      check_equal(ctx.bool_count, 1);
    }

    it("should SAX parse all JSON types correctly") {
      const char *json =
          "{\"n\": null, \"b\": false, \"i\": 123, \"s\": \"hello\", \"a\": [], \"o\": {}}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.null_count, 1);
      check_equal(ctx.bool_count, 1);
      check_equal(ctx.number_count, 1);
      check_equal(ctx.string_count, 1);
      check_equal(ctx.object_start_count, 2);
      check_equal(ctx.object_end_count, 2);
      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
    }

    it("should handle empty objects in SAX") {
      const char *json = "{}";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.object_start_count, 1);
      check_equal(ctx.object_end_count, 1);
    }

    it("should handle empty arrays in SAX") {
      const char *json = "[]";
      sax_test_ctx_t ctx = {0};

      int ret = json_parse_sax(json, strlen(json), &test_handler, &ctx);
      check_equal(ret, 0);
      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
    }

    it("should incrementally SAX parse split strings, literals, and numbers") {
      const char *parts[] = {"{\"na", "me\":\"a\\", "\"b\",", "\"items\":[tr",
                             "ue,n",  "ull,12.5",   "e2]}"};
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        check_equal(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      }
      check_equal(json_sax_parser_finish(parser), 0);

      check_equal(ctx.object_start_count, 1);
      check_equal(ctx.object_end_count, 1);
      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
      check_equal(ctx.key_count, 2);
      check_equal(ctx.string_count, 1);
      check_equal(ctx.last_string, "a\"b");
      check_equal(ctx.bool_count, 1);
      check_equal(ctx.null_count, 1);
      check_equal(ctx.number_count, 1);
      check_within(ctx.last_number, 1250.0, 0.001);

      json_sax_parser_destroy(parser);
    }

    it("should incrementally SAX parse byte by byte") {
      const char *json = "[1,2,{\"x\":\"y\"}]";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < strlen(json); ++i) {
        check_equal(json_sax_parser_feed(parser, json + i, 1), 0);
      }
      check_equal(json_sax_parser_finish(parser), 0);

      check_equal(ctx.array_start_count, 1);
      check_equal(ctx.array_end_count, 1);
      check_equal(ctx.object_start_count, 1);
      check_equal(ctx.object_end_count, 1);
      check_equal(ctx.key_count, 1);
      check_equal(ctx.string_count, 1);
      check_equal(ctx.number_count, 2);
      check_within(ctx.last_number, 2.0, 0.001);
      check_equal(ctx.last_key, "x");
      check_equal(ctx.last_string, "y");

      json_sax_parser_destroy(parser);
    }

    it("should preserve exact number tokens in raw SAX mode") {
      const char *parts[] = {"[9007199254740", "993,-922337203685477", "5808,",
                             "18446744073709551615]"};
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create_raw(&test_raw_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        check_equal(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      }
      check_equal(json_sax_parser_finish(parser), 0);
      check_equal(ctx.number_count, 3);
      check_equal(ctx.raw_numbers[0], "9007199254740993");
      check_equal(ctx.raw_numbers[1], "-9223372036854775808");
      check_equal(ctx.raw_numbers[2], "18446744073709551615");
      json_sax_parser_destroy(parser);

      memset(&ctx, 0, sizeof(ctx));
      check_equal(json_parse_sax_raw("1.25e+9", 7, &test_raw_handler, &ctx), 0);
      check_equal(ctx.raw_numbers[0], "1.25e+9");
      check_null(json_sax_parser_create_raw(NULL, &ctx));
      check_equal(json_parse_sax_raw(NULL, 0, &test_raw_handler, &ctx), -1);
    }

    it("should retain legacy double number callbacks") {
      sax_test_ctx_t ctx = {0};
      check_equal(json_parse_sax("9007199254740993", 16, &test_handler, &ctx), 0);
      check_equal(ctx.number_count, 1);
      check_within(ctx.last_number, 9007199254740992.0, 0.0);
    }

    it("should SAX decode surrogate pairs across chunks") {
      const char *parts[] = {"{\"\\uD83D", "\\uDE00\":\"\\uD83D", "\\uDE00\"}"};
      const char emoji[] = "\xF0\x9F\x98\x80";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        check_equal(json_sax_parser_feed(parser, parts[i], strlen(parts[i])), 0);
      check_equal(json_sax_parser_finish(parser), 0);
      check_equal(ctx.last_key, emoji, 4);
      check_equal(ctx.last_string, emoji, 4);

      json_sax_parser_destroy(parser);
    }

    it("should SAX reject unpaired surrogates") {
      const char *invalid[] = {"\"\\uD83D\"", "\"\\uDE00\"", "\"\\uD83D\\n\""};
      for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        sax_test_ctx_t ctx = {0};
        check_equal(json_parse_sax(invalid[i], strlen(invalid[i]), &test_handler, &ctx), -1);
      }
    }

    it("should report incomplete input at incremental finish") {
      const char *json = "{\"a\": [";
      sax_test_ctx_t ctx = {0};
      json_sax_parser_t *parser = json_sax_parser_create(&test_handler, &ctx);
      check_not_null(parser);

      check_equal(json_sax_parser_feed(parser, json, strlen(json)), 0);
      check_equal(json_sax_parser_finish(parser), -1);
      check_not_null(json_sax_parser_error(parser));

      json_sax_parser_destroy(parser);
    }

    it("should stop incremental SAX parsing when a callback fails") {
      json_sax_handler_t handler = test_handler;
      sax_test_ctx_t ctx = {0};
      handler.on_string = sax_fail_on_string;

      json_sax_parser_t *parser = json_sax_parser_create(&handler, &ctx);
      check_not_null(parser);
      check_equal(json_sax_parser_feed(parser, "\"stop\"", 6), -1);
      check_contains(json_sax_parser_error(parser), "callback");

      json_sax_parser_destroy(parser);
    }

    it("should reject extra data after one JSON document in SAX") {
      sax_test_ctx_t ctx = {0};
      int ret = json_parse_sax("true false", strlen("true false"), &test_handler, &ctx);
      check_equal(ret, -1);
    }
  }
}
