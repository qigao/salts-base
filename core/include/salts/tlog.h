/**
 * @file tlog.h
 * @brief High-performance async logger with multi-sink support
 *
 * Architecture:
 * - Multiple sinks (console, file, custom callback)
 * - Async mode with lock-free ring buffer
 * - Memory pool for zero-alloc hot path
 * - Simple vtable-based sink interface
 */

#ifndef tlog_h
#define tlog_h

#include <salts/api.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <salts/fmt.h>
#include <salts/tstr.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MESSAGE_SIZE 4096

// =============================================================================
// Log Levels
// =============================================================================

/* Original level and seven-field log layout; native immutable metadata.
 * The names retain their original C ABI but require no CMeta runtime. */
typedef enum {
  SALTS_LOG_LEVEL_DEBUG=0, SALTS_LOG_LEVEL_INFO=1,
  SALTS_LOG_LEVEL_WARN=2, SALTS_LOG_LEVEL_ERROR=3, SALTS_LOG_LEVEL_FATAL=4
} log_level_t;
typedef struct { int64_t value; const char *symbol; const char *text; } tlog_enum_item_desc;
typedef struct {
  const char *name; const tlog_enum_item_desc *items; size_t count;
} tlog_enum_desc;
static inline const tlog_enum_desc *log_level_t_meta(void) {
  static const tlog_enum_item_desc values[] = {
    {0, "SALTS_LOG_LEVEL_DEBUG", "DEBUG"},
    {1, "SALTS_LOG_LEVEL_INFO", "INFO"},
    {2, "SALTS_LOG_LEVEL_WARN", "WARN"},
    {3, "SALTS_LOG_LEVEL_ERROR", "ERROR"},
    {4, "SALTS_LOG_LEVEL_FATAL", "FATAL"}
  };
  static const tlog_enum_desc result = {
    "log_level_t", values, sizeof(values)/sizeof(values[0])
  };
  return &result;
}
static inline const char *log_level_t_to_string(log_level_t level) {
  const tlog_enum_desc *m=log_level_t_meta();
  for (size_t i=0; i<m->count; ++i)
    if (m->items[i].value==(int64_t)level) return m->items[i].text;
  return NULL;
}
static inline const char *log_level_t_to_symbol(log_level_t level) {
  const tlog_enum_desc *m=log_level_t_meta();
  for (size_t i=0; i<m->count; ++i)
    if (m->items[i].value==(int64_t)level) return m->items[i].symbol;
  return NULL;
}
static inline bool log_level_t_from_string(const char *text, log_level_t *out) {
  const tlog_enum_desc *m=log_level_t_meta();
  if (!text || !out) return false;
  for (size_t i=0; i<m->count; ++i)
    if (!strcmp(text,m->items[i].symbol) || !strcmp(text,m->items[i].text)) {
      *out = (log_level_t)m->items[i].value; return true;
    }
  return false;
}
typedef struct {
  log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  vstr component;
  vstr file;
  int line;
  vstr message;
} log_entry_t;
typedef struct {
  const char *name, *type_name; size_t offset, size, align;
} tlog_field_desc;
typedef struct {
  const char *name; size_t size, align;
  const tlog_field_desc *fields; size_t field_count;
} tlog_struct_desc;
#ifdef __cplusplus
#define TLOG_ALIGN(type) alignof(type)
#else
#define TLOG_ALIGN(type) _Alignof(type)
#endif
#define TLOG_FIELD(member,type,name) {#member,name,offsetof(log_entry_t,member),sizeof(type),TLOG_ALIGN(type)}
static inline const tlog_struct_desc *log_entry_t_meta(void) {
  static const tlog_field_desc fields[] = {
    TLOG_FIELD(level,log_level_t,"log_level_t"),
    TLOG_FIELD(timestamp_ms,uint64_t,"uint64_t"),
    TLOG_FIELD(thread_id,uint32_t,"uint32_t"),
    TLOG_FIELD(component,vstr,"vstr"),
    TLOG_FIELD(file,vstr,"vstr"),
    TLOG_FIELD(line,int,"int"),
    TLOG_FIELD(message,vstr,"vstr")
  };
  static const tlog_struct_desc result={
    "log_entry_t",sizeof(log_entry_t),TLOG_ALIGN(log_entry_t),
    fields,sizeof(fields)/sizeof(fields[0])
  };
  return &result;
}
static inline const tlog_field_desc *tlog_struct_find_field(
    const tlog_struct_desc *meta,const char *name) {
  if(!meta||!name) return NULL;
  for(size_t i=0;i<meta->field_count;++i)
    if(!strcmp(meta->fields[i].name,name)) return &meta->fields[i];
  return NULL;
}
#undef TLOG_FIELD
#undef TLOG_ALIGN

