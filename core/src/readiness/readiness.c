#include "readiness_internal.h"

#include <salts/error_codes.h>
#include <threads/threads.h>

#include <errno.h>
#include <limits.h>
#include <atomic/atomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

typedef struct readiness_impl readiness_impl;

typedef struct readiness_slot {
  readiness_impl *owner;
  intptr_t native_resource;
  readiness_callback callback;
  readiness_continuation continuation;
  void *callback_user;
  uint32_t index;
  _Atomic uint32_t generation;
  _Atomic uint32_t arm_generation;
  uint64_t arm_token;
  readiness_lifecycle lifecycle;
  readiness_interest interest;
  readiness_delivery delivery;
  readiness_terminal terminal;
  readiness_control control;
  uint32_t arm_waiters;
  uint32_t api_borrows;
  int native_registered;
  int terminal_status;
  int orphaned;
} readiness_slot;

typedef struct readiness_api_borrow {
  readiness_slot *slot;
  readiness_registration *registration;
  uint32_t generation;
} readiness_api_borrow;

struct readiness_impl {
  sync_mutex_t mutex;
  sync_cond_t changed;
  readiness_slot *slots;
  size_t capacity;
  readiness_backend_ops backend_ops;
  void *backend_user;
  uint64_t rejected_full;
  uint64_t stale_events;
  uint64_t duplicate_events;
  uint64_t backend_errors;
  int admission_open;
  int terminalizing;
  int shutdown_inflight;
  int backend_shutdown_inflight;
  int shutdown_complete;
};

static SALTS_THREAD_LOCAL readiness_slot *readiness_callback_slot;

static void readiness_slot_clear_callback(readiness_slot *slot) {
  slot->callback = NULL;
  slot->continuation = NULL;
  slot->callback_user = NULL;
}

static void *readiness_handle_load(
    const readiness_registration *registration) {
#if defined(_MSC_VER)
  return _InterlockedCompareExchangePointer(
      (void *volatile *)&registration->impl, NULL, NULL);
#else
  return __atomic_load_n(&registration->impl, __ATOMIC_ACQUIRE);
#endif
}

static void readiness_handle_store(readiness_registration *registration,
                                   void *value) {
#if defined(_MSC_VER)
  (void)_InterlockedExchangePointer((void *volatile *)&registration->impl,
                                    value);
#else
  __atomic_store_n(&registration->impl, value, __ATOMIC_RELEASE);
#endif
}

static int readiness_handle_clear_if(
    readiness_registration *registration, void *expected) {
#if defined(_MSC_VER)
  return _InterlockedCompareExchangePointer(
             (void *volatile *)&registration->impl, NULL, expected) == expected;
#else
  return __atomic_compare_exchange_n(&registration->impl, &expected, NULL, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
}

#define SALTS_READINESS_ADMISSION_CLOSED \
  ((uintptr_t)1u << (sizeof(uintptr_t) * CHAR_BIT - 1u))
#define SALTS_READINESS_ADMISSION_COUNT_MASK \
  (SALTS_READINESS_ADMISSION_CLOSED - (uintptr_t)1u)

uintptr_t readiness_registration_admission_max_entrants(void) {
  const uintptr_t slot_counter_limit = (uintptr_t)UINT32_MAX;
  return SALTS_READINESS_ADMISSION_COUNT_MASK < slot_counter_limit
             ? SALTS_READINESS_ADMISSION_COUNT_MASK
             : slot_counter_limit;
}

static uintptr_t readiness_admission_load(const uintptr_t *admission) {
#if defined(_MSC_VER) && UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)_InterlockedCompareExchange64(
      (volatile long long *)admission, 0, 0);
#elif defined(_MSC_VER)
  return (uintptr_t)_InterlockedCompareExchange(
      (volatile long *)admission, 0, 0);
#else
  return __atomic_load_n(admission, __ATOMIC_ACQUIRE);
#endif
}

static int readiness_admission_compare_exchange(
    uintptr_t *admission, uintptr_t *expected, uintptr_t desired) {
#if defined(_MSC_VER) && UINTPTR_MAX == UINT64_MAX
  uintptr_t observed = (uintptr_t)_InterlockedCompareExchange64(
      (volatile long long *)admission, (long long)desired,
      (long long)*expected);
#elif defined(_MSC_VER)
  uintptr_t observed = (uintptr_t)_InterlockedCompareExchange(
      (volatile long *)admission, (long)desired, (long)*expected);
#else
  return __atomic_compare_exchange_n(admission, expected, desired, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
#if defined(_MSC_VER)
  if (observed == *expected) return 1;
  *expected = observed;
  return 0;
#endif
}

int readiness_registration_admission_enter(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return SALTS_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    uintptr_t entrants = observed & SALTS_READINESS_ADMISSION_COUNT_MASK;
    if ((observed & SALTS_READINESS_ADMISSION_CLOSED) != 0u)
      return SALTS_EBUSY;
    if (entrants >=
        readiness_registration_admission_max_entrants())
      return -EOVERFLOW;
    if (readiness_admission_compare_exchange(admission, &observed,
                                             observed + 1u))
      return SALTS_OK;
  }
}

int readiness_registration_admission_reserve_register(
    uintptr_t *admission) {
  uintptr_t expected = 0u;
  if (admission == NULL) return SALTS_EINVAL;
  return readiness_admission_compare_exchange(
             admission, &expected, SALTS_READINESS_ADMISSION_CLOSED)
             ? SALTS_OK
             : SALTS_EBUSY;
}

int readiness_registration_admission_close(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return SALTS_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    if ((observed & SALTS_READINESS_ADMISSION_CLOSED) != 0u)
      return SALTS_EALREADY;
    if (readiness_admission_compare_exchange(
            admission, &observed,
            observed | SALTS_READINESS_ADMISSION_CLOSED))
      return SALTS_OK;
  }
}

static void readiness_registration_admission_reopen(uintptr_t *admission) {
  uintptr_t observed = readiness_admission_load(admission);
  while ((observed & SALTS_READINESS_ADMISSION_CLOSED) != 0u) {
    if (readiness_admission_compare_exchange(
            admission, &observed,
            observed & ~SALTS_READINESS_ADMISSION_CLOSED))
      return;
  }
}

