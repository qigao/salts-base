#include "tlog.h"
#include <fs.h>
#include "tinytest.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <thread.h>

static int callback_count = 0;
static vstr callback_file = {NULL, 0};
static int callback_line = 0;
static char callback_message[256];
static void *callback_user_data = NULL;
static int custom_write_count = 0;
static int custom_flush_count = 0;
static int custom_destroy_count = 0;
static atomic_int blocking_callback_entered;
static atomic_int blocking_callback_release;

static void test_callback(const cmeta_log_entry_t *entry, void *user_data) {
  (void)user_data;
  callback_count++;
  printf("  [Callback] level=%s msg=%.*s\n", cmeta_log_level_name(entry->level),
         (int)entry->message.len, entry->message.data ? entry->message.data : "");
}

static void source_callback(const cmeta_log_entry_t *entry, void *user_data) {
  (void)user_data;
  callback_file = entry->file;
  callback_line = entry->line;
  callback_count++;
}

static void count_only_callback(const cmeta_log_entry_t *entry, void *user_data) {
  (void)entry;
  (void)user_data;
  callback_count++;
}

static void capture_message_callback(const cmeta_log_entry_t *entry, void *user_data) {
  size_t n = entry->message.len < sizeof(callback_message) - 1
                 ? entry->message.len
                 : sizeof(callback_message) - 1;
  callback_user_data = user_data;
  if (n > 0) memcpy(callback_message, entry->message.data, n);
  callback_message[n] = '\0';
  callback_count++;
}

static int component_predicate(const cmeta_log_entry_t *entry, void *user_data) {
  const char *required = (const char *)user_data;
  size_t required_len;
  if (!required) return 0;
  required_len = strlen(required);
  return entry->component.len == required_len &&
         (required_len == 0 || memcmp(entry->component.data, required, required_len) == 0);
}

static void custom_sink_write_callback(const cmeta_log_entry_t *entry, void *user_data) {
  size_t n = entry->message.len < sizeof(callback_message) - 1
                 ? entry->message.len
                 : sizeof(callback_message) - 1;
  callback_user_data = user_data;
  if (n > 0) memcpy(callback_message, entry->message.data, n);
  callback_message[n] = '\0';
  custom_write_count++;
}

static void custom_sink_flush_callback(void *user_data) {
  callback_user_data = user_data;
  custom_flush_count++;
}

static void custom_sink_destroy_callback(void *user_data) {
  callback_user_data = user_data;
  custom_destroy_count++;
}

static void blocking_callback(const cmeta_log_entry_t *entry, void *user_data) {
  (void)entry;
  (void)user_data;
  atomic_store(&blocking_callback_entered, 1);
  while (!atomic_load(&blocking_callback_release)) {
    cmeta_thread_yield();
  }
}

typedef enum {
  SALTSMQ_ERR_NONE = 0,
  SALTSMQ_ERR_INVALID_ARG = -1,
  SALTSMQ_ERR_NO_MEMORY = -2,
  SALTSMQ_ERR_INVALID_STATE = -3,
  SALTSMQ_ERR_TIMEOUT = -4,
  SALTSMQ_ERR_CONN_FAILED = -5
} saltsmq_error_t;

static const char *saltsmq_strerror_internal(int errnum) {
  switch (errnum) {
  case SALTSMQ_ERR_NONE:
    return "No error";
  case SALTSMQ_ERR_INVALID_ARG:
    return "Invalid argument";
  case SALTSMQ_ERR_NO_MEMORY:
    return "Out of memory";
  case SALTSMQ_ERR_INVALID_STATE:
    return "Invalid socket state";
  case SALTSMQ_ERR_TIMEOUT:
    return "Operation timed out";
  case SALTSMQ_ERR_CONN_FAILED:
    return "Connection failed";
  default:
    return "Unknown error";
  }
}

