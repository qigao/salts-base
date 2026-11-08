#include "fmt.h"
#include "tinytest.hpp"

#include <chrono>
#include <string>
#include <string_view>

enum class fmt_test_color : unsigned short { blue = 7 };

spec("FMT C++ Tests") {
  it("should expose the same fmt metadata in C++") {
    const fmt_enum_desc *meta = fmt_type_t_meta();
    fmt_type_t parsed = FMT_TYPE_NONE;

    check_not_null(meta);
    check_equal(meta->count, static_cast<size_t>(15));
    check_equal(fmt_type_t_to_symbol(FMT_TYPE_DOUBLE), "FMT_TYPE_DOUBLE");
    check_true(fmt_type_t_from_string("time", &parsed));
    check_equal(parsed, FMT_TYPE_TIME);
  }

  it("should preserve builtin overload mappings") {
    fmt_arg_t float_arg = FMT_ARG(3.25f);
    char mutable_text[] = "hello";
    const char *const_text = "world";
    int value = 10;
    void *ptr = &value;
    const void *const_ptr = &value;
    vstr view = vstr_from_cstr("view");
    cmeta_timeval_t time_value = {42, 7};

    check_equal(FMT_ARG(static_cast<char>('a')).type, FMT_TYPE_CHAR);
    check_equal(FMT_ARG(static_cast<signed char>(-1)).type, FMT_TYPE_CHAR);
    check_equal(FMT_ARG(static_cast<unsigned char>(1)).type, FMT_TYPE_CHAR);
    check_equal(FMT_ARG(static_cast<short>(-2)).type, FMT_TYPE_INT);
    check_equal(FMT_ARG(static_cast<unsigned short>(2)).type, FMT_TYPE_UINT);
    check_equal(FMT_ARG(42).type, FMT_TYPE_INT);
    check_equal(FMT_ARG(42u).type, FMT_TYPE_UINT);
    check_equal(FMT_ARG(42L).type, FMT_TYPE_LONG);
    check_equal(FMT_ARG(42UL).type, FMT_TYPE_ULONG);
    check_equal(FMT_ARG(42LL).type, FMT_TYPE_LLONG);
    check_equal(FMT_ARG(42ULL).type, FMT_TYPE_ULLONG);
    check_equal(float_arg.type, FMT_TYPE_DOUBLE);
    check_equal(float_arg.val.f, 3.25);
    check_equal(FMT_ARG(3.14).type, FMT_TYPE_DOUBLE);
    check_equal(FMT_ARG(true).type, FMT_TYPE_BOOL);
    check_equal(FMT_ARG(mutable_text).type, FMT_TYPE_STR);
    check_equal(FMT_ARG(const_text).type, FMT_TYPE_STR);
    check_equal(FMT_ARG(ptr).type, FMT_TYPE_PTR);
    check_equal(FMT_ARG(const_ptr).type, FMT_TYPE_PTR);
    check_equal(FMT_ARG(view).type, FMT_TYPE_STRV);
    check_equal(FMT_ARG(time_value).type, FMT_TYPE_TIME);
  }

  it("should preserve C++ adapters") {
    std::string text = "owned";
    std::string_view text_view = "borrowed";
    auto time_point = std::chrono::system_clock::time_point(std::chrono::seconds(42));

    check_equal(FMT_ARG(text).type, FMT_TYPE_STR);
    check_equal(FMT_ARG(text_view).type, FMT_TYPE_STRV);
    check_equal(FMT_ARG(fmt_test_color::blue).type, FMT_TYPE_UINT);
    check_equal(FMT_ARG(time_point).type, FMT_TYPE_TIME);
  }

  it("should separate raw text from formatted arguments") {
    char buf[128];

    check_equal(fmt_text(buf, sizeof(buf), "literal"), 7);
    check_equal(buf, "literal");
    check_equal(fmt(buf, sizeof(buf), "{}:{}", 7, "ok"), 4);
    check_equal(buf, "7:ok");
    check_equal(fmt(buf, sizeof(buf), "{}{}{}{}{}{}{}{}", 1, 2, 3, 4, 5, 6, 7, 8), 8);
    check_equal(buf, "12345678");
  }
}
