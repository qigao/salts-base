/**
 * @file fmt.h
 * @brief Type-safe formatting using C11 _Generic
 */

#ifndef FMT_H
#define FMT_H

#include "cmeta_api.h"
#include <timeval.h>
#include "fmt_pp.h"
#include "fmt_lexer.h"
#include "tstr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define ENUM_NAME(x) #x

#ifdef __cplusplus
  #include <chrono>
  #include <type_traits>
  #include <utility>
  #define FMT_DETAIL_CAST(type, value) static_cast<type>(value)
#else
  #define FMT_DETAIL_CAST(type, value) ((type)(value))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Type Tags
 * ============================================================================ */

#define FMT_DETAIL_TYPE_SCHEMA(M) \
  M(FMT_TYPE_CHAR, 1, "char", char, c, fmt_arg_char) \
  M(FMT_TYPE_INT, 2, "int", int, i, fmt_arg_int) \
  M(FMT_TYPE_UINT, 3, "uint", unsigned int, u, fmt_arg_uint) \
  M(FMT_TYPE_LONG, 4, "long", long, l, fmt_arg_long) \
  M(FMT_TYPE_ULONG, 5, "ulong", unsigned long, ul, fmt_arg_ulong) \
  M(FMT_TYPE_LLONG, 6, "llong", long long, ll, fmt_arg_llong) \
  M(FMT_TYPE_ULLONG, 7, "ullong", unsigned long long, ull, fmt_arg_ullong) \
  M(FMT_TYPE_DOUBLE, 8, "double", double, f, fmt_arg_double) \
  M(FMT_TYPE_STR, 9, "str", const char *, s, fmt_arg_str) \
  M(FMT_TYPE_PTR, 10, "ptr", const void *, p, fmt_arg_ptr) \
  M(FMT_TYPE_SIZE, 11, "size", size_t, sz, fmt_arg_size) \
  M(FMT_TYPE_BOOL, 12, "bool", int, b, fmt_arg_bool) \
  M(FMT_TYPE_STRV, 13, "strv", vstr, sv, fmt_arg_strv) \
  M(FMT_TYPE_TIME, 14, "time", cmeta_timeval_t, tv, fmt_arg_timeval)


#define FMT_TYPE_ITEM(name, value, text, type, member, constructor) name = value,
typedef enum {
  FMT_TYPE_NONE = 0,
  FMT_DETAIL_TYPE_SCHEMA(FMT_TYPE_ITEM)
} fmt_type_t;
#undef FMT_TYPE_ITEM

/* ============================================================================
 * Tagged Argument Structure
 * ============================================================================ */

/**
 * @brief Public value type for one formatted argument.
 *
 * This struct is intentionally not opaque: fmt() builds stack-allocated argument
 * arrays through C11 _Generic/C++ overloads, and callers may also pass explicit
 * fmt_arg_t arrays to fmt_print(). Construct values through fmt_arg_* helpers so
 * new enum values or union members can be added without changing call sites.
 *
 * Strings and vstr values are non-owning views. The referenced storage must
 * remain valid until fmt_print() returns.
 */
typedef struct {
  fmt_type_t type;
  union {
#define FMT_TYPE_FIELD(name, value, text, type, member, constructor) type member;
    FMT_DETAIL_TYPE_SCHEMA(FMT_TYPE_FIELD)
#undef FMT_TYPE_FIELD
  } val;
} fmt_arg_t;

/* ============================================================================
 * Type Detection Helpers
 * ============================================================================ */

#ifdef __cplusplus
  #define FMT_MAKE_ARG(t, m, v)                                                                    \
    fmt_arg_t arg;                                                                                 \
    arg.type = t;                                                                                  \
    arg.val.m = v;                                                                                 \
    return arg;
#else
  #define FMT_MAKE_ARG(t, m, v) return (fmt_arg_t){t, {.m = v}};
#endif

#define FMT_MAKE_FN(name, value, text, type, member, constructor)                                  \
  static inline fmt_arg_t constructor(type x) { FMT_MAKE_ARG(name, member, x) }

FMT_DETAIL_TYPE_SCHEMA(FMT_MAKE_FN)
#undef FMT_MAKE_FN

/* Native, immutable formatter enum metadata. No CMeta reflection runtime. */
typedef struct fmt_enum_item_desc {
  int64_t value;
  const char *symbol;
  const char *text;
} fmt_enum_item_desc;
typedef struct fmt_enum_desc {
  const char *name;
  const fmt_enum_item_desc *items;
  size_t count;
} fmt_enum_desc;

