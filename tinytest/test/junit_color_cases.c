#include "tinytest.h"

spec("junit color output") {
  it("failing test emits XML-safe message") {
    check_true(0);
  }
}
