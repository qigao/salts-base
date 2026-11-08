#include "cmeta_buffer.h"
#include <thread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
  #include <malloc.h>
#endif

/* ── internal types ───────────────────────────────────────── */

#define MEM_ALIGNMENT 8
#define POOL_SIZE_CLASSES 9
#define BUFFER_CACHE_LINE_SIZE 64U
#define BUFFER_CACHE_SHARDS 8U
#define BUFFER_CACHE_SLOTS 8U
#define BUFFER_CACHE_FULL_MASK ((1U << BUFFER_CACHE_SLOTS) - 1U)
#define INTERNAL_BUFFER_CACHE_SHARDS 8U
#define INTERNAL_BUFFER_CACHE_SLOTS 4U
#define INTERNAL_BUFFER_CACHE_FULL_MASK ((1U << INTERNAL_BUFFER_CACHE_SLOTS) - 1U)

_Static_assert(INTERNAL_BUFFER_CACHE_SHARDS == BUFFER_CACHE_SHARDS,
               "internal and external caches share the thread shard selector");
_Static_assert(INTERNAL_BUFFER_CACHE_SHARDS * INTERNAL_BUFFER_CACHE_SLOTS ==
                   MEM_BUFFER_RECYCLE_LIMIT,
               "internal buffer cache capacity must match the public limit");

typedef struct mem_slab_s mem_slab_t;

typedef struct {
    mem_slab_t* slab;
} slab_tag_t;

typedef struct free_node_s {
    struct free_node_s* next;
} free_node_t;

struct mem_slab_s {
    void* memory;
    size_t block_size;
    size_t block_count;
    size_t free_count;
    free_node_t* free_list;
    mem_slab_t* next;
};

#define OVERSIZE_MAGIC 0x5652535A
typedef struct oversize_header_s {
    uint32_t magic;
    size_t total_size;
    struct oversize_header_s* next;
    void* slab_null_mark; // This will be at ptr - sizeof(void*), must be NULL
} oversize_header_t;

/* ── globals ──────────────────────────────────────────────── */

static mem_pool_t g_global_pool;
static cmeta_once_t g_global_once = SALTS_ONCE_INIT;

/* Cache-line isolated shards avoid false sharing between independent atomic slots. */
#ifdef _MSC_VER
__declspec(align(BUFFER_CACHE_LINE_SIZE)) struct buffer_cache_shard_s {
    _Atomic(mem_buffer_t *) slots[BUFFER_CACHE_SLOTS];
    atomic_uint ready;
};
#else
struct buffer_cache_shard_s {
    _Atomic(mem_buffer_t *) slots[BUFFER_CACHE_SLOTS];
    atomic_uint ready;
} __attribute__((aligned(BUFFER_CACHE_LINE_SIZE)));
#endif
typedef struct buffer_cache_shard_s buffer_cache_shard_t;
_Static_assert(sizeof(buffer_cache_shard_t) % BUFFER_CACHE_LINE_SIZE == 0,
               "buffer cache shards must not share cache lines");

#ifdef _MSC_VER
__declspec(align(BUFFER_CACHE_LINE_SIZE)) struct internal_buffer_cache_shard_s {
    _Atomic(mem_buffer_t *) slots[INTERNAL_BUFFER_CACHE_SLOTS];
    atomic_uint ready;
};
#else
struct internal_buffer_cache_shard_s {
    _Atomic(mem_buffer_t *) slots[INTERNAL_BUFFER_CACHE_SLOTS];
    atomic_uint ready;
} __attribute__((aligned(BUFFER_CACHE_LINE_SIZE)));
#endif
typedef struct internal_buffer_cache_shard_s internal_buffer_cache_shard_t;
_Static_assert(sizeof(internal_buffer_cache_shard_t) % BUFFER_CACHE_LINE_SIZE == 0,
               "internal buffer cache shards must not share cache lines");

typedef struct internal_buffer_cache_s {
    internal_buffer_cache_shard_t shards[INTERNAL_BUFFER_CACHE_SHARDS];
} internal_buffer_cache_t;

#ifdef _MSC_VER
__declspec(align(BUFFER_CACHE_LINE_SIZE)) struct slab_magazine_shard_s {
    _Atomic(void *) slots[POOL_SIZE_CLASSES];
};
#else
struct slab_magazine_shard_s {
    _Atomic(void *) slots[POOL_SIZE_CLASSES];
} __attribute__((aligned(BUFFER_CACHE_LINE_SIZE)));
#endif
typedef struct slab_magazine_shard_s slab_magazine_shard_t;
_Static_assert(sizeof(slab_magazine_shard_t) % BUFFER_CACHE_LINE_SIZE == 0,
               "slab magazine shards must not share cache lines");

typedef struct slab_magazine_s {
    slab_magazine_shard_t shards[BUFFER_CACHE_SHARDS];
} slab_magazine_t;

typedef struct mem_pool_cache_s {
    _Atomic(internal_buffer_cache_t *) buffer_cache;
    _Atomic(slab_magazine_t *) slab_magazine;
} mem_pool_cache_t;

