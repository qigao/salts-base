#include <salts/bytes.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>

spec("Original counted bytes accumulator") {
  it("preserves fixed capacity, bounded append, consume, view and reset") {
    bytes_t b = bytes_INIT;
    bytes_view_t view = {0};
    check(bytes_init(&b, 6) == SALTS_OK);
    check(bytes_append(&b, "abc", 3) == SALTS_OK &&
          bytes_append(&b, "def", 3) == SALTS_OK);
    check(bytes_size(&b) == 6 && bytes_available(&b) == 0);
    check(bytes_append(&b, "x", 1) == SALTS_ENOSPC);
    check(bytes_consume(&b, 2) == SALTS_OK);
    check(bytes_view(&b, &view) == SALTS_OK && view.size == 4 &&
          memcmp(view.data, "cdef", 4) == 0);
    check(bytes_append(&b, view.data + 1, 3) == SALTS_ENOSPC);
    check(bytes_consume(&b, 4) == SALTS_OK);
    bytes_reset(&b);
    check(bytes_size(&b) == 0);
    bytes_destroy(&b);
  }
}
