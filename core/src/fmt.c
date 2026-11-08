/**
 * @file fmt.c
 * @brief High-performance type-safe formatting implementation
 */

#include "fmt.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Suppress warnings for dynamic printf-compatible format strings. */
#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wformat-nonliteral"
  #pragma GCC diagnostic ignored "-Wformat-contains-nul"
#endif

/* ============================================================================
 * Formatting Helpers
 * ============================================================================ */

enum {
  FMT_TEMP_CAP = 256,
  FMT_FORMAT_CAP = 64,
  FMT_MODIFIER_MAX = 59
};

typedef struct {
  char text[FMT_FORMAT_CAP];
  size_t len;
  int valid;
} fmt_spec_t;

typedef enum {
  FMT_LENGTH_NONE,
  FMT_LENGTH_HH,
  FMT_LENGTH_H,
  FMT_LENGTH_L,
  FMT_LENGTH_LL,
  FMT_LENGTH_J,
  FMT_LENGTH_Z,
  FMT_LENGTH_T,
  FMT_LENGTH_CAP_L
} fmt_length_t;

static inline size_t fmt_available(const char *dst, const char *end) {
  return (dst < end) ? (size_t)(end - dst) : 0;
}

static inline int fmt_copy_to_buffer(char *dst, char *end, const char *src, size_t len) {
  size_t avail = fmt_available(dst, end);
  if (len > avail)
    len = avail;
  if (len > 0)
    memcpy(dst, src, len);
  return (int)len;
}

#define FMT_CALL_LIBC(buf, cap, format, ap) vsnprintf((buf), (cap), (format), (ap))

#define FMT_DEFINE_VWRITE(name, call_backend)                                                        \
  static int fmt_vwrite_##name(char *dst, char *end, const char *format, va_list ap) {              \
    char temp[FMT_TEMP_CAP];                                                                         \
    size_t avail = fmt_available(dst, end);                                                          \
    va_list ap_copy;                                                                                 \
    va_copy(ap_copy, ap);                                                                            \
                                                                                                     \
    int written = call_backend(temp, sizeof(temp), format, ap);                                      \
    if (written <= 0) {                                                                              \
      va_end(ap_copy);                                                                               \
      return 0;                                                                                      \
    }                                                                                                \
                                                                                                     \
    size_t requested = (size_t)written;                                                              \
    size_t copy_len = requested;                                                                     \
    if (copy_len > avail)                                                                            \
      copy_len = avail;                                                                              \
                                                                                                     \
    if (requested < sizeof(temp)) {                                                                  \
      va_end(ap_copy);                                                                               \
      return fmt_copy_to_buffer(dst, end, temp, copy_len);                                           \
    }                                                                                                \
                                                                                                     \
    if (copy_len == 0) {                                                                             \
      va_end(ap_copy);                                                                               \
      return 0;                                                                                      \
    }                                                                                                \
                                                                                                     \
    if (copy_len > (size_t)INT_MAX - 1)                                                              \
      copy_len = (size_t)INT_MAX - 1;                                                                \
    char *dynamic = (char *)malloc(copy_len + 1);                                                    \
    if (!dynamic) {                                                                                  \
      va_end(ap_copy);                                                                               \
      return 0;                                                                                      \
    }                                                                                                \
                                                                                                     \
    int second = call_backend(dynamic, copy_len + 1, format, ap_copy);                               \
    va_end(ap_copy);                                                                                 \
    if (second <= 0) {                                                                               \
      free(dynamic);                                                                                 \
      return 0;                                                                                      \
    }                                                                                                \
                                                                                                     \
    int copied = fmt_copy_to_buffer(dst, end, dynamic, copy_len);                                    \
    free(dynamic);                                                                                   \
    return copied;                                                                                   \
  }

#define FMT_DEFINE_WRITE(name)                                                                       \
  static int fmt_write_##name(char *dst, char *end, const char *format, ...) {                       \
    va_list ap;                                                                                      \
    va_start(ap, format);                                                                            \
    int copied = fmt_vwrite_##name(dst, end, format, ap);                                            \
    va_end(ap);                                                                                      \
    return copied;                                                                                   \
  }

FMT_DEFINE_VWRITE(libc, FMT_CALL_LIBC)
FMT_DEFINE_WRITE(libc)

