/**
 * @file coro_pool.c
 * @brief Generic bounded coroutine reuse pool.
 */

#include "coro_pool.h"
#include <assert.h>
#include <stdlib.h>

typedef struct coro_pool_entry_s {
  coro_pool_t *owner;
  coro_t *coro;
  int is_free;
  struct coro_pool_entry_s *next;
} coro_pool_entry_t;

struct coro_pool_s {
  coro_pool_config_t config;
  coro_pool_entry_t *free_list;
  size_t free_count;
  size_t active_count;
  size_t total_capacity;
};

static coro_pool_entry_t *coro_pool_create_entry(coro_pool_t *pool);
static void coro_pool_destroy_entry(coro_pool_t *pool, coro_pool_entry_t *entry);
static void coro_pool_push_free(coro_pool_t *pool, coro_pool_entry_t *entry);
static void coro_pool_discard_callback(coro_t *co, void *arg);
static void coro_pool_scheduler_cleanup(coro_t *co, void *arg);

static void *coro_pool_alloc_entry(coro_pool_t *pool, size_t size) {
  if (pool->config.alloc_fn) {
    return pool->config.alloc_fn(pool->config.allocator_data, size);
  }
  return calloc(1, size);
}

static void coro_pool_free_entry(coro_pool_t *pool, void *ptr) {
  if (!ptr) return;
  if (pool->config.free_fn) {
    pool->config.free_fn(pool->config.allocator_data, ptr);
  } else if (!pool->config.alloc_fn) {
    free(ptr);
  }
}

coro_pool_t *coro_pool_create(const coro_pool_config_t *config) {
  coro_pool_config_t defaults = CORO_POOL_CONFIG_DEFAULT;
  coro_pool_t *pool = NULL;

  if (!config) {
    config = &defaults;
  }

  pool = (coro_pool_t *)calloc(1, sizeof(*pool));
  if (!pool) {
    return NULL;
  }

  pool->config = *config;

  for (size_t i = 0; i < pool->config.initial_capacity; i++) {
    coro_pool_entry_t *entry = coro_pool_create_entry(pool);
    if (!entry) {
      coro_pool_destroy(pool);
      return NULL;
    }
    pool->total_capacity++;
    coro_pool_push_free(pool, entry);
  }

  return pool;
}

void coro_pool_destroy(coro_pool_t *pool) {
  if (!pool) return;
  assert(pool->active_count == 0 && "destroying coroutine pool with active coroutines");

  coro_pool_entry_t *entry = pool->free_list;
  while (entry) {
    coro_pool_entry_t *next = entry->next;
    coro_pool_destroy_entry(pool, entry);
    entry = next;
  }

  free(pool);
}

coro_t *coro_pool_acquire(coro_pool_t *pool, coro_fn fn, void *arg) {
  coro_pool_entry_t *entry = NULL;

  if (!pool || !fn) {
    return NULL;
  }

  if (pool->free_list) {
    entry = pool->free_list;
    pool->free_list = entry->next;
    pool->free_count--;
    entry->next = NULL;
    entry->is_free = 0;
  } else {
    if (pool->config.max_capacity > 0 && pool->total_capacity >= pool->config.max_capacity) {
      return NULL;
    }
    entry = coro_pool_create_entry(pool);
    if (!entry) {
      return NULL;
    }
    pool->total_capacity++;
  }

  if (!entry->coro) {
    coro_opts_t opts = {.stack_size = pool->config.stack_size,
                        .storage_size = pool->config.storage_size,
                        .user_data = NULL};
    entry->coro = coro_create(fn, arg, &opts);
    if (!entry->coro) {
      coro_pool_push_free(pool, entry);
      return NULL;
    }
  } else if (coro_reset(entry->coro, fn, arg) != 0) {
    coro_pool_push_free(pool, entry);
    return NULL;
  }

  coro_set_owner_data(entry->coro, entry);
  coro_set_discard(entry->coro, coro_pool_discard_callback, NULL);
  pool->active_count++;
  return entry->coro;
}

