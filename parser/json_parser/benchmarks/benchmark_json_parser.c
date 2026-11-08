/**
 * @file benchmark_json_parser.c
 * @brief TinyTest benchmarks for JSON parser performance
 */

#include "json_parser.h"
#include "tinytest.h"
#include <fmt.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char *json;
  size_t json_len;
  size_t iterations;
  int owned;
} benchmark_t;

enum {
  BENCH_TINY_OBJECT = 0,
  BENCH_SMALL_OBJECT,
  BENCH_ARRAY_100_NUMBERS,
  BENCH_ARRAY_1000_NUMBERS,
  BENCH_ARRAY_10000_NUMBERS,
  BENCH_ARRAY_100_OBJECTS,
  BENCH_ARRAY_1000_OBJECTS,
  BENCH_NESTED_DEPTH_10,
  BENCH_NESTED_DEPTH_50,
  BENCH_NESTED_DEPTH_100,
  BENCH_STRINGS_100_X_100,
  BENCH_STRINGS_100_X_1000,
  BENCH_MQTT_SMALL,
  BENCH_MQTT_MEDIUM,
  BENCH_MQTT_LARGE,
  BENCH_COUNT
};

static benchmark_t g_benchmarks[BENCH_COUNT] = {
    {"tiny object", "{\"a\":1}", 0, 0, 0},
    {"small object", "{\"name\":\"test\",\"value\":42,\"active\":true}", 0, 0, 0},
    {"array 100 numbers", NULL, 0, 0, 1},
    {"array 1000 numbers", NULL, 0, 0, 1},
    {"array 10000 numbers", NULL, 0, 0, 1},
    {"array 100 objects", NULL, 0, 0, 1},
    {"array 1000 objects", NULL, 0, 0, 1},
    {"nested depth 10", NULL, 0, 0, 1},
    {"nested depth 50", NULL, 0, 0, 1},
    {"nested depth 100", NULL, 0, 0, 1},
    {"100 strings x 100 chars", NULL, 0, 0, 1},
    {"100 strings x 1000 chars", NULL, 0, 0, 1},
    {"mqtt config small", NULL, 0, 0, 1},
    {"mqtt config medium", NULL, 0, 0, 1},
    {"mqtt config large", NULL, 0, 0, 1},
};

static int g_benchmarks_initialized = 0;
static volatile size_t g_sink_size = 0;
static volatile double g_sink_num = 0.0;

static char *generate_array_of_numbers(size_t count) {
  size_t buf_size = count * 12 + 3;
  char *buf = (char *)malloc(buf_size);
  if (!buf)
    return NULL;

  int pos = 0;
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "[");
  for (size_t i = 0; i < count; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos, "{}", i);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "]");
  return buf;
}

static char *generate_array_of_objects(size_t count) {
  size_t buf_size = count * 100 + 3;
  char *buf = (char *)malloc(buf_size);
  if (!buf)
    return NULL;

  int pos = 0;
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "[");
  for (size_t i = 0; i < count; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos,
               "{{\"id\":{},\"name\":\"item_{}\",\"value\":{}.{},\"active\":true}}", i, i,
               i * 10, i % 10);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "]");
  return buf;
}

static char *generate_nested_object(int depth) {
  size_t buf_size = (size_t)depth * 50 + 100;
  char *buf = (char *)malloc(buf_size);
  if (!buf)
    return NULL;

  int pos = 0;
  for (int i = 0; i < depth; i++) {
    pos += fmt(buf + pos, buf_size - (size_t)pos, "{{\"level{}\":", i);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "\"deep_value\"");
  for (int i = 0; i < depth; i++) {
    pos += fmt_text(buf + pos, buf_size - (size_t)pos, "}}");
  }
  return buf;
}

static char *generate_string_heavy(size_t string_count, size_t string_len) {
  size_t buf_size = string_count * (string_len + 20) + 100;
  char *buf = (char *)malloc(buf_size);
  if (!buf)
    return NULL;

  char *value = (char *)malloc(string_len + 1);
  if (!value) {
    free(buf);
    return NULL;
  }

  memset(value, 'x', string_len);
  value[string_len] = '\0';

  int pos = 0;
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "{{");
  for (size_t i = 0; i < string_count; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos, "\"key{}\":\"{}\"", i, value);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "}}");

  free(value);
  return buf;
}