static buffer_cache_shard_t g_external_wrapper_shards[BUFFER_CACHE_SHARDS];
static atomic_size_t g_external_wrapper_next_shard;
static SALTS_THREAD_LOCAL unsigned int g_external_wrapper_shard;
static SALTS_THREAD_LOCAL unsigned int g_external_wrapper_shard_initialized;
static SALTS_THREAD_LOCAL unsigned int g_external_wrapper_take_cursor;
static SALTS_THREAD_LOCAL unsigned int g_external_wrapper_put_cursor;
static SALTS_THREAD_LOCAL unsigned int g_internal_buffer_take_slot;
static SALTS_THREAD_LOCAL unsigned int g_internal_buffer_put_slot;
static SALTS_THREAD_LOCAL mem_pool_t *g_slab_magazine_hint_pool;
static SALTS_THREAD_LOCAL unsigned int g_slab_magazine_checked_classes;
static SALTS_THREAD_LOCAL unsigned int g_slab_magazine_ready_classes;

/* Hashed locks to provide thread safety for mem_pool_t without increasing its size */
#define POOL_LOCK_COUNT 32
static cmeta_mutex_t g_pool_locks[POOL_LOCK_COUNT];
static cmeta_once_t g_pool_locks_once = SALTS_ONCE_INIT;

static void pool_locks_init_cb(void) {
    for (int i = 0; i < POOL_LOCK_COUNT; i++) {
        cmeta_mutex_init(&g_pool_locks[i]);
    }
}

static inline cmeta_mutex_t* get_pool_lock(mem_pool_t* pool) {
    cmeta_once(&g_pool_locks_once, pool_locks_init_cb);
    return &g_pool_locks[((uintptr_t)pool >> 7) % POOL_LOCK_COUNT];
}

static void global_pool_init_cb(void) {
    mem_init(&g_global_pool, 0);
}

static unsigned int external_wrapper_current_shard(void) {
    if (!g_external_wrapper_shard_initialized) {
        g_external_wrapper_shard =
            (unsigned int)(atomic_fetch_add_explicit(&g_external_wrapper_next_shard, 1U,
                                                     memory_order_relaxed) %
                           BUFFER_CACHE_SHARDS);
        g_external_wrapper_shard_initialized = 1U;
    }
    return g_external_wrapper_shard;
}

static mem_buffer_t *external_wrapper_pool_take(void) {
    unsigned int shard = external_wrapper_current_shard();
    buffer_cache_shard_t *pool_shard = &g_external_wrapper_shards[shard];
    unsigned int ready = atomic_load_explicit(&pool_shard->ready, memory_order_acquire);
    unsigned int offset;

    while (ready != 0) {
        unsigned int desired;
        unsigned int bit = 0;
        unsigned int slot = 0;
        mem_buffer_t *buffer;

        for (offset = 0; offset < BUFFER_CACHE_SLOTS; ++offset) {
            slot = (g_external_wrapper_take_cursor + offset) % BUFFER_CACHE_SLOTS;
            bit = 1U << slot;
            if ((ready & bit) != 0) break;
        }
        if (offset == BUFFER_CACHE_SLOTS) return NULL;

        desired = ready & ~bit;
        if (!atomic_compare_exchange_strong_explicit(&pool_shard->ready, &ready, desired,
                                                     memory_order_acq_rel, memory_order_acquire))
            return NULL;

        buffer = atomic_load_explicit(&pool_shard->slots[slot], memory_order_acquire);
        if (buffer != NULL &&
            atomic_compare_exchange_strong_explicit(&pool_shard->slots[slot], &buffer, NULL,
                                                    memory_order_acquire, memory_order_relaxed)) {
            g_external_wrapper_take_cursor = (slot + 1U) % BUFFER_CACHE_SLOTS;
            return buffer;
        }
        ready = atomic_load_explicit(&pool_shard->ready, memory_order_acquire);
    }
    return NULL;
}

static int external_wrapper_pool_put(mem_buffer_t *buffer) {
    unsigned int shard = external_wrapper_current_shard();
    buffer_cache_shard_t *pool_shard = &g_external_wrapper_shards[shard];
    unsigned int ready = atomic_load_explicit(&pool_shard->ready, memory_order_relaxed);
    unsigned int offset;

    if (ready == BUFFER_CACHE_FULL_MASK) return 0;
    for (offset = 0; offset < BUFFER_CACHE_SLOTS; ++offset) {
        unsigned int slot = (g_external_wrapper_put_cursor + offset) % BUFFER_CACHE_SLOTS;
        mem_buffer_t *expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&pool_shard->slots[slot], &expected, buffer,
                                                    memory_order_release,
                                                    memory_order_relaxed)) {
            g_external_wrapper_put_cursor = (slot + 1U) % BUFFER_CACHE_SLOTS;
            atomic_fetch_or_explicit(&pool_shard->ready, 1U << slot, memory_order_release);
            return 1;
        }
    }
    return 0;
}

static mem_pool_cache_t *mem_pool_cache_from_pool(mem_pool_t *pool) {
    if (!pool || atomic_load_explicit(&pool->recycle_count, memory_order_acquire) == 0)
        return NULL;
    return (mem_pool_cache_t *)pool->recycle_head;
}