#undef FMT_DEFINE_WRITE
#undef FMT_DEFINE_VWRITE
#undef FMT_CALL_LIBC

static inline fmt_spec_t fmt_spec_from_token(const char *modifier, size_t mod_len) {
  fmt_spec_t spec;
  spec.text[0] = '\0';
  spec.len = 0;
  spec.valid = (mod_len == 0 || modifier != NULL) && mod_len <= FMT_MODIFIER_MAX;

  if (spec.valid && modifier && mod_len > 0) {
    memcpy(spec.text, modifier, mod_len);
    spec.text[mod_len] = '\0';
    spec.len = mod_len;
  }

  return spec;
}

static inline int fmt_spec_empty(const fmt_spec_t *spec) {
  return !spec || spec->len == 0;
}

static inline int fmt_spec_is_flag(char c) {
  return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0';
}

static inline int fmt_spec_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static inline fmt_length_t fmt_spec_parse_length(const fmt_spec_t *spec, size_t *pos) {
  size_t i = *pos;
  fmt_length_t length = FMT_LENGTH_NONE;

  if (i >= spec->len)
    return length;

  switch (spec->text[i]) {
  case 'h':
    if (i + 1 < spec->len && spec->text[i + 1] == 'h') {
      length = FMT_LENGTH_HH;
      i += 2;
    } else {
      length = FMT_LENGTH_H;
      ++i;
    }
    break;
  case 'l':
    if (i + 1 < spec->len && spec->text[i + 1] == 'l') {
      length = FMT_LENGTH_LL;
      i += 2;
    } else {
      length = FMT_LENGTH_L;
      ++i;
    }
    break;
  case 'j': length = FMT_LENGTH_J; ++i; break;
  case 'z': length = FMT_LENGTH_Z; ++i; break;
  case 't': length = FMT_LENGTH_T; ++i; break;
  case 'L': length = FMT_LENGTH_CAP_L; ++i; break;
  default: break;
  }

  *pos = i;
  return length;
}

static inline const char *fmt_spec_length_text(fmt_length_t length) {
  switch (length) {
  case FMT_LENGTH_HH: return "hh";
  case FMT_LENGTH_H: return "h";
  case FMT_LENGTH_L: return "l";
  case FMT_LENGTH_LL: return "ll";
  case FMT_LENGTH_J: return "j";
  case FMT_LENGTH_Z: return "z";
  case FMT_LENGTH_T: return "t";
  case FMT_LENGTH_CAP_L: return "L";
  default: return "";
  }
}

static inline fmt_length_t fmt_spec_native_length(fmt_type_t type, char conversion) {
  if (conversion == 'c')
    return FMT_LENGTH_NONE;

  switch (type) {
  case FMT_TYPE_LONG:
  case FMT_TYPE_ULONG: return FMT_LENGTH_L;
  case FMT_TYPE_LLONG:
  case FMT_TYPE_ULLONG: return FMT_LENGTH_LL;
  case FMT_TYPE_SIZE: return FMT_LENGTH_Z;
  default: return FMT_LENGTH_NONE;
  }
}

static inline int fmt_spec_length_compatible(fmt_type_t type, fmt_length_t length,
                                             char conversion) {
  if (conversion == 'c')
    return length == FMT_LENGTH_NONE;

  switch (type) {
  case FMT_TYPE_INT:
  case FMT_TYPE_UINT:
    return length == FMT_LENGTH_NONE || length == FMT_LENGTH_H || length == FMT_LENGTH_HH;
  case FMT_TYPE_LONG:
  case FMT_TYPE_ULONG:
    return length == FMT_LENGTH_NONE || length == FMT_LENGTH_L;
  case FMT_TYPE_LLONG:
  case FMT_TYPE_ULLONG:
    return length == FMT_LENGTH_NONE || length == FMT_LENGTH_LL;
  case FMT_TYPE_SIZE:
    return length == FMT_LENGTH_NONE || length == FMT_LENGTH_Z;
  case FMT_TYPE_DOUBLE:
    return length == FMT_LENGTH_NONE || length == FMT_LENGTH_L;
  default:
    return length == FMT_LENGTH_NONE;
  }
}

