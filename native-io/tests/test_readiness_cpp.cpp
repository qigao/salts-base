#include <readiness.h>
#include <tinytest.hpp>
#include <type_traits>

spec("Standalone readiness C++ linkage") {
  it("uses the NativeIO static API without Core DLL import declarations") {
    static_assert(std::is_standard_layout_v<readiness_reactor>);
    static_assert(std::is_standard_layout_v<readiness_registration>);
    readiness_reactor reactor{};
    const readiness_config config{1u, 1u};
    const auto invalid = static_cast<readiness_backend_kind>(0);
    check(!readiness_backend_supported(invalid));
    check_equal(readiness_reactor_init_kind(&reactor, &config, invalid), SALTS_ENOTSUP);
    check_null(reactor.impl);
  }
}