// =============================================================================
// Sink Interface (vtable pattern)
// =============================================================================

typedef struct log_sink_s log_sink_t;

/* Low-level sink vtable callbacks consume borrowed log-entry views.
 * New custom sinks should use sink_custom_create().
 */
typedef void (*sink_write_fn)(log_sink_t *sink, const log_entry_t *entry);
typedef void (*sink_flush_fn)(log_sink_t *sink);
typedef void (*sink_destroy_fn)(log_sink_t *sink);
typedef int (*sink_filter_fn)(const log_entry_t *entry, void *user_data);
typedef void (*sink_custom_write_fn)(const log_entry_t *entry, void *user_data);
typedef void (*sink_custom_flush_fn)(void *user_data);
typedef void (*sink_custom_destroy_fn)(void *user_data);

typedef enum {
  SALTS_SINK_BORROWED = 0,
  SALTS_SINK_OWNED = 1
} sink_ownership_t;

SALTS_C_API int sink_set_min_level(log_sink_t *sink, log_level_t level);
SALTS_C_API log_level_t sink_get_min_level(const log_sink_t *sink);
SALTS_C_API int sink_set_user_data(log_sink_t *sink, void *user_data);
SALTS_C_API void *sink_get_user_data(const log_sink_t *sink);

// =============================================================================
// Built-in Sinks
// =============================================================================

/**
 * @brief Format pattern placeholders:
 *   {time}      - Timestamp (YYYY-MM-DD HH:MM:SS)
 *   {time_ms}   - Timestamp with milliseconds
 *   {level}     - Log level (DEBUG, INFO, WARN, ERROR, FATAL)
 *   {component} - Component name
 *   {file}      - Source file name
 *   {line}      - Line number
 *   {thread}    - Thread ID
 *   {message}   - Log message
 *
 * Example: "[{time}] [{level}] [{component}] {message}"
 * Default: "[{time}] [{level}] {message}"
 */
#define SALTS_LOG_DEFAULT_PATTERN "[{time}] [{level}] {message}"

#ifndef SALTS_LOG_CAPTURE_SOURCE
  #ifdef NDEBUG
    #define SALTS_LOG_CAPTURE_SOURCE 0
  #else
    #define SALTS_LOG_CAPTURE_SOURCE 1
  #endif
#endif

#if SALTS_LOG_CAPTURE_SOURCE
  #define SALTS_LOG_SOURCE_FILE __FILE__
  #define SALTS_LOG_SOURCE_FILE_VIEW VSTR_LIT(__FILE__)
  #define SALTS_LOG_SOURCE_LINE __LINE__
  #define SALTS_LOG_FULL_PATTERN                                                                   \
    "[{time_ms}] [{level}] [{thread}] [{component}] ({file}:{line}) {message}"
#else
  #define SALTS_LOG_SOURCE_FILE NULL
  #define SALTS_LOG_SOURCE_FILE_VIEW vstr_from_buf(NULL, 0)
  #define SALTS_LOG_SOURCE_LINE 0
  #define SALTS_LOG_FULL_PATTERN "[{time_ms}] [{level}] [{thread}] [{component}] {message}"
#endif

/**
 * @brief Console sink options
 */
typedef struct {
  FILE *output;        // stdout/stderr (default: stdout)
  int use_colors;      // ANSI colors (default: 1)
  const char *pattern; // Format pattern (default: SALTS_LOG_DEFAULT_PATTERN)
                       // SALTS_LOG_FULL_PATTERN includes file:line only when
                       // SALTS_LOG_CAPTURE_SOURCE is enabled.
} console_sink_opts_t;

