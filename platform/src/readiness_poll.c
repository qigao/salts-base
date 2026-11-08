#if defined(__linux__) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include "readiness_internal.h"

#include <error_codes.h>
#include <thread.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct cmeta_readiness_poll_record {
  int fd;
  uint64_t registration_token;
  uint64_t event_token;
  cmeta_readiness_events events;
  int active;
  int armed;
} cmeta_readiness_poll_record;

typedef struct cmeta_readiness_poll_snapshot {
  size_t record_index;
  uint64_t registration_token;
  uint64_t event_token;
  cmeta_readiness_events events;
} cmeta_readiness_poll_snapshot;

typedef struct cmeta_readiness_poll_backend {
  cmeta_readiness_reactor *reactor;
  cmeta_readiness_poll_record *records;
  struct pollfd *pollfds;
  cmeta_readiness_poll_snapshot *snapshots;
  size_t capacity;
  size_t event_batch_capacity;
  size_t next_scan_index;
  size_t controls_pending;
  cmeta_mutex_t mutex;
  cmeta_cond_t changed;
  cmeta_thread_t thread;
  int control_read_fd;
  int control_write_fd;
  int thread_started;
  int thread_exited;
  atomic_int stopping;
} cmeta_readiness_poll_backend;

typedef enum poll_record_change {
  POLL_RECORD_REGISTER = 0,
  POLL_RECORD_ARM,
  POLL_RECORD_UNARM,
  POLL_RECORD_CLOSE
} poll_record_change;

static uint32_t poll_token_index(uint64_t token) { return (uint32_t)token; }

static int poll_fd_valid(int fd) {
  int status;
  do {
    status = fcntl(fd, F_GETFD);
  } while (status < 0 && errno == EINTR);
  return status >= 0 ? SALTS_OK : -errno;
}

#if !defined(__linux__)
static int poll_set_nonblocking_cloexec(int fd) {
  int flags;
  do {
    flags = fcntl(fd, F_GETFL);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) return -errno;
  while (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (errno != EINTR) return -errno;
  }
  do {
    flags = fcntl(fd, F_GETFD);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) return -errno;
  while (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
    if (errno != EINTR) return -errno;
  }
  return SALTS_OK;
}
#endif

static int poll_control_pipe_create(int fds[2]) {
#if defined(__linux__)
  if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) return SALTS_OK;
  return -errno;
#else
  int status;
  if (pipe(fds) != 0) return -errno;
  status = poll_set_nonblocking_cloexec(fds[0]);
  if (status == SALTS_OK) status = poll_set_nonblocking_cloexec(fds[1]);
  if (status != SALTS_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
#endif
}

