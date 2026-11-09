#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include "native_io_readiness.h"

#include <error_codes.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

typedef struct cmeta_io_epoll_state {
  int epoll_fd;
  int wake_fd;
  struct epoll_event *events;
  size_t event_capacity;
} cmeta_io_epoll_state;

static uint32_t epoll_native_interests(uint32_t interests) {
  uint32_t native = EPOLLERR | EPOLLHUP | EPOLLET;
  if ((interests & SALTS_IO_READY_READ) != 0u) native |= EPOLLIN | EPOLLPRI | EPOLLRDHUP;
  if ((interests & SALTS_IO_READY_WRITE) != 0u) native |= EPOLLOUT;
  return native;
}

static int epoll_driver_init(void *driver_state, size_t batch_capacity) {
  cmeta_io_epoll_state *state = (cmeta_io_epoll_state *)driver_state;
  if (batch_capacity > (size_t)INT_MAX || batch_capacity > SIZE_MAX / sizeof(struct epoll_event))
    return SALTS_ERANGE;
  state->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (state->epoll_fd < 0) return -errno;
  state->wake_fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
  if (state->wake_fd < 0) {
    const int saved_error = errno;
    (void)close(state->epoll_fd);
    state->epoll_fd = -1;
    return -saved_error;
  }
  {
    struct epoll_event wake_event = {0};
    wake_event.events = EPOLLIN;
    wake_event.data.u64 = 0u;
    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, state->wake_fd, &wake_event) != 0) {
      const int saved_error = errno;
      (void)close(state->wake_fd);
      (void)close(state->epoll_fd);
      state->wake_fd = -1;
      state->epoll_fd = -1;
      return -saved_error;
    }
  }
  state->events = (struct epoll_event *)calloc(batch_capacity, sizeof(*state->events));
  if (state->events == NULL) {
    const int saved_error = errno;
    (void)close(state->wake_fd);
    (void)close(state->epoll_fd);
    state->wake_fd = -1;
    state->epoll_fd = -1;
    return saved_error == 0 ? SALTS_ENOMEM : -saved_error;
  }
  state->event_capacity = batch_capacity;
  return SALTS_OK;
}

static int epoll_driver_update(void *driver_state, int fd, uint64_t token, uint32_t old_interests,
                               uint32_t new_interests) {
  cmeta_io_epoll_state *state = (cmeta_io_epoll_state *)driver_state;
  struct epoll_event event;
  int operation;
  int status;
  if (old_interests == new_interests) return SALTS_OK;
  if (old_interests == 0u) operation = EPOLL_CTL_ADD;
  else if (new_interests == 0u) operation = EPOLL_CTL_DEL;
  else operation = EPOLL_CTL_MOD;
  event.events = epoll_native_interests(new_interests);
  event.data.u64 = token;
  do {
    status = epoll_ctl(state->epoll_fd, operation, fd, operation == EPOLL_CTL_DEL ? NULL : &event);
  } while (status < 0 && errno == EINTR);
  if (status == 0) return SALTS_OK;
  /* release follows caller close. The fd number may already be reused by a
   * non-pollable resource (EPERM); it cannot name this registration anymore. */
  if (operation == EPOLL_CTL_DEL && (errno == ENOENT || errno == EBADF || errno == EPERM))
    return SALTS_OK;
  return -errno;
}

static int epoll_driver_wait(void *driver_state, cmeta_io_ready_event *events,
                             size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  cmeta_io_epoll_state *state = (cmeta_io_epoll_state *)driver_state;
  const size_t limit =
      event_capacity < state->event_capacity ? event_capacity : state->event_capacity;
  const uint64_t started_ms = cmeta_monotonic_ms();
  uint32_t remaining_ms = timeout_ms;
  int count;
  for (;;) {
    const int native_timeout = remaining_ms == UINT32_MAX         ? -1
                               : remaining_ms > (uint32_t)INT_MAX ? INT_MAX
                                                                   : (int)remaining_ms;
    count = epoll_wait(state->epoll_fd, state->events, (int)limit, native_timeout);
    if (count > 0 || (count < 0 && errno != EINTR)) break;
    remaining_ms = native_io_remaining_timeout(started_ms, timeout_ms);
    if (remaining_ms == 0u) return SALTS_ETIMEDOUT;
  }
  if (count < 0) return -errno;
  for (int index = 0; index < count; ++index) {
    const uint32_t native = state->events[index].events;
    uint32_t interests = 0u;
    if (state->events[index].data.u64 == 0u) {
      uint64_t wake_count;
      ssize_t read_status;
      do {
        read_status = read(state->wake_fd, &wake_count, sizeof(wake_count));
      } while (read_status < 0 && errno == EINTR);
      if (read_status < 0 && errno != EAGAIN) return -errno;
      events[index] = (cmeta_io_ready_event){0u, SALTS_IO_READY_WAKE, native};
      continue;
    }
    if ((native & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0u) interests |= SALTS_IO_READY_READ;
    if ((native & EPOLLOUT) != 0u) interests |= SALTS_IO_READY_WRITE;
    if ((native & (EPOLLERR | EPOLLHUP)) != 0u) interests |= SALTS_IO_READY_ERROR;
    events[index] = (cmeta_io_ready_event){state->events[index].data.u64, interests, native};
  }
  *out_count = (size_t)count;
  return SALTS_OK;
}

static int epoll_driver_wake(void *driver_state) {
  cmeta_io_epoll_state *state = (cmeta_io_epoll_state *)driver_state;
  const uint64_t signal = 1u;
  ssize_t status;
  do {
    status = write(state->wake_fd, &signal, sizeof(signal));
  } while (status < 0 && errno == EINTR);
  if (status == (ssize_t)sizeof(signal) || (status < 0 && errno == EAGAIN)) return SALTS_OK;
  return status < 0 ? -errno : SALTS_EIO;
}

static void epoll_driver_destroy(void *driver_state) {
  cmeta_io_epoll_state *state = (cmeta_io_epoll_state *)driver_state;
  free(state->events);
  state->events = NULL;
  if (state->wake_fd >= 0) (void)close(state->wake_fd);
  state->wake_fd = -1;
  if (state->epoll_fd >= 0) (void)close(state->epoll_fd);
  state->epoll_fd = -1;
}

static const cmeta_io_readiness_driver_ops epoll_driver_ops = {
    epoll_driver_init, epoll_driver_update, epoll_driver_wait, epoll_driver_wake,
    epoll_driver_destroy, true};

int cmeta_io_epoll_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  if (config->kind != NATIVE_IO_BACKEND_EPOLL) return SALTS_ENOTSUP;
  return cmeta_io_readiness_backend_init(backend, config, &epoll_driver_ops,
                                         sizeof(cmeta_io_epoll_state));
}
