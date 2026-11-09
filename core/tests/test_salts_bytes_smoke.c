#include <salts_bytes.h>
#include <error_codes.h>
#include <tinytest.h>
#include <string.h>

spec("Original counted bytes accumulator") {
  it("preserves fixed capacity, bounded append, consume, view and reset") {
    salts_bytes_t b = salts_bytes_INIT;
    salts_bytes_view_t view = {0};
    check(salts_bytes_init(&b, 6) == SALTS_OK);
    check(salts_bytes_append(&b, "abc", 3) == SALTS_OK &&
          salts_bytes_append(&b, "def", 3) == SALTS_OK);
    check(salts_bytes_size(&b) == 6 && salts_bytes_available(&b) == 0);
    check(salts_bytes_append(&b, "x", 1) == SALTS_ENOSPC);
    check(salts_bytes_consume(&b, 2) == SALTS_OK);
    check(salts_bytes_view(&b, &view) == SALTS_OK && view.size == 4 &&
          memcmp(view.data, "cdef", 4) == 0);
    check(salts_bytes_append(&b, view.data + 1, 3) == SALTS_ENOSPC);
    check(salts_bytes_consume(&b, 4) == SALTS_OK);
    salts_bytes_reset(&b);
    check(salts_bytes_size(&b) == 0);
    salts_bytes_destroy(&b);
  }
}
