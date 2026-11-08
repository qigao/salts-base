#ifndef SALTS_READINESS_H
#define SALTS_READINESS_H

#include <platform_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_readiness_reactor {
  void *impl;
} cmeta_readiness_reactor;

typedef struct cmeta_readiness_registration {
  void *impl;
  uintptr_t _admission;
} cmeta_readiness_registration;

typedef uint32_t cmeta_readiness_events;

enum {
  SALTS_READINESS_EVENT_READ = 1u << 0,
  SALTS_READINESS_EVENT_WRITE = 1u << 1,
  SALTS_READINESS_EVENT_ERROR = 1u << 2,
  SALTS_READINESS_EVENT_HANGUP = 1u << 3
};

typedef struct cmeta_readiness_config {
  size_t registration_capacity;
  size_t event_batch_capacity;
} cmeta_readiness_config;

typedef enum cmeta_readiness_backend_kind {
  SALTS_READINESS_BACKEND_EPOLL = 1,
  SALTS_READINESS_BACKEND_KQUEUE,
  SALTS_READINESS_BACKEND_POLL
} cmeta_readiness_backend_kind;

typedef struct cmeta_readiness_stats {
  size_t capacity;
  size_t registered_count;
  size_t armed_count;
  size_t callbacks_inflight;
  uint64_t rejected_full;
  uint64_t stale_events;
  uint64_t duplicate_events;
  uint64_t backend_errors;
} cmeta_readiness_stats;

typedef void (*cmeta_readiness_callback)(void *user, cmeta_readiness_events events, int status);

typedef enum cmeta_readiness_action {
  SALTS_READINESS_COMPLETE = 0,
  SALTS_READINESS_REARM = 1
} cmeta_readiness_action;

typedef struct cmeta_readiness_callback_result {
  cmeta_readiness_action action;
  cmeta_readiness_events interests;
} cmeta_readiness_callback_result;

typedef cmeta_readiness_callback_result (*cmeta_readiness_continuation)(
    void *user, cmeta_readiness_events events, int status);

/*
 * Reactors and registrations are zero-state handles. A successful register
 * borrows native_resource until cmeta_readiness_close() returns SALTS_OK.
 * Registration handles are move-only: copying the complete handle is allowed
 * only at a quiescent ownership-transfer boundary. Controls on one handle are
 * concurrency-safe; register, close, or reuse of that handle must not race its
 * ownership transfer. Reactor destroy requires every registration handle and
 * every public registration API call associated with the reactor to be
 * quiescent.
 *
 * Capacity is fixed at initialization. registration_capacity and
 * event_batch_capacity must be nonzero, the batch may not exceed capacity + 1,
 * and registration_capacity may not exceed UINT32_MAX - 1. The native factory
 * returns SALTS_ENOTSUP when no configured backend exists; it never falls back.
 * The explicit poll backend additionally rejects registration_capacity above
 * INT_MAX - 1 because poll() reports the ready descriptor count as int.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_init(cmeta_readiness_reactor *reactor,
                                 const cmeta_readiness_config *config);

/** Returns compile-time availability; it does not probe runtime OS policy. */
SALTS_CORE_PLATFORM_API bool SALTS_CORE_PLATFORM_CALL cmeta_readiness_backend_supported(cmeta_readiness_backend_kind kind);

