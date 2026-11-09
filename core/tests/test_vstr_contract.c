#include <core/vstr.h>
#include <tinytest.h>
#include <stddef.h>
#include <string.h>

_Static_assert(offsetof(vstr, data) == 0, "borrowed data starts at byte zero");
_Static_assert(offsetof(vstr, len) == sizeof(const char *),
    "view contains only pointer and length");
_Static_assert(sizeof(vstr) == sizeof(const char *) + sizeof(size_t),
    "view has no hidden reflection metadata or owner");

spec("Core borrowed vstr") {
  it("preserves pointer-size layout and validates borrowed views") {
    const char not_terminated[3] = {'a', '\0', 'b'};
    const vstr view = vstr_from_buf(not_terminated, sizeof(not_terminated));
    const vstr empty = vstr_from_cstr(NULL);
    const vstr invalid = vstr_from_buf(NULL, 2u);
    const vstr text = vstr_from_cstr("abc");

    check(vstr_is_valid(view));
    check(view.data == not_terminated);
    check(view.len == sizeof(not_terminated));
    check(memcmp(view.data, not_terminated, view.len) == 0);
    check(vstr_is_valid(empty));
    check(empty.len == 0u);
    check(!vstr_is_valid(invalid));
    check(invalid.len == 2u);
    check(vstr_is_valid(text));
    check(text.len == 3u);
  }

  it("finds counted bytes and substrings across vector boundaries") {
    char storage[112];
    const char pattern[] = {'x', '\0', (char)0x80};
    for (size_t alignment = 0u; alignment < 16u; ++alignment) {
      char *data = storage + alignment;
      for (size_t length = 0u; length <= 80u; ++length) {
        memset(data, 'x', length);
        const vstr view = vstr_from_buf(data, length);
        check_equal(vstr_find_char(view, '\0'), VSTR_NPOS);
        check_equal(vstr_find(view, vstr_from_buf(pattern, sizeof(pattern))), VSTR_NPOS);
        if (length >= sizeof(pattern)) {
          const size_t expected = length - sizeof(pattern);
          memcpy(data + expected, pattern, sizeof(pattern));
          check_equal(vstr_find(view, vstr_from_buf(pattern, sizeof(pattern))), expected);
          check_equal(vstr_find_char(view, '\0'), length - 2u);
          check_equal(vstr_find_char(view, (char)0x80), length - 1u);
        }
      }
    }
  }

  it("returns the first binary delimiter for small and general sets") {
    char storage[112];
    const char delimiters[] = {';', '\0', '\r', (char)0x80, '\n', '\t', '|', ','};
    for (size_t alignment = 0u; alignment < 16u; ++alignment) {
      char *data = storage + alignment;
      for (size_t length = 0u; length <= 80u; ++length) {
        for (size_t count = 1u; count <= sizeof(delimiters); ++count) {
          const vstr view = vstr_from_buf(data, length);
          const vstr set = vstr_from_buf(delimiters, count);
          memset(data, 'x', length);
          check_equal(vstr_find_any(view, set), VSTR_NPOS);
          if (length != 0u) {
            const size_t expected = length / 2u;
            data[expected] = delimiters[count - 1u];
            data[length - 1u] = delimiters[0];
            check_equal(vstr_find_any(view, set), expected);
          }
        }
      }
    }
    check_equal(vstr_find_any(vstr_from_cstr("x;y"), vstr_from_cstr(";;;;")), 1u);
  }

  it("preserves empty and invalid search semantics") {
    const vstr empty = vstr_from_buf(NULL, 0u);
    const vstr invalid = vstr_from_buf(NULL, 1u);
    check_equal(vstr_find(empty, empty), 0u);
    check_equal(vstr_find(invalid, empty), 0u);
    check_equal(vstr_find(empty, vstr_from_cstr("x")), VSTR_NPOS);
    check_equal(vstr_find(vstr_from_cstr("x"), invalid), VSTR_NPOS);
    check_equal(vstr_find_char(empty, '\0'), VSTR_NPOS);
    check_equal(vstr_find_char(invalid, 'x'), VSTR_NPOS);
    check_equal(vstr_find_any(invalid, vstr_from_cstr("x")), VSTR_NPOS);
    check_equal(vstr_find_any(vstr_from_cstr("x"), invalid), VSTR_NPOS);
    check_equal(vstr_find_any(vstr_from_cstr("x"), empty), VSTR_NPOS);
  }
}