static mem_pool_cache_t *mem_pool_cache_ensure_nolock(mem_pool_t *pool) {
    mem_pool_cache_t *cache = mem_pool_cache_from_pool(pool);

    if (cache) return cache;
    cache = (mem_pool_cache_t *)calloc(1, sizeof(*cache));
    if (!cache) return NULL;
    pool->recycle_head = (mem_buffer_t *)cache;
    atomic_store_explicit(&pool->recycle_count, 1U, memory_order_release);
    return cache;
}

/* Pool-owned slots avoid ABA. Common hit/put is O(1); a miss scans at most 32 slots. */
static internal_buffer_cache_t *internal_buffer_cache_from_pool(mem_pool_t *pool) {
    mem_pool_cache_t *pool_cache = mem_pool_cache_from_pool(pool);
    return pool_cache ? atomic_load_explicit(&pool_cache->buffer_cache, memory_order_acquire) : NULL;
}

static int internal_buffer_cache_ensure_nolock(mem_pool_t *pool) {
    mem_pool_cache_t *pool_cache = mem_pool_cache_ensure_nolock(pool);
    internal_buffer_cache_t *cache;

    if (!pool_cache) return 0;
    cache = atomic_load_explicit(&pool_cache->buffer_cache, memory_order_acquire);
    if (cache) return 1;
    cache = (internal_buffer_cache_t *)calloc(1, sizeof(*cache));
    if (!cache) return 0;
    atomic_store_explicit(&pool_cache->buffer_cache, cache, memory_order_release);
    return 1;
}

static mem_buffer_t *internal_buffer_cache_take(mem_pool_t *pool, size_t min_size) {
    internal_buffer_cache_t *cache = internal_buffer_cache_from_pool(pool);
    unsigned int start_shard = external_wrapper_current_shard();
    unsigned int shard_offset;

    if (!cache) return NULL;
    for (shard_offset = 0; shard_offset < INTERNAL_BUFFER_CACHE_SHARDS; ++shard_offset) {
        unsigned int shard =
            (start_shard + shard_offset) % INTERNAL_BUFFER_CACHE_SHARDS;
        internal_buffer_cache_shard_t *cache_shard = &cache->shards[shard];
        unsigned int ready = atomic_load_explicit(&cache_shard->ready, memory_order_acquire);
        unsigned int visited = 0;

        while ((ready & ~visited) != 0) {
            unsigned int slot_offset;
            unsigned int bit = 0;
            unsigned int slot = 0;
            unsigned int desired;
            mem_buffer_t *buffer;

            for (slot_offset = 0; slot_offset < INTERNAL_BUFFER_CACHE_SLOTS; ++slot_offset) {
                slot =
                    (g_internal_buffer_take_slot + slot_offset) % INTERNAL_BUFFER_CACHE_SLOTS;
                bit = 1U << slot;
                if ((ready & bit) != 0 && (visited & bit) == 0) break;
            }
            if (slot_offset == INTERNAL_BUFFER_CACHE_SLOTS) break;
            visited |= bit;
            desired = ready & ~bit;
            if (!atomic_compare_exchange_strong_explicit(&cache_shard->ready, &ready, desired,
                                                         memory_order_acq_rel,
                                                         memory_order_acquire))
                continue;

            buffer = atomic_load_explicit(&cache_shard->slots[slot], memory_order_acquire);
            if (buffer != NULL && buffer->capacity >= min_size) {
                atomic_store_explicit(&cache_shard->slots[slot], NULL, memory_order_release);
                buffer->next = NULL;
                buffer->used = 0;
                atomic_store_explicit(&buffer->ref_count, 1U, memory_order_relaxed);
                g_internal_buffer_take_slot = (slot + 1U) % INTERNAL_BUFFER_CACHE_SLOTS;
                return buffer;
            }

            if (buffer != NULL)
                atomic_fetch_or_explicit(&cache_shard->ready, bit, memory_order_release);
            ready = atomic_load_explicit(&cache_shard->ready, memory_order_acquire);
        }
    }
    return NULL;
}

static int internal_buffer_cache_put(mem_pool_t *pool, mem_buffer_t *buffer) {
    internal_buffer_cache_t *cache = internal_buffer_cache_from_pool(pool);
    unsigned int start_shard = external_wrapper_current_shard();
    unsigned int shard_offset;

    if (!cache) return 0;
    for (shard_offset = 0; shard_offset < INTERNAL_BUFFER_CACHE_SHARDS; ++shard_offset) {
        unsigned int shard =
            (start_shard + shard_offset) % INTERNAL_BUFFER_CACHE_SHARDS;
        internal_buffer_cache_shard_t *cache_shard = &cache->shards[shard];
        unsigned int ready = atomic_load_explicit(&cache_shard->ready, memory_order_relaxed);
        unsigned int slot_offset;

        if (ready == INTERNAL_BUFFER_CACHE_FULL_MASK) continue;
        for (slot_offset = 0; slot_offset < INTERNAL_BUFFER_CACHE_SLOTS; ++slot_offset) {
            unsigned int slot =
                (g_internal_buffer_put_slot + slot_offset) % INTERNAL_BUFFER_CACHE_SLOTS;
            mem_buffer_t *expected = NULL;
            if (atomic_compare_exchange_strong_explicit(&cache_shard->slots[slot], &expected,
                                                        buffer, memory_order_release,
                                                        memory_order_relaxed)) {
                atomic_fetch_or_explicit(&cache_shard->ready, 1U << slot, memory_order_release);
                g_internal_buffer_put_slot = (slot + 1U) % INTERNAL_BUFFER_CACHE_SLOTS;
                return 1;
            }
        }
    }
    return 0;
}

