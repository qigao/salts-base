#ifndef SALTS_OBJECT_POOL_H
#define SALTS_OBJECT_POOL_H

#include "cmeta_api.h"
#include <error_codes.h>
#include <thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file object_pool.h
 * @brief High-performance fixed-size object pool with free-list
 *
 * DESIGN PHILOSOPHY (Linus-style "good taste"):
 * - Simple free-list (intrusive linked list)
 * - Zero overhead when pool is warm (no malloc)
 * - Cache-friendly (contiguous allocation)
 * - Thread-local by default (no locks)
 *
 * USAGE SCENARIO:
 * - Rete Token/Activation allocation
 * - Frequent alloc/free of same-sized objects
 * - Predictable memory usage
 *
 * PERFORMANCE:
 * - Allocation: O(1) from the active bump chunk; O(chunks) when reusing a free slot
 * - Deallocation: O(chunks) ownership lookup, then O(1) bitmap validation/free-list push
 */

typedef struct object_pool_s object_pool_t;

typedef struct {
  size_t object_size;        // Size of each object (must be >= sizeof(void*))
  size_t initial_capacity;   // Initial number of objects to allocate
  size_t max_capacity;       // Maximum capacity (0 = unlimited)
  bool zero_on_alloc;        // Zero memory on allocation
} object_pool_config_t;

/**
 * @brief Create an object pool
 * @param config Pool configuration
 * @return Pool handle, or NULL on failure
 */
SALTS_C_API object_pool_t *object_pool_create(const object_pool_config_t *config);

/** Create with a slot stride respecting alignment. Alignment must be a power
 * of two no greater than object_pool_max_alignment(); NULL rejects unsupported
 * alignment, invalid configuration, overflow or allocation failure.
 * Existing object_pool_create retains pointer-stride alignment semantics. */
SALTS_C_API object_pool_t *object_pool_create_aligned(
    const object_pool_config_t *config, size_t alignment);
/** Maximum fundamental alignment supported by the native allocator ABI. */
SALTS_C_API size_t object_pool_max_alignment(void);

/** Single-threaded membership query; true only for a checked-out slot start.
 * Does not dereference obj. It shares the pool's mutation/lifetime constraints. */
SALTS_C_API bool object_pool_is_allocated(const object_pool_t *pool, const void *obj);

/** True for any byte in live pool storage, including free slots/interiors.
 * Read-only, O(chunks), same single-threaded lifetime constraints as the pool. */
SALTS_C_API bool object_pool_contains(const object_pool_t *pool, const void *obj);

SALTS_C_API void object_pool_destroy(object_pool_t *pool);
SALTS_C_API void *object_pool_alloc(object_pool_t *pool);
SALTS_C_API void object_pool_free(object_pool_t *pool, void *obj);
SALTS_C_API size_t object_pool_allocated_count(const object_pool_t *pool);


/*
 * Address-stable, thread-affine checked-out-slot facade owned by Salts::Core.
 * It manages storage/lease validity only and intentionally knows nothing about
 * CMeta/DataDesc or payload lifecycle callbacks.
 */
typedef struct object_pool_owner_state {
  object_pool_t *storage;
  const struct object_pool_owner_state *self;
  const void *thread;
  bool busy;
} object_pool_owner_state;

typedef struct object_pool_lease {
  void *value;
  object_pool_owner_state *owner;
  const struct object_pool_lease *self;
} object_pool_lease;

static inline int object_pool_owner_check(object_pool_owner_state *pool) {
  if (pool == NULL || pool->self != pool || pool->storage == NULL ||
      pool->thread != cmeta_thread_current_token())
    return SALTS_EINVAL;
  return pool->busy ? SALTS_EBUSY : SALTS_OK;
}

/** Begin/end a synchronous payload operation on the owner thread. No callback
 * runs here. Every successful begin requires one end; raw storage/state mutation
 * during the operation is forbidden. Reentrant owner commands report EBUSY. */
static inline int object_pool_owner_begin(object_pool_owner_state *pool) {
  int status = object_pool_owner_check(pool);
  if (status != SALTS_OK) return status;
  pool->busy = true;
  return SALTS_OK;
}

static inline int object_pool_owner_end(object_pool_owner_state *pool) {
  if (pool == NULL || pool->self != pool || pool->storage == NULL ||
      pool->thread != cmeta_thread_current_token() || !pool->busy)
    return SALTS_EINVAL;
  pool->busy = false;
  return SALTS_OK;
}