/**
 * @brief File sink options
 */
typedef struct {
  const char *path;    // Log file path
  size_t max_size;     // Max file size before rotation (0 = no limit)
  int max_files;       // Max rotated files to keep (0 = no rotation)
  int append;          // Append to existing file (default: 1)
  const char *pattern; // Format pattern (default: "[{time}] [{level}] {message}")
} file_sink_opts_t;

/**
 * @brief Callback sink - custom log handling
 */
typedef void (*log_callback_fn)(const log_entry_t *entry, void *user_data);

typedef struct {
  sink_custom_write_fn write;
  sink_custom_flush_fn flush;
  sink_custom_destroy_fn destroy;
  void *user_data;
} sink_custom_opts_t;

/**
 * @brief Create console sink (stdout/stderr with optional colors)
 */
SALTS_C_API log_sink_t *sink_console_create(const console_sink_opts_t *opts);

/**
 * @brief Create file sink with optional rotation
 *
 * Uses fs_pwrite for atomic append operations without mutex locks
 * on the POSIX write path. Windows uses serialized positional I/O behind
 * fs_pwrite to preserve the same offset semantics with CRT file handles.
 * Rotation is protected by mutex but happens rarely.
 *
 * Best for: All file logging scenarios, especially high-concurrency
 */
SALTS_C_API log_sink_t *sink_file_create(const file_sink_opts_t *opts);

/**
 * @brief Create callback sink for custom handling
 */
SALTS_C_API log_sink_t *sink_callback_create(log_callback_fn callback,
                                                       void *user_data);

/**
 * @brief Create an opaque custom sink with optional flush/destroy callbacks.
 *
 * Ownership of opts->user_data remains with the returned sink only after this
 * function succeeds. On failure, the caller still owns opts->user_data.
 */
SALTS_C_API log_sink_t *sink_custom_create(const sink_custom_opts_t *opts);

typedef struct {
  log_level_t min_level;
  log_level_t max_level;
  const char *component;            // Optional exact component match
  sink_filter_fn predicate;   // Optional extra predicate, non-zero means allow
  void *predicate_user_data;
} sink_filter_opts_t;
#define SALTS_SINK_FILTER_OPTS_DEFAULT \
  { SALTS_LOG_LEVEL_DEBUG, SALTS_LOG_LEVEL_FATAL, NULL, NULL, NULL }

/**
 * @brief Create a decorator sink that filters entries before forwarding to inner.
 *
 * Pass NULL for default DEBUG..FATAL filtering, or initialize opts with
 * SALTS_SINK_FILTER_OPTS_DEFAULT before overriding selected fields.
 * Ownership of inner is transferred only after this function succeeds.
 */
SALTS_C_API log_sink_t *sink_filter_create(log_sink_t *inner,
                                                     sink_ownership_t ownership,
                                                     const sink_filter_opts_t *opts);

/**
 * @brief Create a decorator sink that formats entries before forwarding to inner.
 *
 * The inner sink receives an entry whose message points to a decorator-owned
 * stack buffer valid only for the duration of the inner write call.
 * Ownership of inner is transferred only after this function succeeds.
 */
SALTS_C_API log_sink_t *sink_format_create(log_sink_t *inner,
                                                     sink_ownership_t ownership,
                                                     const char *pattern);

typedef struct {
  uint64_t entries_seen;
  uint64_t entries_forwarded;
  uint64_t entries_filtered;
  uint64_t bytes_forwarded;
} sink_metrics_t;

/**
 * @brief Create a decorator sink that records metrics and forwards to inner.
 *
 * The returned sink may be added to a logger like any other sink. When ownership
 * is SALTS_SINK_OWNED, destroying the decorator also destroys inner.
 * Ownership of inner is transferred only after this function succeeds.
 */
SALTS_C_API log_sink_t *sink_metrics_create(log_sink_t *inner,
                                                      sink_ownership_t ownership);

/**
 * @brief Read metrics from a sink created by sink_metrics_create.
 * @return 0 on success, -1 if sink is not a metrics decorator or args are invalid.
 */
