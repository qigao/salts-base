#include "native_io_readiness.h"

#include <error_codes.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(uintptr_t) >= sizeof(uint64_t),
               "NativeIO kqueue endpoint tokens require a 64-bit target");

#define SALTS_IO_KQUEUE_WAKE_IDENT ((uintptr_t)UINTPTR_MAX)

typedef struct cmeta_io_kqueue_state {
  int kqueue_fd;
  struct kevent *events;
  size_t event_capacity;
} cmeta_io_kqueue_state;

typedef struct cmeta_io_kqueue_change {
  int16_t filter;
  bool add;
} cmeta_io_kqueue_change;

static int kqueue_apply_change(cmeta_io_kqueue_state *state, int fd, uint64_t token,
                               cmeta_io_kqueue_change change) {
  struct kevent event;
  int status;
  EV_SET(&event, (uintptr_t)fd, change.filter, change.add ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0u, 0,
         change.add ? (void *)(uintptr_t)token : NULL);
  do {
    status = kevent(state->kqueue_fd, &event, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  if (status == 0) return SALTS_OK;
  if (!change.add && errno == ENOENT) return SALTS_OK;
  return -errno;
}

static int kqueue_driver_init(void *driver_state, size_t batch_capacity) {
  cmeta_io_kqueue_state *state = (cmeta_io_kqueue_state *)driver_state;
  if (batch_capacity > (size_t)INT_MAX || batch_capacity > SIZE_MAX / sizeof(struct kevent))
    return SALTS_ERANGE;
  state->kqueue_fd = kqueue();
  if (state->kqueue_fd < 0) return -errno;
  {
    struct kevent wake_event;
    EV_SET(&wake_event, SALTS_IO_KQUEUE_WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, 0, NULL);
    if (kevent(state->kqueue_fd, &wake_event, 1, NULL, 0, NULL) != 0) {
      const int saved_error = errno;
      (void)close(state->kqueue_fd);
      state->kqueue_fd = -1;
      return -saved_error;
    }
  }
  state->events = (struct kevent *)calloc(batch_capacity, sizeof(*state->events));
  if (state->events == NULL) {
    (void)close(state->kqueue_fd);
    state->kqueue_fd = -1;
    return SALTS_ENOMEM;
  }
  state->event_capacity = batch_capacity;
  return SALTS_OK;
}

static int kqueue_driver_update(void *driver_state, int fd, uint64_t token, uint32_t old_interests,
                                uint32_t new_interests) {
  cmeta_io_kqueue_state *state = (cmeta_io_kqueue_state *)driver_state;
  cmeta_io_kqueue_change changes[2];
  size_t count = 0u;
  size_t applied = 0u;
  int status;
  if ((old_interests & SALTS_IO_READY_READ) != (new_interests & SALTS_IO_READY_READ))
    changes[count++] =
        (cmeta_io_kqueue_change){EVFILT_READ, (new_interests & SALTS_IO_READY_READ) != 0u};
  if ((old_interests & SALTS_IO_READY_WRITE) != (new_interests & SALTS_IO_READY_WRITE))
    changes[count++] =
        (cmeta_io_kqueue_change){EVFILT_WRITE, (new_interests & SALTS_IO_READY_WRITE) != 0u};
  while (applied < count) {
    status = kqueue_apply_change(state, fd, token, changes[applied]);
    if (status != SALTS_OK) {
      while (applied != 0u) {
        cmeta_io_kqueue_change rollback;
        --applied;
        rollback = changes[applied];
        rollback.add = !rollback.add;
        (void)kqueue_apply_change(state, fd, token, rollback);
      }
      return status;
    }
    ++applied;
  }
  return SALTS_OK;
}

static int kqueue_driver_wait(void *driver_state, cmeta_io_ready_event *events,
                              size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  cmeta_io_kqueue_state *state = (cmeta_io_kqueue_state *)driver_state;
  const size_t limit =
      event_capacity < state->event_capacity ? event_capacity : state->event_capacity;
  struct timespec timeout;
  const struct timespec *timeout_pointer = NULL;
  const uint64_t started_ms = cmeta_monotonic_ms();
  uint32_t remaining_ms = timeout_ms;
  int count;
  for (;;) {
    if (remaining_ms != UINT32_MAX) {
      timeout.tv_sec = (time_t)(remaining_ms / 1000u);
      timeout.tv_nsec = (long)(remaining_ms % 1000u) * 1000000L;
      timeout_pointer = &timeout;
    }
    count = kevent(state->kqueue_fd, NULL, 0, state->events, (int)limit, timeout_pointer);
    if (count > 0 || (count < 0 && errno != EINTR)) break;
    remaining_ms = native_io_remaining_timeout(started_ms, timeout_ms);
    if (remaining_ms == 0u) return SALTS_ETIMEDOUT;
  }
  if (count < 0) return -errno;
  for (int index = 0; index < count; ++index) {
    const struct kevent *native = &state->events[index];
    uint32_t interests = 0u;
    uint32_t native_status = 0u;
    if (native->filter == EVFILT_USER && native->ident == SALTS_IO_KQUEUE_WAKE_IDENT) {
      events[index] = (cmeta_io_ready_event){0u, SALTS_IO_READY_WAKE, 0u};
      continue;
    }
    if (native->filter == EVFILT_READ) interests |= SALTS_IO_READY_READ;
    if (native->filter == EVFILT_WRITE) interests |= SALTS_IO_READY_WRITE;
    if ((native->flags & EV_EOF) != 0u) interests |= SALTS_IO_READY_ERROR;
    if ((native->flags & EV_ERROR) != 0u) {
      interests |= SALTS_IO_READY_ERROR;
      if (native->data > 0 && (uintmax_t)native->data <= UINT32_MAX)
        native_status = (uint32_t)native->data;
    }
    events[index] =
        (cmeta_io_ready_event){(uint64_t)(uintptr_t)native->udata, interests, native_status};
  }
  *out_count = (size_t)count;
  return SALTS_OK;
}

static int kqueue_driver_wake(void *driver_state) {
  cmeta_io_kqueue_state *state = (cmeta_io_kqueue_state *)driver_state;
  struct kevent event;
  int status;
  EV_SET(&event, SALTS_IO_KQUEUE_WAKE_IDENT, EVFILT_USER, 0u, NOTE_TRIGGER, 0, NULL);
  do {
    status = kevent(state->kqueue_fd, &event, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  return status == 0 ? SALTS_OK : -errno;
}

static void kqueue_driver_destroy(void *driver_state) {
  cmeta_io_kqueue_state *state = (cmeta_io_kqueue_state *)driver_state;
  free(state->events);
  state->events = NULL;
  if (state->kqueue_fd >= 0) (void)close(state->kqueue_fd);
  state->kqueue_fd = -1;
}

static const cmeta_io_readiness_driver_ops kqueue_driver_ops = {
    kqueue_driver_init, kqueue_driver_update, kqueue_driver_wait, kqueue_driver_wake,
    kqueue_driver_destroy, false};

int cmeta_io_kqueue_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  if (config->kind != NATIVE_IO_BACKEND_KQUEUE) return SALTS_ENOTSUP;
  return cmeta_io_readiness_backend_init(backend, config, &kqueue_driver_ops,
                                         sizeof(cmeta_io_kqueue_state));
}