/* Parses one bounded printf-style modifier in O(spec->len) time and O(1) space.
 * The tagged argument owns its native length, so a format string cannot make
 * libc read a different variadic type. */
static inline int fmt_spec_printf(char *dst, size_t dst_size, const fmt_spec_t *spec,
                                  fmt_type_t type, char default_conversion,
                                  const char *conversion_chars, char *out_conversion) {
  size_t pos = 0;
  size_t prefix_len;
  fmt_length_t parsed_length;
  fmt_length_t output_length;
  const char *length_text;
  size_t length_len;
  char conversion;
  size_t needed;

  if (!dst || dst_size == 0 || !spec || !spec->valid)
    return 0;

  while (pos < spec->len && fmt_spec_is_flag(spec->text[pos]))
    ++pos;
  while (pos < spec->len && fmt_spec_is_digit(spec->text[pos]))
    ++pos;
  if (pos < spec->len && spec->text[pos] == '.') {
    ++pos;
    while (pos < spec->len && fmt_spec_is_digit(spec->text[pos]))
      ++pos;
  }

  prefix_len = pos;
  parsed_length = fmt_spec_parse_length(spec, &pos);
  if (pos < spec->len) {
    conversion = spec->text[pos++];
  } else {
    conversion = default_conversion;
  }

  if (pos != spec->len || !strchr(conversion_chars, conversion) ||
      !fmt_spec_length_compatible(type, parsed_length, conversion))
    return 0;

  output_length = parsed_length == FMT_LENGTH_NONE
                      ? fmt_spec_native_length(type, conversion)
                      : parsed_length;
  length_text = fmt_spec_length_text(output_length);
  length_len = strlen(length_text);
  needed = 1 + prefix_len + length_len + 1;

  if (needed >= dst_size)
    return 0;

  pos = 0;
  dst[pos++] = '%';
  if (prefix_len > 0) {
    memcpy(dst + pos, spec->text, prefix_len);
    pos += prefix_len;
  }
  if (length_len > 0) {
    memcpy(dst + pos, length_text, length_len);
    pos += length_len;
  }
  dst[pos++] = conversion;
  dst[pos] = '\0';
  if (out_conversion)
    *out_conversion = conversion;
  return 1;
}