SALTS_C_API int sink_metrics_snapshot(log_sink_t *sink, sink_metrics_t *out);

/**
 * @brief Destroy a sink
 */
SALTS_C_API void sink_destroy(log_sink_t *sink);

// =============================================================================
// Logger
// =============================================================================

typedef struct tlog_s tlog_t;

/**
 * @brief Logger configuration
 */
typedef struct {
  log_level_t min_level; // Global minimum level
  size_t buffer_size;          // Ring buffer size for async (default: 64KB)
  size_t pool_size;            // Memory pool size (default: 32KB)
} tlog_config_t;

/**
 * @brief Create logger with configuration
 */
SALTS_C_API tlog_t *tlog_create(const tlog_config_t *config);

/**
 * @brief Destroy logger and all attached sinks
 */
SALTS_C_API void tlog_destroy(tlog_t *logger);

/**
 * @brief Add sink to logger and transfer ownership
 * @param logger Target logger
 * @param sink Sink to add (ownership transferred on success)
 * @return 0 on success, -1 on failure
 * 
 * OWNERSHIP SEMANTICS:
 * - On SUCCESS (returns 0): Logger takes ownership. DO NOT call sink_destroy(sink).
 *                           The sink will be destroyed automatically with the logger.
 * - On FAILURE (returns -1): Caller retains ownership. MUST call sink_destroy(sink)
 *                           to avoid memory leak.
 * 
 * THREAD SAFETY: NOT thread-safe. Do not call concurrently with other tlog_*
 *                functions on the same logger.
 * 
 * CORRECT USAGE:
 *   log_sink_t *sink = sink_console_create(NULL);
 *   if (!sink) return -1;
 *   
 *   if (tlog_add_sink(logger, sink) != 0) {
 *     sink_destroy(sink);  // ← Cleanup on failure
 *     return -1;
 *   }
 *   // sink is now owned by logger, will be destroyed with logger
 * 
 * INCORRECT USAGE:
 *   log_sink_t *sink = sink_console_create(NULL);
 *   tlog_add_sink(logger, sink);  // ❌ Ignores error
 *   sink_destroy(sink);     // ❌ Double-free if add succeeded!
 * 
 * MULTIPLE SINKS:
 *   // Each sink is independently owned after successful add
 *   log_sink_t *console = sink_console_create(NULL);
 *   log_sink_t *file = sink_file_create(&file_opts);
 *   
 *   if (tlog_add_sink(logger, console) != 0) {
 *     sink_destroy(console);
 *     sink_destroy(file);  // ← Still our responsibility
 *     return -1;
 *   }
 *   
 *   if (tlog_add_sink(logger, file) != 0) {
 *     sink_destroy(file);   // ← Still our responsibility
 *     // console already owned by logger, will be cleaned up
 *     return -1;
 *   }
 *   // Both sinks now owned by logger
 */
SALTS_C_API int tlog_add_sink(tlog_t *logger, log_sink_t *sink);

/**
 * @brief Remove sink from logger without destroying it
 *
 * Call tlog_flush(logger) before removing when logs already published before
 * removal must still be delivered to this sink.
 */
SALTS_C_API void tlog_remove_sink(tlog_t *logger, log_sink_t *sink);

/**
 * @brief Flush all sinks (blocks until async queue is drained)
 */
SALTS_C_API void tlog_flush(tlog_t *logger);

// =============================================================================
// Logging Functions
// =============================================================================

/**
 * @brief Log a pre-formatted string directly
 */
SALTS_C_API void log_str(tlog_t *logger, log_level_t level, vstr component,
                             vstr file, int line, vstr message);

/**
 * @brief Log a message using typed arguments (auto-detects types for {})
 */
SALTS_C_API void log_typed(tlog_t *logger, log_level_t level,
                               vstr component, vstr file, int line, vstr pattern,
                               const fmt_arg_t *args, size_t arg_count);

// =============================================================================
// Level Control
// =============================================================================

/**
 * @brief Set logger minimum level.
 * @return 0 on success, -1 if logger is NULL or level is invalid.
 */
SALTS_C_API int tlog_set_level_ex(tlog_t *logger, log_level_t level);