static inline int object_pool_owner_init(
    object_pool_owner_state *pool, size_t size, size_t align, size_t capacity) {
  object_pool_config_t config;
  size_t stride, slot_align;

  if (pool == NULL || pool->self != NULL || pool->storage != NULL ||
      capacity == 0 || align == 0 || (align & (align - 1u)) != 0u ||
      align > object_pool_max_alignment())
    return SALTS_EINVAL;

  slot_align = align > sizeof(void *) ? align : sizeof(void *);
  stride = size > sizeof(void *) ? size : sizeof(void *);
  if (stride > SIZE_MAX - (slot_align - 1u))
    return SALTS_ERANGE;
  stride = (stride + slot_align - 1u) & ~(slot_align - 1u);
  if (capacity > SIZE_MAX / stride)
    return SALTS_ERANGE;

  config.object_size = stride;
  config.initial_capacity = capacity;
  config.max_capacity = capacity;
  config.zero_on_alloc = false;

  pool->storage = object_pool_create_aligned(&config, align);
  if (pool->storage == NULL)
    return SALTS_ENOMEM;

  pool->self = pool;
  pool->thread = cmeta_thread_current_token();
  pool->busy = false;
  return SALTS_OK;
}

static inline int object_pool_lease_check(
    object_pool_owner_state *pool, object_pool_lease *lease) {
  int status = object_pool_owner_check(pool);
  if (status != SALTS_OK)
    return status;
  if (lease == NULL || lease->self != lease || lease->owner != pool ||
      !object_pool_is_allocated(pool->storage, lease->value))
    return SALTS_EINVAL;
  return SALTS_OK;
}

static inline int object_pool_owner_acquire(
    object_pool_owner_state *pool, object_pool_lease *lease) {
  void *value;
  int status = object_pool_owner_check(pool);
  if (status != SALTS_OK)
    return status;
  if (lease == NULL || lease->self != NULL || lease->owner != NULL ||
      lease->value != NULL)
    return SALTS_EINVAL;

  value = object_pool_alloc(pool->storage);
  if (value == NULL)
    return SALTS_ENOBUFS;

  lease->owner = pool;
  lease->self = lease;
  lease->value = value;
  return SALTS_OK;
}

static inline void *object_pool_lease_get(
    object_pool_owner_state *pool, object_pool_lease *lease) {
  return object_pool_lease_check(pool, lease) == SALTS_OK
             ? lease->value
             : NULL;
}

static inline int object_pool_owner_release(
    object_pool_owner_state *pool, object_pool_lease *lease) {
  int status = object_pool_lease_check(pool, lease);
  if (status != SALTS_OK)
    return status;
  object_pool_free(pool->storage, lease->value);
  lease->value = NULL;
  lease->owner = NULL;
  lease->self = NULL;
  return SALTS_OK;
}

/** Validate an external move target without borrowing or mutating payload.
 * It must not overlap any byte of this pool's storage, even a free slot.
 * The caller separately guarantees destination size and semantic-zero state. */
static inline int object_pool_owner_check_destination(
    object_pool_owner_state *pool, object_pool_lease *lease,
    const void *destination) {
  int status = object_pool_lease_check(pool, lease);
  if (status != SALTS_OK) return status;
  if (destination == NULL || object_pool_contains(pool->storage, destination))
    return SALTS_EINVAL;
  return SALTS_OK;
}

static inline int object_pool_owner_destroy(object_pool_owner_state *pool) {
  int status = object_pool_owner_check(pool);
  if (status != SALTS_OK)
    return status;
  if (object_pool_allocated_count(pool->storage) != 0u)
    return SALTS_EBUSY;
  object_pool_destroy(pool->storage);
  pool->storage = NULL;
  pool->self = NULL;
  pool->thread = NULL;
  pool->busy = false;
  return SALTS_OK;
}

/**
 * @brief Destroy an object pool
 * @param pool Pool handle
 *
 * WARNING: Does not call destructors. User must ensure all objects are properly cleaned up.
 */
SALTS_C_API void object_pool_destroy(object_pool_t *pool);

/**
 * @brief Allocate an object from the pool
 * @param pool Pool handle
 * @return Pointer to object, or NULL if pool is at max capacity
 *
 * PERFORMANCE: O(1) from the active bump chunk; O(chunks) when reusing a free slot
 */
SALTS_C_API void *object_pool_alloc(object_pool_t *pool);

/**
 * @brief Deallocate an object back to the pool
 * @param pool Pool handle
 * @param obj Pointer to object (must have been allocated from this pool)
 *
 * PERFORMANCE: O(chunks) ownership lookup, then O(1) bitmap validation/free-list push
 * WARNING: Does not call destructor. User must clean up object before returning.
 */
SALTS_C_API void object_pool_free(object_pool_t *pool, void *obj);

/**
 * @brief Get pool statistics
 */
SALTS_C_API size_t object_pool_allocated_count(const object_pool_t *pool);
SALTS_C_API size_t object_pool_free_count(const object_pool_t *pool);
SALTS_C_API size_t object_pool_capacity(const object_pool_t *pool);
SALTS_C_API size_t object_pool_peak_usage(const object_pool_t *pool);

/**
 * @brief Reset pool statistics
 */
SALTS_C_API void object_pool_reset_stats(object_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_OBJECT_POOL_H */
