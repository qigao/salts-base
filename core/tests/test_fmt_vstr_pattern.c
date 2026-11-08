#include "fmt.h"
#include "tinytest.h"
#include <string.h>

spec("fmt vstr pattern contract") {
  it("formats a non-NUL-terminated bounded pattern") {
    const char raw_pattern[] = {'[', '{', '}', ']', 'x'};
    const vstr pattern = vstr_from_buf(raw_pattern, 4U);
    const fmt_arg_t arg = fmt_arg_int(42);
    char buf[16];

    check_equal(fmt_print_v(buf, sizeof(buf), pattern, &arg, 1U), 4);
    check_equal(buf, "[42]");
  }

  it("appends a bounded pattern directly to tstr") {
    const char raw_pattern[] = {'<', '{', '}', '>', 'x'};
    const vstr pattern = vstr_from_buf(raw_pattern, 4U);
    const vstr value = vstr_from_buf("view", 4U);
    const fmt_arg_t arg = fmt_arg_strv(value);
    tstr out = fmt_print_tstr_v(NULL, pattern, &arg, 1U);

    check_not_null(out);
    if (out != NULL) {
      check_equal(tstr_len(out), 6U);
      check_equal(memcmp(out, "<view>", 6U), 0);
      tstr_free(out);
    }
  }
}
