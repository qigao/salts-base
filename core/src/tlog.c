/**
 * @file tlog.c
 * @brief Production-ready multi-sink async logger implementation
 *
 * Design:
 * - Sync mode: Direct write to all sinks
 * - Async mode: Lock-free queue + background thread, fully non-blocking
 * - Thread-safe default logger initialization
 * - Bounded queue with backpressure handling
 * - Proper flush with drain synchronization
 */

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <clock.h>
#include <thread.h>
#include "tlog.h"
#include "fmt.h"
#include "log_pattern_lexer.h"
#include "cmeta_buffer.h"
#include <stdatomic.h>
#include <fs.h>
#include <disruptor.h>


#ifdef _WIN32
  #include <windows.h>
  #define gettid() GetCurrentThreadId()
#else
  #ifdef __linux__
    #include <sys/syscall.h>
    #include <unistd.h>
    #define gettid() (uint32_t)syscall(SYS_gettid)
  #elif defined(__APPLE__)
    #include <pthread.h>
static uint32_t tlog_gettid(void) {
  uint64_t thread_id = 0;
  if (pthread_threadid_np(NULL, &thread_id) != 0) return 0;
  return (uint32_t)thread_id;
}
    #define gettid() tlog_gettid()
  #else
    #include <pthread.h>
    #define gettid() (uint32_t)pthread_self()
  #endif
#endif

// =============================================================================
// Constants
// =============================================================================

#define MAX_SINKS 8
#define DEFAULT_POOL_SIZE (32 * 1024)
#define DEFAULT_ASYNC_BUFFER_SIZE (64 * 1024)
#define STRING_PADDING 8          // Padding for string alignment

// ANSI color codes
#define COLOR_RESET "\033[0m"
#define COLOR_DEBUG "\033[36m"
#define COLOR_INFO "\033[32m"
#define COLOR_WARN "\033[33m"
#define COLOR_ERROR "\033[31m"
#define COLOR_FATAL "\033[35m"

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4200) // nonstandard extension: zero-sized array
#endif

typedef struct {
  cmeta_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  int line;
  vstr component;
  vstr file;
  vstr message;
  char data[];
} async_log_entry_t;

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

// =============================================================================
// Default Logger and Time Cache Initialization
// =============================================================================

#define MAX_PATTERN_TOKENS 16

typedef struct {
  log_token_t type;
  vstr text;
} compiled_token_t;

typedef struct {
  compiled_token_t tokens[MAX_PATTERN_TOKENS];
  tstr pattern;
  int count;
} compiled_pattern_t;

static tlog_t *g_default_logger = NULL;
static cmeta_once_t g_default_logger_mutex_once = SALTS_ONCE_INIT;
static cmeta_mutex_t g_default_logger_mutex;
static int g_default_logger_mutex_init = 0;

typedef enum {
  SINK_KIND_CONSOLE = 1,
  SINK_KIND_FILE,
  SINK_KIND_CALLBACK,
  SINK_KIND_CUSTOM,
  SINK_KIND_FILTER,
  SINK_KIND_FORMAT,
  SINK_KIND_METRICS
} sink_kind_t;

struct cmeta_log_sink_s {
  cmeta_sink_write_fn write;
  cmeta_sink_flush_fn flush;
  cmeta_sink_destroy_fn destroy;
  sink_kind_t kind;
  _Atomic int min_level;
  _Atomic uintptr_t user_data;
};

static void sink_base_init(cmeta_log_sink_t *sink, cmeta_sink_write_fn write,
                           cmeta_sink_flush_fn flush, cmeta_sink_destroy_fn destroy,
                           sink_kind_t kind) {
  sink->write = write;
  sink->flush = flush;
  sink->destroy = destroy;
  sink->kind = kind;
  atomic_init(&sink->min_level, (int)SALTS_LOG_LEVEL_DEBUG);
  atomic_init(&sink->user_data, (uintptr_t)NULL);
}

static int sink_accepts_level(const cmeta_log_sink_t *sink, cmeta_log_level_t level) {
  return level >= (cmeta_log_level_t)atomic_load_explicit(&sink->min_level, memory_order_relaxed);
}

static int log_level_is_valid(cmeta_log_level_t level) {
  return cmeta_log_level_t_to_string(level) != NULL;
}

static void *sink_user_data(const cmeta_log_sink_t *sink) {
  return (void *)atomic_load_explicit(&sink->user_data, memory_order_relaxed);
}

static void sink_write_entry(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  if (sink && sink->write) {
    sink->write(sink, entry);
  }
}

static void sink_flush_inner(cmeta_log_sink_t *sink) {
  if (sink && sink->flush) {
    sink->flush(sink);
  }
}

typedef struct {
  uint64_t last_sec;
  char str[32];
  int len;
} tlog_time_cache_t;

static SALTS_THREAD_LOCAL tlog_time_cache_t tls_time_cache = {0};
static SALTS_THREAD_LOCAL uint32_t tls_thread_id = 0;
static SALTS_THREAD_LOCAL char tls_msg_buf[MAX_MESSAGE_SIZE + STRING_PADDING];

static inline uint32_t get_cached_tid(void) {
  if (tls_thread_id == 0) {
    tls_thread_id = gettid();
  }
  return tls_thread_id;
}

static void init_tlog_globals(void) {
  cmeta_mutex_init(&g_default_logger_mutex);
  g_default_logger_mutex_init = 1;
}

static void pattern_free(compiled_pattern_t *cp);

static void get_cached_time(uint64_t ts_ms, char *buf, int *written) {
  uint64_t sec = ts_ms / 1000;

  if (sec == tls_time_cache.last_sec && tls_time_cache.len > 0) {
    memcpy(buf, tls_time_cache.str, tls_time_cache.len);
    *written = tls_time_cache.len;
    return;
  }

  time_t t = (time_t)sec;
  struct tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  int len =
      (int)strftime(tls_time_cache.str, sizeof(tls_time_cache.str), "%Y-%m-%d %H:%M:%S", &tm_buf);
  tls_time_cache.last_sec = sec;
  tls_time_cache.len = len;
  memcpy(buf, tls_time_cache.str, len);
  *written = len;
}

static int pattern_compile(const char *pattern, compiled_pattern_t *cp) {
  if (!pattern || !cp) {
    return -1;
  }
  if (cp->pattern) {
    pattern_free(cp);
  }
  tstr copy = tstr_dup(pattern);
  if (!copy) {
    return -1;
  }

  const char *cursor = copy;
  const char *pattern_end = copy + tstr_len(copy);
  const char *token_start;
  size_t token_len;
  log_token_t token;
  cp->pattern = copy;
  cp->count = 0;
  while ((token = log_pattern_scan_n(&cursor, pattern_end, &token_start, &token_len)) != LOG_TOKEN_END &&
         cp->count < MAX_PATTERN_TOKENS) {
    compiled_token_t *ct = &cp->tokens[cp->count++];
    ct->type = token;
    if (token == LOG_TOKEN_TEXT || token == LOG_TOKEN_UNKNOWN) {
      ct->text = vstr_from_buf(token_start, token_len);
    } else {
      ct->text = (vstr){NULL, 0};
    }
  }
  if (token != LOG_TOKEN_END) {
    pattern_free(cp);
    cp->count = 0;
    return -1;
  }
  if (cp->count == 0) {
    pattern_free(cp);
    return -1;
  }
  return 0;
}

