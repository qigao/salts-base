/**
 * @file coro.c
 * @brief Coroutine implementation using vendor/minicoro.
 */

#define MINICORO_IMPL
#include "coro.h"
#include "minicoro.h"
#include <assert.h>
#include <stdint.h>
#include <thread.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Coroutine
// =============================================================================

// Internal coroutine structure
struct coro_s {
  mco_coro *mco;                             // minicoro handle
  void *mco_allocation;                      // owning allocation for coro shell + minicoro object
  coro_fn fn;                                // user entry function
  void *arg;                                 // user argument
  void *user_data;                           // user data
  void *owner_data;                          // lifecycle adapter metadata
  coro_scheduler_t *scheduler;               // owning scheduler (if any)
  coro_t *next;                              // linked list for scheduler
  coro_t *prev;                              // doubly linked list for O(1) removal
  size_t stack_size;                         // saved for reset
  size_t storage_size;                       // saved for reset
  uint8_t waiting_for_io;                    // 1 = blocked on I/O, skip in scheduler
  uint8_t in_ready_queue;                    // 1 = already in the ready queue
  coro_t *ready_next;                        // next in ready queue
  coro_t *ready_prev;                        // prev in ready queue (for O(1) removal)
  void (*cleanup_fn)(coro_t *co, void *arg); // cleanup callback
  void *cleanup_arg;                         // cleanup argument
  void (*discard_fn)(coro_t *co, void *arg); // force-destroy callback
  void *discard_arg;                         // force-destroy argument
};

struct coro_scheduler_s {
  coro_t *head; // linked list of ALL coroutines (for cleanup)
  coro_t *tail;
  coro_t *ready_head; // linked list of READY coroutines
  coro_t *ready_tail;
  int count;       // number of alive coroutines
  int ready_count; // number of ready coroutines
};

// Thread-local current scheduler (for coro_current_scheduler)
static SALTS_THREAD_LOCAL coro_scheduler_t *tls_current_scheduler = NULL;

// Wrapper to adapt minicoro callback to our API
static void coro_entry_wrapper(mco_coro *mco) {
  coro_t *co = (coro_t *)mco_get_user_data(mco);
  if (co && co->fn) {
    co->fn(co, co->arg);
  }
}

static void coro_scheduler_link_ready(coro_scheduler_t *sched, coro_t *co) {
  co->scheduler = sched;
  co->next = NULL;
  co->prev = sched->tail;

  if (sched->tail) {
    sched->tail->next = co;
  } else {
    sched->head = co;
  }
  sched->tail = co;
  sched->count++;

  co->waiting_for_io = 0;
  co->in_ready_queue = 1;
  co->ready_next = NULL;
  co->ready_prev = sched->ready_tail;
  if (sched->ready_tail) {
    sched->ready_tail->ready_next = co;
  } else {
    sched->ready_head = co;
  }
  sched->ready_tail = co;
  sched->ready_count++;
}

static void *coro_alloc_combined_block(size_t coro_size) {
  const size_t align = sizeof(void *) > 16 ? sizeof(void *) : 16;
  const size_t prefix = sizeof(coro_t) + align - 1;
  size_t total;
  if (coro_size > SIZE_MAX - prefix) return NULL;
  total = prefix + coro_size;
  return calloc(1, total);
}

static mco_coro *coro_block_to_mco(void *block) {
  const uintptr_t base = (uintptr_t)block + sizeof(coro_t);
  const uintptr_t aligned = (base + 15u) & ~(uintptr_t)15u;
  return (mco_coro *)aligned;
}

coro_t *coro_create(coro_fn fn, void *arg, const coro_opts_t *opts) {
  if (!fn) return NULL;
  if (opts && opts->stack_size > SIZE_MAX - 15u) return NULL;

  // Setup minicoro descriptor
  mco_desc desc =
      mco_desc_init(coro_entry_wrapper, opts && opts->stack_size ? opts->stack_size : 0);

  if (opts && opts->storage_size) {
    desc.storage_size = opts->storage_size;
  }

  void *block = coro_alloc_combined_block(desc.coro_size);
  if (!block) return NULL;

  coro_t *co = (coro_t *)block;
  co->mco_allocation = block;
  co->mco = coro_block_to_mco(block);
  co->fn = fn;
  co->arg = arg;
  co->cleanup_fn = NULL;
  co->cleanup_arg = NULL;
  co->discard_fn = NULL;
  co->discard_arg = NULL;
  desc.user_data = co;

  if (opts) {
    co->user_data = opts->user_data;
    co->stack_size = opts->stack_size;
  }
  co->storage_size = desc.storage_size;

  // Initialize minicoro in the combined allocation
  mco_result res = mco_init(co->mco, &desc);
  if (res != MCO_SUCCESS) {
    free(block);
    return NULL;
  }

  return co;
}

