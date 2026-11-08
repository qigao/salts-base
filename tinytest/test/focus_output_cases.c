#include "tinytest.h"

spec("focus output") {
  it_skip("explicit skip") {
    check_true(0);
  }

  fit("focused test") {
    check_true(1);
  }

  it("unfocused test") {
    check_true(1);
  }
}