void coro_pool_release(coro_pool_t *pool, coro_t *co) {
  coro_pool_entry_t *entry = NULL;

  if (!pool || !co) return;
  if (coro_state(co) != coro_DEAD) return;

  entry = (coro_pool_entry_t *)coro_get_owner_data(co);
  if (!entry || entry->owner != pool) {
    coro_destroy(co);
    return;
  }
  if (entry->is_free) return;

  coro_detach_scheduler(co);
  coro_set_discard(co, NULL, NULL);

  if (pool->active_count > 0) {
    pool->active_count--;
  }
  coro_pool_push_free(pool, entry);
}

int coro_pool_abandon(coro_pool_t *pool, coro_t *co) {
  coro_pool_entry_t *entry = NULL;

  if (!pool || !co || coro_state(co) == coro_RUNNING) return -1;
  entry = (coro_pool_entry_t *)coro_get_owner_data(co);
  if (!entry || entry->owner != pool || entry->is_free || entry->coro != co) return -1;

  coro_detach_scheduler(co);
  coro_set_owner_data(co, NULL);
  coro_set_discard(co, NULL, NULL);
  entry->coro = NULL;
  if (pool->active_count > 0) pool->active_count--;
  coro_destroy(co);
  coro_pool_push_free(pool, entry);
  return 0;
}

void coro_pool_discard_coro(coro_t *co) {
  coro_pool_entry_t *entry = NULL;
  coro_pool_t *pool = NULL;

  if (!co) return;

  entry = (coro_pool_entry_t *)coro_get_owner_data(co);
  if (!entry || !entry->owner || entry->is_free) return;

  pool = entry->owner;
  coro_detach_scheduler(co);
  coro_set_owner_data(co, NULL);
  coro_set_discard(co, NULL, NULL);
  entry->coro = NULL;

  if (pool->active_count > 0) {
    pool->active_count--;
  }
  coro_pool_push_free(pool, entry);
}

void coro_pool_forget_active(coro_pool_t *pool) {
  if (pool) {
    pool->active_count = 0;
  }
}

coro_t *coro_spawn_pooled(coro_scheduler_t *sched, coro_pool_t *pool, coro_fn fn,
                                void *arg) {
  coro_t *co = NULL;

  if (!sched || !pool || !fn) {
    return NULL;
  }

  co = coro_pool_acquire(pool, fn, arg);
  if (!co) {
    return NULL;
  }

  coro_set_cleanup(co, coro_pool_scheduler_cleanup, pool);
  coro_scheduler_adopt(sched, co);
  return co;
}

size_t coro_pool_free_count(const coro_pool_t *pool) {
  return pool ? pool->free_count : 0;
}

size_t coro_pool_active_count(const coro_pool_t *pool) {
  return pool ? pool->active_count : 0;
}

size_t coro_pool_capacity(const coro_pool_t *pool) {
  return pool ? pool->total_capacity : 0;
}

size_t coro_pool_retained_count(const coro_pool_t *pool) {
  size_t retained;
  coro_pool_entry_t *entry;
  if (!pool) return 0;
  retained = pool->active_count;
  for (entry = pool->free_list; entry; entry = entry->next)
    if (entry->coro) retained++;
  return retained;
}

static coro_pool_entry_t *coro_pool_create_entry(coro_pool_t *pool) {
  coro_pool_entry_t *entry =
      (coro_pool_entry_t *)coro_pool_alloc_entry(pool, sizeof(*entry));
  if (!entry) return NULL;
  entry->owner = pool;
  entry->coro = NULL;
  entry->is_free = 0;
  entry->next = NULL;
  return entry;
}

static void coro_pool_destroy_entry(coro_pool_t *pool, coro_pool_entry_t *entry) {
  if (!entry) return;
  if (entry->coro) {
    coro_set_owner_data(entry->coro, NULL);
    coro_set_discard(entry->coro, NULL, NULL);
    coro_destroy(entry->coro);
    entry->coro = NULL;
  }
  coro_pool_free_entry(pool, entry);
}

static void coro_pool_push_free(coro_pool_t *pool, coro_pool_entry_t *entry) {
  if (!pool || !entry || entry->is_free) return;
  entry->is_free = 1;
  entry->next = pool->free_list;
  pool->free_list = entry;
  pool->free_count++;
}

static void coro_pool_discard_callback(coro_t *co, void *arg) {
  (void)arg;
  coro_pool_discard_coro(co);
}

static void coro_pool_scheduler_cleanup(coro_t *co, void *arg) {
  coro_pool_release((coro_pool_t *)arg, co);
}
