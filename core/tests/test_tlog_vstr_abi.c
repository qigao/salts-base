#include "tlog.h"
#include "tinytest.h"
#include <stddef.h>
#include <string.h>

static char captured_component[32];
static size_t captured_component_len;
static char captured_file[32];
static size_t captured_file_len;
static char captured_message[64];
static size_t captured_message_len;

static void capture_view(char *dst, size_t dst_size, size_t *dst_len, vstr value) {
  size_t n = value.len < dst_size ? value.len : dst_size;
  if (n > 0) memcpy(dst, value.data, n);
  *dst_len = n;
}

static void capture_views(const cmeta_log_entry_t *entry, void *user_data) {
  (void)user_data;
  capture_view(captured_component, sizeof(captured_component), &captured_component_len,
               entry->component);
  capture_view(captured_file, sizeof(captured_file), &captured_file_len, entry->file);
  capture_view(captured_message, sizeof(captured_message), &captured_message_len,
               entry->message);
}

spec("TLog vstr ABI contract") {
  it("exposes seven-field view-native reflection") {
    const tlog_struct_desc *meta = cmeta_log_entry_t_meta();
    const char *names[] = {"level", "timestamp_ms", "thread_id", "component",
                           "file", "line", "message"};
    const char *types[] = {"cmeta_log_level_t", "uint64_t", "uint32_t", "vstr",
                           "vstr", "int", "vstr"};

    check_not_null(meta);
    check_equal(meta->field_count, (size_t)7);
    for (size_t i = 0; i < 7; ++i) {
      check_equal(meta->fields[i].name, names[i]);
      check_equal(meta->fields[i].type_name, types[i]);
    }
    check_null(tlog_struct_find_field(meta, "message_len"));
  }

  it("logs non-NUL-terminated raw views by exact length") {
    const char component_raw[] = {'c', 'o', 'm', 'p'};
    const char file_raw[] = {'f', '.', 'c'};
    const char message_raw[] = {'h', 'e', 'l', 'l', 'o'};
    tlog_t *logger = tlog_create(NULL);
    cmeta_log_sink_t *sink = cmeta_sink_callback_create(capture_views, NULL);

    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);

    cmeta_log_str(logger, SALTS_LOG_LEVEL_INFO,
                  vstr_from_buf(component_raw, sizeof(component_raw)),
                  vstr_from_buf(file_raw, sizeof(file_raw)), 17,
                  vstr_from_buf(message_raw, sizeof(message_raw)));
    tlog_flush(logger);

    check_equal(captured_component_len, sizeof(component_raw));
    check_equal(captured_file_len, sizeof(file_raw));
    check_equal(captured_message_len, sizeof(message_raw));
    check(memcmp(captured_component, component_raw, sizeof(component_raw)) == 0);
    check(memcmp(captured_file, file_raw, sizeof(file_raw)) == 0);
    check(memcmp(captured_message, message_raw, sizeof(message_raw)) == 0);

    tlog_destroy(logger);
  }

  it("copies borrowed views before async return") {
    char component_raw[] = {'o', 'l', 'd'};
    char file_raw[] = {'a', '.', 'c'};
    char message_raw[] = {'b', 'e', 'f', 'o', 'r', 'e'};
    const char expected_component[] = {'o', 'l', 'd'};
    const char expected_file[] = {'a', '.', 'c'};
    const char expected_message[] = {'b', 'e', 'f', 'o', 'r', 'e'};
    tlog_t *logger = tlog_create(NULL);
    cmeta_log_sink_t *sink = cmeta_sink_callback_create(capture_views, NULL);

    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);

    cmeta_log_str(logger, SALTS_LOG_LEVEL_INFO,
                  vstr_from_buf(component_raw, sizeof(component_raw)),
                  vstr_from_buf(file_raw, sizeof(file_raw)), 3,
                  vstr_from_buf(message_raw, sizeof(message_raw)));
    memset(component_raw, 'x', sizeof(component_raw));
    memset(file_raw, 'y', sizeof(file_raw));
    memset(message_raw, 'z', sizeof(message_raw));
    tlog_flush(logger);

    check(memcmp(captured_component, expected_component, sizeof(expected_component)) == 0);
    check(memcmp(captured_file, expected_file, sizeof(expected_file)) == 0);
    check(memcmp(captured_message, expected_message, sizeof(expected_message)) == 0);

    tlog_destroy(logger);
  }

  it("accepts a bounded typed format pattern") {
    const char pattern_raw[] = {'[', '{', '}', ']'};
    fmt_arg_t arg = fmt_arg_int(42);
    tlog_t *logger = tlog_create(NULL);
    cmeta_log_sink_t *sink = cmeta_sink_callback_create(capture_views, NULL);

    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);

    cmeta_log_typed(logger, SALTS_LOG_LEVEL_INFO,
                    vstr_from_buf("typed", 5), (vstr){NULL, 0}, 0,
                    vstr_from_buf(pattern_raw, sizeof(pattern_raw)), &arg, 1U);
    tlog_flush(logger);

    check_equal(captured_message_len, (size_t)4);
    check(memcmp(captured_message, "[42]", 4) == 0);

    tlog_destroy(logger);
  }
}