/**
 * Initializes exactly kind. Unsupported kinds return SALTS_ENOTSUP and clear
 * reactor; this selector never falls back to another backend.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_init_kind(cmeta_readiness_reactor *reactor,
                                      const cmeta_readiness_config *config,
                                      cmeta_readiness_backend_kind kind);

/*
 * Shutdown atomically closes admission and reserves one SALTS_ESHUTDOWN
 * delivery for every armed registration; ordinary readiness cannot overtake
 * that terminal delivery. Registrations remain owned by their handles and must
 * be closed before destroy. A failed shutdown preserves the reactor and may be
 * retried; destroy returns SALTS_EBUSY until backend shutdown succeeds.
 * Shutdown invoked from any callback owned by this reactor returns
 * SALTS_EBUSY without committing a state transition. Repeated shutdown after
 * success returns SALTS_EALREADY.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_shutdown(cmeta_readiness_reactor *reactor);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_destroy(cmeta_readiness_reactor *reactor);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_register(cmeta_readiness_reactor *reactor, intptr_t native_resource,
                             cmeta_readiness_registration *registration);

/*
 * Arm is one-shot. callback and user must already be valid before this call.
 * Backend events are serialized behind the arm control operation; delivery is
 * released after its commit clears the control gate, and may race the
 * function's final return and the caller's observation of that return. Keep
 * callback and user valid until callback completion or successful unarm/close.
 * An external arm of the same registration waits for an inflight callback to
 * return, then re-evaluates terminal, admission, generation, and slot state.
 * Arm from inside that registration's callback fails fast with SALTS_EBUSY.
 * The backend event and user callback are dispatched without a Platform lock
 * held. status is SALTS_OK for readiness or an exact negative terminal backend
 * code; terminal delivery uses events == 0.
 * For portable explicit-poll behavior, request ERROR/HANGUP together with the
 * related READ or WRITE interest; some poll implementations do not report
 * terminal bits for a descriptor whose native interest mask is empty.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_arm(cmeta_readiness_registration *registration, cmeta_readiness_events events,
                        cmeta_readiness_callback callback, void *user);

/*
 * Continuation arm is the additive persistent-reactor form of the one-shot
 * contract. For ordinary readiness, the callback returns COMPLETE or REARM
 * with the next nonzero interest set. Platform serializes and commits REARM
 * only after the callback returns; callers must not invoke registration
 * controls recursively from the callback. Terminal callbacks use events == 0,
 * ignore the returned action, and never rearm. An invalid action or invalid
 * REARM interests completes the arm and makes dispatch return SALTS_EINVAL.
 * If the backend rejects a continuation rearm, Platform invokes the same
 * continuation exactly once more with events == 0 and the backend error.
 *
 * @param registration Open registration owned by the caller.
 * @param events Initial nonzero interest set.
 * @param continuation Bounded nonblocking callback kept valid through its
 *        terminal invocation or successful unarm/close.
 * @param user Borrowed callback context with the same lifetime as continuation.
 * @return SALTS_OK after the initial arm commits; SALTS_EINVAL for invalid
 *         arguments; SALTS_EALREADY if already armed; SALTS_EBUSY for callback
 *         reentry or conflicting control; SALTS_ESHUTDOWN after admission
 *         closes; otherwise the exact initial backend arm error.
 *
 * A continuing callback returns (cmeta_readiness_callback_result){
 *     work_remains ? SALTS_READINESS_REARM : SALTS_READINESS_COMPLETE,
 *     work_remains ? SALTS_READINESS_EVENT_READ : 0u};
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_arm_continuation(cmeta_readiness_registration *registration,
                                     cmeta_readiness_events events,
                                     cmeta_readiness_continuation continuation, void *user);

/*
 * External unarm/close calls return only after an inflight callback is
 * quiescent. Unarm or close from inside that registration's callback fails
 * fast with SALTS_EBUSY. Close also returns SALTS_EBUSY while terminal
 * delivery is reserved. Only SALTS_OK consumes and clears the registration;
 * SALTS_EBUSY and backend errors preserve the handle and cleanup ownership so
 * the caller can retry after quiescence. While fatal or shutdown terminalization
 * is in progress, registration controls invoked by any callback on that reactor
 * fail fast with SALTS_EBUSY; external callers wait and then re-evaluate state.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_unarm(cmeta_readiness_registration *registration);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_close(cmeta_readiness_registration *registration);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_readiness_reactor_stats(cmeta_readiness_reactor *reactor, cmeta_readiness_stats *stats);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_READINESS_H */
