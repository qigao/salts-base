#include <process_async.h>
#include <shell.h>
#include <tinytest.hpp>
#include <type_traits>

spec("Native asynchronous Process C++ API") {
  it("keeps its public handles and callback types usable from C++17") {
    static_assert(std::is_same_v<decltype(&process_async_start),
        int (*)(process_async *, const process_options_t *, const process_async_config *)>);
    static_assert(std::is_standard_layout_v<shell_options>);
    static_assert(std::is_standard_layout_v<shell_result>);
    process_async process{};
    check_null(process.impl);
  }
}

#include <native_io_accept.h>

suite("NativeIO accept C++ linkage") {
  it("links the installed C accept contract without mangling") {
    uintptr_t transport = 0u;
    check_equal(native_io_internal_take_stream_accept(nullptr, native_io_request{}, &transport), SALTS_EINVAL);
    check_equal(transport, UINTPTR_MAX);
  }
}
