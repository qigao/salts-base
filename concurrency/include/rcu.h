#ifndef SALTS_RCU_H
#define SALTS_RCU_H

#include <error_codes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_rcu {
  void *impl;
} cmeta_rcu;
typedef struct cmeta_rcu_guard {
  cmeta_rcu *owner;
  const struct cmeta_rcu_guard *self;
  const void *value;
  unsigned epoch;
} cmeta_rcu_guard;

/** Zero-initialized, noncopyable domain; transfers initial on success only.
 * MPMC readers/writers, O(1) short mutex sections, fixed max_readers and one
 * retired snapshot. Published payloads are immutable. Init/destroy require
 * external quiescence. No operation allocates after successful init. */
int cmeta_rcu_init(cmeta_rcu *domain, void *initial, size_t max_readers);
/** Pins one snapshot in a zero-initialized, address-stable noncopyable guard.
 * ENOBUFS is reader exhaustion; ESHUTDOWN is closed admission. A guard may
 * survive suspension and exclusive cross-thread handoff if its storage and
 * domain survive. Cancellation must release it. No thread-local registration. */
int cmeta_rcu_read_lock(cmeta_rcu *domain, cmeta_rcu_guard *guard);
const void *cmeta_rcu_load(const cmeta_rcu_guard *guard);
/** Releases exactly one original guard. Duplicate/copy release returns EINVAL. */
int cmeta_rcu_read_unlock(cmeta_rcu_guard *guard);
/** Transfers replacement only on success. EBUSY preserves ownership when the
 * previous retired pointer has not been reclaimed. Rejects pointer republication. */
int cmeta_rcu_replace(cmeta_rcu *domain, void *replacement);
/** Transfers retired to the caller after its epoch readers exit; EBUSY while
 * pinned, ENOENT with no retired pointer. Clears out_retired on failure. */
int cmeta_rcu_try_reclaim(cmeta_rcu *domain, void **out_retired);
/** Stops admission without waiting; active guards remain valid. */
int cmeta_rcu_close(cmeta_rcu *domain);
/** Requires close, no API calls/readers and no retired pointer. Transfers
 * current to the caller; EBUSY leaves the domain intact. Idempotent at zero. */
int cmeta_rcu_destroy(cmeta_rcu *domain, void **out_current);

/*
 * Typed RCU declaration owned by Salts::Concurrency.
 * This adds C type safety only; reclamation/lifetime semantics remain cmeta_rcu.
 */
#define SALTS_RCU_TYPE(name_, type_)                                                               \
  typedef struct name_ {                                                                           \
    cmeta_rcu domain;                                                                              \
  } name_;                                                                                         \
  typedef struct name_##_guard {                                                                   \
    cmeta_rcu_guard guard;                                                                         \
  } name_##_guard;                                                                                 \
  static inline int name_##_init(name_ *self, type_ *initial, size_t readers) {                    \
    return cmeta_rcu_init(self != NULL ? &self->domain : NULL, initial, readers);                  \
  }                                                                                                \
  static inline int name_##_read_lock(name_ *self, name_##_guard *guard) {                         \
    return cmeta_rcu_read_lock(self != NULL ? &self->domain : NULL,                                \
                               guard != NULL ? &guard->guard : NULL);                              \
  }                                                                                                \
  static inline const type_ *name_##_load(const name_##_guard *guard) {                            \
    return (const type_ *)cmeta_rcu_load(guard != NULL ? &guard->guard : NULL);                    \
  }                                                                                                \
  static inline int name_##_read_unlock(name_##_guard *guard) {                                    \
    return cmeta_rcu_read_unlock(guard != NULL ? &guard->guard : NULL);                            \
  }                                                                                                \
  static inline int name_##_replace(name_ *self, type_ *replacement) {                             \
    return cmeta_rcu_replace(self != NULL ? &self->domain : NULL, replacement);                    \
  }                                                                                                \
  static inline int name_##_try_reclaim(name_ *self, type_ **out) {                                \
    void *value = NULL;                                                                            \
    int status;                                                                                    \
    if (out == NULL) return SALTS_EINVAL;                                                          \
    *out = NULL;                                                                                   \
    status = cmeta_rcu_try_reclaim(self != NULL ? &self->domain : NULL, &value);                   \
    *out = (type_ *)value;                                                                         \
    return status;                                                                                 \
  }                                                                                                \
  static inline int name_##_close(name_ *self) {                                                   \
    return cmeta_rcu_close(self != NULL ? &self->domain : NULL);                                   \
  }                                                                                                \
  static inline int name_##_destroy(name_ *self, type_ **out) {                                    \
    void *value = NULL;                                                                            \
    int status;                                                                                    \
    if (out == NULL) return SALTS_EINVAL;                                                          \
    *out = NULL;                                                                                   \
    status = cmeta_rcu_destroy(self != NULL ? &self->domain : NULL, &value);                       \
    *out = (type_ *)value;                                                                         \
    return status;                                                                                 \
  }                                                                                                \
  typedef type_ name_##_value_type


#ifdef __cplusplus
}
#endif

#endif
