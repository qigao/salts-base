#ifndef SALTS_MEM_H
#define SALTS_MEM_H

#include "cmeta_api.h"
#ifdef __cplusplus
  #include <atomic>
  #define ATOMIC_SIZE_T std::atomic<size_t>
  #define ATOMIC_UINT32_T std::atomic<uint32_t>
#else
  #include <stdatomic.h>
  #define ATOMIC_SIZE_T _Atomic size_t
  #define ATOMIC_UINT32_T _Atomic uint32_t
#endif
#include <thread.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory buffer size constants */
#define MEM_RECV_BUFFER_SIZE 8192
#define MEM_SEND_BUFFER_SIZE 8192
#define MEM_HANDSHAKE_BUFFER_SIZE 4096
#define MEM_FRAME_BUFFER_SIZE 65536
#define MEM_FRAGMENT_BUFFER_SIZE (1024 * 1024)

/* Pool configuration constants */
#define MEM_WRITE_IOV_CAPACITY 64
#define MEM_SEND_OP_POOL_CAPACITY 256

/* Arena initial sizes (pre-allocate to reduce fragmentation) */
#define MEM_ARENA_CONTEXT_INIT_SIZE (128 * 1024) /* 128KB for coro_context */
#define MEM_ARENA_SERVER_INIT_SIZE (256 * 1024)  /* 256KB for servers (more connections) */
#define MEM_ARENA_CLIENT_INIT_SIZE (64 * 1024)   /* 64KB for clients */
#define MEM_ARENA_POOL_INIT_SIZE (64 * 1024)     /* 64KB for connection pools */
#define MEM_BUFFER_RECYCLE_LIMIT 32U             /* Per-pool cached internal buffers */

typedef struct mem_pool_s mem_pool_t;
typedef struct mem_buffer_s mem_buffer_t;
typedef struct mem_slice_s mem_slice_t;

#ifndef MEM_ASSERT
  #define MEM_ASSERT(x) assert(x)
#endif

struct mem_pool_s {
  void *slabs[9];
  ATOMIC_SIZE_T total_allocated;
  ATOMIC_SIZE_T total_used;
  void *oversize_head;
  mem_buffer_t *recycle_head; /* Private cache metadata; retained for ABI stability. */
  ATOMIC_SIZE_T recycle_count; /* Private compatibility storage; use the query API. */
};

struct mem_buffer_s {
  char *data;
  size_t capacity;
  size_t used;
  ATOMIC_UINT32_T ref_count;
  mem_pool_t *pool;
  struct mem_buffer_s *next;
  int is_external;
  int is_oversized; /* 1=malloc, 0=slab */
  void (*free_cb)(void *, void *);
  void *free_user_data;
};

struct mem_slice_s {
  char *data;
  size_t length;
  mem_buffer_t *buffer;
};

/*
 * Ownership model:
 * - mem_pool_t owns allocation storage and must outlive pool-managed buffers
 *   borrowed from it.
 * - Allocation and buffer retain/release operations are thread-safe. Pool
 *   reset, trim, and destroy require exclusive lifecycle ownership.
 * - mem_buffer_t is a shared buffer handle with an atomic reference count.
 *   mem_buffer_retain()/mem_buffer_release() are the preferred ownership names.
 *   mem_ref()/mem_unref()/mem_release() remain as compatibility aliases.
 * - mem_slice_t is a borrowed view that retains its source buffer until
 *   mem_slice_release().
 */

/**
 * @brief Initialize slab pool
 * @param pool Pool structure
 * @param initial_size Ignored (for API compatibility)
 * @return 0 on success, -1 on failure
 */
SALTS_C_API int mem_init(mem_pool_t *pool, size_t initial_size);

/**
 * @brief Get global shared slab pool (lazy init)
 * @return Pointer to global pool
 */
SALTS_C_API mem_pool_t *mem_global(void);

/**
 * @brief Destroy pool and free all memory
 * @param pool Pool structure
 */
SALTS_C_API void mem_destroy(mem_pool_t *pool);

/**
 * @brief Return a pool allocation to its owning slab or oversize allocator
 * @param pool Pool that produced ptr
 * @param ptr Allocation returned by mem_alloc(), or NULL
 *
 * Small slab blocks first enter a bounded pool-owned atomic magazine. Full
 * magazines return blocks to the existing locked slab free-list.
 */
SALTS_C_API void mem_free(mem_pool_t* pool, void* ptr);
/**
 * @brief Reset pool (mark all blocks as free while retaining slabs for reuse)
 * @param pool Pool structure
 *
 * Call mem_trim() after reset when retained slab memory should be returned to the system.
 */
SALTS_C_API void mem_reset(mem_pool_t *pool);

/**
 * @brief Trim empty slabs
 * @param pool Pool structure
 */
SALTS_C_API void mem_trim(mem_pool_t *pool);

/**
 * @brief Allocate from slab pool
 * @param pool Pool structure
 * @param size Allocation size
 * @return Pointer to allocated memory, or NULL on failure
 *
 * Small size classes use a lazily allocated 8-shard magazine with one slot
 * per class and shard (72 blocks maximum per pool). Each thread accesses its
 * ownership shard in O(1); misses use the locked slab allocator.
 */
SALTS_C_API void *mem_alloc(mem_pool_t *pool, size_t size);