void readiness_registration_admission_leave(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return;
  observed = readiness_admission_load(admission);
  while ((observed & SALTS_READINESS_ADMISSION_COUNT_MASK) != 0u) {
    if (readiness_admission_compare_exchange(admission, &observed,
                                             observed - 1u))
      return;
  }
}

int readiness_registration_admission_reset(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return SALTS_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    if ((observed & SALTS_READINESS_ADMISSION_COUNT_MASK) != 0u)
      return SALTS_EBUSY;
    if (readiness_admission_compare_exchange(admission, &observed, 0u))
      return SALTS_OK;
  }
}

uint32_t readiness_registration_admission_entrants(
    const uintptr_t *admission) {
  if (admission == NULL) return 0u;
  return (uint32_t)(readiness_admission_load(admission) &
                    SALTS_READINESS_ADMISSION_COUNT_MASK);
}

int readiness_state_model_valid(
    const readiness_state_view *view) {
  int inactive;
  if (view == NULL ||
      view->lifecycle > SALTS_READINESS_LIFECYCLE_RETIRED ||
      view->interest > SALTS_READINESS_INTEREST_UNARMING ||
      view->delivery > SALTS_READINESS_DELIVERY_CALLBACK ||
      view->terminal > SALTS_READINESS_TERMINAL_DELIVERING ||
      view->control > SALTS_READINESS_CONTROL_CLOSE)
    return 0;
  inactive = view->lifecycle == SALTS_READINESS_LIFECYCLE_FREE ||
             view->lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED;
  if (inactive)
    return view->interest == SALTS_READINESS_INTEREST_IDLE &&
           view->delivery == SALTS_READINESS_DELIVERY_IDLE &&
           view->terminal == SALTS_READINESS_TERMINAL_NONE &&
           view->control == SALTS_READINESS_CONTROL_NONE &&
           view->callback == NULL && view->arm_token == 0u &&
           view->arm_waiters == 0u && view->api_borrows == 0u &&
           !view->native_registered && !view->orphaned;
  if ((view->interest == SALTS_READINESS_INTEREST_ARMED ||
       view->interest == SALTS_READINESS_INTEREST_ARMING) &&
      (view->callback == NULL || view->arm_token == 0u))
    return 0;
  if (view->delivery == SALTS_READINESS_DELIVERY_CALLBACK &&
      view->callback == NULL)
    return 0;
  if (view->terminal == SALTS_READINESS_TERMINAL_RESERVED &&
      (view->lifecycle != SALTS_READINESS_LIFECYCLE_OPEN ||
       view->interest != SALTS_READINESS_INTEREST_ARMED ||
       view->delivery != SALTS_READINESS_DELIVERY_IDLE))
    return 0;
  if (view->terminal == SALTS_READINESS_TERMINAL_DELIVERING &&
      (view->interest != SALTS_READINESS_INTEREST_IDLE ||
       view->delivery != SALTS_READINESS_DELIVERY_CALLBACK))
    return 0;
  if ((view->interest == SALTS_READINESS_INTEREST_ARMING) !=
      (view->control == SALTS_READINESS_CONTROL_ARM))
    return 0;
  if ((view->interest == SALTS_READINESS_INTEREST_UNARMING) !=
      (view->control == SALTS_READINESS_CONTROL_UNARM))
    return 0;
  if (view->control == SALTS_READINESS_CONTROL_REGISTER &&
      (view->lifecycle != SALTS_READINESS_LIFECYCLE_OPEN ||
       view->interest != SALTS_READINESS_INTEREST_IDLE))
    return 0;
  if (view->control == SALTS_READINESS_CONTROL_CLOSE &&
      view->lifecycle != SALTS_READINESS_LIFECYCLE_CLOSING)
    return 0;
  if (view->orphaned &&
      (view->lifecycle != SALTS_READINESS_LIFECYCLE_CLOSING ||
       !view->native_registered))
    return 0;
  return 1;
}

int readiness_callback_forms_valid(
    readiness_callback callback,
    readiness_continuation continuation) {
  return (callback == NULL) != (continuation == NULL);
}

static int readiness_callback_on_impl(const readiness_impl *impl) {
  return readiness_callback_slot != NULL && readiness_callback_slot->owner == impl;
}

static uint64_t readiness_slot_token(const readiness_slot *slot) {
  return ((uint64_t)slot->generation << 32) | (uint64_t)slot->index;
}

static void readiness_slot_reclaim(readiness_slot *slot) {
  readiness_lifecycle inactive_lifecycle =
      readiness_generation_available(slot->generation) &&
              readiness_generation_available(slot->arm_generation)
          ? SALTS_READINESS_LIFECYCLE_FREE
          : SALTS_READINESS_LIFECYCLE_RETIRED;
  slot->native_resource = 0;
  readiness_slot_clear_callback(slot);
  slot->interest = SALTS_READINESS_INTEREST_IDLE;
  slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
  slot->terminal = SALTS_READINESS_TERMINAL_NONE;
  slot->control = SALTS_READINESS_CONTROL_NONE;
  slot->native_registered = 0;
  slot->terminal_status = SALTS_OK;
  slot->orphaned = 0;
  slot->arm_token = 0;
  slot->lifecycle = inactive_lifecycle;
}

static int readiness_slot_can_reclaim(const readiness_slot *slot) {
  return slot->lifecycle == SALTS_READINESS_LIFECYCLE_CLOSING &&
         slot->interest == SALTS_READINESS_INTEREST_IDLE &&
         slot->delivery == SALTS_READINESS_DELIVERY_IDLE &&
         slot->terminal == SALTS_READINESS_TERMINAL_NONE &&
         slot->control == SALTS_READINESS_CONTROL_NONE &&
         slot->arm_waiters == 0u && slot->api_borrows == 0u &&
         !slot->native_registered && !slot->orphaned;
}

static void readiness_slot_try_reclaim(readiness_slot *slot) {
  if (readiness_slot_can_reclaim(slot)) readiness_slot_reclaim(slot);
}

static void readiness_wait_slot_control(readiness_impl *impl, readiness_slot *slot) {
  while (slot->control != SALTS_READINESS_CONTROL_NONE)
    sync_cond_wait(&impl->changed, &impl->mutex);
}

static int readiness_wait_public_gate(readiness_impl *impl) {
  while (impl->terminalizing) {
    if (readiness_callback_on_impl(impl)) return SALTS_EBUSY;
    sync_cond_wait(&impl->changed, &impl->mutex);
  }
  return SALTS_OK;
}