static int poll_control_write(cmeta_readiness_poll_backend *backend) {
  const uint8_t byte = 1u;
  ssize_t written;
  do {
    written = write(backend->control_write_fd, &byte, sizeof(byte));
  } while (written < 0 && errno == EINTR);
  if (written == (ssize_t)sizeof(byte) ||
      (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
    return SALTS_OK;
  return written < 0 ? -errno : SALTS_EIO;
}

static int poll_control_drain(cmeta_readiness_poll_backend *backend) {
  uint8_t bytes[64];
  for (;;) {
    ssize_t count = read(backend->control_read_fd, bytes, sizeof(bytes));
    if (count > 0) continue;
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return SALTS_OK;
    return count == 0 ? SALTS_EIO : -errno;
  }
}

static short poll_interest_events(cmeta_readiness_events events) {
  short native_events = 0;
  if ((events & SALTS_READINESS_EVENT_READ) != 0u)
    native_events = (short)(native_events | POLLIN | POLLPRI);
  if ((events & SALTS_READINESS_EVENT_WRITE) != 0u)
    native_events = (short)(native_events | POLLOUT);
  return native_events;
}

static cmeta_readiness_events poll_translate_events(short native_events) {
  cmeta_readiness_events events = 0u;
  if ((native_events & (POLLIN | POLLPRI)) != 0) events |= SALTS_READINESS_EVENT_READ;
  if ((native_events & POLLOUT) != 0) events |= SALTS_READINESS_EVENT_WRITE;
  if ((native_events & (POLLERR | POLLNVAL)) != 0) events |= SALTS_READINESS_EVENT_ERROR;
  if ((native_events & POLLHUP) != 0) events |= SALTS_READINESS_EVENT_HANGUP;
  return events;
}

static int poll_record_change_apply(cmeta_readiness_poll_backend *backend,
                                    poll_record_change change, intptr_t native_resource,
                                    uint64_t token, uint64_t event_token,
                                    cmeta_readiness_events events) {
  const uint32_t index = poll_token_index(token);
  cmeta_readiness_poll_record previous;
  cmeta_readiness_poll_record desired;
  int fd = -1;
  int status;

  if ((uint32_t)(token >> 32) == 0u || (size_t)index >= backend->capacity) return SALTS_EINVAL;
  if (change == POLL_RECORD_REGISTER) {
    if (native_resource < 0 || native_resource > INT_MAX) return SALTS_EINVAL;
    fd = (int)native_resource;
    status = poll_fd_valid(fd);
    if (status != SALTS_OK) return status;
  }

  cmeta_mutex_lock(&backend->mutex);
  while (backend->controls_pending != 0u)
    cmeta_cond_wait(&backend->changed, &backend->mutex);
  previous = backend->records[index];
  desired = previous;
  switch (change) {
  case POLL_RECORD_REGISTER:
    if (previous.active) {
      cmeta_mutex_unlock(&backend->mutex);
      return SALTS_EALREADY;
    }
    for (size_t i = 0u; i < backend->capacity; ++i) {
      if (backend->records[i].active && backend->records[i].fd == fd) {
        cmeta_mutex_unlock(&backend->mutex);
        return SALTS_EALREADY;
      }
    }
    desired.fd = fd;
    desired.registration_token = token;
    desired.event_token = 0u;
    desired.events = 0u;
    desired.active = 1;
    desired.armed = 0;
    break;
  case POLL_RECORD_ARM:
    if (!previous.active || previous.registration_token != token || previous.armed) {
      cmeta_mutex_unlock(&backend->mutex);
      return previous.armed ? SALTS_EALREADY : SALTS_EINVAL;
    }
    desired.event_token = event_token;
    desired.events = events;
    desired.armed = 1;
    break;
  case POLL_RECORD_UNARM:
  case POLL_RECORD_CLOSE:
    if (!previous.active || previous.registration_token != token) {
      cmeta_mutex_unlock(&backend->mutex);
      return SALTS_EINVAL;
    }
    desired.event_token = 0u;
    desired.events = 0u;
    desired.armed = 0;
    if (change == POLL_RECORD_CLOSE) {
      desired.fd = -1;
      desired.registration_token = 0u;
      desired.active = 0;
    }
    break;
  }
  backend->records[index] = desired;
  backend->controls_pending = 1u;
  cmeta_mutex_unlock(&backend->mutex);

  status = poll_control_write(backend);

  cmeta_mutex_lock(&backend->mutex);
  if (status != SALTS_OK) backend->records[index] = previous;
  backend->controls_pending = 0u;
  cmeta_cond_broadcast(&backend->changed);
  cmeta_mutex_unlock(&backend->mutex);
  return status;
}

static int poll_register_resource(void *user, intptr_t native_resource, uint64_t token) {
  return poll_record_change_apply((cmeta_readiness_poll_backend *)user, POLL_RECORD_REGISTER,
                                  native_resource, token, 0u, 0u);
}

static int poll_arm(void *user, uint64_t token, uint64_t event_token,
                    cmeta_readiness_events events) {
  return poll_record_change_apply((cmeta_readiness_poll_backend *)user, POLL_RECORD_ARM, -1, token,
                                  event_token, events);
}

static int poll_unarm(void *user, uint64_t token) {
  return poll_record_change_apply((cmeta_readiness_poll_backend *)user, POLL_RECORD_UNARM, -1,
                                  token, 0u, 0u);
}

static int poll_close_registration(void *user, uint64_t token) {
  return poll_record_change_apply((cmeta_readiness_poll_backend *)user, POLL_RECORD_CLOSE, -1,
                                  token, 0u, 0u);
}

static size_t poll_snapshot_build(cmeta_readiness_poll_backend *backend) {
  size_t count = 1u;
  cmeta_mutex_lock(&backend->mutex);
  while (backend->controls_pending != 0u)
    cmeta_cond_wait(&backend->changed, &backend->mutex);
  backend->pollfds[0].fd = backend->control_read_fd;
  backend->pollfds[0].events = POLLIN;
  backend->pollfds[0].revents = 0;
  backend->snapshots[0].registration_token = 0u;
  backend->snapshots[0].event_token = 0u;
  backend->snapshots[0].events = 0u;
  backend->snapshots[0].record_index = SIZE_MAX;
  for (size_t offset = 0u; offset < backend->capacity; ++offset) {
    size_t index = backend->next_scan_index + offset;
    const cmeta_readiness_poll_record *record;
    if (index >= backend->capacity) index -= backend->capacity;
    record = &backend->records[index];
    if (!record->active || !record->armed) continue;
    backend->pollfds[count].fd = record->fd;
    backend->pollfds[count].events = poll_interest_events(record->events);
    backend->pollfds[count].revents = 0;
    backend->snapshots[count].record_index = index;
    backend->snapshots[count].registration_token = record->registration_token;
    backend->snapshots[count].event_token = record->event_token;
    backend->snapshots[count].events = record->events;
    ++count;
  }
  cmeta_mutex_unlock(&backend->mutex);
  return count;
}

static int poll_snapshot_claim(cmeta_readiness_poll_backend *backend, size_t snapshot_index) {
  const cmeta_readiness_poll_snapshot *snapshot = &backend->snapshots[snapshot_index];
  const size_t index = snapshot->record_index;
  int claimed = 0;
  cmeta_mutex_lock(&backend->mutex);
  if (index < backend->capacity) {
    cmeta_readiness_poll_record *record = &backend->records[index];
    if (record->active && record->armed && record->fd == backend->pollfds[snapshot_index].fd &&
        record->registration_token == snapshot->registration_token &&
        record->event_token == snapshot->event_token) {
      record->armed = 0;
      backend->next_scan_index = index + 1u;
      if (backend->next_scan_index == backend->capacity) backend->next_scan_index = 0u;
      claimed = 1;
    }
  }
  cmeta_mutex_unlock(&backend->mutex);
  return claimed;
}

static void poll_thread_mark_exited(cmeta_readiness_poll_backend *backend) {
  cmeta_mutex_lock(&backend->mutex);
  backend->thread_exited = 1;
  cmeta_cond_broadcast(&backend->changed);
  cmeta_mutex_unlock(&backend->mutex);
}

static void poll_thread_entry(void *user) {
  cmeta_readiness_poll_backend *backend = (cmeta_readiness_poll_backend *)user;
  int terminal_status = SALTS_OK;
  for (;;) {
    const size_t count = poll_snapshot_build(backend);
    size_t delivered = 0u;
    int ready;
    do {
      ready = poll(backend->pollfds, (nfds_t)count, -1);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) {
      terminal_status = -errno;
      break;
    }

    if (backend->pollfds[0].revents != 0) {
      if ((backend->pollfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        terminal_status = SALTS_EIO;
        break;
      }
      terminal_status = poll_control_drain(backend);
      if (terminal_status != SALTS_OK) break;
    }

    for (size_t i = 1u; i < count && delivered < backend->event_batch_capacity; ++i) {
      cmeta_readiness_events events;
      if (backend->pollfds[i].revents == 0 || !poll_snapshot_claim(backend, i)) continue;
      events = poll_translate_events(backend->pollfds[i].revents);
      if ((backend->snapshots[i].events & SALTS_READINESS_EVENT_READ) == 0u)
        events &= ~SALTS_READINESS_EVENT_READ;
      if ((backend->snapshots[i].events & SALTS_READINESS_EVENT_WRITE) == 0u)
        events &= ~SALTS_READINESS_EVENT_WRITE;
      if (events == 0u) continue;
      ++delivered;
      (void)cmeta_readiness_backend_dispatch_generation(
          backend->reactor, backend->snapshots[i].registration_token,
          backend->snapshots[i].event_token, events, SALTS_OK);
    }
    if (backend->pollfds[0].revents != 0 &&
        atomic_load_explicit(&backend->stopping, memory_order_acquire))
      break;
  }
  if (terminal_status != SALTS_OK)
    (void)cmeta_readiness_backend_fail(backend->reactor, terminal_status);
  poll_thread_mark_exited(backend);
}

static int poll_shutdown(void *user) {
  cmeta_readiness_poll_backend *backend = (cmeta_readiness_poll_backend *)user;
  int status;
  if (!backend->thread_started) return SALTS_OK;
  atomic_store_explicit(&backend->stopping, 1, memory_order_release);
  status = poll_control_write(backend);
  if (status != SALTS_OK) {
    atomic_store_explicit(&backend->stopping, 0, memory_order_release);
    return status;
  }
  cmeta_mutex_lock(&backend->mutex);
  while (!backend->thread_exited)
    cmeta_cond_wait(&backend->changed, &backend->mutex);
  cmeta_mutex_unlock(&backend->mutex);
  status = cmeta_thread_join(&backend->thread);
  if (status != SALTS_OK) return status;
  backend->thread_started = 0;
  return SALTS_OK;
}

static void poll_backend_destroy(void *user) {
  cmeta_readiness_poll_backend *backend = (cmeta_readiness_poll_backend *)user;
  if (backend == NULL) return;
  if (backend->control_read_fd >= 0) (void)close(backend->control_read_fd);
  if (backend->control_write_fd >= 0) (void)close(backend->control_write_fd);
  cmeta_cond_destroy(&backend->changed);
  cmeta_mutex_destroy(&backend->mutex);
  free(backend->snapshots);
  free(backend->pollfds);
  free(backend->records);
  free(backend);
}

static const cmeta_readiness_backend_ops poll_backend_ops = {
    poll_register_resource,  poll_arm,      poll_unarm,
    poll_close_registration, poll_shutdown, poll_backend_destroy};

static int poll_config_validate(const cmeta_readiness_config *config) {
  size_t snapshot_capacity;
  if (config == NULL || config->registration_capacity == 0u || config->event_batch_capacity == 0u)
    return SALTS_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u ||
      config->registration_capacity > (size_t)INT_MAX - 1u)
    return SALTS_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u) return SALTS_EINVAL;
  snapshot_capacity = config->registration_capacity + 1u;
  if (config->registration_capacity > SIZE_MAX / sizeof(cmeta_readiness_poll_record) ||
      snapshot_capacity > SIZE_MAX / sizeof(struct pollfd) ||
      snapshot_capacity > SIZE_MAX / sizeof(cmeta_readiness_poll_snapshot))
    return SALTS_ERANGE;
  return SALTS_OK;
}

int cmeta_readiness_poll_init(cmeta_readiness_reactor *reactor,
                              const cmeta_readiness_config *config) {
  cmeta_readiness_poll_backend *backend;
  const size_t snapshot_capacity = config != NULL ? config->registration_capacity + 1u : 0u;
  int control_fds[2] = {-1, -1};
  int status = poll_config_validate(config);
  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL) return SALTS_EINVAL;
  if (status != SALTS_OK) return status;

  backend = (cmeta_readiness_poll_backend *)calloc(1u, sizeof(*backend));
  if (backend == NULL) return SALTS_ENOMEM;
  backend->control_read_fd = -1;
  backend->control_write_fd = -1;
  backend->capacity = config->registration_capacity;
  backend->event_batch_capacity = config->event_batch_capacity;
  atomic_init(&backend->stopping, 0);
  backend->records =
      (cmeta_readiness_poll_record *)calloc(backend->capacity, sizeof(*backend->records));
  backend->pollfds = (struct pollfd *)calloc(snapshot_capacity, sizeof(*backend->pollfds));
  backend->snapshots =
      (cmeta_readiness_poll_snapshot *)calloc(snapshot_capacity, sizeof(*backend->snapshots));
  cmeta_mutex_init(&backend->mutex);
  cmeta_cond_init(&backend->changed);
  if (backend->records == NULL || backend->pollfds == NULL || backend->snapshots == NULL ||
      backend->mutex == NULL || backend->changed == NULL) {
    poll_backend_destroy(backend);
    return SALTS_ENOMEM;
  }

  status = poll_control_pipe_create(control_fds);
  if (status != SALTS_OK) {
    poll_backend_destroy(backend);
    return status;
  }
  backend->control_read_fd = control_fds[0];
  backend->control_write_fd = control_fds[1];
  status = cmeta_readiness_reactor_init_backend(reactor, config, &poll_backend_ops, backend);
  if (status != SALTS_OK) {
    poll_backend_destroy(backend);
    return status;
  }
  backend->reactor = reactor;
  status = cmeta_thread_create(&backend->thread, poll_thread_entry, backend);
  if (status != SALTS_OK) {
    (void)cmeta_readiness_reactor_shutdown(reactor);
    (void)cmeta_readiness_reactor_destroy(reactor);
    return status;
  }
  backend->thread_started = 1;
  return SALTS_OK;
}
