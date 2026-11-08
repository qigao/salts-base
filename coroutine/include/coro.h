/**
 * @file coro.h
 * @brief Lightweight coroutine support backed by vendor/minicoro.
 *
 * Wraps minicoro for stackful asymmetric coroutines. This header is the
 * primitive coroutine layer: it owns coroutine lifecycle, cooperative yielding,
 * and a small scheduler. Higher-level modules may layer event loops, object
 * pools, sockets, or task APIs on top.
 */

#ifndef coro_H
#define coro_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Coroutine handle (opaque) */
typedef struct coro_s coro_t;

/** Coroutine entry function */
typedef void (*coro_fn)(coro_t *co, void *arg);

/** Coroutine state */
typedef enum {
  coro_DEAD = 0, /**< Finished or never started */
  coro_READY,    /**< Created, not yet resumed for the first time */
  coro_RUNNING,  /**< Currently executing on the call stack */
  coro_SUSPENDED /**< Yielded (or resumed a child) — waiting to run again */
} coro_state_t;

/** Coroutine creation options */
typedef struct {
  size_t stack_size;   /**< Stack size (0 = default 128KiB) */
  size_t storage_size; /**< Storage buffer size (0 = default 1KB) */
  void *user_data;     /**< User data accessible from coroutine */
} coro_opts_t;

/** Default options */
#define coro_OPTS_DEFAULT {0, 0, NULL}

// =============================================================================
// Lifecycle (Advanced API - Manual Management)
// =============================================================================

/**
 * @brief Create a coroutine (Advanced API)
 *
 * @warning This is a low-level API. Manual management requires:
 *          - Calling coro_resume() to start/continue execution
 *          - Calling coro_destroy() when done
 *          - Careful handling of coroutine state and cleanup
 *
 * @param fn Entry function
 * @param arg Argument passed to entry function
 * @param opts Options (NULL for defaults)
 * @return Coroutine handle or NULL on failure
 */
coro_t *coro_create(coro_fn fn, void *arg, const coro_opts_t *opts);

/**
 * @brief Destroy a coroutine (Advanced API)
 *
 * @warning Only call this for coroutines that are not owned by a scheduler or
 *          a higher-level lifecycle adapter.
 *
 * @param co Coroutine to destroy
 */
void coro_destroy(coro_t *co);

// =============================================================================
// Execution Control
// =============================================================================

/**
 * @brief Resume a coroutine (start or continue execution) (Advanced API)
 *
 * @warning This is for manually-created coroutines only.
 *          Coroutines spawned via coro_context_spawn() are automatically
 *          resumed by the scheduler.
 *
 * @param co Coroutine to resume
 * @return 0 on success, -1 on error
 */
int coro_resume(coro_t *co);

/**
 * @brief Yield from current coroutine (suspend execution)
 * @return 0 on success, -1 on error
 *
 * @note Must be called from within a coroutine
 */
int coro_yield(void);

/**
 * @brief Reset a coroutine for reuse (Advanced API)
 *
 * This allows reusing the coroutine's stack and memory buffer for a new
 * entry function. The coroutine must be in DEAD state.
 *
 * @param co Coroutine to reset
 * @param fn New entry function
 * @param arg New argument
 * @return 0 on success, -1 on error
 */
int coro_reset(coro_t *co, coro_fn fn, void *arg);

/**
 * @brief Get current coroutine state
 * @param co Coroutine to query
 * @return Current state
 */
coro_state_t coro_state(coro_t *co);

/**
 * @brief Check if coroutine is alive (not dead)
 * @param co Coroutine to check
 * @return 1 if alive, 0 if dead
 */
int coro_alive(coro_t *co);

// =============================================================================
// Context
// =============================================================================

/**
 * @brief Get currently running coroutine
 * @return Current coroutine or NULL if not in a coroutine
 */
coro_t *coro_running(void);

/**
 * @brief Get user data from coroutine
 * @param co Coroutine
 * @return User data pointer
 */
void *coro_get_data(coro_t *co);

/**
 * @brief Set user data on coroutine
 * @param co Coroutine
 * @param data User data pointer
 */
void coro_set_data(coro_t *co, void *data);

/**
 * @brief Get lifecycle owner metadata from coroutine.
 *
 * This slot is reserved for lifecycle adapters such as coroutine pools. It is
 * independent from user data so pooled coroutines do not need to overwrite
 * application-owned data.
 *
 * @param co Coroutine
 * @return Owner metadata pointer
 */
void *coro_get_owner_data(coro_t *co);

/**
 * @brief Set lifecycle owner metadata on coroutine.
 * @param co Coroutine
 * @param data Owner metadata pointer
 */
void coro_set_owner_data(coro_t *co, void *data);

// =============================================================================
// Data Passing (LIFO storage buffer)
// =============================================================================