static void pattern_free(compiled_pattern_t *cp) {
  if (cp->pattern) {
    tstr_free(cp->pattern);
    cp->pattern = NULL;
  }
  cp->count = 0;
}

// =============================================================================
// Async Entry Management
// =============================================================================

static uint64_t round_up_pow2_u64(uint64_t value) {
  if (value <= 1U) {
    return 1U;
  }
  value--;
  value |= value >> 1U;
  value |= value >> 2U;
  value |= value >> 4U;
  value |= value >> 8U;
  value |= value >> 16U;
  value |= value >> 32U;
  return value + 1U;
}

static uint64_t logger_disruptor_capacity(size_t buffer_size_bytes) {
  size_t buffer_bytes = buffer_size_bytes ? buffer_size_bytes : DEFAULT_ASYNC_BUFFER_SIZE;
  uint64_t entries = (uint64_t)(buffer_bytes / sizeof(mem_buffer_t *));
  if (entries < 1024U) {
    entries = 1024U;
  }
  return round_up_pow2_u64(entries);
}

static mem_buffer_t *async_entry_create(mem_pool_t *pool, const cmeta_log_entry_t *entry) {
  size_t comp_len = entry->component.len;
  size_t file_len = entry->file.len;
  size_t msg_len = entry->message.len;
  size_t total_size = sizeof(async_log_entry_t) + comp_len + STRING_PADDING + file_len +
                      STRING_PADDING + msg_len + STRING_PADDING;
  mem_buffer_t *buffer = mem_get_buffer(pool, total_size);
  if (!buffer) return NULL;
  mem_set_used(buffer, total_size);

  async_log_entry_t *ae = (async_log_entry_t *)buffer->data;
  ae->level = entry->level;
  ae->timestamp_ms = entry->timestamp_ms;
  ae->thread_id = entry->thread_id;
  ae->line = entry->line;

  char *ptr = ae->data;
  if (comp_len > 0) {
    memcpy(ptr, entry->component.data, comp_len);
    ae->component = vstr_from_buf(ptr, comp_len);
  } else {
    ae->component = (vstr){NULL, 0};
  }
  memset(ptr + comp_len, 0, STRING_PADDING);
  ptr += comp_len + STRING_PADDING;

  if (file_len > 0) {
    memcpy(ptr, entry->file.data, file_len);
    ae->file = vstr_from_buf(ptr, file_len);
  } else {
    ae->file = (vstr){NULL, 0};
  }
  memset(ptr + file_len, 0, STRING_PADDING);
  ptr += file_len + STRING_PADDING;

  if (msg_len > 0) {
    memcpy(ptr, entry->message.data, msg_len);
    ae->message = vstr_from_buf(ptr, msg_len);
  } else {
    ae->message = (vstr){NULL, 0};
  }
  memset(ptr + msg_len, 0, STRING_PADDING);
  return buffer;
}

// =============================================================================
// Console Sink
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  FILE *output;
  int use_colors;
  compiled_pattern_t pattern;
  cmeta_mutex_t write_mutex; // Thread-safe writes
} console_sink_t;

static const char *get_level_color(cmeta_log_level_t level) {
  switch (level) {
  case SALTS_LOG_LEVEL_DEBUG: return COLOR_DEBUG;
  case SALTS_LOG_LEVEL_INFO:  return COLOR_INFO;
  case SALTS_LOG_LEVEL_WARN:  return COLOR_WARN;
  case SALTS_LOG_LEVEL_ERROR: return COLOR_ERROR;
  case SALTS_LOG_LEVEL_FATAL: return COLOR_FATAL;
  default:                    return COLOR_RESET;
  }
}

// =============================================================================
// Pattern Formatter
// =============================================================================

static int format_with_pattern(char *buf, size_t buf_size, const compiled_pattern_t *cp,
                               const cmeta_log_entry_t *entry) {
  if (!cp || !buf || buf_size == 0)
    return 0;

  char *dst = buf;
  char *end = buf + buf_size - 1;
  int written;

  for (int i = 0; i < cp->count && dst < end; ++i) {
    const compiled_token_t *ct = &cp->tokens[i];
    written = 0;

    switch (ct->type) {
    case LOG_TOKEN_TIME: {
      get_cached_time(entry->timestamp_ms, dst, &written);
      break;
    }
    case LOG_TOKEN_TIME_MS: {
      get_cached_time(entry->timestamp_ms, dst, &written);
      if (written > 0 && dst + written + 4 < end) {
        fmt(dst + written, 5, ".{:03}", (unsigned)(entry->timestamp_ms % 1000));
        written += 4;
      }
      break;
    }
    case LOG_TOKEN_LEVEL: {
      vstr name = vstr_from_cstr(cmeta_log_level_name(entry->level));
      written = (int)name.len;
      if (written > 0 && dst + written < end) {
        memcpy(dst, name.data, written);
      }
      break;
    }
    case LOG_TOKEN_COMPONENT:
      written = (int)entry->component.len;
      if (written > 0 && dst + written < end) memcpy(dst, entry->component.data, (size_t)written);
      break;
    case LOG_TOKEN_FILE:
      written = (int)entry->file.len;
      if (written > 0 && dst + written < end) memcpy(dst, entry->file.data, (size_t)written);
      break;
    case LOG_TOKEN_LINE: {
      if (entry->line <= 0) {
        break;
      }
      char temp[16];
      fmt(temp, sizeof(temp), "{}", entry->line);
      written = (int)strlen(temp);
      if (written > 0 && dst + written < end) {
        memcpy(dst, temp, written);
      }
      break;
    }
    case LOG_TOKEN_THREAD: {
      char temp[16];
      fmt(temp, sizeof(temp), "{}", entry->thread_id);
      written = (int)strlen(temp);
      if (written > 0 && dst + written < end) {
        memcpy(dst, temp, written);
      }
      break;
    }
    case LOG_TOKEN_MESSAGE:
      written = (int)entry->message.len;
      if (written > 0 && dst + written < end) memcpy(dst, entry->message.data, (size_t)written);
      break;
    case LOG_TOKEN_TEXT:
    case LOG_TOKEN_UNKNOWN:
      if (ct->text.data && ct->text.len > 0 && dst + ct->text.len < end) {
        memcpy(dst, ct->text.data, ct->text.len);
        written = (int)ct->text.len;
      }
      break;
    default:
      break;
    }

    if (written > 0)
      dst += written;
  }

  *dst = '\0';
  return (int)(dst - buf);
}

