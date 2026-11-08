#ifndef SALTS_CORE_RUNTIME_ABI_H
#define SALTS_CORE_RUNTIME_ABI_H

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Pre-stable native ABI 0.1, not compatible with legacy Salts 2.3.
 * Bump the symbol/SONAME when a public record or lifecycle contract breaks. */
#define SALTS_CORE_RUNTIME_ABI_MAJOR UINT32_C(0)
#define SALTS_CORE_RUNTIME_ABI_MINOR UINT32_C(1)
#define SALTS_CORE_RUNTIME_ABI_VERSION UINT32_C(1)

/* Exact linked-epoch admission marker, not a substitute for DSO owner audits. */
void salts_core_runtime_abi_0_1_required(void);
uint32_t salts_core_runtime_abi_version(void);
size_t salts_core_runtime_mem_pool_size(void);
size_t salts_core_runtime_mem_buffer_size(void);
size_t salts_core_runtime_mem_slice_size(void);

#ifdef __cplusplus
}
#endif
#endif /* SALTS_CORE_RUNTIME_ABI_H */