void coro_detach_scheduler(coro_t *co) {
  if (!co || !co->scheduler) return;

  coro_scheduler_t *sched = co->scheduler;

  /* Remove from main list */
  if (co->prev) co->prev->next = co->next;
  else if (sched->head == co) sched->head = co->next;

  if (co->next) co->next->prev = co->prev;
  else if (sched->tail == co) sched->tail = co->prev;

  /* Remove from ready list if present */
  if (co->in_ready_queue) {
    if (co->ready_prev) co->ready_prev->ready_next = co->ready_next;
    else if (sched->ready_head == co) sched->ready_head = co->ready_next;

    if (co->ready_next) co->ready_next->ready_prev = co->ready_prev;
    else if (sched->ready_tail == co) sched->ready_tail = co->ready_prev;

    sched->ready_count--;
    co->in_ready_queue = 0;
  }

  sched->count--;
  co->scheduler = NULL;
}

void coro_destroy(coro_t *co) {
  if (!co) return;

  coro_detach_scheduler(co);

  if (co->mco) {
    mco_uninit(co->mco);
  }
  free(co->mco_allocation ? co->mco_allocation : co);
}

int coro_resume(coro_t *co) {
  if (!co || !co->mco) return -1;
  mco_result res = mco_resume(co->mco);
  return (res == MCO_SUCCESS) ? 0 : -1;
}

int coro_reset(coro_t *co, coro_fn fn, void *arg) {
  if (!co || !co->mco || !fn) return -1;

  if (co->scheduler) return -1;

  // Verify coroutine is dead or hasn't started
  mco_state status = mco_status(co->mco);
  if (status != MCO_DEAD && status != MCO_SUSPENDED) return -1;

  // Uninit without freeing the stack memory
  mco_uninit(co->mco);

  co->fn = fn;
  co->arg = arg;
  co->waiting_for_io = 0;
  co->in_ready_queue = 0;
  co->next = NULL;
  co->prev = NULL;
  co->ready_next = NULL;
  co->ready_prev = NULL;
  co->cleanup_fn = NULL;
  co->cleanup_arg = NULL;
  co->discard_fn = NULL;
  co->discard_arg = NULL;

  // Re-init with same sizes
  mco_desc desc = mco_desc_init(coro_entry_wrapper, co->stack_size);
  desc.storage_size = co->storage_size;
  desc.user_data = co;

  mco_result res = mco_init(co->mco, &desc);
  return (res == MCO_SUCCESS) ? 0 : -1;
}

int coro_yield(void) {
  mco_coro *mco = mco_running();
  assert(mco && "coro_yield() must be called from within a coroutine");
  mco_result res = mco_yield(mco);
  return (res == MCO_SUCCESS) ? 0 : -1;
}

coro_state_t coro_state(coro_t *co) {
  if (!co || !co->mco) return coro_DEAD;

  mco_state state = mco_status(co->mco);
  switch (state) {
  case MCO_DEAD:
    return coro_DEAD;
  case MCO_RUNNING:
    return coro_RUNNING;
  case MCO_SUSPENDED:
    return coro_SUSPENDED;
  case MCO_NORMAL:
    /* MCO_NORMAL: this coroutine is alive but not the one currently
       running (it resumed a child coroutine). Treat as SUSPENDED —
       it's alive and will be resumed again when the child yields. */
    return coro_SUSPENDED;
  default:
    return coro_DEAD;
  }
}

int coro_alive(coro_t *co) { return coro_state(co) != coro_DEAD; }

coro_t *coro_running(void) {
  mco_coro *mco = mco_running();
  if (!mco) return NULL;
  return (coro_t *)mco_get_user_data(mco);
}

void *coro_get_data(coro_t *co) { return co ? co->user_data : NULL; }

void coro_set_data(coro_t *co, void *data) {
  if (co) co->user_data = data;
}

void *coro_get_owner_data(coro_t *co) { return co ? co->owner_data : NULL; }

void coro_set_owner_data(coro_t *co, void *data) {
  if (co) co->owner_data = data;
}

int coro_push(coro_t *co, const void *data, size_t size) {
  if (!co || !co->mco || !data || !size) return -1;
  mco_result res = mco_push(co->mco, data, size);
  return (res == MCO_SUCCESS) ? 0 : -1;
}

int coro_pop(coro_t *co, void *data, size_t size) {
  if (!co || !co->mco || !data || !size) return -1;
  mco_result res = mco_pop(co->mco, data, size);
  return (res == MCO_SUCCESS) ? 0 : -1;
}

size_t coro_bytes_stored(coro_t *co) {
  if (!co || !co->mco) return 0;
  return mco_get_bytes_stored(co->mco);
}

coro_scheduler_t *coro_scheduler_create(void) {
  coro_scheduler_t *sched = calloc(1, sizeof(coro_scheduler_t));
  return sched;
}