static void console_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  console_sink_t *cs = (console_sink_t *)sink;
  if (!sink_accepts_level(sink, entry->level))
    return;

  char formatted[MAX_MESSAGE_SIZE];
  int len = format_with_pattern(formatted, sizeof(formatted) - 2, &cs->pattern, entry);
  if ((len <= 0)) return;

  cmeta_mutex_lock(&cs->write_mutex);
  FILE *out = cs->output;
  if (cs->use_colors) {
    const char *color = get_level_color(entry->level);
    fputs(color, out);
    fwrite(formatted, 1, len, out);
    fputs(COLOR_RESET "\n", out);
  } else {
    formatted[len] = '\n';
    fwrite(formatted, 1, len + 1, out);
  }
  cmeta_mutex_unlock(&cs->write_mutex);
}

static void console_sink_flush(cmeta_log_sink_t *sink) {
  console_sink_t *cs = (console_sink_t *)sink;
  cmeta_mutex_lock(&cs->write_mutex);
  fflush(cs->output);
  cmeta_mutex_unlock(&cs->write_mutex);
}

static void console_sink_destroy(cmeta_log_sink_t *sink) {
  console_sink_t *cs = (console_sink_t *)sink;
  cmeta_mutex_destroy(&cs->write_mutex);
  pattern_free(&cs->pattern);
  free(sink);
}

cmeta_log_sink_t *cmeta_sink_console_create(const cmeta_console_sink_opts_t *opts) {
  console_sink_t *sink = calloc(1, sizeof(console_sink_t));
  if (!sink)
    return NULL;

  cmeta_mutex_init(&sink->write_mutex);

  sink_base_init(&sink->base, console_sink_write, console_sink_flush, console_sink_destroy,
                 SINK_KIND_CONSOLE);

  if (opts) {
    sink->output = opts->output ? opts->output : stdout;
    sink->use_colors = opts->use_colors;
    if (pattern_compile(opts->pattern ? opts->pattern : SALTS_LOG_DEFAULT_PATTERN,
                        &sink->pattern) != 0) {
      cmeta_mutex_destroy(&sink->write_mutex);
      free(sink);
      return NULL;
    }
  } else {
    sink->output = stdout;
    sink->use_colors = 1;
    if (pattern_compile(SALTS_LOG_DEFAULT_PATTERN, &sink->pattern) != 0) {
      cmeta_mutex_destroy(&sink->write_mutex);
      free(sink);
      return NULL;
    }
  }

  return &sink->base;
}

// =============================================================================
// File Sink (Thread-Safe)
// =============================================================================
// File Sink (pwrite-based, lock-free with rotation)
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  salts_file_t fd;
  char *path;
  compiled_pattern_t pattern;
  _Atomic int64_t offset;
  _Atomic int64_t bytes_written; // Summary counter for current file size
  atomic_int rotate_flag;     // 1 => rotate before next write
  size_t max_size;
  int max_files;
  cmeta_mutex_t rotate_mutex;
} file_sink_t;

static void file_sink_rotate(file_sink_t *fs) {
  cmeta_mutex_lock(&fs->rotate_mutex);

  // Close current file
  if (fs->fd != SALTS_INVALID_FILE) {
    salts_fs_close(fs->fd);
    fs->fd = SALTS_INVALID_FILE;
  }

  if (fs->max_files > 0) {
    // Rotate files: file.log.N -> file.log.N+1
    tstr old_path = NULL;
    tstr new_path = NULL;
    for (int i = fs->max_files - 1; i >= 0; i--) {
      tstr_free(old_path);
      tstr_free(new_path);
      if (i == 0) {
        old_path = tstr_dup(fs->path);
      } else {
        old_path = tstr_format("{}.{}", fs->path, i);
      }
      new_path = tstr_format("{}.{}", fs->path, i + 1);
      if (old_path && new_path) {
        salts_fs_rename(old_path, new_path);
      }
    }
    tstr_free(old_path);
    tstr_free(new_path);
  }

  // Open new file (when max_files == 0, just recreate/truncate the current file)
  fs->fd = salts_fs_open(fs->path, SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_O_TRUNC,
                         SALTS_FS_DEFAULT_MODE);
  if (fs->fd != SALTS_INVALID_FILE) {
    atomic_store(&fs->offset, 0);
    atomic_store(&fs->bytes_written, 0);
    atomic_store(&fs->rotate_flag, 0);
  }

  cmeta_mutex_unlock(&fs->rotate_mutex);
}

static void file_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  file_sink_t *fs = (file_sink_t *)sink;
  if (!sink_accepts_level(sink, entry->level))
    return;

  char line[MAX_MESSAGE_SIZE];
  int len = format_with_pattern(line, sizeof(line) - 1, &fs->pattern, entry);
  if (len <= 0)
    return;
  line[len++] = '\n';

  if (fs->max_size > 0 && atomic_load(&fs->rotate_flag)) {
    file_sink_rotate(fs);
  }
  if (fs->fd == SALTS_INVALID_FILE) {
    return;
  }

  int64_t write_offset = atomic_fetch_add(&fs->offset, len);
  int written = salts_fs_pwrite(fs->fd, line, (size_t)len, write_offset);
  if (written <= 0) {
    return;
  }

  if (fs->max_size > 0) {
    int64_t total_written = atomic_fetch_add(&fs->bytes_written, written) + written;
    if (total_written >= (int64_t)fs->max_size) {
      atomic_store(&fs->rotate_flag, 1);
      file_sink_rotate(fs);
    }
  }
}

static void file_sink_flush(cmeta_log_sink_t *sink) {
  file_sink_t *fs = (file_sink_t *)sink;
  cmeta_mutex_lock(&fs->rotate_mutex);
  if (fs->fd != SALTS_INVALID_FILE) {
    salts_fs_fsync(fs->fd);
  }
  cmeta_mutex_unlock(&fs->rotate_mutex);
}

static void file_sink_destroy(cmeta_log_sink_t *sink) {
  file_sink_t *fs = (file_sink_t *)sink;
  cmeta_mutex_lock(&fs->rotate_mutex);
  if (fs->fd != SALTS_INVALID_FILE) {
    salts_fs_close(fs->fd);
  }
  cmeta_mutex_unlock(&fs->rotate_mutex);
  cmeta_mutex_destroy(&fs->rotate_mutex);
  tstr_free(fs->path);
  pattern_free(&fs->pattern);
  free(fs);
}