static int readiness_wait_public_slot_control(readiness_impl *impl,
                                              readiness_slot *slot,
                                              uint32_t generation) {
  for (;;) {
    int status = readiness_wait_public_gate(impl);
    if (status != SALTS_OK) return status;
    if (slot->generation != generation ||
        slot->lifecycle == SALTS_READINESS_LIFECYCLE_FREE ||
        slot->lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED)
      return SALTS_EBUSY;
    if (slot->lifecycle == SALTS_READINESS_LIFECYCLE_CLOSING)
      return SALTS_EBUSY;
    if (slot->control == SALTS_READINESS_CONTROL_NONE) return SALTS_OK;
    sync_cond_wait(&impl->changed, &impl->mutex);
  }
}

static int readiness_controls_inflight(const readiness_impl *impl) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].control != SALTS_READINESS_CONTROL_NONE) return 1;
  }
  return 0;
}

static void readiness_wait_controls(readiness_impl *impl) {
  while (readiness_controls_inflight(impl))
    sync_cond_wait(&impl->changed, &impl->mutex);
}

static void readiness_snapshot_terminal(readiness_impl *impl, int status) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    readiness_slot *slot = &impl->slots[i];
    if (slot->lifecycle == SALTS_READINESS_LIFECYCLE_OPEN &&
        slot->interest == SALTS_READINESS_INTEREST_ARMED &&
        slot->terminal == SALTS_READINESS_TERMINAL_NONE) {
      slot->terminal = SALTS_READINESS_TERMINAL_RESERVED;
      slot->terminal_status = status;
    }
  }
}

static readiness_impl *readiness_impl_from_reactor(readiness_reactor *reactor) {
  return reactor != NULL ? (readiness_impl *)reactor->impl : NULL;
}

static readiness_slot *readiness_slot_from_pointer(void *pointer) {
  readiness_slot *slot;
  readiness_impl *impl;
  uintptr_t address;
  uintptr_t begin;
  uintptr_t end;

  if (pointer == NULL) return NULL;
  slot = (readiness_slot *)pointer;
  impl = slot->owner;
  if (impl == NULL || impl->slots == NULL) return NULL;
  address = (uintptr_t)slot;
  begin = (uintptr_t)impl->slots;
  end = begin + impl->capacity * sizeof(*impl->slots);
  if (address < begin || address >= end || (address - begin) % sizeof(*impl->slots) != 0)
    return NULL;
  return slot;
}

static int readiness_api_borrow_acquire(
    readiness_registration *registration,
    readiness_api_borrow *borrow) {
  readiness_slot *slot;
  readiness_impl *impl;
  void *pointer;

  if (registration == NULL || borrow == NULL)
    return SALTS_EINVAL;
  borrow->slot = NULL;
  borrow->registration = NULL;
  borrow->generation = 0u;
  {
    int status = readiness_registration_admission_enter(
        &registration->_admission);
    if (status != SALTS_OK) return status;
  }
  pointer = readiness_handle_load(registration);
  slot = readiness_slot_from_pointer(pointer);
  if (slot == NULL) {
    readiness_registration_admission_leave(&registration->_admission);
    return SALTS_EINVAL;
  }
  impl = slot->owner;
  borrow->generation = atomic_load_explicit(&slot->generation,
                                             memory_order_acquire);
  sync_mutex_lock(&impl->mutex);
  if (readiness_handle_load(registration) != pointer ||
      slot->generation != borrow->generation ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED) {
    readiness_registration_admission_leave(&registration->_admission);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EBUSY;
  }
  if (slot->api_borrows == UINT32_MAX) {
    readiness_registration_admission_leave(&registration->_admission);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return -EOVERFLOW;
  }
  ++slot->api_borrows;
  borrow->slot = slot;
  borrow->registration = registration;
  sync_mutex_unlock(&impl->mutex);
  return SALTS_OK;
}

static void readiness_api_borrow_release(
    readiness_api_borrow *borrow) {
  readiness_slot *slot;
  readiness_impl *impl;
  if (borrow == NULL || borrow->slot == NULL) return;
  slot = borrow->slot;
  impl = slot->owner;
  sync_mutex_lock(&impl->mutex);
  if (slot->api_borrows != 0u) --slot->api_borrows;
  readiness_registration_admission_leave(
      &borrow->registration->_admission);
  readiness_slot_try_reclaim(slot);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  borrow->slot = NULL;
  borrow->registration = NULL;
}

static int readiness_api_borrow_return(
    readiness_api_borrow *borrow, int status) {
  readiness_api_borrow_release(borrow);
  return status;
}

static int readiness_events_valid(readiness_events events) {
  const readiness_events valid = SALTS_READINESS_EVENT_READ | SALTS_READINESS_EVENT_WRITE |
                                       SALTS_READINESS_EVENT_ERROR | SALTS_READINESS_EVENT_HANGUP;
  return events != 0 && (events & ~valid) == 0;
}

static int readiness_config_validate(const readiness_config *config) {
  if (config == NULL || config->registration_capacity == 0 || config->event_batch_capacity == 0)
    return SALTS_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u) return SALTS_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u) return SALTS_EINVAL;
  if (config->registration_capacity > SIZE_MAX / sizeof(readiness_slot))
    return SALTS_ERANGE;
  return SALTS_OK;
}

static int readiness_backend_ops_validate(const readiness_backend_ops *backend_ops) {
  return backend_ops != NULL && backend_ops->register_resource != NULL &&
         backend_ops->arm != NULL && backend_ops->unarm != NULL && backend_ops->close != NULL &&
         backend_ops->shutdown != NULL && backend_ops->destroy != NULL;
}

