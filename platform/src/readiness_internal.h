#ifndef SALTS_READINESS_INTERNAL_H
#define SALTS_READINESS_INTERNAL_H

#include <error_codes.h>
#include <readiness.h>

#include <errno.h>

typedef struct cmeta_readiness_generation_step {
  uint32_t previous;
  uint32_t next;
} cmeta_readiness_generation_step;

typedef enum cmeta_readiness_lifecycle {
  SALTS_READINESS_LIFECYCLE_FREE = 0,
  SALTS_READINESS_LIFECYCLE_OPEN,
  SALTS_READINESS_LIFECYCLE_CLOSING,
  SALTS_READINESS_LIFECYCLE_RETIRED
} cmeta_readiness_lifecycle;

typedef enum cmeta_readiness_interest {
  SALTS_READINESS_INTEREST_IDLE = 0,
  SALTS_READINESS_INTEREST_ARMING,
  SALTS_READINESS_INTEREST_ARMED,
  SALTS_READINESS_INTEREST_UNARMING
} cmeta_readiness_interest;

typedef enum cmeta_readiness_delivery {
  SALTS_READINESS_DELIVERY_IDLE = 0,
  SALTS_READINESS_DELIVERY_CALLBACK
} cmeta_readiness_delivery;

typedef enum cmeta_readiness_terminal {
  SALTS_READINESS_TERMINAL_NONE = 0,
  SALTS_READINESS_TERMINAL_RESERVED,
  SALTS_READINESS_TERMINAL_DELIVERING
} cmeta_readiness_terminal;

typedef enum cmeta_readiness_control {
  SALTS_READINESS_CONTROL_NONE = 0,
  SALTS_READINESS_CONTROL_REGISTER,
  SALTS_READINESS_CONTROL_ARM,
  SALTS_READINESS_CONTROL_UNARM,
  SALTS_READINESS_CONTROL_CLOSE
} cmeta_readiness_control;

typedef struct cmeta_readiness_state_view {
  cmeta_readiness_lifecycle lifecycle;
  cmeta_readiness_interest interest;
  cmeta_readiness_delivery delivery;
  cmeta_readiness_terminal terminal;
  cmeta_readiness_control control;
  cmeta_readiness_callback callback;
  uint64_t arm_token;
  uint32_t arm_waiters;
  uint32_t api_borrows;
  int native_registered;
  int orphaned;
} cmeta_readiness_state_view;

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_state_model_valid(
    const cmeta_readiness_state_view *view);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_callback_forms_valid(
    cmeta_readiness_callback callback,
    cmeta_readiness_continuation continuation);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_enter(uintptr_t *admission);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_reserve_register(
    uintptr_t *admission);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_close(uintptr_t *admission);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_leave(uintptr_t *admission);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_reset(uintptr_t *admission);
SALTS_CORE_PLATFORM_API uintptr_t SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_max_entrants(void);
SALTS_CORE_PLATFORM_API uint32_t SALTS_CORE_PLATFORM_CALL cmeta_readiness_registration_admission_entrants(
    const uintptr_t *admission);

static inline int cmeta_readiness_generation_available(uint32_t generation) {
  return generation != UINT32_MAX;
}

static inline int cmeta_readiness_generation_prepare(
    uint32_t generation, cmeta_readiness_generation_step *step) {
  if (step == NULL) return SALTS_EINVAL;
  if (!cmeta_readiness_generation_available(generation)) return -EOVERFLOW;
  step->previous = generation;
  step->next = generation + 1u;
  return SALTS_OK;
}

static inline uint32_t cmeta_readiness_generation_commit(
    const cmeta_readiness_generation_step *step) {
  return step->next;
}

static inline uint32_t cmeta_readiness_generation_rollback(
    const cmeta_readiness_generation_step *step) {
  return step->previous;
}

typedef struct cmeta_readiness_backend_ops {
  /* A failing register/arm/unarm/close hook leaves its native effect uncommitted and
   * retryable.  shutdown may make monotonic partial progress on failure, but retains
   * backend ownership for a later retry.  Only shutdown SALTS_OK proves backend callbacks,
   * its thread, and all native reactor access are quiescent. */
  int (*register_resource)(void *user, intptr_t native_resource, uint64_t token);
  /* The arm hook must not call cmeta_readiness_backend_dispatch() or
   * cmeta_readiness_backend_dispatch_generation() inline on its own execution
   * thread, nor wait for a dispatch it triggered: dispatch waits for this arm's
   * control gate and such a hook would wait for itself. The hook may notify an
   * independent reactor/producer thread; its queued dispatch is released after
   * the hook returns and the state engine commits or rolls back the arm control
   * operation and clears that gate. */
  int (*arm)(void *user, uint64_t token, uint64_t arm_token,
             cmeta_readiness_events events);
  int (*unarm)(void *user, uint64_t token);
  int (*close)(void *user, uint64_t token);
  int (*shutdown)(void *user);
  void (*destroy)(void *user);
} cmeta_readiness_backend_ops;

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_init_backend(cmeta_readiness_reactor *reactor,
                                         const cmeta_readiness_config *config,
                                         const cmeta_readiness_backend_ops *backend_ops,
                                         void *backend_user);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_dispatch(cmeta_readiness_reactor *reactor, uint64_t token,
                                     cmeta_readiness_events events, int status);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_dispatch_generation(cmeta_readiness_reactor *reactor,
                                                uint64_t token, uint64_t arm_token,
                                                cmeta_readiness_events events, int status);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_fail(cmeta_readiness_reactor *reactor, int status);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_wait_admission_closed(cmeta_readiness_reactor *reactor);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_wait_arm_waiter(
    cmeta_readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_wait_arm_waiter_observe(
    cmeta_readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns, uint32_t *api_borrows);

#if defined(__linux__)
SALTS_CORE_PLATFORM_API uint32_t SALTS_CORE_PLATFORM_CALL cmeta_readiness_epoll_interest_events(cmeta_readiness_events events);
#endif

#if defined(SALTS_ENABLE_EPOLL_READINESS)
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_epoll_init(cmeta_readiness_reactor *reactor,
                               const cmeta_readiness_config *config);
#endif

#if defined(SALTS_ENABLE_KQUEUE_READINESS)
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_kqueue_init(cmeta_readiness_reactor *reactor,
                                const cmeta_readiness_config *config);
#endif

#if defined(SALTS_ENABLE_POLL_READINESS)
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_poll_init(cmeta_readiness_reactor *reactor,
                              const cmeta_readiness_config *config);
#endif

#endif /* SALTS_READINESS_INTERNAL_H */