/**
 * @brief Allocate a checked array from the slab pool
 * @param pool Pool structure
 * @param element_size Size of one element
 * @param count Number of elements
 * @return Pointer to allocated memory, or NULL on overflow/allocation failure
 */
SALTS_C_API void *mem_alloc_array(mem_pool_t *pool, size_t element_size, size_t count);

/**
 * @brief Duplicate string using pool allocation
 * @param pool Pool structure
 * @param str String to duplicate
 * @return Duplicated string, or NULL on failure
 */
SALTS_C_API char *mem_strdup(mem_pool_t *pool, const char *str);

/**
 * @brief Format string using pool allocation
 * @param pool Pool structure
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * @return Formatted string, or NULL on failure
 */
SALTS_C_API char *mem_sprintf(mem_pool_t *pool, const char *fmt, ...);

/**
 * @brief Get buffer from pool (new or recycled)
 * @param pool Pool structure
 * @param min_size Minimum capacity required
 * @return Buffer instance, or NULL on failure
 *
 * Released internal buffers use a pool-owned bounded atomic cache. Each pool
 * retains at most 32 entries; cache saturation returns entries to the slab
 * allocator. Cache metadata allocation failure disables the optimization
 * without changing allocation or ownership semantics.
 */
SALTS_C_API mem_buffer_t *mem_get_buffer(mem_pool_t *pool, size_t min_size);

/**
 * @brief Release one shared buffer reference
 * @param buffer Buffer instance
 */
SALTS_C_API void mem_release(mem_buffer_t *buffer);

/**
 * @brief Increment buffer reference count
 * @param buffer Buffer instance
 */
SALTS_C_API void mem_ref(mem_buffer_t *buffer);

/**
 * @brief Decrement buffer reference count (recycle when reaches 0)
 * @param buffer Buffer instance
 */
SALTS_C_API void mem_unref(mem_buffer_t *buffer);

/**
 * @brief Retain a shared buffer reference and return the same handle
 * @param buffer Buffer instance
 * @return The same buffer, or NULL
 */
SALTS_C_API mem_buffer_t *mem_buffer_retain(mem_buffer_t *buffer);

/**
 * @brief Release one shared buffer reference
 * @param buffer Buffer instance
 */
SALTS_C_API void mem_buffer_release(mem_buffer_t *buffer);

/**
 * @brief Wrap external memory as zero-copy buffer
 * @param data External memory pointer
 * @param size Memory size
 * @param free_cb Callback to free memory when refcount reaches 0 (can be NULL)
 * @param user_data User data for free_cb
 * @return Buffer wrapping external memory, or NULL on failure
 *
 * Wrapper shells are reused from a process-global, bounded 64-entry atomic cache.
 * The callback is invoked before the shell becomes available for reuse.
 */
SALTS_C_API mem_buffer_t *mem_wrap_external(void *data, size_t size,
                                          void (*free_cb)(void *data, void *user_data),
                                          void *user_data);

/**
 * @brief Check if buffer wraps external memory
 * @param buffer Buffer instance
 * @return 1 if external, 0 if pool-managed
 */
SALTS_C_API int mem_is_external(const mem_buffer_t *buffer);

SALTS_C_API size_t mem_pool_total_allocated(const mem_pool_t *pool);
SALTS_C_API size_t mem_pool_total_used(const mem_pool_t *pool);
/** @brief Return a concurrent snapshot of cached internal buffer shells. */
SALTS_C_API size_t mem_pool_recycle_count(const mem_pool_t *pool);
SALTS_C_API char *mem_buffer_data(mem_buffer_t *buffer);
SALTS_C_API const char *mem_buffer_const_data(const mem_buffer_t *buffer);
SALTS_C_API size_t mem_buffer_capacity(const mem_buffer_t *buffer);
SALTS_C_API size_t mem_buffer_used(const mem_buffer_t *buffer);
SALTS_C_API uint32_t mem_buffer_ref_count(const mem_buffer_t *buffer);
SALTS_C_API const mem_pool_t *mem_buffer_pool(const mem_buffer_t *buffer);

/**
 * @brief Create zero-copy slice from buffer
 * @param buffer Source buffer
 * @param offset Starting offset
 * @param length Slice length
 * @return Slice structure
 */
SALTS_C_API mem_slice_t mem_slice(mem_buffer_t *buffer, size_t offset, size_t length);

/**
 * @brief Release slice (decrements buffer refcount)
 * @param slice Slice instance
 */
SALTS_C_API void mem_slice_release(mem_slice_t *slice);

#define MEM_ALLOC(pool, type) ((type *)mem_alloc(pool, sizeof(type)))

#define MEM_ALLOC_ARRAY(pool, type, count) \
  ((type *)mem_alloc_array((pool), sizeof(type), (count)))

static inline void mem_set_used(mem_buffer_t *buffer, size_t used) {
  if (!buffer) return;
  buffer->used = (used > buffer->capacity) ? buffer->capacity : used;
}

static inline size_t mem_remaining(const mem_buffer_t *buffer) {
  if (!buffer) return 0;
  MEM_ASSERT(buffer->used <= buffer->capacity);
  return buffer->capacity - buffer->used;
}

static inline char *mem_write_ptr(mem_buffer_t *buffer) {
  if (!buffer) return NULL;
  MEM_ASSERT(buffer->used <= buffer->capacity);
  return buffer->data + buffer->used;
}

#ifdef __cplusplus
}
#endif

#endif /* SALTS_MEM_H */
