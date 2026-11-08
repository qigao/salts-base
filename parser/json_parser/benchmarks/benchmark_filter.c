/**
 * @file benchmark_filter.c
 * @brief Benchmark for compiled JSONPath filter expression evaluation
 *
 * Measures the filter VM throughput on a synthetic config-like document with
 * comparison, boolean, regex/contains, and length() predicates. Numbers are
 * for local relative comparison only (no committed cross-machine baseline).
 */

#include "json_parser.h"
#include <fmt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
static double get_time_ms(void) {
  LARGE_INTEGER freq, counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
  #include <sys/time.h>
static double get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

static char *generate_items(size_t count) {
  size_t capacity = count * 96 + 128;
  char *buf = (char *)malloc(capacity);
  size_t pos = 0;
  if (!buf) return NULL;
  pos += (size_t)snprintf(buf + pos, capacity - pos, "{\"items\":[");
  for (size_t i = 0; i < count; ++i) {
    pos += (size_t)snprintf(buf + pos, capacity - pos, "%s{\"port\":%d,\"name\":\"item-%03zu\",\"active\":%s}",
                            i == 0 ? "" : ",", 1000 + (int)(i % 9000), i, (i % 2) ? "true" : "false");
  }
  pos += (size_t)snprintf(buf + pos, capacity - pos, "]}");
  return buf;
}

typedef struct {
  const char *expr;
  size_t iterations;
} filter_case_t;

static void run_case(const char *json, json_value_t *root, const char *expr,
                     size_t items, int iterations) {
  json_path_program_t *program = json_path_compile(expr);
  double start;
  double elapsed;
  size_t total = 0;
  if (!program) {
    printf("%-46s compile failed\n", expr);
    return;
  }
  start = get_time_ms();
  for (int i = 0; i < iterations; ++i) {
    json_path_result_t *result = json_path_query_compiled(root, program);
    if (result) {
      total += json_path_result_size(result);
      json_path_result_free(result);
    }
  }
  elapsed = get_time_ms() - start;
  printf("%-46s %6.2f M items/s  (total matches %zu)\n", expr,
         (double)items * (double)iterations / (elapsed / 1000.0) / 1e6, total);
  json_path_program_free(program);
  (void)json;
}

int main(void) {
  const size_t items = 20000;
  const int iterations = 50;
  char *json = generate_items(items);
  json_value_t *root;
  if (!json) return 1;
  root = json_parse(json, strlen(json));
  if (!root) {
    free(json);
    return 1;
  }

  run_case(json, root, "$.items[?@.port >= 8000].name", items, iterations);
  run_case(json, root, "$.items[?@.port >= 8000 && @.active].name", items, iterations);
  run_case(json, root, "$.items[?(@.port < 2000 || @.port > 9000)].name", items, iterations);
  run_case(json, root, "$.items[?@.name ~ '^item-0'].name", items, iterations);
  run_case(json, root, "$.items[?contains(@.name, 'item-1')].name", items, iterations);
  run_case(json, root, "$.items[?length(@.name) > 7].name", items, iterations);

  json_free(root);
  free(json);
  return 0;
}