cmeta_log_sink_t *cmeta_sink_file_create(const cmeta_file_sink_opts_t *opts) {
  if (!opts || !opts->path)
    return NULL;

  file_sink_t *sink = calloc(1, sizeof(file_sink_t));
  if (!sink)
    return NULL;

  cmeta_mutex_init(&sink->rotate_mutex);

  sink_base_init(&sink->base, file_sink_write, file_sink_flush, file_sink_destroy, SINK_KIND_FILE);

  sink->path = tstr_dup(opts->path);
  if (!sink->path ||
      pattern_compile(opts->pattern ? opts->pattern : SALTS_LOG_DEFAULT_PATTERN,
                      &sink->pattern) != 0) {
    cmeta_mutex_destroy(&sink->rotate_mutex);
    tstr_free(sink->path);
    free(sink);
    return NULL;
  }
  sink->max_size = opts->max_size;
  sink->max_files = opts->max_files;

  int flags = SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT;
  flags |= opts->append ? SALTS_FS_O_APPEND : SALTS_FS_O_TRUNC;

  sink->fd = salts_fs_open(opts->path, flags, SALTS_FS_DEFAULT_MODE);
  if (sink->fd == SALTS_INVALID_FILE) {
    cmeta_mutex_destroy(&sink->rotate_mutex);
    tstr_free(sink->path);
    pattern_free(&sink->pattern);
    free(sink);
    return NULL;
  }

  // Initialize counters from current file size if appending
  if (opts->append) {
    int64_t pos = salts_fs_seek(sink->fd, 0, SEEK_END);
    int64_t initial = pos > 0 ? pos : 0;
    atomic_store(&sink->offset, initial);
    atomic_store(&sink->bytes_written, initial);
  } else {
    atomic_store(&sink->offset, 0);
    atomic_store(&sink->bytes_written, 0);
  }
  atomic_store(&sink->rotate_flag, 0);

  return &sink->base;
}

// =============================================================================
// Callback Sink
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  cmeta_log_callback_fn callback;
} callback_sink_t;

static void callback_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  callback_sink_t *cs = (callback_sink_t *)sink;
  if (!sink_accepts_level(sink, entry->level))
    return;
  if (cs->callback) {
    cs->callback(entry, sink_user_data(sink));
  }
}

static void callback_sink_flush(cmeta_log_sink_t *sink) { (void)sink; }
static void callback_sink_destroy(cmeta_log_sink_t *sink) { free(sink); }

cmeta_log_sink_t *cmeta_sink_callback_create(cmeta_log_callback_fn callback, void *user_data) {
  if (!callback)
    return NULL;

  callback_sink_t *sink = calloc(1, sizeof(callback_sink_t));
  if (!sink)
    return NULL;

  sink_base_init(&sink->base, callback_sink_write, callback_sink_flush, callback_sink_destroy,
                 SINK_KIND_CALLBACK);
  atomic_store_explicit(&sink->base.user_data, (uintptr_t)user_data, memory_order_relaxed);
  sink->callback = callback;

  return &sink->base;
}

// =============================================================================
// Custom Sink
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  cmeta_sink_custom_write_fn write;
  cmeta_sink_custom_flush_fn flush;
  cmeta_sink_custom_destroy_fn destroy;
} custom_sink_t;

static void custom_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  custom_sink_t *cs = (custom_sink_t *)sink;
  if (!sink_accepts_level(sink, entry->level)) {
    return;
  }
  cs->write(entry, sink_user_data(sink));
}

static void custom_sink_flush(cmeta_log_sink_t *sink) {
  custom_sink_t *cs = (custom_sink_t *)sink;
  if (cs->flush) {
    cs->flush(sink_user_data(sink));
  }
}

static void custom_sink_destroy(cmeta_log_sink_t *sink) {
  custom_sink_t *cs = (custom_sink_t *)sink;
  if (cs->destroy) {
    cs->destroy(sink_user_data(sink));
  }
  free(cs);
}

cmeta_log_sink_t *cmeta_sink_custom_create(const cmeta_sink_custom_opts_t *opts) {
  if (!opts || !opts->write) {
    return NULL;
  }

  custom_sink_t *sink = calloc(1, sizeof(custom_sink_t));
  if (!sink) {
    return NULL;
  }

  sink_base_init(&sink->base, custom_sink_write, custom_sink_flush, custom_sink_destroy,
                 SINK_KIND_CUSTOM);
  atomic_store_explicit(&sink->base.user_data, (uintptr_t)opts->user_data, memory_order_relaxed);
  sink->write = opts->write;
  sink->flush = opts->flush;
  sink->destroy = opts->destroy;

  return &sink->base;
}

// =============================================================================
// Sink Accessors
// =============================================================================

int cmeta_sink_set_min_level(cmeta_log_sink_t *sink, cmeta_log_level_t level) {
  if (!sink || !log_level_is_valid(level)) {
    return -1;
  }
  atomic_store_explicit(&sink->min_level, (int)level, memory_order_relaxed);
  return 0;
}

cmeta_log_level_t cmeta_sink_get_min_level(const cmeta_log_sink_t *sink) {
  return sink ? (cmeta_log_level_t)atomic_load_explicit(&sink->min_level, memory_order_relaxed)
              : SALTS_LOG_LEVEL_INFO;
}

int cmeta_sink_set_user_data(cmeta_log_sink_t *sink, void *user_data) {
  if (!sink) {
    return -1;
  }
  atomic_store_explicit(&sink->user_data, (uintptr_t)user_data, memory_order_relaxed);
  return 0;
}

void *cmeta_sink_get_user_data(const cmeta_log_sink_t *sink) {
  return sink ? sink_user_data(sink) : NULL;
}

// =============================================================================
// Filter Sink Decorator
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  cmeta_log_sink_t *inner;
  int owns_inner;
  cmeta_log_level_t min_level;
  cmeta_log_level_t max_level;
  char *component;
  cmeta_sink_filter_fn predicate;
  void *predicate_user_data;
} filter_sink_t;

static int filter_sink_allows(filter_sink_t *fs, const cmeta_log_entry_t *entry) {
  if (!sink_accepts_level(&fs->base, entry->level) || entry->level < fs->min_level ||
      entry->level > fs->max_level) {
    return 0;
  }
  if (fs->component != NULL) {
    size_t expected_len = tstr_len(fs->component);
    if (entry->component.len != expected_len ||
        (expected_len > 0 && memcmp(entry->component.data, fs->component, expected_len) != 0)) {
      return 0;
    }
  }
  if (fs->predicate != NULL && !fs->predicate(entry, fs->predicate_user_data)) {
    return 0;
  }
  return 1;
}

static void filter_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  filter_sink_t *fs = (filter_sink_t *)sink;
  if (!filter_sink_allows(fs, entry)) {
    return;
  }
  sink_write_entry(fs->inner, entry);
}