static char *generate_mqtt_proxy_config(size_t listeners, size_t upstreams, size_t filters) {
  size_t buf_size = (listeners + upstreams + filters) * 200 + 500;
  char *buf = (char *)malloc(buf_size);
  if (!buf)
    return NULL;

  int pos = 0;
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "{{\"listeners\":[");
  for (size_t i = 0; i < listeners; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos,
               "{{\"port\":{},\"transport\":\"tcp\",\"host\":\"0.0.0.0\"}}", 1883 + i);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "],\"upstreams\":[");
  for (size_t i = 0; i < upstreams; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos,
               "{{\"host\":\"10.0.0.{}\",\"port\":1883,\"weight\":{}}}", i + 1, (i % 3) + 1);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "],\"filters\":[");
  for (size_t i = 0; i < filters; i++) {
    if (i > 0)
      pos += fmt_text(buf + pos, buf_size - (size_t)pos, ",");
    pos += fmt(buf + pos, buf_size - (size_t)pos,
               "{{\"type\":\"topic\",\"action\":\"deny\",\"pattern\":\"$SYS/{}/#\"}}", i);
  }
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "],\"settings\":{{");
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "\"max_clients\":10000,");
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "\"connect_timeout_ms\":5000,");
  pos += fmt_text(buf + pos, buf_size - (size_t)pos, "\"hash_replicas\":150}}}}");

  return buf;
}

static int bench_quick_mode(void) {
  const char *value = getenv("JSON_BENCH_QUICK");
  return value && value[0] && strcmp(value, "0") != 0;
}

static void set_benchmark_iterations(int quick) {
  g_benchmarks[BENCH_TINY_OBJECT].iterations = quick ? 10000 : 100000;
  g_benchmarks[BENCH_SMALL_OBJECT].iterations = quick ? 10000 : 100000;
  g_benchmarks[BENCH_ARRAY_100_NUMBERS].iterations = quick ? 1000 : 10000;
  g_benchmarks[BENCH_ARRAY_1000_NUMBERS].iterations = quick ? 100 : 1000;
  g_benchmarks[BENCH_ARRAY_10000_NUMBERS].iterations = quick ? 10 : 100;
  g_benchmarks[BENCH_ARRAY_100_OBJECTS].iterations = quick ? 1000 : 10000;
  g_benchmarks[BENCH_ARRAY_1000_OBJECTS].iterations = quick ? 100 : 1000;
  g_benchmarks[BENCH_NESTED_DEPTH_10].iterations = quick ? 10000 : 100000;
  g_benchmarks[BENCH_NESTED_DEPTH_50].iterations = quick ? 10000 : 50000;
  g_benchmarks[BENCH_NESTED_DEPTH_100].iterations = quick ? 5000 : 20000;
  g_benchmarks[BENCH_STRINGS_100_X_100].iterations = quick ? 1000 : 10000;
  g_benchmarks[BENCH_STRINGS_100_X_1000].iterations = quick ? 100 : 1000;
  g_benchmarks[BENCH_MQTT_SMALL].iterations = quick ? 10000 : 100000;
  g_benchmarks[BENCH_MQTT_MEDIUM].iterations = quick ? 1000 : 10000;
  g_benchmarks[BENCH_MQTT_LARGE].iterations = quick ? 100 : 1000;
}

static void init_benchmarks(void) {
  if (g_benchmarks_initialized)
    return;

  set_benchmark_iterations(bench_quick_mode());

  g_benchmarks[BENCH_ARRAY_100_NUMBERS].json = generate_array_of_numbers(100);
  g_benchmarks[BENCH_ARRAY_1000_NUMBERS].json = generate_array_of_numbers(1000);
  g_benchmarks[BENCH_ARRAY_10000_NUMBERS].json = generate_array_of_numbers(10000);
  g_benchmarks[BENCH_ARRAY_100_OBJECTS].json = generate_array_of_objects(100);
  g_benchmarks[BENCH_ARRAY_1000_OBJECTS].json = generate_array_of_objects(1000);
  g_benchmarks[BENCH_NESTED_DEPTH_10].json = generate_nested_object(10);
  g_benchmarks[BENCH_NESTED_DEPTH_50].json = generate_nested_object(50);
  g_benchmarks[BENCH_NESTED_DEPTH_100].json = generate_nested_object(100);
  g_benchmarks[BENCH_STRINGS_100_X_100].json = generate_string_heavy(100, 100);
  g_benchmarks[BENCH_STRINGS_100_X_1000].json = generate_string_heavy(100, 1000);
  g_benchmarks[BENCH_MQTT_SMALL].json = generate_mqtt_proxy_config(2, 3, 5);
  g_benchmarks[BENCH_MQTT_MEDIUM].json = generate_mqtt_proxy_config(10, 20, 50);
  g_benchmarks[BENCH_MQTT_LARGE].json = generate_mqtt_proxy_config(50, 100, 200);

  for (size_t i = 0; i < BENCH_COUNT; i++) {
    check_not_null(g_benchmarks[i].json);
    g_benchmarks[i].json_len = strlen(g_benchmarks[i].json);
  }

  for (int i = 0; i < 1000; i++) {
    json_value_t *v = json_parse("{\"x\":1}", 7);
    check_not_null(v);
    json_free(v);
  }

  g_benchmarks_initialized = 1;
}