static void internal_buffer_cache_clear(mem_pool_t *pool) {
    internal_buffer_cache_t *cache = internal_buffer_cache_from_pool(pool);
    unsigned int shard;

    if (!cache) return;
    for (shard = 0; shard < INTERNAL_BUFFER_CACHE_SHARDS; ++shard) {
        internal_buffer_cache_shard_t *cache_shard = &cache->shards[shard];
        unsigned int slot;

        atomic_store_explicit(&cache_shard->ready, 0U, memory_order_release);
        for (slot = 0; slot < INTERNAL_BUFFER_CACHE_SLOTS; ++slot)
            atomic_store_explicit(&cache_shard->slots[slot], NULL, memory_order_relaxed);
    }
}

static size_t internal_buffer_cache_count(const mem_pool_t *pool) {
    const internal_buffer_cache_t *cache =
        internal_buffer_cache_from_pool((mem_pool_t *)pool);
    size_t count = 0;
    unsigned int shard;

    if (!cache) return 0;
    for (shard = 0; shard < INTERNAL_BUFFER_CACHE_SHARDS; ++shard) {
        unsigned int ready =
            atomic_load_explicit(&((internal_buffer_cache_t *)cache)->shards[shard].ready,
                                 memory_order_acquire);
        while (ready != 0) {
            count += ready & 1U;
            ready >>= 1U;
        }
    }
    return count;
}

static slab_magazine_t *slab_magazine_from_pool(mem_pool_t *pool) {
    mem_pool_cache_t *pool_cache = mem_pool_cache_from_pool(pool);
    return pool_cache ? atomic_load_explicit(&pool_cache->slab_magazine, memory_order_acquire)
                      : NULL;
}

static int slab_magazine_ensure_nolock(mem_pool_t *pool) {
    mem_pool_cache_t *pool_cache = mem_pool_cache_ensure_nolock(pool);
    slab_magazine_t *magazine;

    if (!pool_cache) return 0;
    magazine = atomic_load_explicit(&pool_cache->slab_magazine, memory_order_acquire);
    if (magazine) return 1;
    magazine = (slab_magazine_t *)calloc(1, sizeof(*magazine));
    if (!magazine) return 0;
    atomic_store_explicit(&pool_cache->slab_magazine, magazine, memory_order_release);
    return 1;
}

/* TLS tracks only local publication hints; the atomic slot remains the ownership source. */
static void *slab_magazine_take(mem_pool_t *pool, int class_idx, int *needs_init) {
    slab_magazine_t *magazine;
    unsigned int shard;
    unsigned int bit;

    *needs_init = 0;
    if (class_idx < 0 || class_idx >= POOL_SIZE_CLASSES) return NULL;
    bit = 1U << (unsigned int)class_idx;
    if ((g_slab_magazine_checked_classes & bit) != 0U &&
        (g_slab_magazine_ready_classes & bit) == 0U)
        return NULL;
    if (g_slab_magazine_hint_pool != pool) {
        g_slab_magazine_hint_pool = pool;
        g_slab_magazine_checked_classes = 0U;
        g_slab_magazine_ready_classes = 0U;
    }

    g_slab_magazine_checked_classes |= bit;
    g_slab_magazine_ready_classes &= ~bit;
    magazine = slab_magazine_from_pool(pool);
    if (!magazine) {
        *needs_init = 1;
        return NULL;
    }
    shard = external_wrapper_current_shard();
    return atomic_exchange_explicit(&magazine->shards[shard].slots[class_idx], NULL,
                                    memory_order_acquire);
}

static int slab_magazine_put(mem_pool_t *pool, int class_idx, void *ptr) {
    slab_magazine_t *magazine;
    unsigned int shard;
    unsigned int bit;
    void *expected = NULL;

    if (class_idx < 0 || class_idx >= POOL_SIZE_CLASSES || !ptr) return 0;
    bit = 1U << (unsigned int)class_idx;
    if ((g_slab_magazine_ready_classes & bit) != 0U) return 0;
    if (g_slab_magazine_hint_pool != pool) {
        g_slab_magazine_hint_pool = pool;
        g_slab_magazine_checked_classes = 0U;
        g_slab_magazine_ready_classes = 0U;
    }

    magazine = slab_magazine_from_pool(pool);
    if (!magazine) return 0;
    shard = external_wrapper_current_shard();
    if (!atomic_compare_exchange_strong_explicit(&magazine->shards[shard].slots[class_idx],
                                                 &expected, ptr, memory_order_release,
                                                 memory_order_relaxed)) {
        if (expected != NULL) {
            g_slab_magazine_checked_classes |= bit;
            g_slab_magazine_ready_classes |= bit;
        }
        return 0;
    }
    g_slab_magazine_checked_classes |= bit;
    g_slab_magazine_ready_classes |= bit;
    return 1;
}

