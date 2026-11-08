#ifndef SALTS_THREAD_POOL_H
#define SALTS_THREAD_POOL_H

#include <error_codes.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_threadpool_s cmeta_threadpool_t;
typedef void (*cmeta_task_fn)(void *arg);

/**
 * Copied task descriptor for exact terminal lifecycle notification.
 *
 * A successful submission invokes exactly one of run or cancel, then invokes
 * finalize when non-NULL. A rejected submission invokes no callback. The arg
 * payload is borrowed until finalize returns, or until run/cancel returns when
 * finalize is NULL. When finalize is present, run/cancel must leave arg valid
 * for it; final ownership release belongs in finalize. All callbacks run in
 * the pool's callback context and must not destroy or synchronously wait on the
 * same pool.
 */
typedef struct cmeta_threadpool_task_s {
  cmeta_task_fn run;
  cmeta_task_fn cancel;
  cmeta_task_fn finalize;
  void *arg;
} cmeta_threadpool_task_t;

typedef struct {
  int num_threads;
  /* Maximum queued work; running callbacks do not consume this capacity. */
  size_t queue_capacity;
} cmeta_threadpool_config_t;

enum { SALTS_THREADPOOL_LF_VERSION = 1, SALTS_THREADPOOL_LF_MAX_BATCH = 32 };

/** Versioned, explicit configuration for the optional CPU Leader/Followers pool.
 * queue_capacity bounds queued descriptors; up to num_threads * max_batch
 * additional descriptors may be claimed/executing/finalizing. Payload storage
 * is borrowed and must be bounded separately by the application. */
typedef struct cmeta_threadpool_lf_config {
  size_t struct_size;
  uint32_t version;
  int num_threads;
  size_t queue_capacity;
  size_t max_batch;
} cmeta_threadpool_lf_config_t;

/** Initialize struct_size and version before querying. Snapshot counters are
 * mutually consistent under the LF monitor. running/cancelling exclude
 * finalizing; terminal counts include finalize completion. SIZE_MAX leader_id
 * means no worker is eligible to lead, or the pool is closed and settled.
 * An idle open pool may retain a sleeping leader. Wake/election
 * telemetry saturates at UINT64_MAX; rejected_tasks saturates at INT64_MAX.
 * Reaching INT64_MAX accepted tasks closes admission with DRAIN, so lifetime
 * counters never silently wrap. */
typedef struct cmeta_threadpool_lf_stats {
  size_t struct_size;
  uint32_t version;
  size_t max_batch;
  size_t queued_tasks;
  size_t claimed_tasks;
  size_t running_tasks;
  size_t cancelling_tasks;
  size_t finalizing_tasks;
  size_t sleeping_workers;
  size_t leader_id;
  int64_t accepted_tasks;
  int64_t completed_tasks;
  int64_t cancelled_tasks;
  uint64_t handoffs;
  uint64_t wake_signals;
  uint64_t empty_wakes;
} cmeta_threadpool_lf_stats_t;

typedef enum cmeta_threadpool_shutdown_policy {
  SALTS_THREADPOOL_SHUTDOWN_DRAIN = 0,
  SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING
} cmeta_threadpool_shutdown_policy_t;

typedef struct {
  int num_threads;
  /* Configured queued-work limit, excluding active callbacks. */
  size_t queue_capacity;
  int accepting;
  int64_t submitted_tasks;
  int64_t started_tasks;
  int64_t completed_tasks;
  int64_t rejected_tasks;
  int64_t queued_tasks;
  int64_t active_tasks;
  int64_t pending_tasks;
  int64_t peak_pending_tasks;
} cmeta_threadpool_stats_t;