#define FMT_TYPE_META_ITEM(name, value, text, type, member, constructor) \
  {(int64_t)(name), #name, (text)},

static const fmt_enum_item_desc fmt_type_t__enum_items[] = {
  {(int64_t)FMT_TYPE_NONE, "FMT_TYPE_NONE", "none"},
  FMT_DETAIL_TYPE_SCHEMA(FMT_TYPE_META_ITEM)
};
static const fmt_enum_desc fmt_type_t__enum_meta = {
  "fmt_type_t", fmt_type_t__enum_items,
  sizeof(fmt_type_t__enum_items) / sizeof(fmt_type_t__enum_items[0])
};

static inline const fmt_enum_desc *fmt_type_t_meta(void) {
  return &fmt_type_t__enum_meta;
}
static inline const char *fmt_type_t_to_string(fmt_type_t value) {
  const fmt_enum_desc *meta = fmt_type_t_meta();
  for (size_t i=0; i<meta->count; ++i)
    if (meta->items[i].value == (int64_t)value) return meta->items[i].text;
  return NULL;
}
static inline const char *fmt_type_t_to_symbol(fmt_type_t value) {
  const fmt_enum_desc *meta = fmt_type_t_meta();
  for (size_t i=0; i<meta->count; ++i)
    if (meta->items[i].value == (int64_t)value) return meta->items[i].symbol;
  return NULL;
}
static inline bool fmt_type_t_from_string(const char *text, fmt_type_t *out) {
  const fmt_enum_desc *meta = fmt_type_t_meta();
  if (!text || !out) return false;
  for (size_t i=0; i<meta->count; ++i) {
    if (strcmp(text,meta->items[i].text)==0 ||
        strcmp(text,meta->items[i].symbol)==0) {
      *out = (fmt_type_t)meta->items[i].value;
      return true;
    }
  }
  return false;
}
#undef FMT_TYPE_META_ITEM
#undef FMT_DETAIL_TYPE_SCHEMA

static inline fmt_arg_t fmt_arg_time(time_t x) {
#ifdef __cplusplus
  fmt_arg_t arg;
  arg.type = FMT_TYPE_TIME;
  arg.val.tv.tv_sec = FMT_DETAIL_CAST(int64_t, x);
  arg.val.tv.tv_usec = 0;
  return arg;
#else
  return (fmt_arg_t){FMT_TYPE_TIME, {.tv = {FMT_DETAIL_CAST(int64_t, x), 0}}};
#endif
}

#define FMT_TIME(t) fmt_arg_time(t)

/* fmt_arg_t has one floating representation. Keep the widening explicit so
 * strict C++ consumers do not diagnose the intentional float adapter. */
static inline fmt_arg_t fmt_arg_float(float x) {
  return fmt_arg_double(FMT_DETAIL_CAST(double, x));
}

#undef FMT_MAKE_ARG
#undef FMT_DETAIL_CAST

/* Keep signed/unsigned byte sources on the public character representation
 * without an implicit signedness conversion. Bit-copying preserves the source
 * byte exactly; printf's %c path later consumes that byte representation. */
static inline fmt_arg_t fmt_arg_schar(signed char x) {
  char value;
  memcpy(&value, &x, sizeof(value));
  return fmt_arg_char(value);
}

static inline fmt_arg_t fmt_arg_uchar(unsigned char x) {
  char value;
  memcpy(&value, &x, sizeof(value));
  return fmt_arg_char(value);
}

/* ============================================================================
 * Type Wrappers (C++ Overloads or C11 _Generic)
 * ============================================================================ */

#ifdef __cplusplus
  #define FMT_DETAIL_BOOL_SOURCE bool
#else
  #define FMT_DETAIL_BOOL_SOURCE _Bool
#endif

/* Detection rows are separate from storage rows because several promoted
 * source types intentionally share one fmt_arg_t representation. */
#define FMT_DETAIL_NUMERIC_DETECT_SCHEMA(M) \
  M(char, fmt_arg_char) \
  M(signed char, fmt_arg_schar) \
  M(unsigned char, fmt_arg_uchar) \
  M(short, fmt_arg_int) \
  M(unsigned short, fmt_arg_uint) \
  M(int, fmt_arg_int) \
  M(unsigned int, fmt_arg_uint) \
  M(long, fmt_arg_long) \
  M(unsigned long, fmt_arg_ulong) \
  M(long long, fmt_arg_llong) \
  M(unsigned long long, fmt_arg_ullong) \
  M(float, fmt_arg_float) \
  M(double, fmt_arg_double) \
  M(FMT_DETAIL_BOOL_SOURCE, fmt_arg_bool)

#define FMT_DETAIL_OBJECT_DETECT_SCHEMA(M) \
  M(char *, fmt_arg_str) \
  M(const char *, fmt_arg_str) \
  M(void *, fmt_arg_ptr) \
  M(const void *, fmt_arg_ptr) \
  M(vstr, fmt_arg_strv) \
  M(cmeta_timeval_t, fmt_arg_timeval)

#ifdef __cplusplus
} /* End extern "C" to allow C++ overloading */