int readiness_reactor_init_backend(readiness_reactor *reactor,
                                         const readiness_config *config,
                                         const readiness_backend_ops *backend_ops,
                                         void *backend_user) {
  readiness_impl *impl;
  int status;

  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL || !readiness_backend_ops_validate(backend_ops)) return SALTS_EINVAL;
  status = readiness_config_validate(config);
  if (status != SALTS_OK) return status;

  impl = (readiness_impl *)calloc(1, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->slots = (readiness_slot *)calloc(config->registration_capacity, sizeof(*impl->slots));
  if (impl->slots == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }

  sync_mutex_init(&impl->mutex);
  sync_cond_init(&impl->changed);
  if (impl->mutex == NULL || impl->changed == NULL) {
    sync_cond_destroy(&impl->changed);
    sync_mutex_destroy(&impl->mutex);
    free(impl->slots);
    free(impl);
    return SALTS_ENOMEM;
  }

  impl->capacity = config->registration_capacity;
  impl->backend_ops = *backend_ops;
  impl->backend_user = backend_user;
  impl->admission_open = 1;
  for (size_t i = 0; i < impl->capacity; ++i) {
    impl->slots[i].owner = impl;
    impl->slots[i].index = (uint32_t)i;
  }
  reactor->impl = impl;
  return SALTS_OK;
}

bool readiness_backend_supported(readiness_backend_kind kind) {
  switch (kind) {
#if defined(SALTS_ENABLE_EPOLL_READINESS)
    case SALTS_READINESS_BACKEND_EPOLL:
      return true;
#endif
#if defined(SALTS_ENABLE_KQUEUE_READINESS)
    case SALTS_READINESS_BACKEND_KQUEUE:
      return true;
#endif
#if defined(SALTS_ENABLE_POLL_READINESS)
    case SALTS_READINESS_BACKEND_POLL:
      return true;
#endif
    default:
      return false;
  }
}

int readiness_reactor_init_kind(
    readiness_reactor *reactor, const readiness_config *config,
    readiness_backend_kind kind) {
  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL || config == NULL) return SALTS_EINVAL;

  switch (kind) {
#if defined(SALTS_ENABLE_EPOLL_READINESS)
    case SALTS_READINESS_BACKEND_EPOLL:
      return readiness_epoll_init(reactor, config);
#endif
#if defined(SALTS_ENABLE_KQUEUE_READINESS)
    case SALTS_READINESS_BACKEND_KQUEUE:
      return readiness_kqueue_init(reactor, config);
#endif
#if defined(SALTS_ENABLE_POLL_READINESS)
    case SALTS_READINESS_BACKEND_POLL:
      return readiness_poll_init(reactor, config);
#endif
    default:
      return SALTS_ENOTSUP;
  }
}

int readiness_reactor_init(readiness_reactor *reactor,
                                 const readiness_config *config) {
#if defined(SALTS_ENABLE_EPOLL_READINESS)
  return readiness_reactor_init_kind(
      reactor, config, SALTS_READINESS_BACKEND_EPOLL);
#elif defined(SALTS_ENABLE_KQUEUE_READINESS)
  return readiness_reactor_init_kind(
      reactor, config, SALTS_READINESS_BACKEND_KQUEUE);
#else
  if (reactor != NULL) reactor->impl = NULL;
  return reactor == NULL || config == NULL ? SALTS_EINVAL : SALTS_ENOTSUP;
#endif
}