static void filter_sink_flush(cmeta_log_sink_t *sink) {
  filter_sink_t *fs = (filter_sink_t *)sink;
  sink_flush_inner(fs->inner);
}

static void filter_sink_destroy(cmeta_log_sink_t *sink) {
  filter_sink_t *fs = (filter_sink_t *)sink;
  if (fs->owns_inner && fs->inner) {
    cmeta_sink_destroy(fs->inner);
  }
  tstr_free(fs->component);
  free(fs);
}

cmeta_log_sink_t *cmeta_sink_filter_create(cmeta_log_sink_t *inner,
                                           cmeta_sink_ownership_t ownership,
                                           const cmeta_sink_filter_opts_t *opts) {
  if (!inner) {
    return NULL;
  }

  filter_sink_t *sink = calloc(1, sizeof(filter_sink_t));
  if (!sink) {
    return NULL;
  }

  sink_base_init(&sink->base, filter_sink_write, filter_sink_flush, filter_sink_destroy,
                 SINK_KIND_FILTER);
  sink->inner = inner;
  sink->owns_inner = ownership == SALTS_SINK_OWNED;
  sink->min_level = opts ? opts->min_level : SALTS_LOG_LEVEL_DEBUG;
  sink->max_level = opts ? opts->max_level : SALTS_LOG_LEVEL_FATAL;
  sink->predicate = opts ? opts->predicate : NULL;
  sink->predicate_user_data = opts ? opts->predicate_user_data : NULL;

  if (sink->min_level < SALTS_LOG_LEVEL_DEBUG || sink->min_level > SALTS_LOG_LEVEL_FATAL ||
      sink->max_level < SALTS_LOG_LEVEL_DEBUG || sink->max_level > SALTS_LOG_LEVEL_FATAL ||
      sink->min_level > sink->max_level) {
    free(sink);
    return NULL;
  }

  if (opts && opts->component) {
    sink->component = tstr_dup(opts->component);
    if (!sink->component) {
      free(sink);
      return NULL;
    }
  }

  return &sink->base;
}

// =============================================================================
// Formatting Sink Decorator
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  cmeta_log_sink_t *inner;
  int owns_inner;
  compiled_pattern_t pattern;
} format_sink_t;

static void format_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  format_sink_t *fs = (format_sink_t *)sink;
  char formatted[MAX_MESSAGE_SIZE];
  cmeta_log_entry_t formatted_entry;
  int len;

  if (!sink_accepts_level(sink, entry->level) || fs->inner == NULL) {
    return;
  }

  len = format_with_pattern(formatted, sizeof(formatted), &fs->pattern, entry);
  if (len <= 0) {
    return;
  }

  formatted_entry = *entry;
  formatted_entry.message = vstr_from_buf(formatted, (size_t)len);
  sink_write_entry(fs->inner, &formatted_entry);
}

static void format_sink_flush(cmeta_log_sink_t *sink) {
  format_sink_t *fs = (format_sink_t *)sink;
  sink_flush_inner(fs->inner);
}

static void format_sink_destroy(cmeta_log_sink_t *sink) {
  format_sink_t *fs = (format_sink_t *)sink;
  if (fs->owns_inner && fs->inner) {
    cmeta_sink_destroy(fs->inner);
  }
  pattern_free(&fs->pattern);
  free(fs);
}

cmeta_log_sink_t *cmeta_sink_format_create(cmeta_log_sink_t *inner,
                                           cmeta_sink_ownership_t ownership,
                                           const char *pattern) {
  if (!inner) {
    return NULL;
  }

  format_sink_t *sink = calloc(1, sizeof(format_sink_t));
  if (!sink) {
    return NULL;
  }

  sink_base_init(&sink->base, format_sink_write, format_sink_flush, format_sink_destroy,
                 SINK_KIND_FORMAT);
  sink->inner = inner;
  sink->owns_inner = ownership == SALTS_SINK_OWNED;
  if (pattern_compile(pattern ? pattern : "{message}", &sink->pattern) != 0) {
    free(sink);
    return NULL;
  }

  return &sink->base;
}

// =============================================================================
// Metrics Sink Decorator
// =============================================================================

typedef struct {
  cmeta_log_sink_t base;
  cmeta_log_sink_t *inner;
  int owns_inner;
  _Atomic uint64_t entries_seen;
  _Atomic uint64_t entries_forwarded;
  _Atomic uint64_t entries_filtered;
  _Atomic uint64_t bytes_forwarded;
} metrics_sink_t;

static void metrics_sink_write(cmeta_log_sink_t *sink, const cmeta_log_entry_t *entry) {
  metrics_sink_t *ms = (metrics_sink_t *)sink;

  atomic_fetch_add(&ms->entries_seen, 1);
  if (!sink_accepts_level(sink, entry->level) || ms->inner == NULL) {
    atomic_fetch_add(&ms->entries_filtered, 1);
    return;
  }

  atomic_fetch_add(&ms->entries_forwarded, 1);
  atomic_fetch_add(&ms->bytes_forwarded, entry->message.len);
  sink_write_entry(ms->inner, entry);
}

static void metrics_sink_flush(cmeta_log_sink_t *sink) {
  metrics_sink_t *ms = (metrics_sink_t *)sink;
  sink_flush_inner(ms->inner);
}

static void metrics_sink_destroy(cmeta_log_sink_t *sink) {
  metrics_sink_t *ms = (metrics_sink_t *)sink;
  if (ms->owns_inner && ms->inner) {
    cmeta_sink_destroy(ms->inner);
  }
  free(ms);
}

cmeta_log_sink_t *cmeta_sink_metrics_create(cmeta_log_sink_t *inner,
                                            cmeta_sink_ownership_t ownership) {
  if (!inner) {
    return NULL;
  }

  metrics_sink_t *sink = calloc(1, sizeof(metrics_sink_t));
  if (!sink) {
    return NULL;
  }

  sink_base_init(&sink->base, metrics_sink_write, metrics_sink_flush, metrics_sink_destroy,
                 SINK_KIND_METRICS);
  sink->inner = inner;
  sink->owns_inner = ownership == SALTS_SINK_OWNED;

  return &sink->base;
}

int cmeta_sink_metrics_snapshot(cmeta_log_sink_t *sink, cmeta_sink_metrics_t *out) {
  metrics_sink_t *ms;

  if (!sink || !out || sink->kind != SINK_KIND_METRICS) {
    return -1;
  }

  ms = (metrics_sink_t *)sink;
  out->entries_seen = atomic_load(&ms->entries_seen);
  out->entries_forwarded = atomic_load(&ms->entries_forwarded);
  out->entries_filtered = atomic_load(&ms->entries_filtered);
  out->bytes_forwarded = atomic_load(&ms->bytes_forwarded);
  return 0;
}

