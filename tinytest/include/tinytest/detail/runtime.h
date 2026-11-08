#ifndef TINYTEST_DETAIL_RUNTIME_H
#define TINYTEST_DETAIL_RUNTIME_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef TTEST_TLS
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#define TTEST_TLS _Thread_local
#elif defined(_MSC_VER)
#define TTEST_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define TTEST_TLS __thread
#else
#define TTEST_TLS
#endif
#endif

typedef struct ttest_config_type__ ttest_config_type__;
typedef void (*ttest_spec_fn__)(ttest_config_type__ *ttest_config__);

typedef enum ttest_error_code__ {
  TTEST_ERR_OK__ = 0,
  TTEST_ERR_IO__ = -1,
  TTEST_ERR_TIME__ = -2,
  TTEST_ERR_FORMAT__ = -3
} ttest_error_code__;

typedef struct ttest_result__ {
  bool ok;
  ttest_error_code__ error;
  const char *message;
} ttest_result__;

typedef struct ttest_spec_entry__ {
  const char *name;
  ttest_spec_fn__ fn;
  struct ttest_spec_entry__ *next;
} ttest_spec_entry__;

typedef struct ttest_array__ {
  void **values;
  size_t capacity;
  size_t size;
} ttest_array__;

typedef enum ttest_node_type__ {
  TTEST_NODE_GROUP__ = 1,
  TTEST_NODE_TEST__ = 2,
  TTEST_NODE_INTERIM__ = 3
} ttest_node_type__;

enum {
  TTEST_RESULT_CATEGORY_NONE__ = 0,
  TTEST_RESULT_CATEGORY_PASS__,
  TTEST_RESULT_CATEGORY_FAIL__,
  TTEST_RESULT_CATEGORY_SKIP__,
  TTEST_RESULT_CATEGORY_FILTER__,
  TTEST_RESULT_CATEGORY_TODO__
};

/* Single source for enum values, display names, and result classification. */
#define TTEST_TEST_RESULT_X__                                                                     \
  X(PENDING, 0, "pending", TTEST_RESULT_CATEGORY_NONE__)                                          \
  X(PASSED, 1, "passed", TTEST_RESULT_CATEGORY_PASS__)                                            \
  X(FAILED, 2, "failed", TTEST_RESULT_CATEGORY_FAIL__)                                            \
  X(SKIPPED, 3, "skipped", TTEST_RESULT_CATEGORY_SKIP__)                                         \
  X(EXPECTED_FAIL, 4, "expected_fail", TTEST_RESULT_CATEGORY_PASS__)                              \
  X(UNEXPECTED_PASS, 5, "unexpected_pass", TTEST_RESULT_CATEGORY_TODO__)                          \
  X(FILTERED, 6, "filtered", TTEST_RESULT_CATEGORY_FILTER__)

typedef enum ttest_test_result__ {
#define X(name, value, label, category) TTEST_RESULT_##name##__ = value,
  TTEST_TEST_RESULT_X__
#undef X
} ttest_test_result__;

typedef enum ttest_node_flags__ {
  ttest_node_flags_none__ = 0,
  ttest_node_flags_focus__ = 1 << 0,
  ttest_node_flags_skip__ = 1 << 1,
  ttest_node_flags_expected_fail__ = 1 << 2,
  ttest_node_flags_benchmark__ = 1 << 3,
} ttest_node_flags__;

typedef struct ttest_test_step__ {
  size_t level;
  int id;
  char *name;
  ttest_node_type__ type;
  ttest_node_flags__ flags;
  bool executed;
  bool passed;
  ttest_test_result__ result;
  const char *skip_reason;
  char *failure_message;
  char *failure_location;
  double execution_time_ms;
  char *full_path;
  ttest_array__ *before_each_nodes;
  ttest_array__ *after_each_nodes;
} ttest_test_step__;

typedef struct ttest_bench_entry__ {
  const char *title;
  size_t samples;
  size_t operations_per_sample;
  size_t bytes_per_sample;
  bool tracks_bytes;
  double avg_op_us;
  double avg_sample_us;
  double min_sample_us;
  double max_sample_us;
  double ops_s;
  double mib_s;
} ttest_bench_entry__;

typedef struct ttest_node__ {
  int id;
  int next_node_id;
  char *name;
  ttest_node_flags__ flags;
  ttest_node_type__ type;
  ttest_array__ *list_before;
  ttest_array__ *list_after;
  ttest_array__ *list_before_each;
  ttest_array__ *list_after_each;
  ttest_array__ *list_children;
} ttest_node__;

enum ttest_run_type__ { TTEST_INIT_RUN__ = 1, TTEST_TEST_RUN__ = 2 };