static void slab_magazine_clear(mem_pool_t *pool, int return_to_slabs) {
    slab_magazine_t *magazine = slab_magazine_from_pool(pool);
    unsigned int shard;

    if (!magazine) return;
    for (shard = 0; shard < BUFFER_CACHE_SHARDS; ++shard) {
        int class_idx;
        for (class_idx = 0; class_idx < POOL_SIZE_CLASSES; ++class_idx) {
            void *ptr = atomic_exchange_explicit(&magazine->shards[shard].slots[class_idx], NULL,
                                                 memory_order_acq_rel);
            if (ptr && return_to_slabs) {
                void **tag_ptr = (void **)((char *)ptr - sizeof(void *));
                mem_slab_t *slab = (mem_slab_t *)(*tag_ptr);
                free_node_t *node = (free_node_t *)ptr;
                node->next = slab->free_list;
                slab->free_list = node;
                slab->free_count++;
            }
        }
    }
}

/* ── internal helpers ─────────────────────────────────────── */

static inline size_t align_size(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
}

static int size_class_index(size_t size) {
    if (size <= 32) return 0;
    if (size <= 64) return 1;
    if (size <= 128) return 2;
    if (size <= 256) return 3;
    if (size <= 512) return 4;
    if (size <= 1024) return 5;
    if (size <= 2048) return 6;
    if (size <= 4096) return 7;
    if (size <= 8192) return 8;
    return -1;
}

static size_t index_to_size(int idx) {
    static const size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    return (idx >= 0 && idx < POOL_SIZE_CLASSES) ? sizes[idx] : 0;
}

static mem_slab_t* alloc_slab(size_t block_size) {
    size_t block_count = 4096 / block_size;
    if (block_count < 4) block_count = 4;

    size_t memory_size = block_size * block_count;
    mem_slab_t* slab = (mem_slab_t*)malloc(sizeof(mem_slab_t));
    if (!slab) return NULL;

    slab->memory = malloc(memory_size);
    if (!slab->memory) {
        free(slab);
        return NULL;
    }

    slab->block_size = block_size;
    slab->block_count = block_count;
    slab->free_count = block_count;
    slab->free_list = NULL;
    slab->next = NULL;

    char* block = (char*)slab->memory;
    for (size_t i = 0; i < block_count; i++) {
        slab_tag_t* tag = (slab_tag_t*)block;
        tag->slab = slab;

        free_node_t* node = (free_node_t*)(block + sizeof(slab_tag_t));
        node->next = slab->free_list;
        slab->free_list = node;
        block += block_size;
    }

    return slab;
}

static void free_slab(mem_slab_t* slab) {
    if (!slab) return;
    free(slab->memory);
    free(slab);
}

static void oversize_link_nolock(mem_pool_t* pool, oversize_header_t* hdr) {
    hdr->next = (struct oversize_header_s*)pool->oversize_head;
    pool->oversize_head = hdr;
}

static void oversize_unlink_nolock(mem_pool_t* pool, oversize_header_t* hdr) {
    oversize_header_t** prev = (oversize_header_t**)&pool->oversize_head;
    oversize_header_t* curr = (oversize_header_t*)pool->oversize_head;
    while (curr) {
        if (curr == hdr) {
            *prev = (oversize_header_t*)curr->next;
            return;
        }
        prev = (oversize_header_t**)&curr->next;
        curr = (oversize_header_t*)curr->next;
    }
}

static void oversize_free_all_nolock(mem_pool_t* pool) {
    oversize_header_t* curr = (oversize_header_t*)pool->oversize_head;
    while (curr) {
        oversize_header_t* next = (oversize_header_t*)curr->next;
        atomic_fetch_sub(&pool->total_used, curr->total_size);
        free(curr);
        curr = next;
    }
    pool->oversize_head = NULL;
}

static void* slab_alloc_nolock(mem_pool_t* pool, int class_idx) {
    mem_slab_t* slab = (mem_slab_t*)pool->slabs[class_idx];
    while (slab) {
        if (slab->free_list) {
            free_node_t* node = slab->free_list;
            slab->free_list = node->next;
            slab->free_count--;
            atomic_fetch_add(&pool->total_used, slab->block_size);
            return node;
        }
        slab = slab->next;
    }

    /* No free blocks, allocate new slab */
    size_t block_size = index_to_size(class_idx);
    slab = alloc_slab(block_size);
    if (!slab) return NULL;

    slab->next = (mem_slab_t*)pool->slabs[class_idx];
    pool->slabs[class_idx] = slab;
    atomic_fetch_add(&pool->total_allocated, block_size * slab->block_count);

    free_node_t* node = slab->free_list;
    slab->free_list = node->next;
    slab->free_count--;
    atomic_fetch_add(&pool->total_used, block_size);
    return node;
}

/* ── pool lifecycle ───────────────────────────────────────── */

mem_pool_t* mem_global(void) {
    cmeta_once(&g_global_once, global_pool_init_cb);
    return &g_global_pool;
}

int mem_init(mem_pool_t* pool, size_t initial_size) {
    if (!pool) return -1;
    (void)initial_size; /* Retained for source compatibility; allocation remains lazy. */

    for (int i = 0; i < POOL_SIZE_CLASSES; i++) {
        pool->slabs[i] = NULL;
    }
    pool->oversize_head = NULL;
    pool->recycle_head = NULL;

    atomic_store_explicit(&pool->total_allocated, 0, memory_order_relaxed);
    atomic_store_explicit(&pool->total_used, 0, memory_order_relaxed);
    atomic_store_explicit(&pool->recycle_count, 0, memory_order_relaxed);

    return 0;
}