/**
 * @brief Backward-compatible level setter. Invalid inputs are ignored.
 */
SALTS_C_API void tlog_set_level(tlog_t *logger, log_level_t level);
SALTS_C_API log_level_t tlog_get_level(const tlog_t *logger);

// =============================================================================
// Default Logger
// =============================================================================

/**
 * Default-logger access synchronizes only the global pointer. It does not
 * retain the logger or extend its lifetime. The application must keep an
 * installed logger alive while any thread may log through the default and
 * must quiesce those callers before replacing and destroying that logger.
 */
SALTS_C_API void tlog_set_default(tlog_t *logger);
SALTS_C_API tlog_t *tlog_get_default(void);
SALTS_C_API tlog_t *tlog_peek_default(void);

// =============================================================================
// Statistics (for monitoring)
// =============================================================================

/**
 * @brief Get total logs written
 */
SALTS_C_API uint64_t tlog_get_written(const tlog_t *logger);

/**
 * @brief Get total logs dropped (due to backpressure in async mode)
 */
SALTS_C_API uint64_t tlog_get_dropped(const tlog_t *logger);

/**
 * @brief Get current async queue size
 */
SALTS_C_API int tlog_get_queue_size(const tlog_t *logger);

// =============================================================================
// Utility Functions
// =============================================================================

SALTS_C_API const char *log_level_name(log_level_t level);
SALTS_C_API log_level_t log_level_from_name(const char *name);

#ifdef __cplusplus
}
#endif

// =============================================================================
// Convenience Macros
// =============================================================================
//
// Raw-message forms have fixed arity and route directly to log_str.
// Formatted forms use the F suffix, require at least one formatting argument,
// and route to log_typed. This keeps strict-C11 call sites free of
// empty-__VA_ARGS__ detection and comma-elision extensions.

#define SALTS_LOG_RAW_IMPL(logger_expr, lvl, comp, message_expr)                                  \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      const char *_tlog_component = (comp);                                                        \
      const char *_tlog_message = (message_expr);                                                  \
      log_str(_tlog_ptr, (lvl), vstr_from_cstr(_tlog_component),                            \
                    SALTS_LOG_SOURCE_FILE_VIEW, SALTS_LOG_SOURCE_LINE,                             \
                    vstr_from_cstr(_tlog_message));                                                \
    }                                                                                              \
  } while (0)

#ifdef __cplusplus

template <typename... Args>
inline void log_cpp_wrapper(tlog_t *logger, log_level_t level,
                                  const char *component, const char *file, int line,
                                  const char *pattern, const Args &...args) {
  static_assert(sizeof...(Args) > 0, "formatted logging requires at least one argument");
  const fmt_arg_t arg_array[] = {FMT_ARG(args)...};
  log_typed(logger, level, vstr_from_cstr(component), vstr_from_cstr(file), line,
                  vstr_from_cstr(pattern), arg_array, sizeof...(Args));
}

#define SALTS_LOG_FORMAT_IMPL(logger_expr, lvl, comp, pattern, ...)                               \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      log_cpp_wrapper(_tlog_ptr, (lvl), (comp), SALTS_LOG_SOURCE_FILE,                      \
                            SALTS_LOG_SOURCE_LINE, (pattern), __VA_ARGS__);                         \
    }                                                                                              \
  } while (0)

#else

#define SALTS_LOG_FORMAT_IMPL(logger_expr, lvl, comp, pattern, ...)                               \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      const char *_tlog_component = (comp);                                                        \
      const char *_tlog_pattern = (pattern);                                                       \
      log_typed(_tlog_ptr, (lvl), vstr_from_cstr(_tlog_component),                          \
                      SALTS_LOG_SOURCE_FILE_VIEW, SALTS_LOG_SOURCE_LINE,                           \
                      vstr_from_cstr(_tlog_pattern), FMT_ARGS(__VA_ARGS__),                        \
                      (size_t)FMT_ARG_COUNT(__VA_ARGS__));                                         \
    }                                                                                              \
  } while (0)

#endif

#define SALTS_LOG(logger, level, component, message)                                               \
  SALTS_LOG_RAW_IMPL((logger), (level), (component), (message))
