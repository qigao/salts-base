#include "memory_pool.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

MemoryPool* pool_create(size_t size) {
    MemoryPool *pool = malloc(sizeof(MemoryPool));
    if (!pool) return NULL;
    
    pool->pool = malloc(size);
    if (!pool->pool) {
        free(pool);
        return NULL;
    }
    
    pool->size = size;
    pool->used = 0;
    pool->peak_used = 0;
    pool->alloc_count = 0;
    
    return pool;
}

void* pool_alloc(MemoryPool *pool, size_t size) {
    if (!pool || !size) return NULL;

    return pool_alloc_aligned(pool, size, MEMORY_POOL_DEFAULT_ALIGNMENT);
}

void pool_reset(MemoryPool *pool) {
    if (pool) {
        pool->used = 0;
    }
}

void* pool_alloc_aligned(MemoryPool *pool, size_t size, size_t alignment) {
    if (!pool || !size) return NULL;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    if (pool->used > pool->size) {
        return NULL;
    }

    if (pool->used > SIZE_MAX - (alignment - 1U)) {
        return NULL;
    }

    size_t mask = alignment - 1U;
    size_t aligned_offset = (pool->used + mask) & ~mask;

    if (aligned_offset > pool->size) {
        return NULL;
    }

    if (size > pool->size - aligned_offset) {
        return NULL;
    }

    size_t new_used = aligned_offset + size;

    if (new_used < pool->used) return NULL;

    void *ptr = (char *)pool->pool + aligned_offset;
    pool->used = new_used;
    pool->alloc_count++;
    
    // Update peak usage
    if (pool->used > pool->peak_used) {
        pool->peak_used = pool->used;
    }
    
    return ptr;
}

size_t pool_get_used(MemoryPool *pool) {
    return pool ? pool->used : 0;
}

size_t pool_get_available(MemoryPool *pool) {
    return pool ? (pool->size - pool->used) : 0;
}

size_t pool_get_peak(MemoryPool *pool) {
    return pool ? pool->peak_used : 0;
}

size_t pool_mark(MemoryPool *pool) {
    return pool ? pool->used : 0;
}

void pool_rewind(MemoryPool *pool, size_t mark) {
    if (!pool)
        return;
    if (mark <= pool->used) {
        pool->used = mark;
    }
}

void pool_destroy(MemoryPool *pool) {
    if (pool) {
        free(pool->pool);
        free(pool);
    }
}
