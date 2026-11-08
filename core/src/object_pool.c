#include "object_pool.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Memory chunk for contiguous allocation
 */
typedef struct object_pool_chunk_s {
  void *memory;                       // Chunk memory
  uint8_t *allocation_bitmap;         // One bit per slot: 1 while checked out
  size_t capacity;                    // Number of objects in this chunk
  struct object_pool_chunk_s *next;   // Next chunk
} object_pool_chunk_t;

/**
 * @brief Object pool structure
 */
struct object_pool_s {
  size_t object_size;                 // Size of each object
  size_t max_capacity;                // Maximum capacity (0 = unlimited)
  bool zero_on_alloc;                 // Zero memory on allocation

  void *free_list;                    // Free-list head (intrusive linked list)
  size_t free_count;                  // Number of free objects currently in free_list

  uint8_t *bump_ptr;                  // Pointer to unallocated memory in active chunk
  size_t bump_remaining;              // Number of objects remaining in bump area

  object_pool_chunk_t *chunks;        // List of allocated chunks
  size_t total_capacity;              // Total capacity across all chunks
  size_t allocated_count;             // Currently allocated objects
  size_t peak_usage;                  // Peak allocated count
};

/**
 * @brief Allocate a new chunk
 */
static bool object_pool_grow(object_pool_t *pool, size_t count) {
  size_t bitmap_size;

  if (count == 0) {
    return false;
  }

  if (pool->max_capacity > 0) {
    size_t remaining;
    if (pool->total_capacity >= pool->max_capacity) {
      return false;
    }
    remaining = pool->max_capacity - pool->total_capacity;
    if (count > remaining) {
      // Would exceed max capacity
      count = remaining;
    }
  }

  if (count > SIZE_MAX / pool->object_size) {
    return false;
  }

  // Allocate chunk
  object_pool_chunk_t *chunk = (object_pool_chunk_t *)malloc(sizeof(object_pool_chunk_t));
  if (!chunk) {
    return false;
  }

  chunk->capacity = count;
  chunk->memory = calloc(count, pool->object_size);
  if (!chunk->memory) {
    free(chunk);
    return false;
  }
  bitmap_size = count / 8 + (count % 8 != 0);
  chunk->allocation_bitmap = (uint8_t *)calloc(bitmap_size, 1);
  if (!chunk->allocation_bitmap) {
    free(chunk->memory);
    free(chunk);
    return false;
  }

  // Bump allocator: we do not prepopulate the free-list natively.
  // Instead, just set the bump pointer to the new chunk's memory.
  // (Note: we only invoke grow() when bump_remaining is 0, so no old bump space is lost).
  pool->bump_ptr = (uint8_t *)chunk->memory;
  pool->bump_remaining = count;

  pool->total_capacity += count;

  // Add chunk to list
  chunk->next = pool->chunks;
  pool->chunks = chunk;

  return true;
}

static bool object_pool_locate_object(const object_pool_t *pool, const void *obj,
                                      object_pool_chunk_t **chunk_out, size_t *slot_out) {
  object_pool_chunk_t *chunk;
  uintptr_t ptr;

  if (!pool || !obj || !chunk_out || !slot_out) {
    return false;
  }

  ptr = (uintptr_t)obj;
  for (chunk = pool->chunks; chunk != NULL; chunk = chunk->next) {
    uintptr_t start = (uintptr_t)chunk->memory;
    uintptr_t end = start + chunk->capacity * pool->object_size;
    if (ptr >= start && ptr < end) {
      if (((ptr - start) % pool->object_size) != 0) {
        return false;
      }
      *chunk_out = chunk;
      *slot_out = (size_t)((ptr - start) / pool->object_size);
      return true;
    }
  }
  return false;
}

static bool object_pool_slot_is_allocated(const object_pool_chunk_t *chunk, size_t slot) {
  uint8_t mask = (uint8_t)(1u << (slot & 7u));
  return (chunk->allocation_bitmap[slot >> 3u] & mask) != 0;
}

static void object_pool_slot_set_allocated(object_pool_chunk_t *chunk, size_t slot, bool allocated) {
  uint8_t *byte = &chunk->allocation_bitmap[slot >> 3u];
  uint8_t mask = (uint8_t)(1u << (slot & 7u));
  if (allocated) {
    *byte |= mask;
  } else {
    *byte &= (uint8_t)~mask;
  }
}

size_t object_pool_max_alignment(void) {
#if defined(_MSC_VER)
  /* MSVC's C headers do not publish max_align_t. Match its fundamental ABI. */
  typedef union { long double floating; long long integer; void *pointer; } pool_max_align;
  return _Alignof(pool_max_align);
#else
  return _Alignof(max_align_t);
#endif
}