int readiness_register(readiness_reactor *reactor, intptr_t native_resource,
                             readiness_registration *registration) {
  readiness_impl *impl;
  readiness_slot *slot = NULL;
  readiness_generation_step generation_step;
  uint64_t token;
  int exhausted_slot = 0;
  int status;

  if (registration != NULL) {
    status = readiness_registration_admission_reserve_register(
        &registration->_admission);
    if (status != SALTS_OK) return status;
    readiness_handle_store(registration, NULL);
  }
  impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || registration == NULL) {
    if (registration != NULL)
      (void)readiness_registration_admission_reset(
          &registration->_admission);
    return SALTS_EINVAL;
  }

  sync_mutex_lock(&impl->mutex);
  status = readiness_wait_public_gate(impl);
  if (status != SALTS_OK) {
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    sync_mutex_unlock(&impl->mutex);
    return status;
  }
  if (!impl->admission_open) {
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_ESHUTDOWN;
  }
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED) {
      exhausted_slot = 1;
      continue;
    }
    if (impl->slots[i].lifecycle == SALTS_READINESS_LIFECYCLE_FREE) {
      if (!readiness_generation_available(impl->slots[i].generation) ||
          !readiness_generation_available(impl->slots[i].arm_generation)) {
        impl->slots[i].lifecycle = SALTS_READINESS_LIFECYCLE_RETIRED;
        exhausted_slot = 1;
        continue;
      }
      slot = &impl->slots[i];
      break;
    }
  }
  if (slot == NULL) {
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    if (exhausted_slot) {
      sync_mutex_unlock(&impl->mutex);
      return -EOVERFLOW;
    }
    impl->rejected_full += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_ENOBUFS;
  }

  status = readiness_generation_prepare(slot->generation, &generation_step);
  if (status != SALTS_OK) {
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    sync_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->generation = readiness_generation_commit(&generation_step);
  slot->native_resource = native_resource;
  slot->lifecycle = SALTS_READINESS_LIFECYCLE_OPEN;
  slot->interest = SALTS_READINESS_INTEREST_IDLE;
  slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
  slot->terminal = SALTS_READINESS_TERMINAL_NONE;
  slot->control = SALTS_READINESS_CONTROL_REGISTER;
  token = readiness_slot_token(slot);
  sync_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.register_resource(impl->backend_user, native_resource, token);
  sync_mutex_lock(&impl->mutex);
  if (status != SALTS_OK) {
    slot->control = SALTS_READINESS_CONTROL_NONE;
    slot->generation = readiness_generation_rollback(&generation_step);
    slot->lifecycle = SALTS_READINESS_LIFECYCLE_CLOSING;
    readiness_slot_reclaim(slot);
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->native_registered = 1;
  if (impl->admission_open) {
    slot->control = SALTS_READINESS_CONTROL_NONE;
    readiness_handle_store(registration, slot);
    (void)readiness_registration_admission_reset(
        &registration->_admission);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }

  slot->lifecycle = SALTS_READINESS_LIFECYCLE_CLOSING;
  slot->control = SALTS_READINESS_CONTROL_CLOSE;
  sync_mutex_unlock(&impl->mutex);
  status = impl->backend_ops.close(impl->backend_user, token);
  sync_mutex_lock(&impl->mutex);
  if (status == SALTS_OK) {
    slot->native_registered = 0;
  } else {
    slot->orphaned = 1;
  }
  slot->control = SALTS_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  (void)readiness_registration_admission_reset(
      &registration->_admission);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return status == SALTS_OK ? SALTS_ESHUTDOWN : status;
}

static int readiness_arm_impl(
    readiness_registration *registration,
    readiness_events events,
    readiness_callback callback,
    readiness_continuation continuation,
    void *user) {
  readiness_api_borrow borrow = {0};
  readiness_slot *slot;
  readiness_impl *impl;
  readiness_generation_step arm_generation_step;
  uint32_t generation;
  uint64_t token;
  uint64_t arm_token;
  int status;

  if (!readiness_callback_forms_valid(callback, continuation) ||
      !readiness_events_valid(events))
    return SALTS_EINVAL;
  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != SALTS_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  sync_mutex_lock(&impl->mutex);
  if (readiness_callback_slot == slot &&
      slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  generation = borrow.generation;
  for (;;) {
    status = readiness_wait_public_slot_control(impl, slot, generation);
    if (status != SALTS_OK) {
      sync_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, status);
    }
    if (!impl->admission_open) {
      sync_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, SALTS_ESHUTDOWN);
    }
    if (slot->terminal != SALTS_READINESS_TERMINAL_NONE) {
      sync_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
    }
    if (slot->delivery == SALTS_READINESS_DELIVERY_IDLE) {
      if (slot->interest == SALTS_READINESS_INTEREST_ARMED) {
        sync_mutex_unlock(&impl->mutex);
        return readiness_api_borrow_return(&borrow, SALTS_EALREADY);
      }
      if (slot->interest == SALTS_READINESS_INTEREST_IDLE) break;
      sync_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
    }
    if (slot->arm_waiters == UINT32_MAX) {
      sync_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, -EOVERFLOW);
    }
    ++slot->arm_waiters;
    sync_cond_broadcast(&impl->changed);
    while (slot->generation == generation &&
           slot->lifecycle == SALTS_READINESS_LIFECYCLE_OPEN &&
           slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK &&
           slot->terminal == SALTS_READINESS_TERMINAL_NONE &&
           slot->control == SALTS_READINESS_CONTROL_NONE &&
           impl->admission_open && !impl->terminalizing)
      sync_cond_wait(&impl->changed, &impl->mutex);
    --slot->arm_waiters;
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
  }
  status = readiness_generation_prepare(slot->arm_generation, &arm_generation_step);
  if (status != SALTS_OK) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  slot->callback = callback;
  slot->continuation = continuation;
  slot->callback_user = user;
  slot->interest = SALTS_READINESS_INTEREST_ARMING;
  slot->control = SALTS_READINESS_CONTROL_ARM;
  slot->arm_generation = readiness_generation_commit(&arm_generation_step);
  slot->arm_token = ((uint64_t)slot->arm_generation << 32) | (uint64_t)slot->index;
  token = readiness_slot_token(slot);
  arm_token = slot->arm_token;
  sync_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.arm(impl->backend_user, token, arm_token, events);
  sync_mutex_lock(&impl->mutex);
  if (status != SALTS_OK && readiness_slot_token(slot) == token) {
    slot->arm_generation = readiness_generation_rollback(&arm_generation_step);
    slot->terminal_status = SALTS_OK;
    if (slot->interest == SALTS_READINESS_INTEREST_ARMING &&
        slot->control == SALTS_READINESS_CONTROL_ARM) {
      slot->interest = SALTS_READINESS_INTEREST_IDLE;
      readiness_slot_clear_callback(slot);
      slot->arm_token = 0;
    }
  } else if (status == SALTS_OK && readiness_slot_token(slot) == token &&
             slot->interest == SALTS_READINESS_INTEREST_ARMING &&
             slot->control == SALTS_READINESS_CONTROL_ARM) {
    slot->interest = SALTS_READINESS_INTEREST_ARMED;
  }
  slot->control = SALTS_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return readiness_api_borrow_return(&borrow, status);
}

int readiness_arm(readiness_registration *registration,
                        readiness_events events,
                        readiness_callback callback, void *user) {
  return readiness_arm_impl(registration, events, callback, NULL, user);
}

int readiness_arm_continuation(
    readiness_registration *registration,
    readiness_events events,
    readiness_continuation continuation, void *user) {
  return readiness_arm_impl(registration, events, NULL, continuation, user);
}

int readiness_backend_wait_arm_waiter(
    readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns) {
  return readiness_backend_wait_arm_waiter_observe(
      registration, waiters, timeout_ns, NULL);
}

int readiness_backend_wait_arm_waiter_observe(
    readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns, uint32_t *api_borrows) {
  readiness_slot *slot;
  readiness_impl *impl;
  void *pointer;
  uint32_t generation;
  int status = SALTS_OK;

  if (api_borrows != NULL) *api_borrows = UINT32_MAX;
  if (waiters == 0u || timeout_ns == 0u) return SALTS_EINVAL;
  if (registration == NULL) return SALTS_EINVAL;
  status = readiness_registration_admission_enter(
      &registration->_admission);
  if (status != SALTS_OK) return status;
  pointer = readiness_handle_load(registration);
  slot = readiness_slot_from_pointer(pointer);
  if (slot == NULL) {
    readiness_registration_admission_leave(&registration->_admission);
    return SALTS_EINVAL;
  }
  impl = slot->owner;
  generation = atomic_load_explicit(&slot->generation, memory_order_acquire);
  sync_mutex_lock(&impl->mutex);
  if (readiness_handle_load(registration) != pointer ||
      slot->generation != generation ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED) {
    status = SALTS_EBUSY;
  }
  while (slot->generation == generation &&
         slot->lifecycle == SALTS_READINESS_LIFECYCLE_OPEN &&
         slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK &&
         slot->arm_waiters < waiters && status == SALTS_OK)
    status = sync_cond_timedwait(&impl->changed, &impl->mutex, timeout_ns);
  if (status == SALTS_OK &&
      (slot->generation != generation ||
       slot->lifecycle != SALTS_READINESS_LIFECYCLE_OPEN ||
       slot->delivery != SALTS_READINESS_DELIVERY_CALLBACK ||
       slot->arm_waiters < waiters))
    status = SALTS_EBUSY;
  if (api_borrows != NULL) *api_borrows = slot->api_borrows;
  readiness_registration_admission_leave(&registration->_admission);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return status;
}

