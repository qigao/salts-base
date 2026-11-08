#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include "cmeta_api.h"
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#include <cstddef>
#define MEMORY_POOL_DEFAULT_ALIGNMENT alignof(std::max_align_t)
#elif defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#define MEMORY_POOL_DEFAULT_ALIGNMENT __alignof(void*)
#else
#include <stdalign.h>
#define MEMORY_POOL_DEFAULT_ALIGNMENT _Alignof(max_align_t)
#endif

/**
 * @file memory_pool.h
 * @brief Simple arena allocator for zero-allocation parsing
 * 
 * THREAD SAFETY: NOT thread-safe. Use one pool per thread, or add external
 *                synchronization (e.g., mutex) for shared pools.
 * 
 * CONCURRENCY MODEL: Designed for single-threaded parsing loops. For multi-
 *                    threaded scenarios, create one pool per worker thread.
 * 
 * ALIGNMENT: pool_alloc() returns pointers aligned to alignof(max_align_t), suitable
 *            for all scalar and ABI-standard aligned types. For over-aligned
 *            types (e.g., SIMD __m128, __m256), use pool_alloc_aligned().
 */

// Simple and easy Memory pool for zero-allocation parsing
typedef struct MemoryPool {
  uint8_t *pool;
  size_t size;
  size_t used;
  size_t peak_used;     // High water mark
  uint64_t alloc_count; // Statistics
} MemoryPool;

/**
 * @brief Create memory pool
 * @param size Pool size in bytes
 * @return Pointer to pool, or NULL on allocation failure
 * 
 * THREAD SAFETY: Safe to call concurrently (creates independent pools)
 */
SALTS_C_API MemoryPool *pool_create(size_t size);

/**
 * @brief Allocate from pool with default alignment
 * @param pool Memory pool
 * @param size Bytes to allocate
 * @return Pointer aligned to alignof(max_align_t), or NULL if insufficient space
 * 
 * THREAD SAFETY: NOT thread-safe. Use one pool per thread or add external
 *                synchronization for shared pools.
 * 
 * ALIGNMENT: Returned pointer is aligned to alignof(max_align_t). For SIMD types
 *            requiring explicit alignment, use pool_alloc_aligned().
 * 
 * LIFETIME: Allocated memory remains valid until pool_reset(), pool_rewind(),
 *           or pool_destroy() is called.
 * 
 * CORRECT USAGE:
 *   double *d = (double *)pool_alloc(pool, sizeof(double));  // ✅ OK
 *   int *arr = (int *)pool_alloc(pool, 1024);                // ✅ OK
 * 
 * OVER-ALIGNED TYPES:
 *   __m128 *simd = (__m128 *)pool_alloc(pool, sizeof(__m128));  // ⚠️ Under-aligned on some ABIs
 *   // Use pool_alloc_aligned(pool, sizeof(__m128), 16) instead
 */
SALTS_C_API void *pool_alloc(MemoryPool *pool, size_t size);

/**
 * @brief Allocate with explicit alignment requirement
 * @param pool Memory pool
 * @param size Bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Pointer aligned to alignment, or NULL if insufficient space or
 *         alignment is invalid
 * 
 * THREAD SAFETY: NOT thread-safe (same as pool_alloc)
 * 
 * TYPICAL USAGE:
 *   __m128 *v = (__m128 *)pool_alloc_aligned(pool, sizeof(__m128), 16);
 *   __m256 *w = (__m256 *)pool_alloc_aligned(pool, sizeof(__m256), 32);
 */
SALTS_C_API void *pool_alloc_aligned(MemoryPool *pool, size_t size, size_t alignment);

/**
 * @brief Reset pool (reuse memory)
 * @param pool Memory pool
 * 
 * THREAD SAFETY: NOT thread-safe. All outstanding pointers from pool_alloc()
 *                become invalid after reset.
 * 
 * LIFETIME: Invalidates all pointers previously allocated from this pool.
 */
SALTS_C_API void pool_reset(MemoryPool *pool);

/**
 * @brief Get current used bytes
 * @param pool Memory pool
 * @return Bytes currently allocated, or 0 if pool is NULL
 * 
 * THREAD SAFETY: NOT thread-safe (may race with concurrent pool_alloc)
 */
SALTS_C_API size_t pool_get_used(MemoryPool *pool);

/**
 * @brief Get remaining available bytes
 * @param pool Memory pool
 * @return Bytes available for allocation, or 0 if pool is NULL
 * 
 * THREAD SAFETY: NOT thread-safe (may race with concurrent pool_alloc)
 */
SALTS_C_API size_t pool_get_available(MemoryPool *pool);

/**
 * @brief Get peak memory usage
 * @param pool Memory pool
 * @return Maximum bytes ever allocated simultaneously, or 0 if pool is NULL
 * 
 * THREAD SAFETY: NOT thread-safe (may race with pool_alloc updates)
 */
SALTS_C_API size_t pool_get_peak(MemoryPool *pool);

/**
 * @brief Mark current position for stack-style allocations
 * @param pool Memory pool
 * @return Current used offset, or 0 if pool is NULL
 * 
 * THREAD SAFETY: NOT thread-safe
 * 
 * TYPICAL USAGE (stack-style allocation):
 *   size_t mark = pool_mark(pool);
 *   void *temp = pool_alloc(pool, 1024);
 *   // ... use temp ...
 *   pool_rewind(pool, mark);  // Reclaim temp
 */
SALTS_C_API size_t pool_mark(MemoryPool *pool);

/**
 * @brief Rewind pool to previous mark
 * @param pool Memory pool
 * @param mark Position returned by pool_mark()
 * 
 * THREAD SAFETY: NOT thread-safe. All pointers allocated after mark become
 *                invalid after rewind.
 * 
 * LIFETIME: Invalidates all pointers allocated after the mark position.
 */
SALTS_C_API void pool_rewind(MemoryPool *pool, size_t mark);

/**
 * @brief Destroy pool and free all memory
 * @param pool Memory pool
 * 
 * THREAD SAFETY: NOT thread-safe. Caller must ensure no other thread is
 *                accessing the pool.
 * 
 * LIFETIME: Invalidates all pointers allocated from this pool. Pool pointer
 *           itself becomes invalid after this call.
 */
SALTS_C_API void pool_destroy(MemoryPool *pool);

#endif // MEMORY_POOL_H