/* C++ Overloads for automatic type detection */
#define FMT_DETAIL_CPP_DETECT(source_type, constructor)                                            \
  static inline fmt_arg_t fmt_arg_detect(source_type x) { return constructor(x); }
FMT_DETAIL_NUMERIC_DETECT_SCHEMA(FMT_DETAIL_CPP_DETECT)
FMT_DETAIL_OBJECT_DETECT_SCHEMA(FMT_DETAIL_CPP_DETECT)
#undef FMT_DETAIL_CPP_DETECT
#undef FMT_DETAIL_NUMERIC_DETECT_SCHEMA
#undef FMT_DETAIL_OBJECT_DETECT_SCHEMA
#undef FMT_DETAIL_BOOL_SOURCE

/* Fallback for integral types without an exact overload. size_t is a typedef,
 * so it resolves through its exact underlying unsigned-type overload. */
template <typename T = size_t>
static inline
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, unsigned long>::value &&
                                !std::is_same<T, unsigned long long>::value,
                            fmt_arg_t>::type
    fmt_arg_detect(T x) {
  return fmt_arg_size(x);
}
static inline fmt_arg_t fmt_arg_detect(std::chrono::system_clock::time_point tp) {
  auto dur = tp.time_since_epoch();
  auto sec = std::chrono::duration_cast<std::chrono::seconds>(dur);
  auto usec = std::chrono::duration_cast<std::chrono::microseconds>(dur - sec);
  cmeta_timeval_t tv;
  tv.tv_sec = static_cast<int64_t>(sec.count());
  tv.tv_usec = static_cast<int32_t>(usec.count());
  return fmt_arg_timeval(tv);
}

/* Template for classes with c_str() member (e.g. std::string) */
template <typename T>
static inline auto fmt_arg_detect(const T &x) -> decltype(fmt_arg_str(x.c_str())) {
  return fmt_arg_str(x.c_str());
}

/* Template for classes with data()+size() but no c_str() (e.g. std::string_view) */
namespace __fmt_detail {
  template <typename T, typename = void> struct has_c_str : std::false_type {};
  template <typename T>
  struct has_c_str<T, decltype(void(std::declval<T>().c_str()))> : std::true_type {};
} // namespace __fmt_detail
template <typename T>
static inline auto fmt_arg_detect(const T &x) ->
    typename std::enable_if<!__fmt_detail::has_c_str<T>::value,
                            decltype(x.data(), x.size(), fmt_arg_t{})>::type {
  vstr sv;
  sv.data = x.data();
  sv.len = x.size();
  return fmt_arg_strv(sv);
}

/* Template catches all other object pointer types. */
template <typename T> static inline fmt_arg_t fmt_arg_detect(T *x) {
  return fmt_arg_ptr(static_cast<const void *>(x));
}

/* Template for enum types: cast to underlying integer type */
template <typename T>
static inline typename std::enable_if<std::is_enum<T>::value, fmt_arg_t>::type fmt_arg_detect(T x) {
  using U = typename std::underlying_type<T>::type;
  // Promote char/uchar sized enums to int so they print as numbers, not characters
  using P = typename std::conditional<(sizeof(U) == 1), int, U>::type;
  return fmt_arg_detect(static_cast<P>(x));
}

  #define FMT_ARG(x) fmt_arg_detect(x)