void mem_destroy(mem_pool_t* pool) {
    mem_pool_cache_t *pool_cache;
    internal_buffer_cache_t *buffer_cache = NULL;
    slab_magazine_t *slab_magazine = NULL;

    if (!pool) return;

    cmeta_mutex_lock(get_pool_lock(pool));
    internal_buffer_cache_clear(pool);
    slab_magazine_clear(pool, 0);
    pool_cache = mem_pool_cache_from_pool(pool);
    if (pool_cache) {
        buffer_cache = atomic_load_explicit(&pool_cache->buffer_cache, memory_order_relaxed);
        slab_magazine = atomic_load_explicit(&pool_cache->slab_magazine, memory_order_relaxed);
    }
    atomic_store_explicit(&pool->recycle_count, 0U, memory_order_release);
    pool->recycle_head = NULL;
    oversize_free_all_nolock(pool);
    cmeta_mutex_unlock(get_pool_lock(pool));
    free(buffer_cache);
    free(slab_magazine);
    free(pool_cache);

    for (int i = 0; i < POOL_SIZE_CLASSES; i++) {
        mem_slab_t* slab = (mem_slab_t*)pool->slabs[i];
        while (slab) {
            mem_slab_t* next = slab->next;
            free_slab(slab);
            slab = next;
        }
        pool->slabs[i] = NULL;
    }

    pool->recycle_head = NULL;
    pool->oversize_head = NULL;
    atomic_store_explicit(&pool->recycle_count, 0, memory_order_relaxed);

    // No need to memset the whole pool as it's already cleared or will be freed
}

void mem_reset(mem_pool_t* pool) {
    if (!pool) return;

    cmeta_mutex_lock(get_pool_lock(pool));
    internal_buffer_cache_clear(pool);
    slab_magazine_clear(pool, 0);
    oversize_free_all_nolock(pool);

    for (int i = 0; i < POOL_SIZE_CLASSES; i++) {
        mem_slab_t* slab = (mem_slab_t*)pool->slabs[i];
        while (slab) {
            slab->free_list = NULL;
            slab->free_count = slab->block_count;

            char* block = (char*)slab->memory;
            for (size_t j = 0; j < slab->block_count; j++) {
                slab_tag_t* tag = (slab_tag_t*)block;
                tag->slab = slab;

                free_node_t* node = (free_node_t*)(block + sizeof(slab_tag_t));
                node->next = slab->free_list;
                slab->free_list = node;
                block += slab->block_size;
            }
            slab = slab->next;
        }
    }

    atomic_store_explicit(&pool->total_used, 0, memory_order_relaxed);
    cmeta_mutex_unlock(get_pool_lock(pool));
}

void mem_trim(mem_pool_t* pool) {
    if (!pool) return;

    cmeta_mutex_lock(get_pool_lock(pool));
    slab_magazine_clear(pool, 1);
    for (int i = 0; i < POOL_SIZE_CLASSES; i++) {
        mem_slab_t** prev = (mem_slab_t**)&pool->slabs[i];
        mem_slab_t* slab = (mem_slab_t*)pool->slabs[i];

        while (slab) {
            mem_slab_t* next = slab->next;
            if (slab->free_count == slab->block_count) {
                *prev = next;
                size_t slab_bytes = slab->block_size * slab->block_count;
                atomic_fetch_sub(&pool->total_allocated, slab_bytes);
                free_slab(slab);
            } else {
                prev = &slab->next;
            }
            slab = next;
        }
    }
    cmeta_mutex_unlock(get_pool_lock(pool));
}

/* ── public allocator ─────────────────────────────────────── */

void* mem_alloc(mem_pool_t* pool, size_t size) {
    int magazine_needs_init = 0;

    if (!pool || size == 0) return NULL;

    if (size > SIZE_MAX - sizeof(slab_tag_t)) return NULL;
    size_t tagged_size = size + sizeof(slab_tag_t);
    if (tagged_size > SIZE_MAX - (MEM_ALIGNMENT - 1U)) return NULL;
    size_t total = align_size(tagged_size, MEM_ALIGNMENT);
    int class_idx = size_class_index(total);

    if (class_idx < 0) {
        if (size > SIZE_MAX - sizeof(oversize_header_t)) return NULL;
        size_t alloc_size = sizeof(oversize_header_t) + size;
        oversize_header_t* hdr = (oversize_header_t*)malloc(alloc_size);
        if (!hdr) return NULL;
        hdr->magic = OVERSIZE_MAGIC;
        hdr->total_size = alloc_size;
        hdr->slab_null_mark = NULL;
        
        cmeta_mutex_lock(get_pool_lock(pool));
        oversize_link_nolock(pool, hdr);
        cmeta_mutex_unlock(get_pool_lock(pool));
        
        atomic_fetch_add(&pool->total_used, alloc_size);
        return (char*)hdr + sizeof(oversize_header_t);
    }

    void* ptr = slab_magazine_take(pool, class_idx, &magazine_needs_init);
    if (ptr) {
        atomic_fetch_add_explicit(&pool->total_used, index_to_size(class_idx),
                                  memory_order_relaxed);
        return ptr;
    }

    cmeta_mutex_lock(get_pool_lock(pool));
    if (magazine_needs_init) (void)slab_magazine_ensure_nolock(pool);
    ptr = slab_alloc_nolock(pool, class_idx);
    cmeta_mutex_unlock(get_pool_lock(pool));

    return ptr;
}

