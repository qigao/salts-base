#include <salts.h>
#include <tinytest.h>
spec("Core runtime ABI 0.1") {
  it("retains the required ABI symbol and public native layout") {
    core_runtime_abi_0_1_required();
    check(core_runtime_abi_version() == SALTS_CORE_RUNTIME_ABI_VERSION);
    check(SALTS_CORE_RUNTIME_ABI_MAJOR == 0u);
    check(SALTS_CORE_RUNTIME_ABI_MINOR == 1u);
    check(core_runtime_mem_pool_size() == sizeof(mem_pool_t));
    check(core_runtime_mem_buffer_size() == sizeof(mem_buffer_t));
    check(core_runtime_mem_slice_size() == sizeof(mem_slice_t));
  }
}