typedef void (*ttest_invoke_spec_adapter__)(ttest_config_type__ *config, ttest_spec_fn__ fn);

struct ttest_config_type__ {
  enum ttest_run_type__ run;
  int id;
  size_t test_index;
  size_t test_tap_index;
  int target_node_id;
  size_t failed_test_count;
  ttest_test_step__ *current_test;
  ttest_array__ *node_stack;
  ttest_array__ *nodes;
  char *error;
  char *location;
  bool use_color;
  bool use_tap;
  bool has_focus_nodes;
  const char *junit_file;
  size_t warn_count;
  size_t assertion_count;
  size_t assertion_failed_count;
  char info_buffer[2048];
  size_t info_len;
  char location_buf[512];
  jmp_buf jump_buffer;
  bool skip_subsequent;
  bool list_only;
  const char *filter;
  int bench_header_printed;
  size_t bench_header_level;
  bool print_trace;
  ttest_spec_fn__ current_spec;
  ttest_invoke_spec_adapter__ invoke_spec;
};

extern ttest_spec_entry__ *ttest_specs__;
extern size_t ttest_spec_count__;
extern TTEST_TLS ttest_config_type__ *ttest_active_config__;

double ttest_get_time_ms__(void);
ttest_result__ ttest_result_ok__(void);
ttest_result__ ttest_result_error__(ttest_error_code__ error, const char *message);
void ttest_register_spec__(const char *name, ttest_spec_fn__ fn);
ttest_spec_entry__ *ttest_get_spec_entry__(size_t index);
void ttest_cleanup_specs__(void);
void ttest_longjmp_fail_c__(ttest_config_type__ *config);
void ttest_record_unhandled_exception__(ttest_config_type__ *config, const char *message);
int ttest_main__(int argc, char **argv, ttest_invoke_spec_adapter__ invoke_spec,
                 bool default_use_color, bool default_use_tap, bool stdout_is_atty,
                 bool print_trace);
bool ttest_enter_node__(ttest_node_flags__ node_flags, ttest_config_type__ *config,
                        ttest_node_type__ type, ptrdiff_t list_offset, bool format_name,
                        const char *format, ...);
void ttest_exit_node__(ttest_config_type__ *config);
void ttest_indent__(FILE *fp, size_t level);
void ttest_bench_print__(ttest_config_type__ *config, const char *title, size_t samples,
                         double sum_ms, double min_ms, double max_ms,
                         size_t operations_per_sample, size_t bytes_per_sample,
                         bool tracks_bytes, size_t level, bool use_color,
                         int name_width, bool table);
ttest_bench_entry__ ttest_bench_make_entry__(
    const char *title, size_t samples, double sum_ms, double min_ms, double max_ms,
    size_t operations_per_sample, size_t bytes_per_sample, bool tracks_bytes);
bool ttest_bench_require_work__(ttest_config_type__ *config, const char *title, size_t samples,
                                size_t operations_per_sample, size_t bytes_per_sample,
                                bool tracks_bytes, const char *file, const char *line);
int ttest_eval_bool__(int value);

char *tt_temp_dir(void);
char *tt_read_file(const char *path, size_t *out_size);
int tt_write_file(const char *path, const void *data, size_t size);
int tt_remove_file(const char *path);
int tt_make_dir(const char *path);
int tt_is_dir(const char *path);
char *tt_make_temp_file(const char *prefix, const char *suffix);
char *tt_make_temp_dir(const char *prefix);
int tt_remove_tree(const char *path);

int ttest_str_eq__(const char *a, const char *b);
int ttest_str_ne__(const char *a, const char *b);
int ttest_str_contains__(const char *haystack, const char *needle);
int ttest_str_starts_with__(const char *s, const char *prefix);
int ttest_str_ends_with__(const char *s, const char *suffix);
const char *ttest_cstr_or_null__(const char *s);

ttest_array__ *ttest_array_create__(void);
void *ttest_array_push__(ttest_array__ *arr, void *item);
void *ttest_array_last__(ttest_array__ *arr);
void *ttest_array_pop__(ttest_array__ *arr);
void ttest_array_free__(ttest_array__ *arr);
ttest_array__ *ttest_array_get_or_create__(ttest_array__ **arr_ptr);
size_t ttest_array_size__(ttest_array__ *arr);

const char *ttest_test_result_name__(ttest_test_result__ result);
int ttest_test_result_category__(ttest_test_result__ result);
bool ttest_test_result_is_skip__(ttest_test_result__ result);
bool ttest_test_result_is_fail__(ttest_test_result__ result);

char *ttest_vformat__(const char *format, va_list args);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
char *ttest_format__(const char *format, ...);

#endif /* TINYTEST_DETAIL_RUNTIME_H */