int readiness_unarm(readiness_registration *registration) {
  readiness_api_borrow borrow = {0};
  readiness_slot *slot;
  readiness_impl *impl;
  readiness_interest previous_interest;
  uint64_t token;
  uint32_t generation;
  int status;

  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != SALTS_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  sync_mutex_lock(&impl->mutex);
  if (readiness_callback_slot == slot &&
      slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  generation = borrow.generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != SALTS_OK) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  if (slot->terminal != SALTS_READINESS_TERMINAL_NONE) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  if (slot->delivery == SALTS_READINESS_DELIVERY_IDLE &&
      slot->interest != SALTS_READINESS_INTEREST_ARMED) {
    status = slot->interest == SALTS_READINESS_INTEREST_IDLE
                 ? SALTS_EALREADY
                 : SALTS_EBUSY;
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  previous_interest = slot->interest;
  token = readiness_slot_token(slot);
  slot->interest = SALTS_READINESS_INTEREST_UNARMING;
  slot->control = SALTS_READINESS_CONTROL_UNARM;
  sync_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.unarm(impl->backend_user, token);
  sync_mutex_lock(&impl->mutex);
  while (slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK)
    sync_cond_wait(&impl->changed, &impl->mutex);
  if (status == SALTS_OK) {
    slot->interest = SALTS_READINESS_INTEREST_IDLE;
    readiness_slot_clear_callback(slot);
    slot->arm_token = 0;
  } else {
    slot->interest = previous_interest;
  }
  slot->control = SALTS_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return readiness_api_borrow_return(&borrow, status);
}

int readiness_close(readiness_registration *registration) {
  readiness_api_borrow borrow = {0};
  readiness_slot *slot;
  readiness_impl *impl;
  uint64_t token;
  uint32_t generation;
  int self_close;
  int status;

  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != SALTS_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  sync_mutex_lock(&impl->mutex);
  self_close = readiness_callback_slot == slot &&
               slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK;
  if (self_close || slot->terminal != SALTS_READINESS_TERMINAL_NONE) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  generation = borrow.generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != SALTS_OK) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  if (slot->terminal != SALTS_READINESS_TERMINAL_NONE) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  if (slot->lifecycle != SALTS_READINESS_LIFECYCLE_OPEN) {
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, SALTS_EBUSY);
  }
  slot->lifecycle = SALTS_READINESS_LIFECYCLE_CLOSING;
  slot->control = SALTS_READINESS_CONTROL_CLOSE;
  status = readiness_registration_admission_close(
      &registration->_admission);
  if (status != SALTS_OK) {
    slot->lifecycle = SALTS_READINESS_LIFECYCLE_OPEN;
    slot->control = SALTS_READINESS_CONTROL_NONE;
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  token = readiness_slot_token(slot);
  sync_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.close(impl->backend_user, token);
  sync_mutex_lock(&impl->mutex);
  if (status != SALTS_OK) {
    slot->control = SALTS_READINESS_CONTROL_NONE;
    slot->lifecycle = SALTS_READINESS_LIFECYCLE_OPEN;
    readiness_registration_admission_reopen(&registration->_admission);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  slot->native_registered = 0;
  if (slot->delivery == SALTS_READINESS_DELIVERY_IDLE) {
    readiness_slot_clear_callback(slot);
  }
  slot->interest = SALTS_READINESS_INTEREST_IDLE;
  slot->arm_token = 0;
  while (slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK ||
         slot->arm_waiters != 0u || slot->api_borrows != 1u ||
         readiness_registration_admission_entrants(
             &registration->_admission) != 1u)
    sync_cond_wait(&impl->changed, &impl->mutex);
  (void)readiness_handle_clear_if(registration, slot);
  slot->control = SALTS_READINESS_CONTROL_NONE;
  --slot->api_borrows;
  readiness_registration_admission_leave(&registration->_admission);
  (void)readiness_registration_admission_reset(
      &registration->_admission);
  borrow.slot = NULL;
  borrow.registration = NULL;
  readiness_slot_try_reclaim(slot);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return SALTS_OK;
}

static int readiness_dispatch_slot(readiness_impl *impl, readiness_slot *slot,
                                   readiness_events events, int status) {
  readiness_callback callback;
  readiness_continuation continuation;
  readiness_callback_result result = {
      SALTS_READINESS_COMPLETE, 0u};
  readiness_generation_step arm_generation_step;
  readiness_slot *previous_callback_slot;
  void *callback_user;
  uint64_t token;
  uint64_t arm_token;
  int rearm_status;

  slot->interest = SALTS_READINESS_INTEREST_IDLE;
  slot->delivery = SALTS_READINESS_DELIVERY_CALLBACK;
  slot->arm_token = 0;
  callback = slot->callback;
  continuation = slot->continuation;
  callback_user = slot->callback_user;
  sync_mutex_unlock(&impl->mutex);

  previous_callback_slot = readiness_callback_slot;
  readiness_callback_slot = slot;
  if (continuation != NULL)
    result = continuation(callback_user, events, status);
  else
    callback(callback_user, events, status);
  readiness_callback_slot = previous_callback_slot;

  sync_mutex_lock(&impl->mutex);
  if (continuation != NULL && status == SALTS_OK &&
      result.action != SALTS_READINESS_COMPLETE &&
      result.action != SALTS_READINESS_REARM) {
    slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EINVAL;
  }
  if (continuation != NULL && status == SALTS_OK &&
      result.action == SALTS_READINESS_REARM &&
      !readiness_events_valid(result.interests)) {
    slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EINVAL;
  }
  if (continuation != NULL && status == SALTS_OK &&
      result.action == SALTS_READINESS_REARM &&
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_OPEN &&
      slot->terminal == SALTS_READINESS_TERMINAL_NONE &&
      slot->control == SALTS_READINESS_CONTROL_NONE &&
      impl->admission_open && !impl->terminalizing) {
    rearm_status = readiness_generation_prepare(
        slot->arm_generation, &arm_generation_step);
    if (rearm_status == SALTS_OK) {
      slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
      slot->interest = SALTS_READINESS_INTEREST_ARMING;
      slot->control = SALTS_READINESS_CONTROL_ARM;
      slot->arm_generation =
          readiness_generation_commit(&arm_generation_step);
      slot->arm_token =
          ((uint64_t)slot->arm_generation << 32) | (uint64_t)slot->index;
      token = readiness_slot_token(slot);
      arm_token = slot->arm_token;
      sync_mutex_unlock(&impl->mutex);

      rearm_status = impl->backend_ops.arm(
          impl->backend_user, token, arm_token, result.interests);

      sync_mutex_lock(&impl->mutex);
      if (rearm_status == SALTS_OK &&
          readiness_slot_token(slot) == token &&
          slot->interest == SALTS_READINESS_INTEREST_ARMING &&
          slot->control == SALTS_READINESS_CONTROL_ARM) {
        slot->interest = SALTS_READINESS_INTEREST_ARMED;
        slot->control = SALTS_READINESS_CONTROL_NONE;
        sync_cond_broadcast(&impl->changed);
        sync_mutex_unlock(&impl->mutex);
        return SALTS_OK;
      }
      if (readiness_slot_token(slot) == token &&
          slot->interest == SALTS_READINESS_INTEREST_ARMING &&
          slot->control == SALTS_READINESS_CONTROL_ARM) {
        slot->arm_generation =
            readiness_generation_rollback(&arm_generation_step);
        slot->interest = SALTS_READINESS_INTEREST_IDLE;
        slot->arm_token = 0u;
      }
    }
    if (slot->control == SALTS_READINESS_CONTROL_ARM)
      slot->control = SALTS_READINESS_CONTROL_NONE;
    slot->terminal = SALTS_READINESS_TERMINAL_DELIVERING;
    slot->delivery = SALTS_READINESS_DELIVERY_CALLBACK;
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);

    previous_callback_slot = readiness_callback_slot;
    readiness_callback_slot = slot;
    (void)continuation(callback_user, 0u, rearm_status);
    readiness_callback_slot = previous_callback_slot;

    sync_mutex_lock(&impl->mutex);
    slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
    slot->terminal = SALTS_READINESS_TERMINAL_NONE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
  readiness_slot_clear_callback(slot);
  readiness_slot_try_reclaim(slot);
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return SALTS_OK;
}

static int readiness_backend_dispatch_impl(readiness_reactor *reactor, uint64_t token,
                                           uint64_t arm_token, int validate_arm_token,
                                           readiness_events events, int status) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  readiness_slot *slot;
  uint32_t index = (uint32_t)token;
  uint32_t generation = (uint32_t)(token >> 32);

  if (impl == NULL || (status == SALTS_OK && !readiness_events_valid(events))) return SALTS_EINVAL;
  sync_mutex_lock(&impl->mutex);
  if ((size_t)index >= impl->capacity) {
    impl->stale_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  slot = &impl->slots[index];
  if (generation != 0 && slot->generation == generation && impl->terminalizing) {
    impl->duplicate_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  while (slot->control != SALTS_READINESS_CONTROL_NONE &&
         slot->generation == generation)
    sync_cond_wait(&impl->changed, &impl->mutex);
  if (generation != 0 && slot->generation == generation && impl->terminalizing) {
    impl->duplicate_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  if (generation == 0 || slot->generation != generation ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == SALTS_READINESS_LIFECYCLE_RETIRED) {
    impl->stale_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  if (slot->terminal != SALTS_READINESS_TERMINAL_NONE) {
    impl->duplicate_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  if (slot->lifecycle != SALTS_READINESS_LIFECYCLE_OPEN ||
      slot->interest != SALTS_READINESS_INTEREST_ARMED ||
      slot->delivery != SALTS_READINESS_DELIVERY_IDLE) {
    impl->duplicate_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  if (validate_arm_token && (arm_token == 0 || slot->arm_token != arm_token)) {
    impl->stale_events += 1u;
    sync_mutex_unlock(&impl->mutex);
    return SALTS_OK;
  }
  return readiness_dispatch_slot(impl, slot, events, status);
}

int readiness_backend_dispatch(readiness_reactor *reactor, uint64_t token,
                                     readiness_events events, int status) {
  return readiness_backend_dispatch_impl(reactor, token, 0, 0, events, status);
}

int readiness_backend_dispatch_generation(readiness_reactor *reactor,
                                                uint64_t token, uint64_t arm_token,
                                                readiness_events events, int status) {
  return readiness_backend_dispatch_impl(reactor, token, arm_token, 1, events, status);
}

static void readiness_fanout(readiness_impl *impl, int status) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    readiness_callback callback;
    readiness_continuation continuation;
    readiness_slot *previous_callback_slot;
    readiness_slot *slot;
    void *callback_user;
    int terminal_status;

    sync_mutex_lock(&impl->mutex);
    slot = &impl->slots[i];
    readiness_wait_slot_control(impl, slot);
    if (slot->terminal != SALTS_READINESS_TERMINAL_RESERVED) {
      sync_mutex_unlock(&impl->mutex);
      continue;
    }
    slot->terminal = SALTS_READINESS_TERMINAL_DELIVERING;
    slot->interest = SALTS_READINESS_INTEREST_IDLE;
    slot->delivery = SALTS_READINESS_DELIVERY_CALLBACK;
    slot->arm_token = 0;
    callback = slot->callback;
    continuation = slot->continuation;
    callback_user = slot->callback_user;
    terminal_status = slot->terminal_status;
    sync_mutex_unlock(&impl->mutex);

    previous_callback_slot = readiness_callback_slot;
    readiness_callback_slot = slot;
    if (continuation != NULL)
      (void)continuation(
          callback_user, 0,
          terminal_status != SALTS_OK ? terminal_status : status);
    else
      callback(callback_user, 0,
               terminal_status != SALTS_OK ? terminal_status : status);
    readiness_callback_slot = previous_callback_slot;

    sync_mutex_lock(&impl->mutex);
    slot->delivery = SALTS_READINESS_DELIVERY_IDLE;
    slot->terminal = SALTS_READINESS_TERMINAL_NONE;
    readiness_slot_clear_callback(slot);
    slot->terminal_status = SALTS_OK;
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
  }
}

static int readiness_retry_orphan_closes(readiness_impl *impl) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    readiness_slot *slot = &impl->slots[i];
    uint64_t token;
    int status;
    if (!slot->orphaned) continue;

    readiness_wait_slot_control(impl, slot);
    slot->control = SALTS_READINESS_CONTROL_CLOSE;
    token = readiness_slot_token(slot);
    sync_mutex_unlock(&impl->mutex);
    status = impl->backend_ops.close(impl->backend_user, token);
    sync_mutex_lock(&impl->mutex);
    if (status != SALTS_OK) {
      slot->control = SALTS_READINESS_CONTROL_NONE;
      sync_cond_broadcast(&impl->changed);
      return status;
    }
    slot->native_registered = 0;
    slot->orphaned = 0;
    slot->control = SALTS_READINESS_CONTROL_NONE;
    readiness_slot_try_reclaim(slot);
    sync_cond_broadcast(&impl->changed);
  }
  return SALTS_OK;
}

int readiness_backend_fail(readiness_reactor *reactor, int status) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || status >= SALTS_OK) return SALTS_EINVAL;
  sync_mutex_lock(&impl->mutex);
  if (!impl->admission_open) {
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EALREADY;
  }
  impl->admission_open = 0;
  impl->terminalizing = 1;
  impl->backend_errors += 1u;
  sync_cond_broadcast(&impl->changed);
  readiness_wait_controls(impl);
  readiness_snapshot_terminal(impl, status);
  impl->terminalizing = 0;
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  readiness_fanout(impl, status);
  return SALTS_OK;
}

