#include <fs_async.h>
#include <fs_watch.h>
#include <fs_file.h>
#include <tinytest.hpp>

#include <type_traits>

spec("Native filesystem C++ header") {
    it("preserves the C API function types") {
        static_assert(std::is_same_v<
            decltype(&salts_fs_service_init),
            int (*)(salts_fs_service *, const salts_fs_config *)>);
        static_assert(std::is_same_v<
            decltype(&salts_fs_try_rename),
            salts_fs_submit_result (*)(salts_fs_service *, const char *,
                                       const char *)>);
        static_assert(std::is_same_v<
            decltype(&salts_fs_run_ready),
            int (*)(salts_fs_service *, size_t, size_t *)>);
        salts_fs_service service{};
        salts_fs_watch watch{};
        salts_fs_file file{};
        salts_fs_file_runtime runtime{};
        check_null(service.impl);
        check_null(watch.impl);
        check_null(file.impl);
        check_null(runtime.impl);
    }
}