// =============================================================================
// Logger Implementation
// =============================================================================
// Statistics
// =============================================================================
// Logger Structure
// =============================================================================

void cmeta_sink_destroy(cmeta_log_sink_t *sink) {
  if (sink && sink->destroy) {
    sink->destroy(sink);
  }
}

// =============================================================================
// Logger Structure
// =============================================================================

struct tlog_s {
  // ---------------------------------------------------------------------------
  // Configuration (Read-Only)
  // ---------------------------------------------------------------------------
  _Atomic int min_level;
  cmeta_log_sink_t *sinks[MAX_SINKS];
  int sink_count;
  cmeta_mutex_t sink_mutex;

  // ---------------------------------------------------------------------------
  // Disruptor (Replaces custom ring buffer)
  // ---------------------------------------------------------------------------
  disruptor_t *disruptor;
  disruptor_consumer_t consumer;

  // ---------------------------------------------------------------------------
  // Async payload pool (thread-safe via cmeta_buffer)
  // ---------------------------------------------------------------------------
  mem_pool_t async_pool;

  // ---------------------------------------------------------------------------
  // Background Thread
  // ---------------------------------------------------------------------------
  atomic_int running;
  atomic_int consumer_ready;
  cmeta_thread_t thread;
  cmeta_mutex_t wake_mutex;
  cmeta_cond_t wake_cond;

  // ---------------------------------------------------------------------------
  // Stats
  // ---------------------------------------------------------------------------
  alignas(64) _Atomic int64_t logs_written;
  alignas(64) _Atomic int64_t logs_dropped;
  alignas(64) _Atomic int64_t logs_published;  // Total published to disruptor
};

// Forward declarations
static void logger_write_to_sinks(tlog_t *logger, const cmeta_log_entry_t *entry);
static int logger_publish_entry(tlog_t *logger, mem_buffer_t *buffer);

// =============================================================================
// Async Thread - Using Disruptor
// =============================================================================

/**
 * @brief Drain log entries from disruptor range [first_seq, last_seq].
 *
 * Converts each async_log_entry_t back to cmeta_log_entry_t and writes
 * to all sinks.  Releases each mem_buffer_t after processing.
 */
static void logger_drain_entries(tlog_t *logger, uint64_t first_seq, uint64_t last_seq) {
  for (uint64_t seq = first_seq; seq <= last_seq; ++seq) {
    disruptor_cursor_t read_cursor;
    read_cursor.sequence = seq;

    _Atomic(mem_buffer_t *) *entry_ptr =
        (_Atomic(mem_buffer_t *) *)disruptor_show_entry(logger->disruptor, &read_cursor);
    if (!entry_ptr) continue;

    mem_buffer_t *buffer = atomic_load_explicit(entry_ptr, memory_order_acquire);
    while (buffer == NULL) {
      cmeta_thread_yield();
      buffer = atomic_load_explicit(entry_ptr, memory_order_acquire);
    }

    async_log_entry_t *ae = (async_log_entry_t *)buffer->data;

    cmeta_log_entry_t entry = {
      .level = ae->level,
      .timestamp_ms = ae->timestamp_ms,
      .thread_id = ae->thread_id,
      .component = ae->component,
      .file = ae->file,
      .line = ae->line,
      .message = ae->message
    };

    logger_write_to_sinks(logger, &entry);
    atomic_fetch_add(&logger->logs_written, 1);
    mem_release(buffer);
    atomic_store_explicit(entry_ptr, NULL, memory_order_release);
  }
}

static int logger_should_run(void *ctx) {
  tlog_t *logger = (tlog_t *)ctx;
  return atomic_load(&logger->running);
}

static void logger_signal_consumer(tlog_t *logger) {
  if (!logger || !logger->wake_mutex || !logger->wake_cond) {
    return;
  }

  cmeta_mutex_lock(&logger->wake_mutex);
  cmeta_cond_signal(&logger->wake_cond);
  cmeta_mutex_unlock(&logger->wake_mutex);
}

static void logger_broadcast_consumer(tlog_t *logger) {
  if (!logger || !logger->wake_mutex || !logger->wake_cond) {
    return;
  }

  cmeta_mutex_lock(&logger->wake_mutex);
  cmeta_cond_broadcast(&logger->wake_cond);
  cmeta_mutex_unlock(&logger->wake_mutex);
}

static int logger_wait_for_available(tlog_t *logger, uint64_t next_sequence,
                                     disruptor_cursor_t *cursor) {
  if (!logger || !cursor) {
    return 0;
  }

  cursor->sequence = next_sequence;
  if (disruptor_consumer_wait_for_nonblocking(logger->disruptor, cursor)) {
    return 1;
  }

  if (!logger->wake_mutex || !logger->wake_cond) {
    return 0;
  }

  cmeta_mutex_lock(&logger->wake_mutex);
  while (logger_should_run(logger)) {
    cursor->sequence = next_sequence;
    if (disruptor_consumer_wait_for_nonblocking(logger->disruptor, cursor)) {
      cmeta_mutex_unlock(&logger->wake_mutex);
      return 1;
    }
    cmeta_cond_wait(&logger->wake_cond, &logger->wake_mutex);
  }

  cursor->sequence = next_sequence;
  if (disruptor_consumer_wait_for_nonblocking(logger->disruptor, cursor)) {
    cmeta_mutex_unlock(&logger->wake_mutex);
    return 1;
  }
  cmeta_mutex_unlock(&logger->wake_mutex);
  return 0;
}

static void logger_process_batch(void *ctx, uint64_t first_seq, uint64_t last_seq) {
  tlog_t *logger = (tlog_t *)ctx;
  logger_drain_entries(logger, first_seq, last_seq);
}

static void async_logger_thread(void *arg) {
  tlog_t *logger = (tlog_t *)arg;
  uint64_t next_sequence = disruptor_consumer_register(logger->disruptor, &logger->consumer);
  atomic_store(&logger->consumer_ready, 1);

  while (logger_should_run(logger)) {
    disruptor_cursor_t cursor;

    if (!logger_wait_for_available(logger, next_sequence, &cursor)) {
      continue;
    }

    logger_process_batch(logger, next_sequence, cursor.sequence);
    disruptor_consumer_release_entry(logger->disruptor, &logger->consumer, &cursor);
    next_sequence = cursor.sequence + 1;
  }

  {
    disruptor_cursor_t drain_cursor;
    drain_cursor.sequence = next_sequence;
    if (disruptor_consumer_wait_for_nonblocking(logger->disruptor, &drain_cursor)) {
      logger_process_batch(logger, next_sequence, drain_cursor.sequence);
      disruptor_consumer_release_entry(logger->disruptor, &logger->consumer, &drain_cursor);
    }
  }

  disruptor_consumer_unregister(logger->disruptor, &logger->consumer);
}

