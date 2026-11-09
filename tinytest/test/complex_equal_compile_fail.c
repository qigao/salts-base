#include "tinytest.h"

spec("TinyTest CMeta complex rejection") {
  it("rejects complex equality") {
    float _Complex value = 1.0f;

    check_equal(value, value);
  }
}
