/**
 * @file benchmark_toon.c
 * @brief Benchmark for TOON parser performance
 *
 * Tests parsing speed with various TOON sizes and structures.
 */

#include "toonc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

typedef struct {
  const char *name;
  char *toon;
  size_t toon_len;
  int iterations;
} benchmark_t;

/* TOON: [0, 1, 2, ...] */
static char *generate_array_of_numbers(size_t count) {
  size_t buf_size = count * 20 + 50;
  char *buf = (char *)malloc(buf_size);
  if (!buf) return NULL;

  int pos = 0;
  pos += sprintf(buf + pos, "array[%zu]: ", count);
  for (size_t i = 0; i < count; i++) {
    if (i > 0)
      pos += sprintf(buf + pos, ", ");
    pos += sprintf(buf + pos, "%zu", i);
  }
  pos += sprintf(buf + pos, "\n");
  return buf;
}

/* TOON:
 * items:
 *   - id: 0
 *     name: "item_0"
 *     active: true
 *   - ...
 */
static char *generate_array_of_objects(size_t count) {
  size_t buf_size = count * 120 + 20;
  char *buf = (char *)malloc(buf_size);
  if (!buf) return NULL;

  int pos = 0;
  // Generating map of items instead of list with dashes
  for (size_t i = 0; i < count; i++) {
    pos += sprintf(buf + pos,
        "item_%zu:\n"
        "  id: %zu\n"
        "  name: \"item_%zu\"\n"
        "  value: %zu.%zu\n"
        "  active: true\n",
        i, i, i, i * 10, i % 10);
  }
  return buf;
}

/* TOON:
 * level0:
 *   level1:
 *     ...
 *       value: "deep"
 */
static char *generate_nested_object(int depth) {
  /* Calculate exact size needed first */
  size_t required_size = 0;
  for (int i = 0; i < depth; i++) {
    required_size += (i * 2); /* indentation */
    required_size += snprintf(NULL, 0, "level%d:\n", i);
  }
  required_size += (depth * 2); /* final indentation */
  required_size += strlen("value: \"deep_value\"\n") + 1; /* null terminator */

  char *buf = (char *)malloc(required_size);
  if (!buf) return NULL;

  int pos = 0;
  for (int i = 0; i < depth; i++) {
    for (int j = 0; j < i; j++) {
        buf[pos++] = ' ';
        buf[pos++] = ' ';
    }
    pos += sprintf(buf + pos, "level%d:\n", i);
  }
  for (int j = 0; j < depth; j++) {
      buf[pos++] = ' ';
      buf[pos++] = ' ';
  }
  pos += sprintf(buf + pos, "value: \"deep_value\"\n");
  return buf;
}

/* TOON:
 * key0: "xxxx..."
 * key1: "xxxx..."
 */
static char *generate_string_heavy(size_t string_count, size_t string_len) {
  size_t buf_size = string_count * (string_len + 30) + 100;
  char *buf = (char *)malloc(buf_size);
  if (!buf) return NULL;

  char *value = (char *)malloc(string_len + 1);
  if (!value) {
    free(buf);
    return NULL;
  }
  memset(value, 'x', string_len);
  value[string_len] = '\0';

  int pos = 0;
  for (size_t i = 0; i < string_count; i++) {
    pos += sprintf(buf + pos, "key%zu: \"%s\"\n", i, value);
  }

  free(value);
  return buf;
}

static char *generate_mqtt_proxy_config(size_t listeners, size_t upstreams, size_t filters) {
  size_t buf_size = (listeners + upstreams + filters) * 300 + 1000;
  char *buf = (char *)malloc(buf_size);
  if (!buf) return NULL;

  int pos = 0;

  // Listeners
  pos += sprintf(buf + pos, "listeners:\n");
  for (size_t i = 0; i < listeners; i++) {
    pos += sprintf(buf + pos,
        "  - port: %zu\n"
        "    transport: \"tcp\"\n"
        "    host: \"0.0.0.0\"\n", 1883 + i);
  }

  // Upstreams
  pos += sprintf(buf + pos, "\nupstreams:\n");
  for (size_t i = 0; i < upstreams; i++) {
    pos += sprintf(buf + pos,
        "  - host: \"10.0.0.%zu\"\n"
        "    port: 1883\n"
        "    weight: %zu\n", i + 1, (i % 3) + 1);
  }

  // Filters
  pos += sprintf(buf + pos, "\nfilters:\n");
  for (size_t i = 0; i < filters; i++) {
    pos += sprintf(buf + pos,
        "  - type: \"topic\"\n"
        "    action: \"deny\"\n"
        "    pattern: \"$SYS/%zu/#\"\n", i);
  }

  // Settings
  pos += sprintf(buf + pos, "\nsettings:\n");
  pos += sprintf(buf + pos, "  max_clients: 10000\n");
  pos += sprintf(buf + pos, "  connect_timeout_ms: 5000\n");
  pos += sprintf(buf + pos, "  hash_replicas: 150\n");

  return buf;
}

