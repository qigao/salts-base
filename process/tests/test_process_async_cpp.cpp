#include <process_async.h>
#include <shell.h>
#include <tinytest.hpp>
#include <type_traits>

spec("Native asynchronous Process C++ API") {
  it("keeps its public handles and callback types usable from C++17") {
    static_assert(std::is_same_v<decltype(&salts_process_async_start),
        int (*)(salts_process_async *, const salts_process_options_t *, const salts_process_async_config *)>);
    static_assert(std::is_standard_layout_v<salts_shell_options>);
    static_assert(std::is_standard_layout_v<salts_shell_result>);
    salts_process_async process{};
    check_null(process.impl);
  }
}
