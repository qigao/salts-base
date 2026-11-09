#include <core/rcu.h>
#include <threads/threads.h>
#include <stdbool.h>
#include <stdlib.h>

enum { SALTS_RCU_EPOCH_COUNT = 2 };
typedef struct rcu_impl {
  rcu *owner;
  sync_mutex_t lock;
  void *current;
  void *retired;
  size_t readers[SALTS_RCU_EPOCH_COUNT];
  size_t max_readers;
  unsigned epoch;
  unsigned retired_epoch;
  bool closed;
} rcu_impl;

static rcu_impl *rcu_get_impl(const rcu *domain) {
  rcu_impl *impl = domain != NULL ? (rcu_impl *)domain->impl : NULL;
  return impl != NULL && impl->owner == domain ? impl : NULL;
}

int rcu_init(rcu *domain, void *initial, size_t max_readers) {
  rcu_impl *impl;
  if (domain == NULL || max_readers == 0u) return SALTS_EINVAL;
  if (domain->impl != NULL) return SALTS_EALREADY;
  impl = (rcu_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  sync_mutex_init(&impl->lock);
  if (impl->lock == NULL) { free(impl); return SALTS_ENOMEM; }
  impl->owner = domain;
  impl->current = initial;
  impl->max_readers = max_readers;
  domain->impl = impl;
  return SALTS_OK;
}

int rcu_read_lock(rcu *domain, rcu_guard *guard) {
  rcu_impl *impl = rcu_get_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL || guard == NULL) return SALTS_EINVAL;
  if (guard->owner != NULL || guard->self != NULL) return SALTS_EALREADY;
  sync_mutex_lock(&impl->lock);
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
  sync_mutex_unlock(&impl->lock);
  return status;
}

const void *rcu_load(const rcu_guard *guard) {
  return guard != NULL && guard->self == guard && guard->owner != NULL
      ? guard->value : NULL;
}

int rcu_read_unlock(rcu_guard *guard) {
  rcu_impl *impl;
  if (guard == NULL || guard->self != guard || guard->owner == NULL ||
      guard->epoch >= SALTS_RCU_EPOCH_COUNT) return SALTS_EINVAL;
  impl = rcu_get_impl(guard->owner);
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->lock);
  if (impl->readers[guard->epoch] == 0u) {
    sync_mutex_unlock(&impl->lock);
    return SALTS_EPROTO;
  }
  --impl->readers[guard->epoch];
  *guard = (rcu_guard){0};
  sync_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int rcu_replace(rcu *domain, void *replacement) {
  rcu_impl *impl = rcu_get_impl(domain);
  int status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->lock);
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
  sync_mutex_unlock(&impl->lock);
  return status;
}

int rcu_try_reclaim(rcu *domain, void **out_retired) {
  rcu_impl *impl = rcu_get_impl(domain);
  int status = SALTS_OK;
  if (out_retired == NULL) return SALTS_EINVAL;
  *out_retired = NULL;
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->lock);
  if (impl->retired == NULL) status = SALTS_ENOENT;
  else if (impl->readers[impl->retired_epoch] != 0u) status = SALTS_EBUSY;
  else { *out_retired = impl->retired; impl->retired = NULL; }
  sync_mutex_unlock(&impl->lock);
  return status;
}

int rcu_close(rcu *domain) {
  rcu_impl *impl = rcu_get_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->lock);
  impl->closed = true;
  sync_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

int rcu_destroy(rcu *domain, void **out_current) {
  rcu_impl *impl;
  if (domain == NULL || out_current == NULL) return SALTS_EINVAL;
  *out_current = NULL;
  if (domain->impl == NULL) return SALTS_OK;
  impl = rcu_get_impl(domain);
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->lock);
  if (!impl->closed || impl->readers[0] != 0u || impl->readers[1] != 0u ||
      impl->retired != NULL) {
    sync_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  *out_current = impl->current;
  sync_mutex_unlock(&impl->lock);
  sync_mutex_destroy(&impl->lock);
  free(impl);
  domain->impl = NULL;
  return SALTS_OK;
}
