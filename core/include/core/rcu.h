#ifndef SALTS_RCU_H
#define SALTS_RCU_H

#include <core/api.h>

#include <core/error_codes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rcu {
  void *impl;
} rcu;
typedef struct rcu_guard {
  rcu *owner;
  const struct rcu_guard *self;
  const void *value;
  unsigned epoch;
} rcu_guard;

/** Zero-initialized, noncopyable domain; transfers initial on success only.
 * MPMC readers/writers, O(1) short mutex sections, fixed max_readers and one
 * retired snapshot. Published payloads are immutable. Init/destroy require
 * external quiescence. No operation allocates after successful init. */
SALTS_API int rcu_init(rcu *domain, void *initial, size_t max_readers);
/** Pins one snapshot in a zero-initialized, address-stable noncopyable guard.
 * ENOBUFS is reader exhaustion; ESHUTDOWN is closed admission. A guard may
 * survive suspension and exclusive cross-thread handoff if its storage and
 * domain survive. Cancellation must release it. No thread-local registration. */
SALTS_API int rcu_read_lock(rcu *domain, rcu_guard *guard);
SALTS_API const void *rcu_load(const rcu_guard *guard);
/** Releases exactly one original guard. Duplicate/copy release returns EINVAL. */
SALTS_API int rcu_read_unlock(rcu_guard *guard);
/** Transfers replacement only on success. EBUSY preserves ownership when the
 * previous retired pointer has not been reclaimed. Rejects pointer republication. */
SALTS_API int rcu_replace(rcu *domain, void *replacement);
/** Transfers retired to the caller after its epoch readers exit; EBUSY while
 * pinned, ENOENT with no retired pointer. Clears out_retired on failure. */
SALTS_API int rcu_try_reclaim(rcu *domain, void **out_retired);
/** Stops admission without waiting; active guards remain valid. */
SALTS_API int rcu_close(rcu *domain);
/** Requires close, no API calls/readers and no retired pointer. Transfers
 * current to the caller; EBUSY leaves the domain intact. Idempotent at zero. */
SALTS_API int rcu_destroy(rcu *domain, void **out_current);

/*
 * Typed RCU declaration owned by Salts::Core.
 * This adds C type safety only; reclamation/lifetime semantics remain rcu.
 */
#define SALTS_RCU_TYPE(name_, type_)                                                               \
  typedef struct name_ {                                                                           \
    rcu domain;                                                                              \
  } name_;                                                                                         \
  typedef struct name_##_guard {                                                                   \
    rcu_guard guard;                                                                         \
  } name_##_guard;                                                                                 \
  static inline int name_##_init(name_ *self, type_ *initial, size_t readers) {                    \
    return rcu_init(self != NULL ? &self->domain : NULL, initial, readers);                  \
  }                                                                                                \
  static inline int name_##_read_lock(name_ *self, name_##_guard *guard) {                         \
    return rcu_read_lock(self != NULL ? &self->domain : NULL,                                \
                               guard != NULL ? &guard->guard : NULL);                              \
  }                                                                                                \
  static inline const type_ *name_##_load(const name_##_guard *guard) {                            \
    return (const type_ *)rcu_load(guard != NULL ? &guard->guard : NULL);                    \
  }                                                                                                \
  static inline int name_##_read_unlock(name_##_guard *guard) {                                    \
    return rcu_read_unlock(guard != NULL ? &guard->guard : NULL);                            \
  }                                                                                                \
  static inline int name_##_replace(name_ *self, type_ *replacement) {                             \
    return rcu_replace(self != NULL ? &self->domain : NULL, replacement);                    \
  }                                                                                                \
  static inline int name_##_try_reclaim(name_ *self, type_ **out) {                                \
    void *value = NULL;                                                                            \
    int status;                                                                                    \
    if (out == NULL) return SALTS_EINVAL;                                                          \
    *out = NULL;                                                                                   \
    status = rcu_try_reclaim(self != NULL ? &self->domain : NULL, &value);                   \
    *out = (type_ *)value;                                                                         \
    return status;                                                                                 \
  }                                                                                                \
  static inline int name_##_close(name_ *self) {                                                   \
    return rcu_close(self != NULL ? &self->domain : NULL);                                   \
  }                                                                                                \
  static inline int name_##_destroy(name_ *self, type_ **out) {                                    \
    void *value = NULL;                                                                            \
    int status;                                                                                    \
    if (out == NULL) return SALTS_EINVAL;                                                          \
    *out = NULL;                                                                                   \
    status = rcu_destroy(self != NULL ? &self->domain : NULL, &value);                       \
    *out = (type_ *)value;                                                                         \
    return status;                                                                                 \
  }                                                                                                \
  typedef type_ name_##_value_type


#ifdef __cplusplus
}
#endif

#endif
