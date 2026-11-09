#include <rcu.h>
#include <thread.h>
#include <stdbool.h>
#include <stdlib.h>

enum { SALTS_RCU_EPOCH_COUNT = 2 };
typedef struct cmeta_rcu_impl {
  cmeta_rcu *owner;
  cmeta_mutex_t lock;
  void *current;
  void *retired;
  size_t readers[SALTS_RCU_EPOCH_COUNT];
  size_t max_readers;
  unsigned epoch;
  unsigned retired_epoch;
  bool closed;
} cmeta_rcu_impl;

static cmeta_rcu_impl *rcu_impl(const cmeta_rcu *domain) {
  cmeta_rcu_impl *impl = domain != NULL ? (cmeta_rcu_impl *)domain->impl : NULL;
  return impl != NULL && impl->owner == domain ? impl : NULL;
}

int cmeta_rcu_init(cmeta_rcu *domain, void *initial, size_t max_readers) {
  cmeta_rcu_impl *impl;
  if (domain == NULL || max_readers == 0u) return SALTS_EINVAL;
  if (domain->impl != NULL) return SALTS_EALREADY;
  impl = (cmeta_rcu_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  cmeta_mutex_init(&impl->lock);
  if (impl->lock == NULL) { free(impl); return SALTS_ENOMEM; }
  impl->owner = domain;
  impl->current = initial;
  impl->max_readers = max_readers;
  domain->impl = impl;
  return SALTS_OK;
}

int cmeta_rcu_read_lock(cmeta_rcu *domain, cmeta_rcu_guard *guard) {
  cmeta_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL || guard == NULL) return SALTS_EINVAL;
  if (guard->owner != NULL || guard->self != NULL) return SALTS_EALREADY;
  cmeta_mutex_lock(&impl->lock);
  if (impl->closed) status = SALTS_ESHUTDOWN;
  /* Subtraction avoids overflow even when max_readers == SIZE_MAX. */
  else if (impl->readers[0] >= impl->max_readers - impl->readers[1])
    status = SALTS_ENOBUFS;
  else {
    ++impl->readers[impl->epoch];
    guard->owner = domain;
    guard->self = guard;
    guard->value = impl->current;
    guard->epoch = impl->epoch;
  }
  cmeta_mutex_unlock(&impl->lock);
  return status;
}

const void *cmeta_rcu_load(const cmeta_rcu_guard *guard) {
  return guard != NULL && guard->self == guard && guard->owner != NULL
      ? guard->value : NULL;
}

int cmeta_rcu_read_unlock(cmeta_rcu_guard *guard) {
  cmeta_rcu_impl *impl;
  if (guard == NULL || guard->self != guard || guard->owner == NULL ||
      guard->epoch >= SALTS_RCU_EPOCH_COUNT) return SALTS_EINVAL;
  impl = rcu_impl(guard->owner);
  if (impl == NULL) return SALTS_EINVAL;
  cmeta_mutex_lock(&impl->lock);
  if (impl->readers[guard->epoch] == 0u) {
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_EPROTO;
  }
  --impl->readers[guard->epoch];
  *guard = (cmeta_rcu_guard){0};
  cmeta_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int cmeta_rcu_replace(cmeta_rcu *domain, void *replacement) {
  cmeta_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  cmeta_mutex_lock(&impl->lock);
  if (impl->closed) status = SALTS_ESHUTDOWN;
  else if (replacement != NULL &&
           (replacement == impl->current || replacement == impl->retired))
    status = SALTS_EINVAL;
  else if (impl->retired != NULL) status = SALTS_EBUSY;
  else {
    impl->retired = impl->current;
    impl->retired_epoch = impl->epoch;
    impl->epoch = (impl->epoch + 1u) % SALTS_RCU_EPOCH_COUNT;
    impl->current = replacement;
  }
  cmeta_mutex_unlock(&impl->lock);
  return status;
}

int cmeta_rcu_try_reclaim(cmeta_rcu *domain, void **out_retired) {
  cmeta_rcu_impl *impl = rcu_impl(domain);
  int status = SALTS_OK;
  if (out_retired == NULL) return SALTS_EINVAL;
  *out_retired = NULL;
  if (impl == NULL) return SALTS_EINVAL;
  cmeta_mutex_lock(&impl->lock);
  if (impl->retired == NULL) status = SALTS_ENOENT;
  else if (impl->readers[impl->retired_epoch] != 0u) status = SALTS_EBUSY;
  else { *out_retired = impl->retired; impl->retired = NULL; }
  cmeta_mutex_unlock(&impl->lock);
  return status;
}

int cmeta_rcu_close(cmeta_rcu *domain) {
  cmeta_rcu_impl *impl = rcu_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  cmeta_mutex_lock(&impl->lock);
  impl->closed = true;
  cmeta_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int cmeta_rcu_destroy(cmeta_rcu *domain, void **out_current) {
  cmeta_rcu_impl *impl;
  if (domain == NULL || out_current == NULL) return SALTS_EINVAL;
  *out_current = NULL;
  if (domain->impl == NULL) return SALTS_OK;
  impl = rcu_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  cmeta_mutex_lock(&impl->lock);
  if (!impl->closed || impl->readers[0] != 0u || impl->readers[1] != 0u ||
      impl->retired != NULL) {
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  *out_current = impl->current;
  cmeta_mutex_unlock(&impl->lock);
  cmeta_mutex_destroy(&impl->lock);
  free(impl);
  domain->impl = NULL;
  return SALTS_OK;
}
