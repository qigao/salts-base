#ifndef SALTS_CORE_PLATFORM_ATOMIC_H
#define SALTS_CORE_PLATFORM_ATOMIC_H

#include <salts/api.h>

/* Salts::Core compiled C ABI. These slots are NOT C11 _Atomic or
 * C++ std::atomic objects and must not be copied while being used.
 * The provider owns the operations; weaker requested orders currently
 * execute with sequential consistency. No volatile-only fallback.
 */
#include <stdint.h>
#include <salts/error_codes.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
# define SALTS_CORE_ALIGN8 __declspec(align(8))
#elif defined(__GNUC__) || defined(__clang__) || defined(__TINYC__)
# define SALTS_CORE_ALIGN8 __attribute__((aligned(8)))
#else
# error "An explicit eight-byte alignment implementation is required"
#endif

/* Fixed 32-bit ABI even if clients use -fshort-enums. */
typedef uint32_t atomic_order;
enum {
    SALTS_MEMORY_RELAXED = 0,
    SALTS_MEMORY_CONSUME = 1,
    SALTS_MEMORY_ACQUIRE = 2,
    SALTS_MEMORY_RELEASE = 3,
    SALTS_MEMORY_ACQ_REL = 4,
    SALTS_MEMORY_SEQ_CST = 5
};

typedef struct atomic8 { uint8_t value; } atomic8;
typedef struct atomic16 { uint16_t value; } atomic16;
typedef struct atomic32 { uint32_t value; } atomic32;
typedef struct atomic64 { SALTS_CORE_ALIGN8 uint64_t value; } atomic64;
typedef struct atomic_ptr { void *value; } atomic_ptr;

/* Fail closed on cross-compiler ABI mismatch. Never cast native _Atomic
 * storage to these slot types, even if sizes happen to match.
 */
#define SALTS_PLATFORM_PORTABLE_ABI_VERSION 1u
SALTS_API int platform_portable_abi_verify(
    uint32_t version, uint32_t atomic64_bytes, uint32_t atomic64_alignment,
    uint32_t atomic_pointer_bytes);

/* CAS returns SALTS_OK and sets *exchanged to 0/1; on failure it updates
 * *expected with the observed value. Invalid orders/alignment are errors.
 */
#define SALTS_CORE_DECLARE_ATOMIC(N, T) \
  SALTS_API int atomic##N##_init(atomic##N *slot, T value); \
  SALTS_API int atomic##N##_load(const atomic##N *slot, atomic_order order, T *out); \
  SALTS_API int atomic##N##_store(atomic##N *slot, T value, atomic_order order); \
  SALTS_API int atomic##N##_exchange(atomic##N *slot, T value, atomic_order order, T *old); \
  SALTS_API int atomic##N##_fetch_add(atomic##N *slot, T delta, atomic_order order, T *old); \
  SALTS_API int atomic##N##_cas(atomic##N *slot, T *expected, T desired, atomic_order success, atomic_order failure, int *exchanged)
SALTS_CORE_DECLARE_ATOMIC(8, uint8_t);
SALTS_CORE_DECLARE_ATOMIC(16, uint16_t);
SALTS_CORE_DECLARE_ATOMIC(32, uint32_t);
SALTS_CORE_DECLARE_ATOMIC(64, uint64_t);
#undef SALTS_CORE_DECLARE_ATOMIC

SALTS_API int atomic_ptr_init(atomic_ptr *slot, void *value);
SALTS_API int atomic_ptr_load(const atomic_ptr *slot, atomic_order order, void **out);
SALTS_API int atomic_ptr_store(atomic_ptr *slot, void *value, atomic_order order);
SALTS_API int atomic_ptr_exchange(atomic_ptr *slot, void *value, atomic_order order, void **old);
SALTS_API int atomic_ptr_cas(atomic_ptr *slot, void **expected, void *desired, atomic_order success, atomic_order failure, int *exchanged);
SALTS_API int platform_atomic_thread_fence(atomic_order order);
SALTS_API int platform_atomic_signal_fence(atomic_order order);

#ifdef __cplusplus
}
#endif
#endif
