/**
 * @file test_fmt.c
 * @brief Unit tests for fmt.h - C11 _Generic type-safe formatting
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fmt.h"
#include "tinytest.h"

#define BUFFER_SIZE 512

_Static_assert(FMT_TYPE_CHAR == 1 && FMT_TYPE_INT == 2 && FMT_TYPE_UINT == 3 &&
                   FMT_TYPE_LONG == 4 && FMT_TYPE_ULONG == 5 && FMT_TYPE_LLONG == 6 &&
                   FMT_TYPE_ULLONG == 7 && FMT_TYPE_DOUBLE == 8 && FMT_TYPE_STR == 9 &&
                   FMT_TYPE_PTR == 10 && FMT_TYPE_SIZE == 11 && FMT_TYPE_BOOL == 12 &&
                   FMT_TYPE_STRV == 13 && FMT_TYPE_TIME == 14,
               "fmt_type_t values are part of the public fmt_arg_t ABI");

typedef struct fmt_arg_legacy_layout {
  fmt_type_t type;
  union {
    char c;
    int i;
    unsigned int u;
    long l;
    unsigned long ul;
    long long ll;
    unsigned long long ull;
    double f;
    const char *s;
    const void *p;
    size_t sz;
    int b;
    vstr sv;
    cmeta_timeval_t tv;
  } val;
} fmt_arg_legacy_layout;

_Static_assert(sizeof(fmt_arg_t) == sizeof(fmt_arg_legacy_layout),
               "fmt_arg_t size changed");
_Static_assert(_Alignof(fmt_arg_t) == _Alignof(fmt_arg_legacy_layout),
               "fmt_arg_t alignment changed");
_Static_assert(offsetof(fmt_arg_t, type) == offsetof(fmt_arg_legacy_layout, type),
               "fmt_arg_t type offset changed");
_Static_assert(offsetof(fmt_arg_t, val) == offsetof(fmt_arg_legacy_layout, val),
               "fmt_arg_t value offset changed");
#define FMT_ASSERT_VALUE_MEMBER(member) \
  _Static_assert(offsetof(fmt_arg_t, val.member) == \
                     offsetof(fmt_arg_legacy_layout, val.member), \
                 "fmt_arg_t member offset changed: " #member); \
  _Static_assert(sizeof(((fmt_arg_t *)0)->val.member) == \
                     sizeof(((fmt_arg_legacy_layout *)0)->val.member), \
                 "fmt_arg_t member size changed: " #member)
FMT_ASSERT_VALUE_MEMBER(c);
FMT_ASSERT_VALUE_MEMBER(i);
FMT_ASSERT_VALUE_MEMBER(u);
FMT_ASSERT_VALUE_MEMBER(l);
FMT_ASSERT_VALUE_MEMBER(ul);
FMT_ASSERT_VALUE_MEMBER(ll);
FMT_ASSERT_VALUE_MEMBER(ull);
FMT_ASSERT_VALUE_MEMBER(f);
FMT_ASSERT_VALUE_MEMBER(s);
FMT_ASSERT_VALUE_MEMBER(p);
FMT_ASSERT_VALUE_MEMBER(sz);
FMT_ASSERT_VALUE_MEMBER(b);
FMT_ASSERT_VALUE_MEMBER(sv);
FMT_ASSERT_VALUE_MEMBER(tv);
#undef FMT_ASSERT_VALUE_MEMBER

spec("FMT Tests") {
  it("should expose native stable type metadata") {
    const fmt_enum_desc *meta = fmt_type_t_meta();
    const struct {
      fmt_type_t value;
      const char *symbol;
      const char *text;
    } expected[] = {
        {FMT_TYPE_NONE, "FMT_TYPE_NONE", "none"},
        {FMT_TYPE_CHAR, "FMT_TYPE_CHAR", "char"},
        {FMT_TYPE_INT, "FMT_TYPE_INT", "int"},
        {FMT_TYPE_UINT, "FMT_TYPE_UINT", "uint"},
        {FMT_TYPE_LONG, "FMT_TYPE_LONG", "long"},
        {FMT_TYPE_ULONG, "FMT_TYPE_ULONG", "ulong"},
        {FMT_TYPE_LLONG, "FMT_TYPE_LLONG", "llong"},
        {FMT_TYPE_ULLONG, "FMT_TYPE_ULLONG", "ullong"},
        {FMT_TYPE_DOUBLE, "FMT_TYPE_DOUBLE", "double"},
        {FMT_TYPE_STR, "FMT_TYPE_STR", "str"},
        {FMT_TYPE_PTR, "FMT_TYPE_PTR", "ptr"},
        {FMT_TYPE_SIZE, "FMT_TYPE_SIZE", "size"},
        {FMT_TYPE_BOOL, "FMT_TYPE_BOOL", "bool"},
        {FMT_TYPE_STRV, "FMT_TYPE_STRV", "strv"},
        {FMT_TYPE_TIME, "FMT_TYPE_TIME", "time"},
    };
    fmt_type_t parsed = FMT_TYPE_PTR;

    check_not_null(meta);
    check_equal(meta->name, "fmt_type_t");
    check_equal(meta->count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < meta->count; ++i) {
      check_equal(meta->items[i].value, (int64_t)expected[i].value);
      check_equal(meta->items[i].symbol, expected[i].symbol);
      check_equal(meta->items[i].text, expected[i].text);
      check_equal(fmt_type_t_to_symbol(expected[i].value), expected[i].symbol);
      check_equal(fmt_type_t_to_string(expected[i].value), expected[i].text);

      parsed = FMT_TYPE_PTR;
      check_true(fmt_type_t_from_string(expected[i].text, &parsed));
      check_equal(parsed, expected[i].value);
      parsed = FMT_TYPE_PTR;
      check_true(fmt_type_t_from_string(expected[i].symbol, &parsed));
      check_equal(parsed, expected[i].value);
    }

    parsed = FMT_TYPE_BOOL;
    check_false(fmt_type_t_from_string("missing", &parsed));
    check_equal(parsed, FMT_TYPE_BOOL);
    check_false(fmt_type_t_from_string(NULL, &parsed));
    check_false(fmt_type_t_from_string("int", NULL));
    check_null(fmt_type_t_to_string((fmt_type_t)99));
  }

  it("should detect platform support") {
#if FMT_HAS_GENERIC
    printf("  C11 _Generic support: YES\n");
#else
    printf("  C11 _Generic support: NO\n");
#endif
    check(1); // Just informative
  }

  describe("Explicit Type Functions") {
    it("should correctly identify types in fmt_arg_t") {
      /* Integer types */
      fmt_arg_t arg_int = fmt_arg_int(42);
      check_equal(arg_int.type, FMT_TYPE_INT);
      check_equal(arg_int.val.i, 42);

      fmt_arg_t arg_uint = fmt_arg_uint(42u);
      check_equal(arg_uint.type, FMT_TYPE_UINT);
      check_equal(arg_uint.val.u, 42u);

      fmt_arg_t arg_long = fmt_arg_long(42L);
      check_equal(arg_long.type, FMT_TYPE_LONG);
      check_equal((int)arg_long.val.l, 42);

      fmt_arg_t arg_ulong = fmt_arg_ulong(42UL);
      check_equal(arg_ulong.type, FMT_TYPE_ULONG);
      check_equal((unsigned int)arg_ulong.val.ul, 42u);

      fmt_arg_t arg_llong = fmt_arg_llong(42LL);
      check_equal(arg_llong.type, FMT_TYPE_LLONG);
      check_equal((int)arg_llong.val.ll, 42);

      fmt_arg_t arg_ullong = fmt_arg_ullong(42ULL);
      check_equal(arg_ullong.type, FMT_TYPE_ULLONG);
      check_equal((unsigned int)arg_ullong.val.ull, 42u);

      /* Floating point */
      fmt_arg_t arg_double = fmt_arg_double(3.14);
      check_equal(arg_double.type, FMT_TYPE_DOUBLE);
      check(fabs(arg_double.val.f - 3.14) < 0.001);

      /* String */
      const char *str = "hello";
      fmt_arg_t arg_str = fmt_arg_str(str);
      check_equal(arg_str.type, FMT_TYPE_STR);
      check_equal(arg_str.val.s, "hello");

      /* Pointer */
      int x = 10;
      void *ptr = &x;
      fmt_arg_t arg_ptr = fmt_arg_ptr(ptr);
      check_equal(arg_ptr.type, FMT_TYPE_PTR);
      check_equal((const void *)(arg_ptr.val.p), (const void *)(&x));

      /* Character */
      fmt_arg_t arg_char = fmt_arg_char('A');
      check_equal(arg_char.type, FMT_TYPE_CHAR);
      check_equal(arg_char.val.c, 'A');
    }
  }

  describe("FMT_ARG Macro") {
#if FMT_HAS_GENERIC
    it("should preserve the complete C11 builtin type mapping") {
      char mutable_text[] = "hello";
      const char *const_text = "world";
      int value = 10;
      void *ptr = &value;
      const void *const_ptr = &value;
      vstr view = vstr_from_cstr("view");
      cmeta_timeval_t time_value = {42, 7};

      check_equal(FMT_ARG('a').type, FMT_TYPE_INT);
      check_equal(FMT_ARG((char)'a').type, FMT_TYPE_CHAR);
      check_equal(FMT_ARG((signed char)-1).type, FMT_TYPE_CHAR);
      check_equal(FMT_ARG((unsigned char)1).type, FMT_TYPE_CHAR);
      check_equal(FMT_ARG((short)-2).type, FMT_TYPE_INT);
      check_equal(FMT_ARG((unsigned short)2).type, FMT_TYPE_UINT);
      check_equal(FMT_ARG(42).type, FMT_TYPE_INT);
      check_equal(FMT_ARG(42u).type, FMT_TYPE_UINT);
      check_equal(FMT_ARG(42L).type, FMT_TYPE_LONG);
      check_equal(FMT_ARG(42UL).type, FMT_TYPE_ULONG);
      check_equal(FMT_ARG(42LL).type, FMT_TYPE_LLONG);
      check_equal(FMT_ARG(42ULL).type, FMT_TYPE_ULLONG);
      check_equal(FMT_ARG(3.14f).type, FMT_TYPE_DOUBLE);
      check_equal(FMT_ARG(3.14).type, FMT_TYPE_DOUBLE);
      check_equal(FMT_ARG((_Bool)1).type, FMT_TYPE_BOOL);
      check_equal(FMT_ARG("hello").type, FMT_TYPE_STR);
      check_equal(FMT_ARG(mutable_text).type, FMT_TYPE_STR);
      check_equal(FMT_ARG(const_text).type, FMT_TYPE_STR);
      check_equal(FMT_ARG(ptr).type, FMT_TYPE_PTR);
      check_equal(FMT_ARG(const_ptr).type, FMT_TYPE_PTR);
      check_equal(FMT_ARG(&value).type, FMT_TYPE_PTR);
      check_equal(FMT_ARG(view).type, FMT_TYPE_STRV);
      check_equal(FMT_ARG(time_value).type, FMT_TYPE_TIME);
    }

    it("should preserve non-empty argument boundaries") {
      char buf[BUFFER_SIZE];
      int evaluation_count = 0;
      fmt_arg_t *single = FMT_ARGS(++evaluation_count);
      fmt_arg_t *eight = FMT_ARGS(1, 2, 3, 4, 5, 6, 7, 8);
      int formatted;

      check_equal(FMT_ARG_COUNT(1), 1);
      check_equal(FMT_ARG_COUNT(1, 2, 3, 4, 5, 6, 7, 8), 8);
      check_equal(evaluation_count, 1);
      check_equal(single[0].val.i, 1);
      for (int i = 0; i < 8; ++i) {
        check_equal(eight[i].type, FMT_TYPE_INT);
        check_equal(eight[i].val.i, i + 1);
      }

      formatted = fmt_text(buf, sizeof(buf), "literal");
      check_equal(formatted, 7);
      check_equal(buf, "literal");
      formatted = fmt(buf, sizeof(buf), "{}{}{}{}{}{}{}{}", 1, 2, 3, 4, 5, 6, 7, 8);
      check_equal(formatted, 8);
      check_equal(buf, "12345678");
    }
#else
    it("should fallback correctly when _Generic is not available") {
      check_equal(FMT_ARG(42).type, FMT_TYPE_INT);
      check_equal(FMT_ARG(42).val.i, 42);
    }
#endif
  }

  describe("Core Formatting") {
    it("should handle basic formatting") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args1[] = {fmt_arg_int(42)};
      int len = fmt_print(buf, sizeof(buf), "Value: {}", args1, 1);
      check(len > 0);
      check_equal(buf, "Value: 42");

      fmt_arg_t args2[] = {fmt_arg_int(10), fmt_arg_int(20)};
      fmt_print(buf, sizeof(buf), "{} + {} = 30", args2, 2);
      check_equal(buf, "10 + 20 = 30");

      fmt_arg_t args3[] = {fmt_arg_str("World")};
      fmt_print(buf, sizeof(buf), "Hello, {}!", args3, 1);
      check_equal(buf, "Hello, World!");
    }

    it("should handle mixed types") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_str("Alice"), fmt_arg_int(30),
                          fmt_arg_double(95.5)};
      fmt_print(buf, sizeof(buf), "Name: {}, Age: {}, Score: {}", args, 3);
      check_not_null(strstr(buf, "Name: Alice"));
      check_not_null(strstr(buf, "Age: 30"));
      check_not_null(strstr(buf, "Score: "));
    }

    it("should handle format specifiers") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args1[] = {fmt_arg_uint(255)};
      fmt_print(buf, sizeof(buf), "Hex: {:x}", args1, 1);
      check_equal(buf, "Hex: ff");

      fmt_print(buf, sizeof(buf), "Hex: {:X}", args1, 1);
      check_equal(buf, "Hex: FF");

      fmt_arg_t args2[] = {fmt_arg_int(42)};
      fmt_print(buf, sizeof(buf), "Padded: {:05d}", args2, 1);
      check_equal(buf, "Padded: 00042");
    }

    it("should reject incompatible printf specifiers before rendering") {
      char buf[BUFFER_SIZE];
      fmt_arg_t int_arg[] = {fmt_arg_int(42)};
      fmt_arg_t llong_arg[] = {fmt_arg_llong(42)};
      fmt_arg_t uint_arg[] = {fmt_arg_uint(42)};
      fmt_arg_t size_arg[] = {fmt_arg_size((size_t)42)};
      int value = 42;
      fmt_arg_t ptr_arg[] = {fmt_arg_ptr(&value)};

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:lld}", int_arg, 1), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:ld}", llong_arg, 1), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:f}", uint_arg, 1), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:q}", int_arg, 1), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:lld}", size_arg, 1), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "value={:s}", ptr_arg, 1), 0);
      check_equal(buf, "");
    }

    it("should preserve type-compatible explicit lengths") {
      char buf[BUFFER_SIZE];
      fmt_arg_t long_arg[] = {fmt_arg_long(42L)};
      fmt_arg_t llong_arg[] = {fmt_arg_llong(42LL)};
      fmt_arg_t double_arg[] = {fmt_arg_double(42.5)};
      fmt_arg_t size_arg[] = {fmt_arg_size((size_t)42)};
      int value = 42;
      fmt_arg_t ptr_arg[] = {fmt_arg_ptr(&value)};
      char default_ptr[BUFFER_SIZE];

      check_equal(fmt_print(buf, sizeof(buf), "{:ld}", long_arg, 1), 2);
      check_equal(buf, "42");
      check_equal(fmt_print(buf, sizeof(buf), "{:lld}", llong_arg, 1), 2);
      check_equal(buf, "42");
      check_equal(fmt_print(buf, sizeof(buf), "{:.1lf}", double_arg, 1), 4);
      check_equal(buf, "42.5");
      check_equal(fmt_print(buf, sizeof(buf), "{:zu}", size_arg, 1), 2);
      check_equal(buf, "42");
      check_true(fmt_print(default_ptr, sizeof(default_ptr), "{}", ptr_arg, 1) > 0);
      check_equal(fmt_print(buf, sizeof(buf), "{:p}", ptr_arg, 1), (int)strlen(default_ptr));
      check_equal(buf, default_ptr);
    }

    it("should handle large formatted width without truncating to internal temp size") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_int(7)};
      int len = fmt_print(buf, sizeof(buf), "{:300d}", args, 1);
      check_equal(len, 300);
      check_equal(strlen(buf), 300);
      check_equal(buf[299], '7');
      for (int i = 0; i < 299; ++i) {
        check_equal(buf[i], ' ');
      }
    }

    it("should not truncate long strings with string specifiers") {
      char buf[BUFFER_SIZE];
      char long_str[400];
      memset(long_str, 'A', sizeof(long_str) - 1);
      long_str[sizeof(long_str) - 1] = '\0';

      fmt_arg_t args[] = {fmt_arg_str(long_str)};
      int len = fmt_print(buf, sizeof(buf), "{:s}", args, 1);
      check_equal(len, (int)strlen(long_str));
      check_equal(buf, long_str);
    }

    it("should handle escape sequences") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_int(42)};
      fmt_print(buf, sizeof(buf), "Value {{}} is {}", args, 1);
      check_equal(buf, "Value {} is 42");

      fmt_print(buf, sizeof(buf), "{{{{", NULL, 0);
      check_equal(buf, "{{");

      fmt_print(buf, sizeof(buf), "}}}}", NULL, 0);
      check_equal(buf, "}}");
    }

    it("should handle null inputs") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_str(NULL)};
      fmt_print(buf, sizeof(buf), "Value: {}", args, 1);
      check_equal(buf, "Value: (null)");

      check_equal(fmt_print(NULL, 0, "test", NULL, 0), 0);
      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), NULL, NULL, 0), 0);
      check_equal(buf, "");

      strcpy(buf, "stale");
      check_equal(fmt_print(buf, sizeof(buf), "{}", NULL, 1), 0);
      check_equal(buf, "");
    }

    it("should protect against buffer overflow") {
      char small_buf[10];
      fmt_arg_t args[] = {
          fmt_arg_str("This is a very long string that should be truncated")};
      int len = fmt_print(small_buf, sizeof(small_buf), "{}", args, 1);
      check(len < (int)sizeof(small_buf));
      check_equal(small_buf[sizeof(small_buf) - 1], '\0');
      check(strlen(small_buf) < sizeof(small_buf));
    }

    it("should handle missing arguments") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_int(1)};
      fmt_print(buf, sizeof(buf), "{} {} {}", args, 1);
      check_not_null(strstr(buf, "1"));
      check_not_null(strstr(buf, "{}"));

      fmt_print(buf, sizeof(buf), "{} {:x} end", args, 1);
      check_equal(buf, "1 {:x} end");
    }

    it("should handle large numbers") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args1[] = {fmt_arg_llong(9223372036854775807LL)};
      fmt_print(buf, sizeof(buf), "{}", args1, 1);
      check_equal(buf, "9223372036854775807");

      fmt_arg_t args2[] = {fmt_arg_ullong(18446744073709551615ULL)};
      fmt_print(buf, sizeof(buf), "{}", args2, 1);
      check_equal(buf, "18446744073709551615");
    }

    it("should handle pointer formatting") {
      char buf[BUFFER_SIZE];
      int x = 42;
      void *ptr = &x;
      fmt_arg_t args1[] = {fmt_arg_ptr(ptr)};
      fmt_print(buf, sizeof(buf), "Ptr: {}", args1, 1);
      check_not_null(strstr(buf, "Ptr: "));
      check(strlen(buf) > 5);
    }

    it("should handle character formatting") {
      char buf[BUFFER_SIZE];
      fmt_arg_t args[] = {fmt_arg_char('X')};
      fmt_print(buf, sizeof(buf), "Char: {}", args, 1);
      check_equal(buf, "Char: X");

      fmt_arg_t args2[] = {fmt_arg_char('\n')};
      fmt_print(buf, sizeof(buf), "NL:{}", args2, 1);
      check_equal(buf, "NL:\n");
    }

    it("should handle text without placeholders") {
      char buf[BUFFER_SIZE];
      int len = fmt_print(buf, sizeof(buf), "Hello, World!", NULL, 0);
      check_equal(len, 13);
      check_equal(buf, "Hello, World!");
    }
  }

  describe("vstr Formatting") {
    it("should format vstr from cstr") {
      char buf[BUFFER_SIZE];
      vstr name = vstr_from_cstr("alice");
      fmt_arg_t args[] = {fmt_arg_strv(name)};
      fmt_print(buf, sizeof(buf), "user={}", args, 1);
      check_equal(buf, "user=alice");
    }

    it("should format vstr with partial length") {
      char buf[BUFFER_SIZE];
      vstr sub = vstr_from_buf("hello world", 5);
      fmt_arg_t args[] = {fmt_arg_strv(sub)};
      fmt_print(buf, sizeof(buf), "say {}", args, 1);
      check_equal(buf, "say hello");
    }

    it("should format null vstr as (null)") {
      char buf[BUFFER_SIZE];
      vstr empty = vstr_from_buf(NULL, 0);
      fmt_arg_t args[] = {fmt_arg_strv(empty)};
      fmt_print(buf, sizeof(buf), "val={}", args, 1);
      check_equal(buf, "val=(null)");
    }

    it("should clamp oversized vstr lengths to output capacity") {
      char buf[8];
      char source[16] = "abcdefghijkl";
      vstr invalid_len = vstr_from_buf(source, SIZE_MAX);
      fmt_arg_t args[] = {fmt_arg_strv(invalid_len)};
      int len = fmt_print(buf, sizeof(buf), "{}", args, 1);
      check_equal(len, 7);
      check_equal(buf, "abcdefg");
    }

#if FMT_HAS_GENERIC
    it("should auto-detect vstr via _Generic") {
      char buf[BUFFER_SIZE];
      vstr v = vstr_from_cstr("typed");
      check_equal(FMT_ARG(v).type, FMT_TYPE_STRV);
      fmt(buf, sizeof(buf), "v={}", v);
      check_equal(buf, "v=typed");
    }
#endif
  }

  describe("tstr_cat_typed") {
    it("should append formatted content to tstr") {
      tstr s = tstr_new();
      s = tstr_cat_typed(s, "id={} name={}", 42, "alice");
      check_equal(s, "id=42 name=alice");
      tstr_free(s);
    }

    it("should create formatted tstr") {
      tstr s = tstr_format("id={} name={}", 42, "alice");
      check_equal(s, "id=42 name=alice");
      tstr_free(s);
    }

    it("should append through the named format backend") {
      tstr s = tstr_dup("start");
      s = tstr_append_format(s, " id={}", 42);
      check_equal(s, "start id=42");
      tstr_free(s);
    }

    it("should preserve explicit format specifiers in tstr output") {
      tstr s = tstr_format("hex={:08X} dec={:05d} pi={:.2f}", 255u, 42, 3.14159);
      check_equal(s, "hex=000000FF dec=00042 pi=3.14");
      tstr_free(s);
    }

    it("should chain multiple appends") {
      tstr s = tstr_new();
      s = tstr_cat_typed(s, "a={}", 1);
      s = tstr_cat_typed(s, " b={}", 2);
      s = tstr_cat_typed(s, " c={}", 3);
      check_equal(s, "a=1 b=2 c=3");
      tstr_free(s);
    }

    it("should work with vstr argument") {
      tstr s = tstr_new();
      vstr role = vstr_from_cstr("admin");
      s = tstr_cat_typed(s, "role={}", role);
      check_equal(s, "role=admin");
      tstr_free(s);
    }

    it("should format content larger than the old stack buffer") {
      char long_buf[1500];
      memset(long_buf, 'x', sizeof(long_buf));
      vstr long_view = vstr_from_buf(long_buf, sizeof(long_buf));

      tstr s = tstr_format("prefix:{}:suffix", long_view);

      check_equal(tstr_len(s), strlen("prefix:") + sizeof(long_buf) + strlen(":suffix"));
      check_equal(memcmp(s, "prefix:", strlen("prefix:")), 0);
      check_equal(memcmp(s + strlen("prefix:"), long_buf, sizeof(long_buf)), 0);
      check_equal(s + tstr_len(s) - strlen(":suffix"), ":suffix");
      tstr_free(s);
    }

    it("should preserve empty formatted output") {
      tstr s = tstr_format("{}", "");
      check_not_null(s);
      check_equal(tstr_len(s), 0);
      tstr_free(s);
    }
  }

  describe("Time Formatting") {
    it("should format cmeta_timeval_t with milliseconds") {
      char buf[BUFFER_SIZE];
      cmeta_timeval_t tv;
      tv.tv_sec = 1700000000; /* 2023-11-14 22:13:20 UTC */
      tv.tv_usec = 123000;    /* 123ms */
      fmt_arg_t args[] = {fmt_arg_timeval(tv)};
      fmt_print(buf, sizeof(buf), "{}", args, 1);
      check_not_null(strstr(buf, ".123"));
      check_not_null(strstr(buf, "2023"));
      printf("  cmeta_timeval_t: %s\n", buf);
    }

    it("should format time_t without decimal point") {
      char buf[BUFFER_SIZE];
      time_t t = 1700000000;
      fmt_arg_t args[] = {fmt_arg_time(t)};
      fmt_print(buf, sizeof(buf), "{}", args, 1);
      check(strstr(buf, ".") == NULL);
      check_not_null(strstr(buf, "2023"));
      printf("  time_t: %s\n", buf);
    }

    it("should format time_t via FMT_TIME macro") {
      char buf[BUFFER_SIZE];
      time_t t = 1700000000;
      fmt_arg_t args[] = {FMT_TIME(t)};
      fmt_print(buf, sizeof(buf), "{}", args, 1);
      check(strstr(buf, ".") == NULL);
      check_not_null(strstr(buf, "2023"));
    }

    it("should use custom strftime modifier") {
      char buf[BUFFER_SIZE];
      cmeta_timeval_t tv;
      tv.tv_sec = 1700000000;
      tv.tv_usec = 500000;
      fmt_arg_t args[] = {fmt_arg_timeval(tv)};
      fmt_print(buf, sizeof(buf), "{:%H:%M}", args, 1);
      check(strlen(buf) > 0);
      check(strstr(buf, ".") == NULL); /* custom format suppresses ms */
      printf("  custom format: %s\n", buf);
    }

    it("should handle invalid time values deterministically") {
      char buf[BUFFER_SIZE];
      cmeta_timeval_t tv;
      tv.tv_sec = INT64_MAX;
      tv.tv_usec = 0;
      fmt_arg_t args[] = {fmt_arg_timeval(tv)};
      fmt_print(buf, sizeof(buf), "{}", args, 1);
#ifdef _WIN32
      check_equal(buf, "(invalid time)");
#else
      check(strlen(buf) > 0);
#endif
    }

#if FMT_HAS_GENERIC
    it("should auto-detect cmeta_timeval_t via _Generic") {
      char buf[BUFFER_SIZE];
      cmeta_timeval_t tv;
      tv.tv_sec = 1700000000;
      tv.tv_usec = 456000;
      check_equal(FMT_ARG(tv).type, FMT_TYPE_TIME);
      fmt(buf, sizeof(buf), "time={}", tv);
      check_not_null(strstr(buf, "time="));
      check_not_null(strstr(buf, ".456"));
      printf("  auto-detect: %s\n", buf);
    }
#endif
  }
}