static int logger_start_async(tlog_t *logger) {
  atomic_store(&logger->running, 1);
  atomic_store(&logger->consumer_ready, 0);

  if (cmeta_thread_create(&logger->thread, async_logger_thread, logger) != 0) {
    return -1;
  }

  while (!atomic_load(&logger->consumer_ready)) {
    cmeta_thread_yield();
  }

  return 0;
}

static void logger_stop_async(tlog_t *logger) {
  atomic_store(&logger->running, 0);
  logger_broadcast_consumer(logger);
  cmeta_thread_join(&logger->thread);
}

static int logger_publish_entry(tlog_t *logger, mem_buffer_t *buffer) {
  disruptor_cursor_t cursor = {0};
  disruptor_publisher_next_entry_blocking(logger->disruptor, &cursor);

  _Atomic(mem_buffer_t *) *slot =
      (_Atomic(mem_buffer_t *) *)disruptor_acquire_entry(logger->disruptor, &cursor);
  if (!slot || cursor.sequence == 0U) {
    mem_release(buffer);
    atomic_fetch_add(&logger->logs_dropped, 1);
    return -1;
  }

  atomic_store_explicit(slot, buffer, memory_order_release);
  disruptor_publisher_commit_entry_blocking(logger->disruptor, &cursor);
  atomic_fetch_add(&logger->logs_published, 1);
  logger_signal_consumer(logger);
  return 0;
}

// =============================================================================
// Logger Lifecycle
// =============================================================================

tlog_t *tlog_create(const tlog_config_t *config) {
  if (config && !log_level_is_valid(config->min_level)) {
    return NULL;
  }

  tlog_t *logger = calloc(1, sizeof(tlog_t));
  if (!logger)
    return NULL;

  atomic_init(&logger->min_level, (int)(config ? config->min_level : SALTS_LOG_LEVEL_INFO));

  cmeta_mutex_init(&logger->sink_mutex);
  cmeta_mutex_init(&logger->wake_mutex);
  cmeta_cond_init(&logger->wake_cond);

  atomic_store(&logger->logs_written, 0);
  atomic_store(&logger->logs_dropped, 0);
  atomic_store(&logger->logs_published, 0);

  uint64_t disruptor_capacity = logger_disruptor_capacity(config ? config->buffer_size : 0);

  // Create disruptor (replaces custom ring buffer)
  disruptor_config_t disruptor_config = {
    .capacity = disruptor_capacity,
    .entry_size = sizeof(mem_buffer_t *),
    .consumer_capacity = 1  // Single consumer
  };

  logger->disruptor = disruptor_create(&disruptor_config);
  if (!logger->disruptor) {
    cmeta_cond_destroy(&logger->wake_cond);
    cmeta_mutex_destroy(&logger->wake_mutex);
    cmeta_mutex_destroy(&logger->sink_mutex);
    free(logger);
    return NULL;
  }

  // Create async payload pool for log entries
  size_t async_pool_size =
      (config && config->pool_size) ? config->pool_size : DEFAULT_POOL_SIZE;
  if (mem_init(&logger->async_pool, async_pool_size) != 0) {
    disruptor_destroy(logger->disruptor);
    cmeta_cond_destroy(&logger->wake_cond);
    cmeta_mutex_destroy(&logger->wake_mutex);
    cmeta_mutex_destroy(&logger->sink_mutex);
    free(logger);
    return NULL;
  }

  if (logger_start_async(logger) != 0) {
    mem_destroy(&logger->async_pool);
    disruptor_destroy(logger->disruptor);
    cmeta_cond_destroy(&logger->wake_cond);
    cmeta_mutex_destroy(&logger->wake_mutex);
    cmeta_mutex_destroy(&logger->sink_mutex);
    free(logger);
    return NULL;
  }

  return logger;
}

void tlog_destroy(tlog_t *logger) {
  if (!logger)
    return;

  tlog_flush(logger);
 
  logger_stop_async(logger);
  disruptor_destroy(logger->disruptor);
  mem_destroy(&logger->async_pool);

  // Flush and destroy sinks
  for (int i = 0; i < logger->sink_count; i++) {
    sink_flush_inner(logger->sinks[i]);
    cmeta_sink_destroy(logger->sinks[i]);
  }

  cmeta_mutex_destroy(&logger->sink_mutex);
  cmeta_cond_destroy(&logger->wake_cond);
  cmeta_mutex_destroy(&logger->wake_mutex);

  // Clear default logger reference before freeing memory.
  cmeta_once(&g_default_logger_mutex_once, init_tlog_globals);
  if (g_default_logger_mutex_init) {
    cmeta_mutex_lock(&g_default_logger_mutex);
    if (g_default_logger == logger) {
      g_default_logger = NULL;
    }
    cmeta_mutex_unlock(&g_default_logger_mutex);
  }

  free(logger);
}

int tlog_add_sink(tlog_t *logger, cmeta_log_sink_t *sink) {
  if (!logger || !sink)
    return -1;

  cmeta_mutex_lock(&logger->sink_mutex);
  if (logger->sink_count >= MAX_SINKS) {
    cmeta_mutex_unlock(&logger->sink_mutex);
    return -1;
  }
  logger->sinks[logger->sink_count++] = sink;
  cmeta_mutex_unlock(&logger->sink_mutex);
  return 0;
}

void tlog_remove_sink(tlog_t *logger, cmeta_log_sink_t *sink) {
  if (!logger || !sink)
    return;

  cmeta_mutex_lock(&logger->sink_mutex);
  for (int i = 0; i < logger->sink_count; i++) {
    if (logger->sinks[i] == sink) {
      for (int j = i; j < logger->sink_count - 1; j++) {
        logger->sinks[j] = logger->sinks[j + 1];
      }
      logger->sink_count--;
      break;
    }
  }
  cmeta_mutex_unlock(&logger->sink_mutex);
}

void tlog_flush(tlog_t *logger) {
  if (!logger)
    return;
 
  // Wait until all logs visible at flush entry have been written.
  //
  // Async stress tests can legitimately take longer than a fixed heuristic
  // budget under scheduler pressure. Returning early here violates the flush
  // contract and makes callback/file sinks observe partial output.
  int64_t published = atomic_load(&logger->logs_published);
  while (atomic_load(&logger->logs_written) < published) {
    cmeta_sleep_ms(1);
  }

  // Flush all sinks
  cmeta_mutex_lock(&logger->sink_mutex);
  for (int i = 0; i < logger->sink_count; i++) {
    sink_flush_inner(logger->sinks[i]);
  }
  cmeta_mutex_unlock(&logger->sink_mutex);
}

// =============================================================================
// Core Logging Functions
// =============================================================================