int readiness_backend_wait_admission_closed(readiness_reactor *reactor) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL) return SALTS_EINVAL;
  sync_mutex_lock(&impl->mutex);
  while (impl->admission_open)
    sync_cond_wait(&impl->changed, &impl->mutex);
  sync_mutex_unlock(&impl->mutex);
  return SALTS_OK;
}

int readiness_reactor_shutdown(readiness_reactor *reactor) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  int orphan_status;
  int backend_status;
  if (impl == NULL) return SALTS_EINVAL;

  sync_mutex_lock(&impl->mutex);
  if (readiness_callback_on_impl(impl)) {
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EBUSY;
  }
  while (impl->terminalizing) {
    sync_cond_wait(&impl->changed, &impl->mutex);
  }
  if (impl->shutdown_complete || impl->shutdown_inflight) {
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EALREADY;
  }
  impl->shutdown_inflight = 1;
  impl->admission_open = 0;
  impl->terminalizing = 1;
  sync_cond_broadcast(&impl->changed);
  readiness_wait_controls(impl);
  readiness_snapshot_terminal(impl, SALTS_ESHUTDOWN);
  orphan_status = readiness_retry_orphan_closes(impl);
  if (orphan_status != SALTS_OK) {
    sync_mutex_unlock(&impl->mutex);
    readiness_fanout(impl, SALTS_ESHUTDOWN);
    sync_mutex_lock(&impl->mutex);
    impl->shutdown_inflight = 0;
    impl->terminalizing = 0;
    sync_cond_broadcast(&impl->changed);
    sync_mutex_unlock(&impl->mutex);
    return orphan_status;
  }
  readiness_wait_controls(impl);
  impl->backend_shutdown_inflight = 1;
  sync_mutex_unlock(&impl->mutex);

  backend_status = impl->backend_ops.shutdown(impl->backend_user);
  sync_mutex_lock(&impl->mutex);
  impl->backend_shutdown_inflight = 0;
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  readiness_fanout(impl, SALTS_ESHUTDOWN);

  sync_mutex_lock(&impl->mutex);
  for (;;) {
    size_t inflight = 0;
    for (size_t i = 0; i < impl->capacity; ++i)
      inflight +=
          impl->slots[i].delivery == SALTS_READINESS_DELIVERY_CALLBACK ||
          impl->slots[i].control != SALTS_READINESS_CONTROL_NONE;
    if (inflight == 0) break;
    sync_cond_wait(&impl->changed, &impl->mutex);
  }
  impl->shutdown_inflight = 0;
  if (backend_status == SALTS_OK) impl->shutdown_complete = 1;
  impl->terminalizing = 0;
  sync_cond_broadcast(&impl->changed);
  sync_mutex_unlock(&impl->mutex);
  return backend_status;
}