static inline int format_arg_to_buffer(char *dst, char *end, const fmt_arg_t *arg,
                                       const char *modifier, size_t mod_len) {
  fmt_spec_t spec = fmt_spec_from_token(modifier, mod_len);

  if (!spec.valid)
    return -1;

  switch (arg->type) {
  case FMT_TYPE_CHAR:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'c', "c", NULL))
        return -1;
      return fmt_write_libc(dst, end, fb, arg->val.c);
    }
    return fmt_write_libc(dst, end, "%c", arg->val.c);

  case FMT_TYPE_INT:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'd', "diouxXc", &conversion))
        return -1;
      if (strchr("ouxX", conversion))
        return fmt_write_libc(dst, end, fb, (unsigned int)arg->val.i);
      return fmt_write_libc(dst, end, fb, arg->val.i);
    }
    return fmt_write_libc(dst, end, "%d", arg->val.i);

  case FMT_TYPE_UINT:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'u', "diouxXc", &conversion))
        return -1;
      if (conversion == 'd' || conversion == 'i' || conversion == 'c')
        return fmt_write_libc(dst, end, fb, (int)arg->val.u);
      return fmt_write_libc(dst, end, fb, arg->val.u);
    }
    return fmt_write_libc(dst, end, "%u", arg->val.u);

  case FMT_TYPE_LONG:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'd', "diouxXc", &conversion))
        return -1;
      if (conversion == 'c')
        return fmt_write_libc(dst, end, fb, (int)arg->val.l);
      if (strchr("ouxX", conversion))
        return fmt_write_libc(dst, end, fb, (unsigned long)arg->val.l);
      return fmt_write_libc(dst, end, fb, arg->val.l);
    }
    return fmt_write_libc(dst, end, "%ld", arg->val.l);

  case FMT_TYPE_ULONG:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'u', "ouxXc", &conversion))
        return -1;
      if (conversion == 'c')
        return fmt_write_libc(dst, end, fb, (int)arg->val.ul);
      return fmt_write_libc(dst, end, fb, arg->val.ul);
    }
    return fmt_write_libc(dst, end, "%lu", arg->val.ul);

  case FMT_TYPE_LLONG:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'd', "diouxXc", &conversion))
        return -1;
      if (conversion == 'c')
        return fmt_write_libc(dst, end, fb, (int)arg->val.ll);
      if (strchr("ouxX", conversion))
        return fmt_write_libc(dst, end, fb, (unsigned long long)arg->val.ll);
      return fmt_write_libc(dst, end, fb, arg->val.ll);
    }
    return fmt_write_libc(dst, end, "%lld", arg->val.ll);

  case FMT_TYPE_ULLONG:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      char conversion;
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'u', "ouxXc", &conversion))
        return -1;
      if (conversion == 'c')
        return fmt_write_libc(dst, end, fb, (int)arg->val.ull);
      return fmt_write_libc(dst, end, fb, arg->val.ull);
    }
    return fmt_write_libc(dst, end, "%llu", arg->val.ull);

  case FMT_TYPE_DOUBLE:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'g', "fegEG", NULL))
        return -1;
      return fmt_write_libc(dst, end, fb, arg->val.f);
    }
    return fmt_write_libc(dst, end, "%.17g", arg->val.f);

  case FMT_TYPE_STR: {
    const char *s = arg->val.s ? arg->val.s : "(null)";
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 's', "s", NULL))
        return -1;
      return fmt_write_libc(dst, end, fb, s);
    }
    return fmt_copy_to_buffer(dst, end, s, strlen(s));
  }

  case FMT_TYPE_PTR:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'p', "p", NULL))
        return -1;
      return fmt_write_libc(dst, end, fb, (void *)arg->val.p);
    }
    return fmt_write_libc(dst, end, "%p", (void *)arg->val.p);

  case FMT_TYPE_SIZE:
    if (!fmt_spec_empty(&spec)) {
      char fb[FMT_FORMAT_CAP] = {0};
      if (!fmt_spec_printf(fb, sizeof(fb), &spec, arg->type, 'u', "ouxX", NULL))
        return -1;
      return fmt_write_libc(dst, end, fb, arg->val.sz);
    }
    return fmt_write_libc(dst, end, "%zu", arg->val.sz);

  case FMT_TYPE_BOOL: {
    const char *bstr = arg->val.b ? "true" : "false";
    size_t blen = arg->val.b ? 4 : 5;
    return fmt_copy_to_buffer(dst, end, bstr, blen);
  }

  case FMT_TYPE_STRV: {
    const char *s = arg->val.sv.data ? arg->val.sv.data : "(null)";
    size_t slen = arg->val.sv.data ? arg->val.sv.len : 6;
    return fmt_copy_to_buffer(dst, end, s, slen);
  }

  case FMT_TYPE_TIME: {
    time_t sec = (time_t)arg->val.tv.tv_sec;
    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &sec) != 0) {
      return fmt_copy_to_buffer(dst, end, "(invalid time)", 14);
    }
#else
    if (!localtime_r(&sec, &tm_buf)) {
      return fmt_copy_to_buffer(dst, end, "(invalid time)", 14);
    }
#endif
    char temp[FMT_TEMP_CAP];
    const char *time_fmt = !fmt_spec_empty(&spec) ? spec.text : "%Y-%m-%d %H:%M:%S";
    int written = (int)strftime(temp, sizeof(temp), time_fmt, &tm_buf);
    if (written > 0 && fmt_spec_empty(&spec) && arg->val.tv.tv_usec > 0) {
      int ms = arg->val.tv.tv_usec / 1000;
      written += snprintf(temp + written, sizeof(temp) - (size_t)written, ".%03d", ms);
    }
    if (written <= 0)
      return 0;
    size_t copy_len = (size_t)written;
    if (copy_len >= sizeof(temp))
      copy_len = sizeof(temp) - 1;
    return fmt_copy_to_buffer(dst, end, temp, copy_len);
  }

  default:
    return fmt_write_libc(dst, end, "0x%llx", (unsigned long long)(uintptr_t)arg->val.p);
  }
}

/* ============================================================================
 * Internal re2c Formatting Loop
 * ============================================================================ */

