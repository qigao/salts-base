#include <runtime_abi.h>
#include <cmeta_buffer.h>

void salts_core_runtime_abi_0_1_required(void) {
  /* Unique linked epoch contract, never aliased to a different ABI. */
}
uint32_t salts_core_runtime_abi_version(void) {
  return SALTS_CORE_RUNTIME_ABI_VERSION;
}
size_t salts_core_runtime_mem_pool_size(void) { return sizeof(mem_pool_t); }
size_t salts_core_runtime_mem_buffer_size(void) { return sizeof(mem_buffer_t); }
size_t salts_core_runtime_mem_slice_size(void) { return sizeof(mem_slice_t); }