void* mem_alloc_array(mem_pool_t* pool, size_t element_size, size_t count) {
    if (!pool || element_size == 0 || count == 0) return NULL;
    if (count > SIZE_MAX / element_size) return NULL;
    return mem_alloc(pool, element_size * count);
}

void mem_free(mem_pool_t* pool, void* ptr) {
    if (!pool || !ptr) return;

    /* Slab tags are always non-NULL pointers to the slab. 
       Oversize headers have a NULL pointer at the same offset (ptr - 8). */
    void** tag_ptr = (void**)((char*)ptr - sizeof(void*));
    if (*tag_ptr == NULL) {
        oversize_header_t* hdr = (oversize_header_t*)((char*)ptr - sizeof(oversize_header_t));
        if (hdr->magic == OVERSIZE_MAGIC) {
            cmeta_mutex_lock(get_pool_lock(pool));
            oversize_unlink_nolock(pool, hdr);
            cmeta_mutex_unlock(get_pool_lock(pool));
            atomic_fetch_sub(&pool->total_used, hdr->total_size);
            hdr->magic = 0;
            free(hdr);
            return;
        }
    } else {
        mem_slab_t* slab = (mem_slab_t*)(*tag_ptr);
        int class_idx = size_class_index(slab->block_size);
        atomic_fetch_sub_explicit(&pool->total_used, slab->block_size, memory_order_relaxed);
        if (slab_magazine_put(pool, class_idx, ptr)) return;

        cmeta_mutex_lock(get_pool_lock(pool));
        free_node_t* node = (free_node_t*)ptr;
        node->next = slab->free_list;
        slab->free_list = node;
        slab->free_count++;
        cmeta_mutex_unlock(get_pool_lock(pool));
    }
}

char* mem_strdup(mem_pool_t* pool, const char* str) {
    if (!pool || !str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = (char*)mem_alloc(pool, len);
    if (!copy) return NULL;
    memcpy(copy, str, len);
    return copy;
}

char* mem_sprintf(mem_pool_t* pool, const char* fmt, ...) {
    if (!pool || !fmt) return NULL;
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len < 0) return NULL;
    char* buf = (char*)mem_alloc(pool, (size_t)len + 1);
    if (!buf) return NULL;
    va_start(args, fmt);
    vsnprintf(buf, (size_t)len + 1, fmt, args);
    va_end(args);
    return buf;
}

/* ── buffer management ────────────────────────────────────── */

static void pool_return_buffer_to_allocator_nolock(mem_pool_t* pool, mem_buffer_t* buffer) {
    if (!pool || !buffer) return;
    if (buffer->is_oversized) {
        size_t total_size = sizeof(mem_buffer_t) + buffer->capacity;
        atomic_fetch_sub(&pool->total_used, total_size);
        free(buffer);
    } else {
        void **tag_ptr = (void **)((char *)buffer - sizeof(void *));
        mem_slab_t *slab = (mem_slab_t *)(*tag_ptr);
        free_node_t *node = (free_node_t *)buffer;
        node->next = slab->free_list;
        slab->free_list = node;
        slab->free_count++;
        atomic_fetch_sub(&pool->total_used, slab->block_size);
    }
}

mem_buffer_t* mem_get_buffer(mem_pool_t* pool, size_t min_size) {
    if (!pool) return NULL;
    if (min_size > SIZE_MAX - (MEM_ALIGNMENT - 1U)) return NULL;

    size_t aligned_size = align_size(min_size, MEM_ALIGNMENT);
    if (aligned_size > SIZE_MAX - sizeof(mem_buffer_t)) return NULL;
    size_t total_size = sizeof(mem_buffer_t) + aligned_size;
    if (total_size > SIZE_MAX - sizeof(slab_tag_t)) return NULL;
    size_t total_with_tag = total_size + sizeof(slab_tag_t);

    mem_buffer_t* buffer = internal_buffer_cache_take(pool, aligned_size);
    if (buffer) return buffer;

    cmeta_mutex_lock(get_pool_lock(pool));
    (void)internal_buffer_cache_ensure_nolock(pool);
    {
        int class_idx = size_class_index(total_with_tag);
        void* raw;

        if (class_idx >= 0) {
            raw = slab_alloc_nolock(pool, class_idx);
            if (raw) {
                buffer = (mem_buffer_t*)raw;
                buffer->is_oversized = 0;
            }
        } else {
            raw = malloc(total_size);
            if (raw) {
                atomic_fetch_add(&pool->total_used, total_size);
                buffer = (mem_buffer_t*)raw;
                buffer->is_oversized = 1;
            }
        }

        if (raw) {
            buffer->data = (char*)buffer + sizeof(mem_buffer_t);
            buffer->capacity = aligned_size;
            buffer->used = 0;
            atomic_store(&buffer->ref_count, 1);
            buffer->pool = pool;
            buffer->next = NULL;
            buffer->is_external = 0;
            buffer->free_cb = NULL;
            buffer->free_user_data = NULL;
        }
    }
    cmeta_mutex_unlock(get_pool_lock(pool));
    return buffer;
}