cmeta_threadpool_t *cmeta_threadpool_create(int num_threads);
cmeta_threadpool_t *cmeta_threadpool_create_with_config(const cmeta_threadpool_config_t *config);
/** Create an independent CPU LF pool; does not move or progress any I/O owner.
 * config requires exact struct_size, version=SALTS_THREADPOOL_LF_VERSION,
 * num_threads>0, queue_capacity>0 and 1<=max_batch<=32. Existing create APIs
 * keep their original backend. FIFO claims do not imply ordered execution or
 * completion. Callbacks run outside the monitor; successor election precedes
 * handling. CANCEL_PENDING also cancels claimed tasks that have not started.
 *
 * out_pool must point to NULL; failures preserve it. Returns SALTS_EINVAL for
 * invalid/overflowing configuration, SALTS_ENOMEM for unavailable resources.
 * Join all API callers before destroy, and keep callback code/arg providers
 * alive until callbacks/finalize return. Callbacks must return normally.
 *
 * Example:
 *   cmeta_threadpool_lf_config_t config = {
 *     sizeof(config), SALTS_THREADPOOL_LF_VERSION, 4, 128, 1};
 *   cmeta_threadpool_t *pool = NULL;
 *   int status = cmeta_threadpool_create_leader_followers(&config, &pool);
 *   // On SALTS_OK use try_submit_task, shutdown_with_policy, wait_status,
 *   // then destroy. Rejected task admission invokes no callback.
 */
int cmeta_threadpool_create_leader_followers(
    const cmeta_threadpool_lf_config_t *config, cmeta_threadpool_t **out_pool);
/** Returns SALTS_ENOTSUP for a legacy pool, SALTS_EINVAL for invalid version,
 * size or arguments. Failed queries leave out_stats unchanged. */
int cmeta_threadpool_get_lf_stats(cmeta_threadpool_t *pool,
                                 cmeta_threadpool_lf_stats_t *out_stats);
void cmeta_threadpool_destroy(cmeta_threadpool_t *pool);
/**
 * Submit a copied descriptor, waiting for bounded queue space if necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ESHUTDOWN when admission is closed, or SALTS_EBUSY when a callback on
 * this pool would have to wait for the same saturated pool to make progress.
 */
int cmeta_threadpool_submit_task(cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task);
/**
 * Attempt to submit a copied descriptor without waiting for queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ENOBUFS when the queue is full, or SALTS_ESHUTDOWN when admission is
 * closed.
 */
int cmeta_threadpool_try_submit_task(cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task);
/**
 * Submit a task, waiting for bounded queue space when necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ESHUTDOWN when
 * the pool no longer accepts work, or SALTS_EBUSY when a callback on this pool
 * would have to wait for the same saturated pool to make progress.
 */
int cmeta_threadpool_submit(cmeta_threadpool_t *pool, cmeta_task_fn task, void *arg);
/**
 * Attempt to submit a task without waiting for bounded queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ENOBUFS when
 * the queue is full, or SALTS_ESHUTDOWN when the pool no longer accepts work.
 */
int cmeta_threadpool_try_submit(cmeta_threadpool_t *pool, cmeta_task_fn task, void *arg);
/** Wait for accepted work to settle, rejecting a wait from this pool's callback. */
int cmeta_threadpool_wait_status(cmeta_threadpool_t *pool);
void cmeta_threadpool_wait(cmeta_threadpool_t *pool);
/**
 * End admission and either drain queued callbacks or settle them as cancelled.
 * Repeating the selected policy is idempotent; changing it returns SALTS_EBUSY.
 */
int cmeta_threadpool_shutdown_with_policy(cmeta_threadpool_t *pool,
                                          cmeta_threadpool_shutdown_policy_t policy);
void cmeta_threadpool_shutdown(cmeta_threadpool_t *pool);
int cmeta_threadpool_pending(cmeta_threadpool_t *pool);
int64_t cmeta_threadpool_cancelled(cmeta_threadpool_t *pool);
int cmeta_threadpool_size(cmeta_threadpool_t *pool);
size_t cmeta_threadpool_capacity(cmeta_threadpool_t *pool);
int cmeta_threadpool_is_accepting(cmeta_threadpool_t *pool);
void cmeta_threadpool_get_stats(cmeta_threadpool_t *pool, cmeta_threadpool_stats_t *stats);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_THREAD_POOL_H */
