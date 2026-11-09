#ifndef SALTS_API_H
#define SALTS_API_H

#include <stddef.h>
#include <stdint.h>

/* Public functions and callbacks use the compiler default C calling convention. */

#ifndef SALTS_THREAD_LOCAL
  #if defined(__cplusplus)
    #define SALTS_THREAD_LOCAL thread_local
  #elif defined(_MSC_VER)
    #define SALTS_THREAD_LOCAL __declspec(thread)
  #elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
    #define SALTS_THREAD_LOCAL _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define SALTS_THREAD_LOCAL __thread
  #else
    /* ABI-only clients need no compiler TLS. An actual TLS declaration still
     * requires this C11 keyword; never substitute ordinary global storage.
     */
    #define SALTS_THREAD_LOCAL _Thread_local
  #endif
#endif

/* Core linkage markers. Build-system producer/consumer state is supplied by
 * the Salts::Core target; this header does not inspect CMake internals. */
#ifndef SALTS_API
  #if defined(_WIN32) && defined(SALTS_CORE_BUILD)
    #define SALTS_API __declspec(dllexport)
  #elif defined(_WIN32)
    #define SALTS_API __declspec(dllimport)
  #elif defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_API __attribute__((visibility("default")))
  #else
    #define SALTS_API
  #endif
#endif

#ifndef SALTS_C_API
  #ifdef __cplusplus
    #define SALTS_C_API extern "C" SALTS_API
  #else
    #define SALTS_C_API SALTS_API
  #endif
#endif

/* Native ABI admission remains available through the common Core header.
 * Public records and lifecycle contracts belong to this producer epoch. */
#define SALTS_CORE_RUNTIME_ABI_MAJOR UINT32_C(0)
#define SALTS_CORE_RUNTIME_ABI_MINOR UINT32_C(1)
#define SALTS_CORE_RUNTIME_ABI_VERSION UINT32_C(1)
SALTS_C_API void core_runtime_abi_0_1_required(void);
SALTS_C_API uint32_t core_runtime_abi_version(void);
SALTS_C_API size_t core_runtime_mem_pool_size(void);
SALTS_C_API size_t core_runtime_mem_buffer_size(void);
SALTS_C_API size_t core_runtime_mem_slice_size(void);

#endif /* SALTS_API_H */