static void run_benchmark(benchmark_t *bench) {
  double start = get_time_ms();
  size_t total_bytes = 0;

  for (int i = 0; i < bench->iterations; i++) {
    toonObject *root = TOONc_parseStringLen(bench->toon, bench->toon_len);
    if (!root) {
      printf("  ERROR: Parse failed at iteration %d\n", i);
      return;
    }
    total_bytes += bench->toon_len;
    TOONc_free(root);
  }

  double elapsed = get_time_ms() - start;
  double ops_per_sec = bench->iterations / (elapsed / 1000.0);
  double mb_per_sec = (total_bytes / (1024.0 * 1024.0)) / (elapsed / 1000.0);

  printf("  %-30s %8zu bytes  %6d iter  %8.2f ms  %10.0f ops/s  %6.2f MB/s\n", bench->name,
         bench->toon_len, bench->iterations, elapsed, ops_per_sec, mb_per_sec);
}

int main(int argc, char **argv) {
  int quick = (argc > 1 && strcmp(argv[1], "--quick") == 0);

  printf("TOON Parser Benchmark\n");
  printf("=====================\n\n");

  benchmark_t benchmarks[] = {
      // Small TOON
      {"tiny object", "a: 1\n", 0, quick ? 10000 : 100000},
      {"small object", "name: \"test\"\nvalue: 42\nactive: true\n", 0, quick ? 10000 : 100000},

      // Arrays (Inline)
      {"array 100 numbers", NULL, 0, quick ? 1000 : 10000},
      {"array 1000 numbers", NULL, 0, quick ? 100 : 1000},
      {"array 10000 numbers", NULL, 0, quick ? 10 : 100},

      // Lists of Objects (Indented)
      {"list 100 objects", NULL, 0, quick ? 1000 : 10000},
      {"list 1000 objects", NULL, 0, quick ? 100 : 1000},

      // Nested
      {"nested depth 10", NULL, 0, quick ? 10000 : 100000},
      {"nested depth 50", NULL, 0, quick ? 10000 : 50000},
      {"nested depth 100", NULL, 0, quick ? 5000 : 20000},

      // String heavy
      {"100 strings x 100 chars", NULL, 0, quick ? 1000 : 10000},
      {"100 strings x 1000 chars", NULL, 0, quick ? 100 : 1000},

      // Real-world: MQTT proxy config
      {"mqtt config small", NULL, 0, quick ? 10000 : 100000},
      {"mqtt config medium", NULL, 0, quick ? 1000 : 10000},
      {"mqtt config large", NULL, 0, quick ? 100 : 1000},
  };

  // Generate dynamic TOON
  benchmarks[2].toon = generate_array_of_numbers(100);
  benchmarks[3].toon = generate_array_of_numbers(1000);
  benchmarks[4].toon = generate_array_of_numbers(10000);
  benchmarks[5].toon = generate_array_of_objects(100);
  benchmarks[6].toon = generate_array_of_objects(1000);
  benchmarks[7].toon = generate_nested_object(10);
  benchmarks[8].toon = generate_nested_object(50);
  benchmarks[9].toon = generate_nested_object(100);
  benchmarks[10].toon = generate_string_heavy(100, 100);
  benchmarks[11].toon = generate_string_heavy(100, 1000);
  benchmarks[12].toon = generate_mqtt_proxy_config(2, 3, 5);
  benchmarks[13].toon = generate_mqtt_proxy_config(10, 20, 50);
  benchmarks[14].toon = generate_mqtt_proxy_config(50, 100, 200);

  // Calculate lengths
  size_t num_benchmarks = sizeof(benchmarks) / sizeof(benchmarks[0]);
  for (size_t i = 0; i < num_benchmarks; i++) {
    if (benchmarks[i].toon) {
      benchmarks[i].toon_len = strlen(benchmarks[i].toon);
    }
  }

    for (int i = 0; i < 5000; i++) {
        toonObject *root = TOONc_parseStringLen("x: 1\n", 5);
        TOONc_free(root);
    }

  printf("\nResults (DOM Mode):\n");
  printf("  %-30s %14s  %10s  %12s  %14s  %10s\n", "Test", "Size", "Iterations", "Time",
         "Throughput", "Bandwidth");
  printf("  %s\n", "-------------------------------------------------------------------------------"
                   "-------------");

  for (size_t i = 0; i < num_benchmarks; i++) {
    run_benchmark(&benchmarks[i]);
  }

  // Cleanup dynamic TOON
  for (size_t i = 2; i < num_benchmarks; i++) {
    free(benchmarks[i].toon);
  }

  printf("\nDone.\n");
  return 0;
}