extern "C" { /* Re-open extern "C" */

#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define FMT_HAS_GENERIC 1

#ifdef _MSC_VER

    /* MSVC accepts a bounded association set, so compose two schema-generated
     * selectors while retaining the same pointer fallback. */
    #define FMT_DETAIL_C_ASSOC(source_type, constructor) source_type: constructor,
    #define FMT_DETAIL_C_OBJECT_SELECTOR(x)                                                    \
      _Generic((x), FMT_DETAIL_OBJECT_DETECT_SCHEMA(FMT_DETAIL_C_ASSOC)                 \
               default: fmt_arg_ptr)
    #define FMT_ARG(x)                                                                             \
      (_Generic((x), FMT_DETAIL_NUMERIC_DETECT_SCHEMA(FMT_DETAIL_C_ASSOC)                    \
                default: FMT_DETAIL_C_OBJECT_SELECTOR(x)))(x)

#else
    /* Standard C11 _Generic */
    #define FMT_DETAIL_C_ASSOC(source_type, constructor) source_type: constructor,
    #define FMT_ARG(x)                                                                             \
      _Generic((x),                                                                                \
               FMT_DETAIL_NUMERIC_DETECT_SCHEMA(FMT_DETAIL_C_ASSOC)                        \
               FMT_DETAIL_OBJECT_DETECT_SCHEMA(FMT_DETAIL_C_ASSOC)                         \
               default: fmt_arg_ptr)(x)
  #endif
#else
  #define FMT_HAS_GENERIC 0
  /* Fallback: use uintptr_t cast for absolute safety on pointer/long conversion */
  #define FMT_ARG(x) fmt_arg_llong((long long)(uintptr_t)(x))
#endif

/* ============================================================================
 * Non-empty formatted argument lists
 * ============================================================================ */

#define FMT_DETAIL_ARG_ITEM(arg, ignored) FMT_ARG(arg),
#define FMT_ARG_COUNT(...) FMT_PP_NARG(__VA_ARGS__)

#ifndef __cplusplus
  #define FMT_ARGS(...)                                                                            \
    ((fmt_arg_t[]){ FMT_PP_FOR_EACH(FMT_DETAIL_ARG_ITEM, ~, __VA_ARGS__) })
#endif

/* ============================================================================
 * Type-Safe Formatting Function
 * ============================================================================ */

/**
 * @brief Format a string using typed arguments
 *
 * @param buf      Output buffer
 * @param size     Buffer size
 * @param fmt      Format string with {} placeholders
 * @param args     Array of typed arguments
 * @param arg_count Number of arguments
 * @return Number of characters written, or 0 for invalid input or backend failure.
 *
 * A specifier may contain printf flags, a decimal width, a decimal precision,
 * an optional type-compatible length, and an optional conversion. Dynamic
 * width/precision (`*`), unknown conversions, and lengths incompatible with
 * the tagged argument are rejected before calling the printf backend.
 *
 * Error conditions: NULL buf, zero size, NULL fmt, args == NULL with
 * arg_count > 0, or an invalid/incompatible specifier. On valid buf/size,
 * failures leave buf as an empty string.
 */
SALTS_C_API int fmt_print_v(char *buf, size_t size, vstr pattern, const fmt_arg_t *args,
                          size_t arg_count);
SALTS_C_API int fmt_print(char *buf, size_t size, const char *fmt, const fmt_arg_t *args,
                        size_t arg_count);

#define fmt_text(buf, size, text) fmt_print((buf), (size), (text), NULL, 0U)

#ifndef __cplusplus
  #define fmt(buf, size, pattern, ...)                                                             \
    fmt_print((buf), (size), (pattern), FMT_ARGS(__VA_ARGS__),                                    \
              (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#endif

/**
 * @brief Append formatted content directly to tstr.
 *
 * This is the dynamic-string backend for fmt formatting. It avoids fixed-size
 * temporary buffers by growing the render buffer until fmt_print() can produce
 * the full output, then appends that output to s.
 *
 * @param s         Existing tstr, or NULL to create one.
 * @param fmt       Format string with {} placeholders.
 * @param args      Array of typed arguments.
 * @param arg_count Number of arguments.
 * @return Updated tstr. Callers must assign the return value. Invalid input
 *         or an incompatible specifier leaves the existing string unchanged.
 */
SALTS_C_API tstr fmt_print_tstr_v(tstr s, vstr pattern, const fmt_arg_t *args,
                                 size_t arg_count);
SALTS_C_API tstr fmt_print_tstr(tstr s, const char *fmt, const fmt_arg_t *args, size_t arg_count);

/* ============================================================================
 * tstr Integration
 * ============================================================================ */

static inline tstr tstr_cat_typed_impl(tstr s, const char *format, const fmt_arg_t *args,
                                       size_t count) {
  return fmt_print_tstr(s, format, args, count);
}

#ifndef __cplusplus
  #define tstr_cat_typed(s, format, ...)                                                           \
    tstr_cat_typed_impl((s), (format), FMT_ARGS(__VA_ARGS__),                                     \
                        (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#endif

static inline tstr tstr_format_typed_impl(const char *format, const fmt_arg_t *args,
                                          size_t count) {
  return fmt_print_tstr(tstr_new(), format, args, count);
}

#ifndef __cplusplus
  #define tstr_format(format, ...)                                                                 \
    tstr_format_typed_impl((format), FMT_ARGS(__VA_ARGS__),                                       \
                           (size_t)FMT_ARG_COUNT(__VA_ARGS__))

  #define tstr_append_format(s, format, ...)                                                       \
    tstr_cat_typed_impl((s), (format), FMT_ARGS(__VA_ARGS__),                                     \
                        (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#endif

#ifdef __cplusplus
} /* End extern "C" */

/* ============================================================================
 * Custom Formatter Support (ADL-based)
 * ============================================================================ */

/**
 * @brief Buffer for custom formatters - wraps char* with position tracking
 */
struct fmt_buffer_t {
  char *data;
  size_t size;
  size_t pos;

  fmt_buffer_t(char *d, size_t s) : data(d), size(s), pos(0) {}

  void write(const char *str, size_t len) {
    if (pos + len < size) {
      memcpy(data + pos, str, len);
      pos += len;
      data[pos] = '\0';
    }
  }

  void write(const char *str) { write(str, strlen(str)); }

  template <typename... Args> void print(const char *format, const Args &...args) {
    if (pos < size) {
      int n = fmt_cpp_wrapper(data + pos, size - pos, format, args...);
      if (n > 0) pos += static_cast<size_t>(n);
    }
  }
};

/**
 * @brief SFINAE helper to detect ADL fmt_format(fmt_buffer_t&, const T&)
 */
namespace __fmt_detail {
  template <typename T, typename = void> struct has_adl_format : std::false_type {};

  template <typename T>
  struct has_adl_format<T, decltype(void(fmt_format(std::declval<fmt_buffer_t &>(),
                                                    std::declval<const T &>())))> : std::true_type {
  };
} // namespace __fmt_detail

/**
 * @brief fmt_arg_detect for types with ADL fmt_format()
 *
 * Usage: Define in same namespace as your type:
 *   void fmt_format(fmt_buffer_t& buf, const YourType& val) {
 *       buf.print("{}:{}", val.field1, val.field2);
 *   }
 */
template <typename T>
static inline auto fmt_arg_detect(const T &x) -> typename std::enable_if<
    __fmt_detail::has_adl_format<T>::value && !__fmt_detail::has_c_str<T>::value, fmt_arg_t>::type {
  thread_local char buf[512];
  fmt_buffer_t fb(buf, sizeof(buf));
  fmt_format(fb, x);
  vstr sv;
  sv.data = buf;
  sv.len = fb.pos;
  return fmt_arg_strv(sv);
}

/**
 * @brief C++ Helper for type-safe formatting
 */
template <typename... Args>
inline int fmt_cpp_wrapper(char *buf, size_t size, const char *fmt, const Args &...args) {
  const fmt_arg_t arg_array[] = {FMT_ARG(args)..., {FMT_TYPE_NONE, {}}};
  return fmt_print(buf, size, fmt, arg_array, sizeof...(Args));
}

  #define fmt(buf, size, pattern, ...) fmt_cpp_wrapper((buf), (size), (pattern), __VA_ARGS__)

template <typename... Args>
inline tstr tstr_cat_typed_cpp(tstr s, const char *format, const Args &...args) {
  const fmt_arg_t arg_array[] = {FMT_ARG(args)..., {FMT_TYPE_NONE, {}}};
  return fmt_print_tstr(s, format, arg_array, sizeof...(Args));
}

  #define tstr_cat_typed(s, format, ...) tstr_cat_typed_cpp((s), (format), __VA_ARGS__)

template <typename... Args>
inline tstr tstr_format_typed_cpp(const char *format, const Args &...args) {
  const fmt_arg_t arg_array[] = {FMT_ARG(args)..., {FMT_TYPE_NONE, {}}};
  return fmt_print_tstr(tstr_new(), format, arg_array, sizeof...(Args));
}

  #define tstr_format(format, ...) tstr_format_typed_cpp((format), __VA_ARGS__)
  #define tstr_append_format(s, format, ...) tstr_cat_typed_cpp((s), (format), __VA_ARGS__)
#endif

#endif /* FMT_H */