static void free_benchmarks(void) {
  if (!g_benchmarks_initialized)
    return;

  for (size_t i = 0; i < BENCH_COUNT; i++) {
    if (g_benchmarks[i].owned) {
      free((void *)g_benchmarks[i].json);
      g_benchmarks[i].json = NULL;
    }
  }

  g_benchmarks_initialized = 0;
}

static void benchmark_dom_query_workload(const benchmark_t *bench) {
  json_value_t *root = json_parse(bench->json, bench->json_len);
  check_not_null(root);

  switch ((int)(bench - g_benchmarks)) {
  case BENCH_SMALL_OBJECT:
    g_sink_num += json_get_double(root, "value", 0.0);
    g_sink_size += json_get_bool(root, "active", false) ? 1u : 0u;
    break;
  case BENCH_ARRAY_100_OBJECTS: {
    size_t count = json_array_size(root);
    g_sink_size += count;
    json_value_t *first = json_array_get(root, 0);
    g_sink_num += json_get_double(first, "value", 0.0);
    g_sink_size += json_get_bool(first, "active", false) ? 1u : 0u;
    break;
  }
  case BENCH_STRINGS_100_X_1000: {
    vstr s = json_get_string_v(root, "key0");
    g_sink_size += s.len;
    break;
  }
  case BENCH_MQTT_MEDIUM: {
    json_value_t *listeners = json_object_get(root, "listeners");
    json_value_t *upstreams = json_object_get(root, "upstreams");
    json_value_t *settings = json_object_get(root, "settings");
    g_sink_size += json_array_size(listeners);
    g_sink_size += json_array_size(upstreams);
    g_sink_num += json_get_double(settings, "max_clients", 0.0);
    break;
  }
  default:
    g_sink_size += json_object_size(root) + json_array_size(root);
    break;
  }

  json_free(root);
}

typedef struct {
  int seen_first;
} sax_early_stop_ctx_t;

static int sax_stop_on_first_scalar(void *ctx, const char *val, size_t len) {
  (void)val;
  sax_early_stop_ctx_t *state = (sax_early_stop_ctx_t *)ctx;
  state->seen_first = 1;
  g_sink_size += len;
  return 1;
}

static int sax_stop_on_first_number(void *ctx, double val) {
  sax_early_stop_ctx_t *state = (sax_early_stop_ctx_t *)ctx;
  state->seen_first = 1;
  g_sink_num += val;
  return 1;
}

static int sax_stop_on_first_bool(void *ctx, bool val) {
  sax_early_stop_ctx_t *state = (sax_early_stop_ctx_t *)ctx;
  state->seen_first = 1;
  g_sink_size += val ? 1u : 0u;
  return 1;
}

static int sax_stop_on_first_null(void *ctx) {
  sax_early_stop_ctx_t *state = (sax_early_stop_ctx_t *)ctx;
  state->seen_first = 1;
  return 1;
}

static int sax_stop_on_first_key(void *ctx, const char *key, size_t len) {
  sax_early_stop_ctx_t *state = (sax_early_stop_ctx_t *)ctx;
  state->seen_first = 1;
  g_sink_size += len;
  (void)key;
  return 1;
}

#define JSON_DOM_BENCH(IDX)                                                                        \
  benchmark(g_benchmarks[(IDX)].name, g_benchmarks[(IDX)].iterations, 1) {                                                        \
    json_value_t *v = json_parse(g_benchmarks[(IDX)].json, g_benchmarks[(IDX)].json_len);         \
    check_not_null(v);                                                                             \
    json_free(v);                                                                                  \
  }

#define JSON_SAX_BENCH(IDX)                                                                        \
  benchmark(g_benchmarks[(IDX)].name, g_benchmarks[(IDX)].iterations, 1) {                                                        \
    check_equal(json_parse_sax(g_benchmarks[(IDX)].json, g_benchmarks[(IDX)].json_len,           \
                                &null_handler, NULL),                                              \
                 0);                                                                               \
  }

#define JSON_DOM_QUERY_BENCH(IDX)                                                                  \
  benchmark(g_benchmarks[(IDX)].name, g_benchmarks[(IDX)].iterations, 1) {                                                        \
    benchmark_dom_query_workload(&g_benchmarks[(IDX)]);                                           \
  }

#define JSON_SAX_EARLY_STOP_BENCH(IDX)                                                             \
  benchmark(g_benchmarks[(IDX)].name, g_benchmarks[(IDX)].iterations, 1) {  \
    sax_early_stop_ctx_t early_ctx = {0};                                                          \
    check_equal(json_parse_sax(g_benchmarks[(IDX)].json, g_benchmarks[(IDX)].json_len,           \
                                &early_stop_handler, &early_ctx),                                  \
                 -1);                                                                              \
    check_equal(early_ctx.seen_first, 1);                                                         \
  }