void coro_scheduler_destroy(coro_scheduler_t *sched) {
  if (!sched) return;

  /* Forcefully terminate any remaining coroutines.
   * This can happen during test cleanup when async operations
   * haven't fully drained. In production, proper shutdown should
   * ensure clean exit, but we handle residual coroutines gracefully. */
  coro_t *co = sched->head;
  while (co) {
    coro_t *next = co->next;
    if (co->discard_fn) {
      co->discard_fn(co, co->discard_arg);
    }
    coro_destroy(co);
    co = next;
  }

  free(sched);
}

coro_t *coro_spawn(coro_scheduler_t *sched, coro_fn fn, void *arg, const coro_opts_t *opts) {
  if (!sched || !fn) return NULL;

  coro_t *co = coro_create(fn, arg, opts);
  if (!co) return NULL;

  coro_scheduler_link_ready(sched, co);

  return co;
}

void coro_scheduler_adopt(coro_scheduler_t *sched, coro_t *co) {
  if (!sched || !co) return;

  coro_scheduler_link_ready(sched, co);
}

int coro_scheduler_tick(coro_scheduler_t *sched) {
  if (!sched || !sched->ready_head) return sched ? sched->count : 0;

  coro_scheduler_t *prev_sched = tls_current_scheduler;
  tls_current_scheduler = sched;

  /* Process only the coroutines that were ready at the start of the tick.
     We pop from the head to handle deletions robustly during iteration. */
  int to_process = sched->ready_count;
  while (to_process-- > 0 && sched->ready_head) {
    /* Pop from head */
    coro_t *co = sched->ready_head;
    sched->ready_head = co->ready_next;
    if (sched->ready_head) sched->ready_head->ready_prev = NULL;
    else sched->ready_tail = NULL;

    sched->ready_count--;
    co->in_ready_queue = 0;
    co->ready_next = NULL;
    co->ready_prev = NULL;

    if (coro_alive(co)) {
      coro_resume(co);

      if (!coro_alive(co)) {
        /* Finished - removal logic handled by coro_destroy or manually */
        if (co->cleanup_fn) {
          co->cleanup_fn(co, co->cleanup_arg);
        } else {
          coro_destroy(co);
        }
      } else if (!co->waiting_for_io) {
        /* Still ready, put back in ready queue at the tail for NEXT tick */
        co->in_ready_queue = 1;
        co->ready_next = NULL;
        co->ready_prev = sched->ready_tail;
        if (sched->ready_tail) {
          sched->ready_tail->ready_next = co;
        } else {
          sched->ready_head = co;
        }
        sched->ready_tail = co;
        sched->ready_count++;
      }
    }
  }

  tls_current_scheduler = prev_sched;
  return sched->count;
}

void coro_scheduler_run(coro_scheduler_t *sched) {
  if (!sched) return;

  while (coro_scheduler_tick(sched) > 0) {
    // Keep running until all coroutines complete
  }
}

int coro_scheduler_count(coro_scheduler_t *sched) { return sched ? sched->count : 0; }

int coro_scheduler_has_ready(coro_scheduler_t *sched) {
  return (sched && sched->ready_count > 0) ? 1 : 0;
}

coro_scheduler_t *coro_current_scheduler(void) { return tls_current_scheduler; }

int coro_is_scheduled(coro_t *co) { return (co && co->scheduler) ? 1 : 0; }

void coro_set_waiting_for_io(coro_t *co, int waiting) {
  if (!co) return;
  co->waiting_for_io = (uint8_t)(waiting ? 1 : 0);

  if (!waiting && co->scheduler) {
    /* If this coroutine is the one currently running, don't enqueue it here.
     * The scheduler will decide whether to requeue it when control returns
     * from the current resume. Enqueuing the running coroutine causes stale
     * ready-queue entries and spurious wakeups on later I/O waits. */
    if (co == coro_running()) {
      return;
    }

    /* Push to ready queue if not already there */
    coro_scheduler_t *sched = co->scheduler;
    if (!co->in_ready_queue) {
      co->in_ready_queue = 1;
      co->ready_next = NULL;
      co->ready_prev = sched->ready_tail;
      if (sched->ready_tail) {
        sched->ready_tail->ready_next = co;
      } else {
        sched->ready_head = co;
      }
      sched->ready_tail = co;
      sched->ready_count++;
    }
  }
}

void coro_set_cleanup(coro_t *co, void (*fn)(coro_t *, void *), void *arg) {
  if (co) {
    co->cleanup_fn = fn;
    co->cleanup_arg = arg;
  }
}

void coro_set_discard(coro_t *co, void (*fn)(coro_t *, void *), void *arg) {
  if (co) {
    co->discard_fn = fn;
    co->discard_arg = arg;
  }
}