void mem_ref(mem_buffer_t* buffer) {
    if (!buffer) return;
    atomic_fetch_add(&buffer->ref_count, 1);
}

mem_buffer_t* mem_buffer_retain(mem_buffer_t* buffer) {
    mem_ref(buffer);
    return buffer;
}

static void release_external_buffer(mem_buffer_t* buffer) {
    if (buffer->free_cb) {
        buffer->free_cb(buffer->data, buffer->free_user_data);
    }

    buffer->data = NULL;
    buffer->capacity = 0;
    buffer->used = 0;
    atomic_store_explicit(&buffer->ref_count, 0, memory_order_relaxed);
    buffer->pool = NULL;
    buffer->next = NULL;
    buffer->is_external = 0;
    buffer->is_oversized = 0;
    buffer->free_cb = NULL;
    buffer->free_user_data = NULL;
    if (!external_wrapper_pool_put(buffer)) free(buffer);
}

static void release_internal_buffer(mem_buffer_t* buffer) {
    mem_pool_t* pool = buffer->pool;
    if (!pool) return;

    if (buffer->is_oversized) {
        size_t total_size = sizeof(mem_buffer_t) + buffer->capacity;
        atomic_fetch_sub(&pool->total_used, total_size);
        free(buffer);
        return;
    }

    if (!internal_buffer_cache_put(pool, buffer)) {
        cmeta_mutex_lock(get_pool_lock(pool));
        pool_return_buffer_to_allocator_nolock(pool, buffer);
        cmeta_mutex_unlock(get_pool_lock(pool));
    }
}

void mem_unref(mem_buffer_t* buffer) {
    if (!buffer) return;
    uint32_t old_ref = atomic_fetch_sub(&buffer->ref_count, 1);
    if (old_ref == 1) {
        buffer->is_external ? release_external_buffer(buffer) : release_internal_buffer(buffer);
    }
}

void mem_release(mem_buffer_t* buffer) {
    if (buffer) mem_unref(buffer);
}

void mem_buffer_release(mem_buffer_t* buffer) {
    mem_release(buffer);
}

mem_slice_t mem_slice(mem_buffer_t* buffer, size_t offset, size_t length) {
    mem_slice_t slice = {0};
    size_t remaining;

    if (!buffer || offset >= buffer->used) return slice;
    remaining = buffer->used - offset;
    if (length > remaining) length = remaining;
    slice.data = buffer->data + offset;
    slice.length = length;
    slice.buffer = buffer;
    mem_ref(buffer);
    return slice;
}

void mem_slice_release(mem_slice_t* slice) {
    if (slice && slice->buffer) {
        mem_unref(slice->buffer);
        memset(slice, 0, sizeof(*slice));
    }
}

/* ── external wrapping ────────────────────────────────────── */
mem_buffer_t* mem_wrap_external(void* data, size_t size, void (*free_cb)(void*, void*), void* user_data) {
    if (!data || size == 0) return NULL;
    mem_buffer_t* buffer = external_wrapper_pool_take();
    if (!buffer) buffer = (mem_buffer_t*)malloc(sizeof(mem_buffer_t));
    if (!buffer) return NULL;
    buffer->data = (char*)data;
    buffer->capacity = size;
    buffer->used = size;
    atomic_store(&buffer->ref_count, 1);
    buffer->is_external = 1;
    buffer->is_oversized = 0;
    buffer->free_cb = free_cb;
    buffer->free_user_data = user_data;
    buffer->pool = NULL;
    buffer->next = NULL;
    return buffer;
}

int mem_is_external(const mem_buffer_t* buffer) {
    return buffer ? buffer->is_external : 0;
}

size_t mem_pool_total_allocated(const mem_pool_t* pool) {
    return pool ? atomic_load(&((mem_pool_t*)pool)->total_allocated) : 0;
}

size_t mem_pool_total_used(const mem_pool_t* pool) {
    return pool ? atomic_load(&((mem_pool_t*)pool)->total_used) : 0;
}

size_t mem_pool_recycle_count(const mem_pool_t* pool) {
    return internal_buffer_cache_count(pool);
}

char* mem_buffer_data(mem_buffer_t* buffer) {
    return buffer ? buffer->data : NULL;
}

const char* mem_buffer_const_data(const mem_buffer_t* buffer) {
    return buffer ? buffer->data : NULL;
}

size_t mem_buffer_capacity(const mem_buffer_t* buffer) {
    return buffer ? buffer->capacity : 0;
}

size_t mem_buffer_used(const mem_buffer_t* buffer) {
    return buffer ? buffer->used : 0;
}

uint32_t mem_buffer_ref_count(const mem_buffer_t* buffer) {
    return buffer ? atomic_load(&((mem_buffer_t*)buffer)->ref_count) : 0;
}

const mem_pool_t* mem_buffer_pool(const mem_buffer_t* buffer) {
    return buffer ? buffer->pool : NULL;
}