int readiness_reactor_destroy(readiness_reactor *reactor) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL) return SALTS_EINVAL;

  sync_mutex_lock(&impl->mutex);
  if (!impl->shutdown_complete) {
    sync_mutex_unlock(&impl->mutex);
    return SALTS_EBUSY;
  }
  while (impl->backend_shutdown_inflight || readiness_controls_inflight(impl))
    sync_cond_wait(&impl->changed, &impl->mutex);
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].lifecycle != SALTS_READINESS_LIFECYCLE_FREE &&
        impl->slots[i].lifecycle != SALTS_READINESS_LIFECYCLE_RETIRED) {
      sync_mutex_unlock(&impl->mutex);
      return SALTS_EBUSY;
    }
  }
  sync_mutex_unlock(&impl->mutex);

  sync_cond_destroy(&impl->changed);
  sync_mutex_destroy(&impl->mutex);
  impl->backend_ops.destroy(impl->backend_user);
  free(impl->slots);
  free(impl);
  reactor->impl = NULL;
  return SALTS_OK;
}

int readiness_reactor_stats(readiness_reactor *reactor, readiness_stats *stats) {
  readiness_impl *impl = readiness_impl_from_reactor(reactor);
  readiness_stats snapshot = {0};
  if (impl == NULL || stats == NULL) return SALTS_EINVAL;

  sync_mutex_lock(&impl->mutex);
  snapshot.capacity = impl->capacity;
  snapshot.rejected_full = impl->rejected_full;
  snapshot.stale_events = impl->stale_events;
  snapshot.duplicate_events = impl->duplicate_events;
  snapshot.backend_errors = impl->backend_errors;
  for (size_t i = 0; i < impl->capacity; ++i) {
    const readiness_slot *slot = &impl->slots[i];
    if (slot->lifecycle == SALTS_READINESS_LIFECYCLE_OPEN ||
        slot->lifecycle == SALTS_READINESS_LIFECYCLE_CLOSING)
      snapshot.registered_count += 1u;
    if (slot->interest == SALTS_READINESS_INTEREST_ARMED)
      snapshot.armed_count += 1u;
    if (slot->delivery == SALTS_READINESS_DELIVERY_CALLBACK)
      snapshot.callbacks_inflight += 1u;
  }
  sync_mutex_unlock(&impl->mutex);
  *stats = snapshot;
  return SALTS_OK;
}