suite("json_parser benchmark") {
  static json_sax_handler_t null_handler;
  static json_sax_handler_t early_stop_handler;

  before_all() {
    init_benchmarks();
    memset(&null_handler, 0, sizeof(null_handler));
    memset(&early_stop_handler, 0, sizeof(early_stop_handler));
    early_stop_handler.on_null = sax_stop_on_first_null;
    early_stop_handler.on_bool = sax_stop_on_first_bool;
    early_stop_handler.on_number = sax_stop_on_first_number;
    early_stop_handler.on_string = sax_stop_on_first_scalar;
    early_stop_handler.on_object_key = sax_stop_on_first_key;
  }

  after_all() { free_benchmarks(); }

  bench("DOM mode") {
    benchmark_titles("test", "input", "iters", "avg(us)", NULL, "min(us)", "max(us)", "ops/s",
                     "size", "MB/s") {
      JSON_DOM_BENCH(BENCH_TINY_OBJECT);
      JSON_DOM_BENCH(BENCH_SMALL_OBJECT);
      JSON_DOM_BENCH(BENCH_ARRAY_100_NUMBERS);
      JSON_DOM_BENCH(BENCH_ARRAY_1000_NUMBERS);
      JSON_DOM_BENCH(BENCH_ARRAY_10000_NUMBERS);
      JSON_DOM_BENCH(BENCH_ARRAY_100_OBJECTS);
      JSON_DOM_BENCH(BENCH_ARRAY_1000_OBJECTS);
      JSON_DOM_BENCH(BENCH_NESTED_DEPTH_10);
      JSON_DOM_BENCH(BENCH_NESTED_DEPTH_50);
      JSON_DOM_BENCH(BENCH_NESTED_DEPTH_100);
      JSON_DOM_BENCH(BENCH_STRINGS_100_X_100);
      JSON_DOM_BENCH(BENCH_STRINGS_100_X_1000);
      JSON_DOM_BENCH(BENCH_MQTT_SMALL);
      JSON_DOM_BENCH(BENCH_MQTT_MEDIUM);
      JSON_DOM_BENCH(BENCH_MQTT_LARGE);
    }
  }

  bench("SAX mode") {
    benchmark_titles("test", "input", "iters", "avg(us)", NULL, "min(us)", "max(us)", "ops/s",
                     "size", "MB/s") {
      JSON_SAX_BENCH(BENCH_TINY_OBJECT);
      JSON_SAX_BENCH(BENCH_SMALL_OBJECT);
      JSON_SAX_BENCH(BENCH_ARRAY_100_NUMBERS);
      JSON_SAX_BENCH(BENCH_ARRAY_1000_NUMBERS);
      JSON_SAX_BENCH(BENCH_ARRAY_10000_NUMBERS);
      JSON_SAX_BENCH(BENCH_ARRAY_100_OBJECTS);
      JSON_SAX_BENCH(BENCH_ARRAY_1000_OBJECTS);
      JSON_SAX_BENCH(BENCH_NESTED_DEPTH_10);
      JSON_SAX_BENCH(BENCH_NESTED_DEPTH_50);
      JSON_SAX_BENCH(BENCH_NESTED_DEPTH_100);
      JSON_SAX_BENCH(BENCH_STRINGS_100_X_100);
      JSON_SAX_BENCH(BENCH_STRINGS_100_X_1000);
      JSON_SAX_BENCH(BENCH_MQTT_SMALL);
      JSON_SAX_BENCH(BENCH_MQTT_MEDIUM);
      JSON_SAX_BENCH(BENCH_MQTT_LARGE);
    }
  }

  bench("DOM parse+query") {
    benchmark_titles("test", "input", "iters", "avg(us)", NULL, "min(us)", "max(us)", "ops/s",
                     "size", "MB/s") {
      JSON_DOM_QUERY_BENCH(BENCH_SMALL_OBJECT);
      JSON_DOM_QUERY_BENCH(BENCH_ARRAY_100_OBJECTS);
      JSON_DOM_QUERY_BENCH(BENCH_STRINGS_100_X_1000);
      JSON_DOM_QUERY_BENCH(BENCH_MQTT_MEDIUM);
    }
  }

  bench("SAX early-stop") {
    benchmark_titles("test", "input", "iters", "avg(us)", NULL, "min(us)", "max(us)", "ops/s",
                     NULL, NULL) {
      JSON_SAX_EARLY_STOP_BENCH(BENCH_SMALL_OBJECT);
      JSON_SAX_EARLY_STOP_BENCH(BENCH_ARRAY_100_OBJECTS);
      JSON_SAX_EARLY_STOP_BENCH(BENCH_STRINGS_100_X_1000);
      JSON_SAX_EARLY_STOP_BENCH(BENCH_MQTT_MEDIUM);
    }
  }
}