SALTS_C_API int fmt_print_v(char *buf, size_t size, vstr pattern, const fmt_arg_t *args,
                          size_t arg_count) {
  if (!buf || size == 0)
    return 0;
  buf[0] = '\0';
  if ((!pattern.data && pattern.len > 0) || (!args && arg_count > 0))
    return 0;

  const char *cursor = pattern.data ? pattern.data : "";
  const char *format_end = cursor + pattern.len;
  char *dst = buf;
  char *end = buf + size - 1; /* Room for null terminator */
  size_t arg_idx = 0;
  vstr token_view = vstr_from_buf(NULL, 0);

  while (dst < end) {
    fmt_token_t token = fmt_scan_v_n(&cursor, format_end, &token_view);

    switch (token) {
    case FMT_TOKEN_END:
      goto done;

    case FMT_TOKEN_TEXT: {
      size_t len = token_view.len;
      int copied = fmt_copy_to_buffer(dst, end, token_view.data, len);
      dst += copied;
      break;
    }

    case FMT_TOKEN_LBRACE_ESC:
      if (dst < end)
        *dst++ = '{';
      break;

    case FMT_TOKEN_RBRACE_ESC:
      if (dst < end)
        *dst++ = '}';
      break;

    case FMT_TOKEN_PLACEHOLDER:
      if (arg_idx < arg_count) {
        int written = format_arg_to_buffer(dst, end, &args[arg_idx++], NULL, 0);
        if (written < 0)
          goto format_error;
        dst += written;
      } else {
        if (dst < end)
          *dst++ = '{';
        if (dst < end)
          *dst++ = '}';
      }
      break;

    case FMT_TOKEN_SPECIFIER:
      if (arg_idx < arg_count) {
        int written =
            format_arg_to_buffer(dst, end, &args[arg_idx++], token_view.data, token_view.len);
        if (written < 0)
          goto format_error;
        dst += written;
      } else {
        dst += fmt_copy_to_buffer(dst, end, "{:", 2);
        dst += fmt_copy_to_buffer(dst, end, token_view.data, token_view.len);
        dst += fmt_copy_to_buffer(dst, end, "}", 1);
      }
      break;

    case FMT_TOKEN_INVALID:
      // Copy exact content
      {
        size_t len = token_view.len;
        int copied = fmt_copy_to_buffer(dst, end, token_view.data, len);
        dst += copied;
      }
      break;
    }
  }

done:
  if (size > 0) {
    if (dst >= end) {
      buf[size - 1] = '\0';
    } else {
      *dst = '\0';
    }
  }
  return (int)(dst - buf);

format_error:
  buf[0] = '\0';
  return 0;
}

SALTS_C_API int fmt_print(char *buf, size_t size, const char *fmt, const fmt_arg_t *args,
                        size_t arg_count) {
  if (!fmt) {
    if (buf && size > 0)
      buf[0] = '\0';
    return 0;
  }
  return fmt_print_v(buf, size, vstr_from_cstr(fmt), args, arg_count);
}

SALTS_C_API tstr fmt_print_tstr_v(tstr s, vstr pattern, const fmt_arg_t *args,
                                  size_t arg_count) {
  enum { FMT_TSTR_STACK_CAP = 256 };
  char stack[FMT_TSTR_STACK_CAP];
  char *buf = stack;
  size_t cap = FMT_TSTR_STACK_CAP;

  if (!s)
    s = tstr_new();
  if ((!pattern.data && pattern.len > 0) || (!args && arg_count > 0))
    return s;

  for (;;) {
    int written = fmt_print_v(buf, cap, pattern, args, arg_count);
    if (written < 0)
      break;

    if ((size_t)written < cap - 1) {
      if (written > 0)
        s = tstr_cat_len(s, buf, (size_t)written);
      break;
    }

    if (buf != stack)
      free(buf);
    if (cap > (SIZE_MAX / 2))
      return s;

    cap *= 2;
    buf = (char *)malloc(cap);
    if (!buf)
      return s;
  }

  if (buf != stack)
    free(buf);
  return s;
}

SALTS_C_API tstr fmt_print_tstr(tstr s, const char *fmt, const fmt_arg_t *args,
                                size_t arg_count) {
  if (!fmt) {
    if (!s)
      s = tstr_new();
    return s;
  }
  return fmt_print_tstr_v(s, vstr_from_cstr(fmt), args, arg_count);
}

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