spec("TLog Tests") {
  it("should create and destroy a logger") {
    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);
    tlog_destroy(logger);
  }

  it("should handle log levels and console sinks") {
    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_INFO};

    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_console_sink_opts_t sink_opts = {.output = stdout,
                                           .use_colors = 0,
                                           .pattern = NULL};
    tlog_add_sink(logger, cmeta_sink_console_create(&sink_opts));

    SALTS_LOG_INFO(logger, "test", "Info message");
    SALTS_LOG_WARN(logger, "test", "Warning message");
    SALTS_LOG_ERROR(logger, "test", "Error message");
    SALTS_LOG_DEBUG(logger, "test", "Debug message (should not appear)");

    tlog_destroy(logger);
  }

  it("should handle multiple sinks") {
    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};

    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    // Add console sink
    cmeta_console_sink_opts_t console_opts = {.output = stdout,
                                              .use_colors = 1,
                                              .pattern = SALTS_LOG_FULL_PATTERN};
    tlog_add_sink(logger, cmeta_sink_console_create(&console_opts));

    // Add file sink
    cmeta_file_sink_opts_t file_opts = {.path = "test_log.txt",
                                        .max_size = 0,
                                        .max_files = 0,
                                        .append = 0};
    cmeta_log_sink_t *file_sink = cmeta_sink_file_create(&file_opts);
    if (file_sink) {
      tlog_add_sink(logger, file_sink);
    }

    SALTS_LOG_INFO(logger, "test", "Multi-sink test message");

    tlog_destroy(logger);
  }

  it("should control log levels dynamically") {
    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    tlog_add_sink(logger, cmeta_sink_console_create(NULL));

    tlog_set_level(logger, SALTS_LOG_LEVEL_WARN);
    check_equal(tlog_get_level(logger), SALTS_LOG_LEVEL_WARN);
    check_equal(tlog_set_level_ex(logger, SALTS_LOG_LEVEL_ERROR), 0);
    check_equal(tlog_get_level(logger), SALTS_LOG_LEVEL_ERROR);
    check_equal(tlog_set_level_ex(logger, (cmeta_log_level_t)-1), -1);
    check_equal(tlog_get_level(logger), SALTS_LOG_LEVEL_ERROR);
    tlog_set_level(logger, (cmeta_log_level_t)(SALTS_LOG_LEVEL_FATAL + 1));
    check_equal(tlog_get_level(logger), SALTS_LOG_LEVEL_ERROR);
    tlog_set_level(logger, SALTS_LOG_LEVEL_WARN);

    SALTS_LOG_INFO(logger, "test", "Info (should not appear)");
    SALTS_LOG_WARN(logger, "test", "Warning (should appear)");

    tlog_destroy(logger);
  }

  it("should reject invalid logger configuration") {
    tlog_config_t config = {.min_level = (cmeta_log_level_t)(SALTS_LOG_LEVEL_FATAL + 1)};
    check_null(tlog_create(&config));
  }

  it("should return correct level names") {
    check_equal(cmeta_log_level_name(SALTS_LOG_LEVEL_DEBUG), "DEBUG");
    check_equal(cmeta_log_level_name(SALTS_LOG_LEVEL_INFO), "INFO");
    check_equal(cmeta_log_level_name(SALTS_LOG_LEVEL_WARN), "WARN");
    check_equal(cmeta_log_level_name(SALTS_LOG_LEVEL_ERROR), "ERROR");
    check_equal(cmeta_log_level_name(SALTS_LOG_LEVEL_FATAL), "FATAL");

    check_equal(cmeta_log_level_from_name("DEBUG"), SALTS_LOG_LEVEL_DEBUG);
    check_equal(cmeta_log_level_from_name("INFO"), SALTS_LOG_LEVEL_INFO);
    check_equal(cmeta_log_level_from_name("WARN"), SALTS_LOG_LEVEL_WARN);
    check_equal(cmeta_log_level_from_name("ERROR"), SALTS_LOG_LEVEL_ERROR);
    check_equal(cmeta_log_level_from_name("FATAL"), SALTS_LOG_LEVEL_FATAL);
  }

  it("should expose log level metadata without changing legacy parsing") {
    const tlog_enum_desc *meta = cmeta_log_level_t_meta();

    check_equal((int)meta->count, 5);
    check_equal(SALTS_LOG_LEVEL_DEBUG, 0);
    check_equal(SALTS_LOG_LEVEL_FATAL, 4);
    check_equal(cmeta_log_level_t_to_string(SALTS_LOG_LEVEL_ERROR), "ERROR");
    check_equal(cmeta_log_level_name((cmeta_log_level_t)99), "UNKNOWN");
    check_equal(cmeta_log_level_from_name("SALTS_LOG_LEVEL_ERROR"), SALTS_LOG_LEVEL_INFO);
    check_equal(cmeta_log_level_from_name(NULL), SALTS_LOG_LEVEL_INFO);
  }

  it("should expose the stable log entry layout as read-only metadata") {
    const tlog_struct_desc *meta = cmeta_log_entry_t_meta();
    const char *names[] = {
        "level", "timestamp_ms", "thread_id", "component",
        "file", "line", "message"};
    const char *types[] = {
        "cmeta_log_level_t", "uint64_t", "uint32_t", "vstr",
        "vstr", "int", "vstr"};
    const size_t offsets[] = {
        offsetof(cmeta_log_entry_t, level),
        offsetof(cmeta_log_entry_t, timestamp_ms),
        offsetof(cmeta_log_entry_t, thread_id),
        offsetof(cmeta_log_entry_t, component),
        offsetof(cmeta_log_entry_t, file),
        offsetof(cmeta_log_entry_t, line),
        offsetof(cmeta_log_entry_t, message)};
    const size_t sizes[] = {
        sizeof(cmeta_log_level_t), sizeof(uint64_t), sizeof(uint32_t),
        sizeof(vstr), sizeof(vstr), sizeof(int), sizeof(vstr)};
    const size_t aligns[] = {
        _Alignof(cmeta_log_level_t), _Alignof(uint64_t),
        _Alignof(uint32_t), _Alignof(vstr),
        _Alignof(vstr), _Alignof(int), _Alignof(vstr)};
    const tlog_field_desc *component_field;
    const tlog_field_desc *message_field;

    check_not_null(meta);
    check_equal(meta->name, "cmeta_log_entry_t");
    check_equal(meta->size, sizeof(cmeta_log_entry_t));
    check_equal(meta->align, _Alignof(cmeta_log_entry_t));
    check_equal(meta->field_count, (size_t)7);
    for (size_t i = 0; i < meta->field_count; ++i) {
      check_equal(meta->fields[i].name, names[i]);
      check_equal(meta->fields[i].type_name, types[i]);
      check_equal(meta->fields[i].offset, offsets[i]);
      check_equal(meta->fields[i].size, sizes[i]);
      check_equal(meta->fields[i].align, aligns[i]);
    }

    component_field = tlog_struct_find_field(meta, "component");
    message_field = tlog_struct_find_field(meta, "message");
    check_not_null(component_field);
    check_not_null(message_field);
    if (component_field != NULL)
      check_equal(component_field->type_name, "vstr");
    if (message_field != NULL)
      check_equal(message_field->size, sizeof(vstr));
    check_null(tlog_struct_find_field(meta, "message_len"));
    check_null(tlog_struct_find_field(meta, "missing"));
  }

  it("should handle logging from different components") {
    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    tlog_add_sink(logger, cmeta_sink_console_create(NULL));

    SALTS_LOG_INFO(logger, "server", "Server message");
    SALTS_LOG_INFO(logger, "client", "Client message");
    SALTS_LOG_INFO(logger, "protocol", "Protocol message");

    tlog_destroy(logger);
  }

  it("should work with the simplified API") {
    // Test auto-creation of default logger
    tlog_t *default_logger = tlog_get_default();
    check_not_null(default_logger);

    // Test simplified macros with printf-style formatting
    const char *url = "http://example.com/invalid";
    int error_code = 404;

    TLOG_INFO("Starting simplified API test");
    TLOG_DEBUGF("Debug message: value={:d}", 42);
    TLOG_WARNF("Warning: {:s} returned code {:d}", url, error_code);
    TLOG_ERRORF("Invalid URL format: {:s}", url);

    // Test with custom logger set as default
    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};

    tlog_t *custom_logger = tlog_create(&config);
    cmeta_console_sink_opts_t sink_opts = {.output = stdout,
                                           .use_colors = 1,
                                           .pattern = SALTS_LOG_FULL_PATTERN};
    tlog_add_sink(custom_logger, cmeta_sink_console_create(&sink_opts));
    tlog_set_default(custom_logger);

    TLOG_INFO("Custom logger with file:line info");
    TLOG_ERRORF("Error with custom config: {:s}", "test error");

    // Reset to auto-created logger
    tlog_set_default(default_logger);
    (void)default_logger;
    TLOG_INFO("Back to auto-created logger");

    tlog_destroy(custom_logger);
  }

  it("should recreate default logger after default destruction") {
    tlog_t *default_logger = tlog_get_default();
    check_not_null(default_logger);
    tlog_destroy(default_logger);

    tlog_t *recreated = tlog_get_default();
    check_not_null(recreated);
    TLOG_INFO("Default logger recreated after destruction");
    tlog_destroy(recreated);
  }

  it("should report async queue size while a sink is blocked") {
    atomic_store(&blocking_callback_entered, 0);
    atomic_store(&blocking_callback_release, 0);

    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    cmeta_log_sink_t *cb_sink = cmeta_sink_callback_create(blocking_callback, NULL);
    check_not_null(cb_sink);
    tlog_add_sink(logger, cb_sink);

    SALTS_LOG_INFO(logger, "queue", "blocked queue-size sample");
    while (!atomic_load(&blocking_callback_entered)) {
      cmeta_thread_yield();
    }

    check(tlog_get_queue_size(logger) > 0);
    atomic_store(&blocking_callback_release, 1);
    tlog_flush(logger);
    check_equal(tlog_get_queue_size(logger), 0);

    tlog_destroy(logger);
  }

  it("should handle callback sinks") {
    callback_count = 0;

    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    cmeta_log_sink_t *cb_sink = cmeta_sink_callback_create(test_callback, NULL);
    check_not_null(cb_sink);
    tlog_add_sink(logger, cb_sink);

    SALTS_LOG_INFO(logger, "test", "First callback message");
    SALTS_LOG_WARN(logger, "test", "Second callback message");
    SALTS_LOG_ERROR(logger, "test", "Third callback message");

    tlog_flush(logger); // Wait for async queue to drain before checking count
    check_equal(callback_count, 3);

    tlog_destroy(logger);
  }

  it("should decorate a sink with metrics") {
    callback_count = 0;

    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};
    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_log_sink_t *inner = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(inner);
    cmeta_log_sink_t *metrics = cmeta_sink_metrics_create(inner, SALTS_SINK_OWNED);
    check_not_null(metrics);
    tlog_add_sink(logger, metrics);

    SALTS_LOG_INFO(logger, "decorator", "decorated message one");
    SALTS_LOG_WARN(logger, "decorator", "decorated message two");
    tlog_flush(logger);

    cmeta_sink_metrics_t stats = {0};
    check_equal(cmeta_sink_metrics_snapshot(metrics, &stats), 0);
    check_equal(callback_count, 2);
    check_equal((size_t)stats.entries_seen, 2);
    check_equal((size_t)stats.entries_forwarded, 2);
    check_equal((size_t)stats.entries_filtered, 0);
    check(stats.bytes_forwarded > 0);

    tlog_destroy(logger);
  }

  it("should reject metrics snapshots for non-metrics sinks") {
    cmeta_log_sink_t *sink = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(sink);

    cmeta_sink_metrics_t stats = {0};
    check_equal(cmeta_sink_metrics_snapshot(sink, &stats), -1);
    check_equal(cmeta_sink_metrics_snapshot(NULL, &stats), -1);
    check_equal(cmeta_sink_metrics_snapshot(sink, NULL), -1);

    cmeta_sink_destroy(sink);
  }

  it("should let a metrics decorator filter before forwarding") {
    callback_count = 0;

    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};
    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_log_sink_t *inner = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(inner);
    cmeta_log_sink_t *metrics = cmeta_sink_metrics_create(inner, SALTS_SINK_OWNED);
    check_not_null(metrics);
    check_equal(cmeta_sink_set_min_level(metrics, SALTS_LOG_LEVEL_WARN), 0);
    check_equal(cmeta_sink_get_min_level(metrics), SALTS_LOG_LEVEL_WARN);
    tlog_add_sink(logger, metrics);

    SALTS_LOG_DEBUG(logger, "decorator", "filtered debug message");
    SALTS_LOG_ERROR(logger, "decorator", "forwarded error message");
    tlog_flush(logger);

    cmeta_sink_metrics_t stats = {0};
    check_equal(cmeta_sink_metrics_snapshot(metrics, &stats), 0);
    check_equal(callback_count, 1);
    check_equal((size_t)stats.entries_seen, 2);
    check_equal((size_t)stats.entries_forwarded, 1);
    check_equal((size_t)stats.entries_filtered, 1);

    tlog_destroy(logger);
  }

  it("should expose sink attributes through accessors") {
    int marker = 7;
    cmeta_log_sink_t *sink = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(sink);

    check_equal(cmeta_sink_set_min_level(sink, SALTS_LOG_LEVEL_ERROR), 0);
    check_equal(cmeta_sink_get_min_level(sink), SALTS_LOG_LEVEL_ERROR);
    check_equal(cmeta_sink_set_min_level(sink, (cmeta_log_level_t)-1), -1);
    check_equal(cmeta_sink_set_min_level(sink, (cmeta_log_level_t)(SALTS_LOG_LEVEL_FATAL + 1)), -1);
    check_equal(cmeta_sink_set_user_data(sink, &marker), 0);
    check(cmeta_sink_get_user_data(sink) == &marker);

    cmeta_sink_destroy(sink);
  }

  it("should keep ownership with caller when sink attach or decorator creation fails") {
    custom_destroy_count = 0;

    cmeta_sink_custom_opts_t custom_opts = {
        .write = custom_sink_write_callback,
        .flush = NULL,
        .destroy = custom_sink_destroy_callback,
        .user_data = NULL
    };
    cmeta_log_sink_t *inner = cmeta_sink_custom_create(&custom_opts);
    check_not_null(inner);

    cmeta_sink_filter_opts_t filter_opts = SALTS_SINK_FILTER_OPTS_DEFAULT;
    filter_opts.min_level = SALTS_LOG_LEVEL_ERROR;
    filter_opts.max_level = SALTS_LOG_LEVEL_INFO;
    check_null(cmeta_sink_filter_create(inner, SALTS_SINK_OWNED, &filter_opts));
    check_equal(custom_destroy_count, 0);

    check_equal(tlog_add_sink(NULL, inner), -1);
    check_equal(custom_destroy_count, 0);
    cmeta_sink_destroy(inner);
    check_equal(custom_destroy_count, 1);
  }

  it("should decorate a sink with a filter") {
    callback_count = 0;

    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};
    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_log_sink_t *inner = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(inner);
    cmeta_sink_filter_opts_t opts = SALTS_SINK_FILTER_OPTS_DEFAULT;
    opts.min_level = SALTS_LOG_LEVEL_INFO;
    opts.max_level = SALTS_LOG_LEVEL_ERROR;
    opts.predicate = component_predicate;
    opts.predicate_user_data = "allowed";
    cmeta_log_sink_t *filter = cmeta_sink_filter_create(inner, SALTS_SINK_OWNED, &opts);
    check_not_null(filter);
    tlog_add_sink(logger, filter);

    SALTS_LOG_DEBUG(logger, "allowed", "filtered by level");
    SALTS_LOG_INFO(logger, "blocked", "filtered by predicate");
    SALTS_LOG_WARN(logger, "allowed", "forwarded warning");
    SALTS_LOG_FATAL(logger, "allowed", "filtered by max level");
    tlog_flush(logger);

    check_equal(callback_count, 1);

    tlog_destroy(logger);
  }

  it("should decorate a sink with formatting") {
    callback_count = 0;
    callback_message[0] = '\0';
    callback_user_data = NULL;

    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    int marker = 11;
    cmeta_log_sink_t *inner = cmeta_sink_callback_create(capture_message_callback, &marker);
    check_not_null(inner);
    cmeta_log_sink_t *format =
        cmeta_sink_format_create(inner, SALTS_SINK_OWNED, "[{level}] {component}: {message}");
    check_not_null(format);
    tlog_add_sink(logger, format);

    SALTS_LOG_INFO(logger, "fmt", "hello");
    tlog_flush(logger);

    check_equal(callback_count, 1);
    check(callback_user_data == &marker);
    check_equal(callback_message, "[INFO] fmt: hello");

    tlog_destroy(logger);
  }

  it("should fail fast on oversized format patterns") {
    cmeta_console_sink_opts_t console_opts = {
        .output = stdout,
        .use_colors = 0,
        .pattern = "{message}{message}{message}{message}{message}{message}{message}{message}"
                   "{message}{message}{message}{message}{message}{message}{message}{message}"
                   "{message}"
    };
    check_null(cmeta_sink_console_create(&console_opts));

    cmeta_log_sink_t *inner = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(inner);
    cmeta_log_sink_t *format =
        cmeta_sink_format_create(inner, SALTS_SINK_OWNED, console_opts.pattern);
    check_null(format);
    cmeta_sink_destroy(inner);
  }

  it("should support opaque custom sinks") {
    custom_write_count = 0;
    custom_flush_count = 0;
    custom_destroy_count = 0;
    callback_message[0] = '\0';
    callback_user_data = NULL;

    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    int marker = 17;
    cmeta_sink_custom_opts_t opts = {
        .write = custom_sink_write_callback,
        .flush = custom_sink_flush_callback,
        .destroy = custom_sink_destroy_callback,
        .user_data = &marker
    };
    cmeta_log_sink_t *sink = cmeta_sink_custom_create(&opts);
    check_not_null(sink);
    tlog_add_sink(logger, sink);

    SALTS_LOG_INFO(logger, "custom", "custom sink message");
    tlog_flush(logger);
    check_equal(custom_write_count, 1);
    check_equal(custom_flush_count, 1);
    check(callback_user_data == &marker);
    check_equal(callback_message, "custom sink message");

    tlog_destroy(logger);
    check_equal(custom_destroy_count, 1);
    check(callback_user_data == &marker);
  }

  it("should capture source only for debug builds by default") {
    callback_count = 0;
    callback_file = vstr_from_buf(NULL, 0);
    callback_line = 0;

    tlog_t *logger = tlog_create(NULL);
    check_not_null(logger);

    cmeta_log_sink_t *cb_sink = cmeta_sink_callback_create(source_callback, NULL);
    check_not_null(cb_sink);
    tlog_add_sink(logger, cb_sink);

    SALTS_LOG_INFO(logger, "source", "source capture test");
    tlog_flush(logger);

    check_equal(callback_count, 1);
#if SALTS_LOG_CAPTURE_SOURCE
    check(callback_file.len > 0);
    check(callback_file.data != NULL);
    check(callback_line > 0);
    check(strstr(SALTS_LOG_FULL_PATTERN, "{file}") != NULL);
    check(strstr(SALTS_LOG_FULL_PATTERN, "{line}") != NULL);
#else
    check_equal(callback_file.len, (size_t)0);
    check_equal(callback_line, 0);
    check(strstr(SALTS_LOG_FULL_PATTERN, "{file}") == NULL);
    check(strstr(SALTS_LOG_FULL_PATTERN, "{line}") == NULL);
#endif

    tlog_destroy(logger);
  }

  it("should rotate file when written bytes exceed max_size") {
    char base_path[256];
    char rotated_path[300];
    char* sep = "/";
#ifdef _WIN32
    sep = "\\";
#endif
    salts_fs_get_tmpdir(base_path, sizeof(base_path) - 48);
    strcat(base_path, sep);
    strcat(base_path, "test_tlog_rotate.log");
    snprintf(rotated_path, sizeof(rotated_path), "%s.1", base_path);

    salts_fs_unlink(base_path);
    salts_fs_unlink(rotated_path);

    tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_DEBUG};
    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_file_sink_opts_t file_opts = {
        .path = base_path,
        .max_size = 128,
        .max_files = 1,
        .append = 0,
        .pattern = "{message}"
    };
    cmeta_log_sink_t *file_sink = cmeta_sink_file_create(&file_opts);
    check_not_null(file_sink);
    tlog_add_sink(logger, file_sink);

    for (int i = 0; i < 64; i++) {
      SALTS_LOG_INFOF(logger, "rotate", "rotate-msg-{:04d}-abcdefghijklmnopqrstuvwxyz", i);
    }

    tlog_flush(logger);
    tlog_destroy(logger);

    salts_fs_stat_t st_base = {0};
    salts_fs_stat_t st_rot = {0};
    check_equal(salts_fs_stat(base_path, &st_base), 0);
    check_equal(salts_fs_stat(rotated_path, &st_rot), 0);
    check(st_base.is_file);
    check(st_rot.is_file);
    check_greater((size_t)st_rot.size, 0);

    salts_fs_unlink(base_path);
    salts_fs_unlink(rotated_path);
  }

  it("should sustain high-volume async logging without pool exhaustion") {
    callback_count = 0;

    tlog_config_t config = {
        .min_level = SALTS_LOG_LEVEL_DEBUG,
        .buffer_size = 8 * 1024,
        .pool_size = 8 * 1024
    };
    tlog_t *logger = tlog_create(&config);
    check_not_null(logger);

    cmeta_log_sink_t *cb_sink = cmeta_sink_callback_create(count_only_callback, NULL);
    check_not_null(cb_sink);
    tlog_add_sink(logger, cb_sink);

    const int total_logs = 5000;
    for (int i = 0; i < total_logs; i++) {
      SALTS_LOG_INFOF(logger, "stress", "high-volume message {}", i);
    }

    tlog_flush(logger);
    check_equal(callback_count, total_logs);
    check_equal((int)tlog_get_dropped(logger), 0);

    tlog_destroy(logger);
  }

  it("should perform type-safe logging") {
    // These should work with auto-detection {} or typed placeholders
    TLOG_INFOF("Auto-detected string: {}", "Hello World");
    TLOG_INFOF("Auto-detected int: {}", 42);
    TLOG_INFOF("Auto-detected double: {}", 3.14159);
    TLOG_INFOF("Auto-detected bool: {}", true);

    // Mixed usage
    TLOG_INFOF("Mixed: string={}, int={}, ptr={}", "test", 123,
               (void *)(uintptr_t)0xdeadbeef);

    // Explicit specifiers with auto-detected types
    TLOG_INFOF("Hex int: {:04x}", 255);
    TLOG_INFOF("Padded double: {:08.2f}", 12.3456);
  }

 
  describe("SaltsMQ simulation") {

    it("should log SaltsMQ simulated errors") {
      // Set level to DEBUG so we can see the output
      tlog_set_level(tlog_get_default(), SALTS_LOG_LEVEL_DEBUG);

      saltsmq_error_t err = SALTSMQ_ERR_CONN_FAILED;
      TLOG_DEBUGF("Error set: {:s} ({:d})", saltsmq_strerror_internal(err),
                  (int)err);

      // Restoration
      tlog_set_level(tlog_get_default(), SALTS_LOG_LEVEL_INFO);
    }
  }

 }
