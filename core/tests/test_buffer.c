#include <memory/buffer.h>
#include <tinytest.h>
#include <core/error_codes.h>
#include <atomic/atomic.h>
#include <threads/threads.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void external_release(void *data, void *ctx) {
  unsigned *calls = (unsigned *)ctx;
  ++*calls;
  free(data);
}

enum { REUSE_WORKERS = 16, REUSE_ITERATIONS = 512, REUSE_WORDS = 8 };

typedef struct reuse_worker {
  mem_pool_t *pool;
  atomic32 *start;
  unsigned id;
  int failed; /* Written by this worker, read only after join. */
} reuse_worker_t;

static void concurrent_reuse(void *arg) {
  reuse_worker_t *worker = (reuse_worker_t *)arg;
  uint32_t started = 0;
  while (!started) {
    if (atomic32_load(worker->start, SALTS_MEMORY_ACQUIRE, &started) != SALTS_OK) {
      worker->failed = 1;
      return;
    }
    if (!started) sync_thread_yield();
  }

  for (unsigned iteration = 0; iteration < REUSE_ITERATIONS; ++iteration) {
    uint64_t expected[REUSE_WORDS];
    mem_buffer_t *buffer = mem_get_buffer(worker->pool, sizeof(expected));
    mem_slice_t retained;
    if (!buffer) {
      worker->failed = 1;
      return;
    }
    if (mem_buffer_capacity(buffer) < sizeof(expected) ||
        mem_buffer_pool(buffer) != worker->pool ||
        mem_buffer_ref_count(buffer) != 1u || mem_buffer_used(buffer) != 0u) {
      worker->failed = 1;
      mem_buffer_release(buffer);
      return;
    }
    for (unsigned word = 0; word < REUSE_WORDS; ++word)
      expected[word] = ((uint64_t)worker->id << 32) | iteration;
    memcpy(mem_buffer_data(buffer), expected, sizeof(expected));
    mem_set_used(buffer, sizeof(expected));
    retained = mem_slice(buffer, 0u, sizeof(expected));
    mem_buffer_release(buffer);

    /* Other workers churn the same pool while this slice owns its storage.
     * No per-iteration test lock or fence may mask the pool's publication. */
    sync_thread_yield();
    if (!retained.buffer || !retained.data ||
        retained.length != sizeof(expected) ||
        mem_buffer_pool(retained.buffer) != worker->pool ||
        mem_buffer_ref_count(retained.buffer) != 1u ||
        mem_buffer_used(retained.buffer) != sizeof(expected) ||
        memcmp(retained.data, expected, sizeof(expected)) != 0)
      worker->failed = 1;
    mem_slice_release(&retained);
    if (worker->failed) return;
  }
}

spec("Core buffer lifetime") {
  it("keeps retained slices stable during concurrent pool reuse") {
    mem_pool_t pool = {0};
    sync_thread_t threads[REUSE_WORKERS] = {0};
    reuse_worker_t workers[REUSE_WORKERS] = {0};
    atomic32 start;
    unsigned created = 0;
    unsigned failed = 0;

    check(atomic32_init(&start, 0u) == SALTS_OK);
    check(mem_init(&pool, 0u) == 0);
    for (; created < REUSE_WORKERS; ++created) {
      workers[created].pool = &pool;
      workers[created].start = &start;
      workers[created].id = created + 1u;
      if (sync_thread_create(&threads[created], concurrent_reuse,
                             &workers[created]) != SALTS_OK)
        break;
    }
    /* Release even a partially created group, then reclaim only after join.
     * Invalid gate/join state cannot safely leave live workers on this stack. */
    if (atomic32_store(&start, 1u, SALTS_MEMORY_RELEASE) != SALTS_OK) abort();
    for (unsigned i = 0; i < created; ++i) {
      if (sync_thread_join(&threads[i]) != SALTS_OK) abort();
      failed += workers[i].failed != 0;
    }
    mem_destroy(&pool);
    check(created == REUSE_WORKERS);
    check(failed == 0u);
  }

  it("preserves borrowed slices, refcounts, external release and allocation limits") {
    mem_pool_t pool = {0};
    mem_buffer_t *buffer = NULL;
    mem_buffer_t *external = NULL;
    mem_slice_t slice = {0};
    char *owned = NULL;
    unsigned released = 0;
    char *storage = NULL;

    check(mem_init(&pool, 0) == 0);
    buffer = mem_get_buffer(&pool, 32u);
    check(buffer != NULL && mem_buffer_capacity(buffer) >= 32u &&
          mem_buffer_ref_count(buffer) == 1u && !mem_is_external(buffer));

    memcpy(mem_buffer_data(buffer), "buffer", 6u);
    mem_set_used(buffer, 6u);
    slice = mem_slice(buffer, 1u, 99u);
    check(slice.length == 5u && memcmp(slice.data, "uffer", 5u) == 0 &&
          mem_buffer_ref_count(buffer) == 2u);
    mem_buffer_release(buffer); /* Slice still owns buffer. */
    check(slice.data != NULL && memcmp(slice.data, "uffer", 5u) == 0);
    mem_slice_release(&slice);
    check(slice.buffer == NULL && slice.data == NULL && slice.length == 0u);

    storage = (char *)malloc(4u);
    check(storage != NULL);
    memcpy(storage, "abcd", 4u);
    external = mem_wrap_external(storage, 4u, external_release, &released);
    if (external == NULL) free(storage);
    check(external != NULL);
    check(mem_is_external(external) && mem_buffer_used(external) == 4u);
    mem_buffer_retain(external);
    mem_buffer_release(external);
    check(released == 0u);
    mem_buffer_release(external);
    check(released == 1u);

    owned = mem_strdup(&pool, "ok");
    check(owned != NULL && strcmp(owned, "ok") == 0);
    mem_free(&pool, owned);
    check(mem_alloc_array(&pool, SIZE_MAX, 2u) == NULL);
    mem_destroy(&pool);
  }
}
