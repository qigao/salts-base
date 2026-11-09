#ifndef SALTS_READINESS_INTERNAL_H
#define SALTS_READINESS_INTERNAL_H

#include <core/error_codes.h>
#include <readiness.h>

#include <errno.h>

typedef struct readiness_generation_step {
  uint32_t previous;
  uint32_t next;
} readiness_generation_step;

typedef enum readiness_lifecycle {
  SALTS_READINESS_LIFECYCLE_FREE = 0,
  SALTS_READINESS_LIFECYCLE_OPEN,
  SALTS_READINESS_LIFECYCLE_CLOSING,
  SALTS_READINESS_LIFECYCLE_RETIRED
} readiness_lifecycle;

typedef enum readiness_interest {
  SALTS_READINESS_INTEREST_IDLE = 0,
  SALTS_READINESS_INTEREST_ARMING,
  SALTS_READINESS_INTEREST_ARMED,
  SALTS_READINESS_INTEREST_UNARMING
} readiness_interest;

typedef enum readiness_delivery {
  SALTS_READINESS_DELIVERY_IDLE = 0,
  SALTS_READINESS_DELIVERY_CALLBACK
} readiness_delivery;

typedef enum readiness_terminal {
  SALTS_READINESS_TERMINAL_NONE = 0,
  SALTS_READINESS_TERMINAL_RESERVED,
  SALTS_READINESS_TERMINAL_DELIVERING
} readiness_terminal;

typedef enum readiness_control {
  SALTS_READINESS_CONTROL_NONE = 0,
  SALTS_READINESS_CONTROL_REGISTER,
  SALTS_READINESS_CONTROL_ARM,
  SALTS_READINESS_CONTROL_UNARM,
  SALTS_READINESS_CONTROL_CLOSE
} readiness_control;

typedef struct readiness_state_view {
  readiness_lifecycle lifecycle;
  readiness_interest interest;
  readiness_delivery delivery;
  readiness_terminal terminal;
  readiness_control control;
  readiness_callback callback;
  uint64_t arm_token;
  uint32_t arm_waiters;
  uint32_t api_borrows;
  int native_registered;
  int orphaned;
} readiness_state_view;

 int readiness_state_model_valid(
    const readiness_state_view *view);
 int readiness_callback_forms_valid(
    readiness_callback callback,
    readiness_continuation continuation);
 int readiness_registration_admission_enter(uintptr_t *admission);
 int readiness_registration_admission_reserve_register(
    uintptr_t *admission);
 int readiness_registration_admission_close(uintptr_t *admission);
 void readiness_registration_admission_leave(uintptr_t *admission);
 int readiness_registration_admission_reset(uintptr_t *admission);
 uintptr_t readiness_registration_admission_max_entrants(void);
 uint32_t readiness_registration_admission_entrants(
    const uintptr_t *admission);

static inline int readiness_generation_available(uint32_t generation) {
  return generation != UINT32_MAX;
}

static inline int readiness_generation_prepare(
    uint32_t generation, readiness_generation_step *step) {
  if (step == NULL) return SALTS_EINVAL;
  if (!readiness_generation_available(generation)) return -EOVERFLOW;
  step->previous = generation;
  step->next = generation + 1u;
  return SALTS_OK;
}

static inline uint32_t readiness_generation_commit(
    const readiness_generation_step *step) {
  return step->next;
}

static inline uint32_t readiness_generation_rollback(
    const readiness_generation_step *step) {
  return step->previous;
}

typedef struct readiness_backend_ops {
  /* A failing register/arm/unarm/close hook leaves its native effect uncommitted and
   * retryable.  shutdown may make monotonic partial progress on failure, but retains
   * backend ownership for a later retry.  Only shutdown SALTS_OK proves backend callbacks,
   * its thread, and all native reactor access are quiescent. */
  int (*register_resource)(void *user, intptr_t native_resource, uint64_t token);
  /* The arm hook must not call readiness_backend_dispatch() or
   * readiness_backend_dispatch_generation() inline on its own execution
   * thread, nor wait for a dispatch it triggered: dispatch waits for this arm's
   * control gate and such a hook would wait for itself. The hook may notify an
   * independent reactor/producer thread; its queued dispatch is released after
   * the hook returns and the state engine commits or rolls back the arm control
   * operation and clears that gate. */
  int (*arm)(void *user, uint64_t token, uint64_t arm_token,
             readiness_events events);
  int (*unarm)(void *user, uint64_t token);
  int (*close)(void *user, uint64_t token);
  int (*shutdown)(void *user);
  void (*destroy)(void *user);
} readiness_backend_ops;

 int readiness_reactor_init_backend(readiness_reactor *reactor,
                                         const readiness_config *config,
                                         const readiness_backend_ops *backend_ops,
                                         void *backend_user);
 int readiness_backend_dispatch(readiness_reactor *reactor, uint64_t token,
                                     readiness_events events, int status);
 int readiness_backend_dispatch_generation(readiness_reactor *reactor,
                                                uint64_t token, uint64_t arm_token,
                                                readiness_events events, int status);
 int readiness_backend_fail(readiness_reactor *reactor, int status);
 int readiness_backend_wait_admission_closed(readiness_reactor *reactor);
 int readiness_backend_wait_arm_waiter(
    readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns);
 int readiness_backend_wait_arm_waiter_observe(
    readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns, uint32_t *api_borrows);

#if defined(__linux__)
 uint32_t readiness_epoll_interest_events(readiness_events events);
#endif

#if defined(SALTS_ENABLE_EPOLL_READINESS)
 int readiness_epoll_init(readiness_reactor *reactor,
                               const readiness_config *config);
#endif

#if defined(SALTS_ENABLE_KQUEUE_READINESS)
 int readiness_kqueue_init(readiness_reactor *reactor,
                                const readiness_config *config);
#endif

#if defined(SALTS_ENABLE_POLL_READINESS)
 int readiness_poll_init(readiness_reactor *reactor,
                              const readiness_config *config);
#endif

#endif /* SALTS_READINESS_INTERNAL_H */