#define SALTS_LOGF(logger, level, component, pattern, ...)                                         \
  SALTS_LOG_FORMAT_IMPL((logger), (level), (component), (pattern), __VA_ARGS__)

#define SALTS_LOG_DEBUG(logger, component, message)                                                \
  SALTS_LOG_RAW_IMPL((logger), SALTS_LOG_LEVEL_DEBUG, (component), (message))
#define SALTS_LOG_INFO(logger, component, message)                                                 \
  SALTS_LOG_RAW_IMPL((logger), SALTS_LOG_LEVEL_INFO, (component), (message))
#define SALTS_LOG_WARN(logger, component, message)                                                 \
  SALTS_LOG_RAW_IMPL((logger), SALTS_LOG_LEVEL_WARN, (component), (message))
#define SALTS_LOG_ERROR(logger, component, message)                                                \
  SALTS_LOG_RAW_IMPL((logger), SALTS_LOG_LEVEL_ERROR, (component), (message))
#define SALTS_LOG_FATAL(logger, component, message)                                                \
  SALTS_LOG_RAW_IMPL((logger), SALTS_LOG_LEVEL_FATAL, (component), (message))

#define SALTS_LOG_DEBUGF(logger, component, pattern, ...)                                          \
  SALTS_LOG_FORMAT_IMPL((logger), SALTS_LOG_LEVEL_DEBUG, (component), (pattern), __VA_ARGS__)
#define SALTS_LOG_INFOF(logger, component, pattern, ...)                                           \
  SALTS_LOG_FORMAT_IMPL((logger), SALTS_LOG_LEVEL_INFO, (component), (pattern), __VA_ARGS__)
#define SALTS_LOG_WARNF(logger, component, pattern, ...)                                           \
  SALTS_LOG_FORMAT_IMPL((logger), SALTS_LOG_LEVEL_WARN, (component), (pattern), __VA_ARGS__)
#define SALTS_LOG_ERRORF(logger, component, pattern, ...)                                          \
  SALTS_LOG_FORMAT_IMPL((logger), SALTS_LOG_LEVEL_ERROR, (component), (pattern), __VA_ARGS__)
#define SALTS_LOG_FATALF(logger, component, pattern, ...)                                          \
  SALTS_LOG_FORMAT_IMPL((logger), SALTS_LOG_LEVEL_FATAL, (component), (pattern), __VA_ARGS__)

#define TLOG_DEBUG(message)                                                                        \
  SALTS_LOG_RAW_IMPL(tlog_peek_default(), SALTS_LOG_LEVEL_DEBUG, NULL, (message))
#define TLOG_INFO(message)                                                                         \
  SALTS_LOG_RAW_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_INFO, NULL, (message))
#define TLOG_WARN(message)                                                                         \
  SALTS_LOG_RAW_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_WARN, NULL, (message))
#define TLOG_ERROR(message)                                                                        \
  SALTS_LOG_RAW_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_ERROR, NULL, (message))
#define TLOG_FATAL(message)                                                                        \
  SALTS_LOG_RAW_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_FATAL, NULL, (message))

#define TLOG_DEBUGF(pattern, ...)                                                                  \
  SALTS_LOG_FORMAT_IMPL(tlog_peek_default(), SALTS_LOG_LEVEL_DEBUG, NULL, (pattern), __VA_ARGS__)
#define TLOG_INFOF(pattern, ...)                                                                   \
  SALTS_LOG_FORMAT_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_INFO, NULL, (pattern), __VA_ARGS__)
#define TLOG_WARNF(pattern, ...)                                                                   \
  SALTS_LOG_FORMAT_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_WARN, NULL, (pattern), __VA_ARGS__)
#define TLOG_ERRORF(pattern, ...)                                                                  \
  SALTS_LOG_FORMAT_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_ERROR, NULL, (pattern), __VA_ARGS__)
#define TLOG_FATALF(pattern, ...)                                                                  \
  SALTS_LOG_FORMAT_IMPL(tlog_get_default(), SALTS_LOG_LEVEL_FATAL, NULL, (pattern), __VA_ARGS__)

#endif // tlog_h