static void logger_write_to_sinks(tlog_t *logger, const cmeta_log_entry_t *entry) {
  cmeta_mutex_lock(&logger->sink_mutex);
  for (int i = 0; i < logger->sink_count; i++) {
    sink_write_entry(logger->sinks[i], entry);
  }
  cmeta_mutex_unlock(&logger->sink_mutex);
}

void cmeta_log_typed(tlog_t *logger, cmeta_log_level_t level, vstr component,
                     vstr file, int line, vstr pattern, const fmt_arg_t *args, size_t arg_count) {
  if (!logger || !vstr_is_valid(component) || !vstr_is_valid(file) || !vstr_is_valid(pattern) ||
      (!args && arg_count > 0)) return;
  if (level < (cmeta_log_level_t)atomic_load_explicit(&logger->min_level, memory_order_relaxed)) return;

  if (arg_count == 0) {
    cmeta_log_str(logger, level, component, file, line, pattern);
    return;
  }

  int msg_len = fmt_print_v(tls_msg_buf, MAX_MESSAGE_SIZE, pattern, args, arg_count);
  if (msg_len < 0) msg_len = 0;
  if (msg_len >= MAX_MESSAGE_SIZE) msg_len = MAX_MESSAGE_SIZE - 1;
  cmeta_log_str(logger, level, component, file, line,
                vstr_from_buf(tls_msg_buf, (size_t)msg_len));
}

void cmeta_log_str(tlog_t *logger, cmeta_log_level_t level, vstr component, vstr file,
                   int line, vstr message) {
  if (!logger || !vstr_is_valid(component) || !vstr_is_valid(file) || !vstr_is_valid(message)) return;
  if (level < (cmeta_log_level_t)atomic_load_explicit(&logger->min_level, memory_order_relaxed)) return;

  cmeta_log_entry_t entry = {.level = level,
                             .timestamp_ms = cmeta_realtime_ms(),
                             .thread_id = get_cached_tid(),
                             .component = component,
                             .file = file,
                             .line = line,
                             .message = message};
  mem_buffer_t *buffer = async_entry_create(&logger->async_pool, &entry);
  if (!buffer) {
    atomic_fetch_add(&logger->logs_dropped, 1);
    return;
  }
  (void)logger_publish_entry(logger, buffer);
}

// =============================================================================
// Level Control
// =============================================================================

int tlog_set_level_ex(tlog_t *logger, cmeta_log_level_t level) {
  if (!logger || !log_level_is_valid(level)) {
    return -1;
  }
  atomic_store_explicit(&logger->min_level, (int)level, memory_order_relaxed);
  return 0;
}

void tlog_set_level(tlog_t *logger, cmeta_log_level_t level) {
  (void)tlog_set_level_ex(logger, level);
}

cmeta_log_level_t tlog_get_level(const tlog_t *logger) {
  return logger ? (cmeta_log_level_t)atomic_load_explicit(&logger->min_level, memory_order_relaxed)
                : SALTS_LOG_LEVEL_INFO;
}

// =============================================================================
// Statistics
// =============================================================================

uint64_t tlog_get_written(const tlog_t *logger) {
  return logger ? atomic_load(&((tlog_t *)logger)->logs_written) : 0;
}

uint64_t tlog_get_dropped(const tlog_t *logger) {
  return logger ? atomic_load(&((tlog_t *)logger)->logs_dropped) : 0;
}

int tlog_get_queue_size(const tlog_t *logger) {
  int64_t published;
  int64_t written;
  int64_t pending;

  if (!logger) {
    return 0;
  }

  published = atomic_load(&((tlog_t *)logger)->logs_published);
  written = atomic_load(&((tlog_t *)logger)->logs_written);
  pending = published - written;
  if (pending <= 0) {
    return 0;
  }
  if (pending > INT32_MAX) {
    return INT32_MAX;
  }
  return (int)pending;
}

// =============================================================================
// Default Logger (Thread-Safe)
// =============================================================================

static tlog_t *create_default_logger(void) {
  tlog_config_t config = {.min_level = SALTS_LOG_LEVEL_INFO,
                          .pool_size = DEFAULT_POOL_SIZE,
                          .buffer_size = 0};

  tlog_t *logger = tlog_create(&config);
  if (logger) {
    cmeta_log_sink_t *console = cmeta_sink_console_create(NULL);
    if (console) {
      tlog_add_sink(logger, console);
    }
  }
  return logger;
}

void tlog_set_default(tlog_t *logger) {
  cmeta_once(&g_default_logger_mutex_once, init_tlog_globals);
  if (g_default_logger_mutex_init) {
    cmeta_mutex_lock(&g_default_logger_mutex);
    g_default_logger = logger;
    cmeta_mutex_unlock(&g_default_logger_mutex);
  }
}

tlog_t *tlog_get_default(void) {
  tlog_t *logger = NULL;
  tlog_t *created = NULL;

  cmeta_once(&g_default_logger_mutex_once, init_tlog_globals);
  if (!g_default_logger_mutex_init) {
    return NULL;
  }

  cmeta_mutex_lock(&g_default_logger_mutex);
  logger = g_default_logger;
  cmeta_mutex_unlock(&g_default_logger_mutex);
  if (logger) {
    return logger;
  }

  created = create_default_logger();
  if (!created) {
    return NULL;
  }

  cmeta_mutex_lock(&g_default_logger_mutex);
  if (g_default_logger == NULL) {
    g_default_logger = created;
    logger = created;
    created = NULL;
  } else {
    logger = g_default_logger;
  }
  cmeta_mutex_unlock(&g_default_logger_mutex);

  if (created) {
    tlog_destroy(created);
  }
  return logger;
}

tlog_t *tlog_peek_default(void) {
  tlog_t *logger = NULL;
  cmeta_once(&g_default_logger_mutex_once, init_tlog_globals);
  if (!g_default_logger_mutex_init) {
    return NULL;
  }
  cmeta_mutex_lock(&g_default_logger_mutex);
  logger = g_default_logger;
  cmeta_mutex_unlock(&g_default_logger_mutex);
  return logger;
}

// =============================================================================
// Utility Functions
// =============================================================================

const char *cmeta_log_level_name(cmeta_log_level_t level) {
  const char *name = cmeta_log_level_t_to_string(level);
  return name ? name : "UNKNOWN";
}

cmeta_log_level_t cmeta_log_level_from_name(const char *name) {
  const tlog_enum_desc *meta;
  size_t i;
  if (!name) {
    return SALTS_LOG_LEVEL_INFO;
  }

  meta = cmeta_log_level_t_meta();
  for (i = 0; i < meta->count; ++i) {
    if (strcmp(name, meta->items[i].text) == 0) {
      return (cmeta_log_level_t)meta->items[i].value;
    }
  }

  return SALTS_LOG_LEVEL_INFO;
}