/**
 * @brief Push data to coroutine storage
 * @param co Coroutine
 * @param data Data to push
 * @param size Size in bytes
 * @return 0 on success, -1 on error
 */
int coro_push(coro_t *co, const void *data, size_t size);

/**
 * @brief Pop data from coroutine storage
 * @param co Coroutine
 * @param data Buffer to receive data
 * @param size Size in bytes
 * @return 0 on success, -1 on error
 */
int coro_pop(coro_t *co, void *data, size_t size);

/**
 * @brief Get bytes available in storage
 * @param co Coroutine
 * @return Bytes stored
 */
size_t coro_bytes_stored(coro_t *co);

// =============================================================================
// Coroutine Scheduler
// =============================================================================

/** Scheduler handle (opaque) */
typedef struct coro_scheduler_s coro_scheduler_t;

/**
 * @brief Create a coroutine scheduler
 * @return Scheduler or NULL on failure
 */
coro_scheduler_t *coro_scheduler_create(void);

/**
 * @brief Destroy scheduler
 *
 * @warning Prefer draining the scheduler before destroying it. Any remaining
 *          coroutines are force-destroyed; adapters that keep external
 *          bookkeeping must register a discard callback with
 *          coro_set_discard().
 *
 * @param sched Scheduler to destroy
 */
void coro_scheduler_destroy(coro_scheduler_t *sched);

/**
 * @brief Spawn a new coroutine in the scheduler (lazy start).
 *
 * The coroutine is added to the scheduler's tail and will run on the
 * next scheduler tick — it does NOT execute immediately.
 *
 * @param sched Scheduler
 * @param fn    Entry function
 * @param arg   Argument passed to entry function
 * @param opts  Options (NULL for defaults)
 * @return Coroutine handle or NULL on failure
 */
coro_t *coro_spawn(coro_scheduler_t *sched, coro_fn fn, void *arg, const coro_opts_t *opts);

/**
 * @brief Adopt an existing coroutine into the scheduler.
 * @param sched Scheduler
 * @param co    Coroutine to adopt
 */
void coro_scheduler_adopt(coro_scheduler_t *sched, coro_t *co);

/**
 * @brief Run one scheduling round (resume all ready coroutines once)
 * @param sched Scheduler
 * @return Number of coroutines still alive
 */
int coro_scheduler_tick(coro_scheduler_t *sched);

/**
 * @brief Run until all coroutines complete
 * @param sched Scheduler
 */
void coro_scheduler_run(coro_scheduler_t *sched);

/**
 * @brief Get number of active coroutines
 * @param sched Scheduler
 * @return Number of alive coroutines
 */
int coro_scheduler_count(coro_scheduler_t *sched);

/**
 * @brief Check if any managed coroutine is ready to run (not blocked on I/O).
 * @param sched Scheduler
 * @return 1 if any coroutine is ready, 0 otherwise
 */
int coro_scheduler_has_ready(coro_scheduler_t *sched);

/**
 * @brief Get scheduler from current coroutine
 * @return Scheduler or NULL if not in a scheduled coroutine
 */
coro_scheduler_t *coro_current_scheduler(void);

/**
 * @brief Check if a coroutine is managed by a scheduler
 * @param co Coroutine to check
 * @return 1 if managed by scheduler, 0 if manually managed
 */
int coro_is_scheduled(coro_t *co);

/**
 * @brief Mark coroutine as waiting for I/O (internal/advanced use only)
 *
 * @warning This is an internal API for integration with I/O event loops.
 *          Incorrect use can cause coroutines to never be scheduled again.
 *          Most users should NOT call this directly.
 *
 * @param co Coroutine
 * @param waiting 1 = waiting for I/O, 0 = ready to run
 */
void coro_set_waiting_for_io(coro_t *co, int waiting);

/**
 * @brief Set cleanup callback for coroutine
 * @param co Coroutine
 * @param fn Cleanup function
 * @param arg Cleanup argument
 */
void coro_set_cleanup(coro_t *co, void (*fn)(coro_t *co, void *arg), void *arg);

/**
 * @brief Set forced-discard callback for scheduler teardown.
 *
 * Completion cleanup is only valid once a coroutine reaches DEAD state.
 * This callback lets higher-level adapters release external bookkeeping when
 * coro_scheduler_destroy() must force-destroy a still-live coroutine.
 *
 * @param co Coroutine
 * @param fn Discard function
 * @param arg Discard argument
 */
void coro_set_discard(coro_t *co, void (*fn)(coro_t *co, void *arg), void *arg);

/**
 * @brief Remove coroutine from scheduler without destroying it.
 * @param co Coroutine to detach
 */
void coro_detach_scheduler(coro_t *co);

#ifdef __cplusplus
}
#endif

#endif /* coro_H */