object_pool_t *object_pool_create_aligned(const object_pool_config_t *config, size_t alignment) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      alignment > object_pool_max_alignment()) {
    return NULL;
  }
  if (!config || config->object_size < sizeof(void *)) {
    return NULL;
  }

  object_pool_t *pool = (object_pool_t *)calloc(1, sizeof(object_pool_t));
  if (!pool) {
    return NULL;
  }

  pool->object_size = config->object_size;
  // Each recycled slot also holds a native free-list pointer.
  size_t ptr_size = alignment > sizeof(void *) ? alignment : sizeof(void *);
  if (pool->object_size % ptr_size != 0) {
    if (pool->object_size > SIZE_MAX - (ptr_size - 1)) {
      free(pool);
      return NULL;
    }
    pool->object_size += ptr_size - (pool->object_size % ptr_size);
  }
  
  pool->max_capacity = config->max_capacity;
  pool->zero_on_alloc = config->zero_on_alloc;

  // Allocate initial capacity
  if (config->initial_capacity > 0) {
    if (!object_pool_grow(pool, config->initial_capacity)) {
      free(pool);
      return NULL;
    }
  }

  return pool;
}

object_pool_t *object_pool_create(const object_pool_config_t *config) {
  return object_pool_create_aligned(config, sizeof(void *));
}

bool object_pool_is_allocated(const object_pool_t *pool, const void *obj) {
  object_pool_chunk_t *chunk;
  size_t slot;
  return object_pool_locate_object(pool, obj, &chunk, &slot) &&
         object_pool_slot_is_allocated(chunk, slot);
}

bool object_pool_contains(const object_pool_t *pool, const void *obj) {
  const object_pool_chunk_t *chunk;
  uintptr_t address;
  if (pool == NULL || obj == NULL) return false;
  address = (uintptr_t)obj;
  for (chunk = pool->chunks; chunk != NULL; chunk = chunk->next) {
    uintptr_t start = (uintptr_t)chunk->memory;
    if (address >= start && address - start < chunk->capacity * pool->object_size)
      return true;
  }
  return false;
}

void object_pool_destroy(object_pool_t *pool) {
  if (!pool) {
    return;
  }

  // Free all chunks
  object_pool_chunk_t *chunk = pool->chunks;
  while (chunk) {
    object_pool_chunk_t *next = chunk->next;
    free(chunk->allocation_bitmap);
    free(chunk->memory);
    free(chunk);
    chunk = next;
  }

  free(pool);
}

void *object_pool_alloc(object_pool_t *pool) {
  object_pool_chunk_t *chunk = NULL;
  size_t slot = 0;

  if (!pool) {
    return NULL;
  }

  void *obj = NULL;

  if (pool->free_list) {
    // Pop from free-list
    obj = pool->free_list;
    if (!object_pool_locate_object(pool, obj, &chunk, &slot) ||
        object_pool_slot_is_allocated(chunk, slot)) {
      assert(!"object_pool free-list is corrupt");
      return NULL;
    }
    pool->free_list = *(void **)obj;
    pool->free_count--;
  } else if (pool->bump_remaining > 0) {
    // Fast path: Bump allocation
    obj = pool->bump_ptr;
    chunk = pool->chunks;
    slot = (size_t)(pool->bump_ptr - (uint8_t *)chunk->memory) / pool->object_size;
    pool->bump_ptr += pool->object_size;
    pool->bump_remaining--;
  } else {
    // Free-list empty, bump empty, grow pool (1.5x current capacity, capped at 65536)
    size_t grow_size = pool->total_capacity > 0 ? (pool->total_capacity / 2) : 64;
    if (grow_size < 64) grow_size = 64;
    if (grow_size > 65536) grow_size = 65536;

    if (!object_pool_grow(pool, grow_size)) {
      return NULL;  // At max capacity or OOM
    }

    // After growth, allocate from new bump
    obj = pool->bump_ptr;
    chunk = pool->chunks;
    slot = 0;
    pool->bump_ptr += pool->object_size;
    pool->bump_remaining--;
  }

  object_pool_slot_set_allocated(chunk, slot, true);

  pool->allocated_count++;

  // Update peak usage
  if (pool->allocated_count > pool->peak_usage) {
    pool->peak_usage = pool->allocated_count;
  }

  // Zero memory if requested
  if (pool->zero_on_alloc) {
    memset(obj, 0, pool->object_size);
  }

  return obj;
}

void object_pool_free(object_pool_t *pool, void *obj) {
  object_pool_chunk_t *chunk = NULL;
  size_t slot = 0;

  if (!pool || !obj) {
    return;
  }

  if (!object_pool_locate_object(pool, obj, &chunk, &slot) ||
      !object_pool_slot_is_allocated(chunk, slot)) {
    assert(!"object_pool_free received an invalid or already-freed object");
    return;
  }
  if (pool->allocated_count == 0) {
    assert(!"object_pool_free called with no active allocations");
    return;
  }

  object_pool_slot_set_allocated(chunk, slot, false);

  // Push to free-list
  void **node = (void **)obj;
  *node = pool->free_list;
  pool->free_list = node;

  pool->free_count++;
  pool->allocated_count--;
}

size_t object_pool_allocated_count(const object_pool_t *pool) {
  return pool ? pool->allocated_count : 0;
}

size_t object_pool_free_count(const object_pool_t *pool) {
  return pool ? (pool->free_count + pool->bump_remaining) : 0;
}

size_t object_pool_capacity(const object_pool_t *pool) {
  return pool ? pool->total_capacity : 0;
}

size_t object_pool_peak_usage(const object_pool_t *pool) {
  return pool ? pool->peak_usage : 0;
}

void object_pool_reset_stats(object_pool_t *pool) {
  if (pool) {
    pool->peak_usage = pool->allocated_count;
  }
}
