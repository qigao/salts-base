#include "readiness_internal.h"

#include <error_codes.h>
#include <thread.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

typedef struct cmeta_readiness_epoll_record {
  int fd;
  uint64_t registration_token;
  uint64_t event_token;
  int active;
  int watched;
  int armed;
} cmeta_readiness_epoll_record;

typedef struct cmeta_readiness_epoll_backend {
  cmeta_readiness_reactor *reactor;
  cmeta_readiness_epoll_record *records;
  struct epoll_event *event_batch;
  size_t capacity;
  size_t event_batch_capacity;
  cmeta_mutex_t mutex;
  cmeta_cond_t changed;
  cmeta_thread_t thread;
  int epoll_fd;
  int control_fd;
  int thread_started;
  int thread_exited;
  atomic_int stopping;
} cmeta_readiness_epoll_backend;

static uint32_t epoll_token_index(uint64_t token) { return (uint32_t)token; }

static int epoll_ctl_retry(int epoll_fd, int operation, int fd, struct epoll_event *event) {
  int status;
  do {
    status = epoll_ctl(epoll_fd, operation, fd, event);
  } while (status != 0 && errno == EINTR);
  return status == 0 ? SALTS_OK : -errno;
}

static int epoll_fd_valid(int fd) {
  int status;
  do {
    status = fcntl(fd, F_GETFD);
  } while (status < 0 && errno == EINTR);
  return status >= 0 ? SALTS_OK : -errno;
}

static cmeta_readiness_events epoll_translate_events(uint32_t native_events) {
  cmeta_readiness_events events = 0;
  if ((native_events & (uint32_t)(EPOLLIN | EPOLLPRI)) != 0)
    events |= SALTS_READINESS_EVENT_READ;
  if ((native_events & (uint32_t)EPOLLOUT) != 0) events |= SALTS_READINESS_EVENT_WRITE;
  if ((native_events & (uint32_t)EPOLLERR) != 0) events |= SALTS_READINESS_EVENT_ERROR;
  if ((native_events & (uint32_t)EPOLLHUP) != 0) events |= SALTS_READINESS_EVENT_HANGUP;
#if defined(EPOLLRDHUP)
  if ((native_events & (uint32_t)EPOLLRDHUP) != 0) events |= SALTS_READINESS_EVENT_HANGUP;
#endif
  return events;
}

uint32_t cmeta_readiness_epoll_interest_events(cmeta_readiness_events events) {
  uint32_t native_events = (uint32_t)EPOLLONESHOT;
  if ((events & SALTS_READINESS_EVENT_READ) != 0) native_events |= (uint32_t)EPOLLIN;
  if ((events & SALTS_READINESS_EVENT_WRITE) != 0) native_events |= (uint32_t)EPOLLOUT;
#if defined(EPOLLRDHUP)
  if ((events & SALTS_READINESS_EVENT_HANGUP) != 0) native_events |= (uint32_t)EPOLLRDHUP;
#endif
  return native_events;
}

static int epoll_record_snapshot(cmeta_readiness_epoll_backend *backend, uint64_t token,
                                 int *fd, int *watched) {
  uint32_t index = epoll_token_index(token);
  int status = SALTS_OK;
  cmeta_mutex_lock(&backend->mutex);
  if ((size_t)index >= backend->capacity || !backend->records[index].active ||
      backend->records[index].registration_token != token) {
    status = SALTS_EINVAL;
  } else {
    *fd = backend->records[index].fd;
    *watched = backend->records[index].watched;
  }
  cmeta_mutex_unlock(&backend->mutex);
  return status;
}

static int epoll_control_write(cmeta_readiness_epoll_backend *backend) {
  uint64_t value = 1;
  ssize_t written;
  do {
    written = write(backend->control_fd, &value, sizeof(value));
  } while (written < 0 && errno == EINTR);
  if (written == (ssize_t)sizeof(value) || (written < 0 && errno == EAGAIN)) return SALTS_OK;
  return written < 0 ? -errno : SALTS_EIO;
}

static int epoll_register_resource(void *user, intptr_t native_resource, uint64_t token) {
  cmeta_readiness_epoll_backend *backend = (cmeta_readiness_epoll_backend *)user;
  uint32_t index = epoll_token_index(token);
  int fd;
  int status;

  if (native_resource < 0 || native_resource > INT_MAX || (uint32_t)(token >> 32) == 0u ||
      (size_t)index >= backend->capacity)
    return SALTS_EINVAL;
  fd = (int)native_resource;
  status = epoll_fd_valid(fd);
  if (status != SALTS_OK) return status;

  cmeta_mutex_lock(&backend->mutex);
  if (backend->records[index].active) {
    cmeta_mutex_unlock(&backend->mutex);
    return SALTS_EALREADY;
  }
  for (size_t i = 0; i < backend->capacity; ++i) {
    if (backend->records[i].active && backend->records[i].fd == fd) {
      cmeta_mutex_unlock(&backend->mutex);
      return SALTS_EALREADY;
    }
  }
  backend->records[index].fd = fd;
  backend->records[index].registration_token = token;
  backend->records[index].event_token = 0;
  backend->records[index].active = 1;
  backend->records[index].watched = 0;
  backend->records[index].armed = 0;
  cmeta_mutex_unlock(&backend->mutex);
  return SALTS_OK;
}

static int epoll_arm(void *user, uint64_t token, uint64_t arm_token,
                     cmeta_readiness_events events) {
  cmeta_readiness_epoll_backend *backend = (cmeta_readiness_epoll_backend *)user;
  uint32_t index = epoll_token_index(token);
  struct epoll_event event = {0};
  uint64_t previous_event_token;
  int fd;
  int previous_armed;
  int watched;
  int status = epoll_record_snapshot(backend, token, &fd, &watched);
  if (status != SALTS_OK) return status;

  cmeta_mutex_lock(&backend->mutex);
  previous_event_token = backend->records[index].event_token;
  previous_armed = backend->records[index].armed;
  backend->records[index].event_token = arm_token;
  backend->records[index].armed = 1;
  cmeta_mutex_unlock(&backend->mutex);

  event.events = cmeta_readiness_epoll_interest_events(events);
  event.data.u64 = arm_token;
  status = epoll_ctl_retry(backend->epoll_fd, watched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &event);
  if (status != SALTS_OK) {
    cmeta_mutex_lock(&backend->mutex);
    if (backend->records[index].active && backend->records[index].registration_token == token &&
        backend->records[index].event_token == arm_token) {
      backend->records[index].event_token = previous_event_token;
      backend->records[index].armed = previous_armed;
    }
    cmeta_mutex_unlock(&backend->mutex);
    return status;
  }

  cmeta_mutex_lock(&backend->mutex);
  backend->records[index].watched = 1;
  cmeta_mutex_unlock(&backend->mutex);
  return SALTS_OK;
}

static int epoll_remove_watch(cmeta_readiness_epoll_backend *backend, uint64_t token,
                              int close_record) {
  uint32_t index = epoll_token_index(token);
  int fd;
  int watched;
  int status = epoll_record_snapshot(backend, token, &fd, &watched);
  if (status != SALTS_OK) return status;

  if (watched) {
    status = epoll_ctl_retry(backend->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    if (status == -ENOENT) status = SALTS_OK;
    if (status != SALTS_OK) return status;
  }

  cmeta_mutex_lock(&backend->mutex);
  backend->records[index].watched = 0;
  backend->records[index].armed = 0;
  backend->records[index].event_token = 0;
  if (close_record) {
    backend->records[index].fd = -1;
    backend->records[index].registration_token = 0;
    backend->records[index].active = 0;
  }
  cmeta_mutex_unlock(&backend->mutex);
  return SALTS_OK;
}

static int epoll_unarm(void *user, uint64_t token) {
  return epoll_remove_watch((cmeta_readiness_epoll_backend *)user, token, 0);
}

static int epoll_close_registration(void *user, uint64_t token) {
  return epoll_remove_watch((cmeta_readiness_epoll_backend *)user, token, 1);
}

static void epoll_control_drain(cmeta_readiness_epoll_backend *backend) {
  uint64_t value;
  ssize_t count;
  do {
    count = read(backend->control_fd, &value, sizeof(value));
  } while (count < 0 && errno == EINTR);
}

static void epoll_thread_mark_exited(cmeta_readiness_epoll_backend *backend) {
  cmeta_mutex_lock(&backend->mutex);
  backend->thread_exited = 1;
  cmeta_cond_broadcast(&backend->changed);
  cmeta_mutex_unlock(&backend->mutex);
}

static void epoll_thread_entry(void *user) {
  cmeta_readiness_epoll_backend *backend = (cmeta_readiness_epoll_backend *)user;
  int terminal_status = SALTS_OK;
  for (;;) {
    int control_seen = 0;
    int ready;
    do {
      ready = epoll_wait(backend->epoll_fd, backend->event_batch,
                         (int)backend->event_batch_capacity, -1);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) {
      terminal_status = -errno;
      break;
    }

    for (int i = 0; i < ready; ++i) {
      uint64_t event_token = backend->event_batch[i].data.u64;
      uint64_t registration_token = 0;
      cmeta_readiness_events events;
      uint32_t index;

      if (event_token == 0) {
        epoll_control_drain(backend);
        control_seen = 1;
        continue;
      }

      index = epoll_token_index(event_token);
      cmeta_mutex_lock(&backend->mutex);
      if ((size_t)index < backend->capacity && backend->records[index].active) {
        registration_token = backend->records[index].registration_token;
        if (backend->records[index].armed &&
            backend->records[index].event_token == event_token)
          backend->records[index].armed = 0;
      }
      cmeta_mutex_unlock(&backend->mutex);
      if (registration_token == 0) continue;

      events = epoll_translate_events(backend->event_batch[i].events);
      if (events != 0)
        (void)cmeta_readiness_backend_dispatch_generation(
            backend->reactor, registration_token, event_token, events, SALTS_OK);
    }
    if (control_seen && atomic_load_explicit(&backend->stopping, memory_order_acquire)) break;
  }
  if (terminal_status != SALTS_OK)
    (void)cmeta_readiness_backend_fail(backend->reactor, terminal_status);
  epoll_thread_mark_exited(backend);
}

static int epoll_shutdown(void *user) {
  cmeta_readiness_epoll_backend *backend = (cmeta_readiness_epoll_backend *)user;
  int wake_status;
  int join_status;

  if (!backend->thread_started) return SALTS_OK;
  atomic_store_explicit(&backend->stopping, 1, memory_order_release);
  wake_status = epoll_control_write(backend);
  if (wake_status != SALTS_OK) {
    atomic_store_explicit(&backend->stopping, 0, memory_order_release);
    return wake_status;
  }

  cmeta_mutex_lock(&backend->mutex);
  while (!backend->thread_exited)
    cmeta_cond_wait(&backend->changed, &backend->mutex);
  cmeta_mutex_unlock(&backend->mutex);

  join_status = cmeta_thread_join(&backend->thread);
  if (join_status != SALTS_OK) return join_status;
  backend->thread_started = 0;
  return SALTS_OK;
}

static void epoll_backend_destroy(void *user) {
  cmeta_readiness_epoll_backend *backend = (cmeta_readiness_epoll_backend *)user;
  if (backend == NULL) return;
  if (backend->control_fd >= 0) (void)close(backend->control_fd);
  if (backend->epoll_fd >= 0) (void)close(backend->epoll_fd);
  cmeta_cond_destroy(&backend->changed);
  cmeta_mutex_destroy(&backend->mutex);
  free(backend->event_batch);
  free(backend->records);
  free(backend);
}

static const cmeta_readiness_backend_ops epoll_backend_ops = {
    epoll_register_resource, epoll_arm, epoll_unarm, epoll_close_registration, epoll_shutdown,
    epoll_backend_destroy};

static int epoll_config_validate(const cmeta_readiness_config *config) {
  if (config == NULL || config->registration_capacity == 0 || config->event_batch_capacity == 0)
    return SALTS_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u) return SALTS_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u) return SALTS_EINVAL;
  if (config->event_batch_capacity > (size_t)INT_MAX) return SALTS_ERANGE;
  if (config->registration_capacity > SIZE_MAX / sizeof(cmeta_readiness_epoll_record) ||
      config->event_batch_capacity > SIZE_MAX / sizeof(struct epoll_event))
    return SALTS_ERANGE;
  return SALTS_OK;
}

int cmeta_readiness_epoll_init(cmeta_readiness_reactor *reactor,
                               const cmeta_readiness_config *config) {
  cmeta_readiness_epoll_backend *backend;
  struct epoll_event control_event = {0};
  int status;

  status = epoll_config_validate(config);
  if (status != SALTS_OK) return status;
  backend = (cmeta_readiness_epoll_backend *)calloc(1, sizeof(*backend));
  if (backend == NULL) return SALTS_ENOMEM;
  backend->epoll_fd = -1;
  backend->control_fd = -1;
  backend->capacity = config->registration_capacity;
  backend->event_batch_capacity = config->event_batch_capacity;
  atomic_init(&backend->stopping, 0);
  backend->records = (cmeta_readiness_epoll_record *)calloc(backend->capacity,
                                                            sizeof(*backend->records));
  backend->event_batch = (struct epoll_event *)calloc(backend->event_batch_capacity,
                                                      sizeof(*backend->event_batch));
  cmeta_mutex_init(&backend->mutex);
  cmeta_cond_init(&backend->changed);
  if (backend->records == NULL || backend->event_batch == NULL || backend->mutex == NULL ||
      backend->changed == NULL) {
    epoll_backend_destroy(backend);
    return SALTS_ENOMEM;
  }

  backend->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (backend->epoll_fd < 0) {
    status = -errno;
    epoll_backend_destroy(backend);
    return status;
  }
  backend->control_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (backend->control_fd < 0) {
    status = -errno;
    epoll_backend_destroy(backend);
    return status;
  }
  control_event.events = (uint32_t)EPOLLIN;
  control_event.data.u64 = 0;
  status = epoll_ctl_retry(backend->epoll_fd, EPOLL_CTL_ADD, backend->control_fd, &control_event);
  if (status != SALTS_OK) {
    epoll_backend_destroy(backend);
    return status;
  }

  status = cmeta_readiness_reactor_init_backend(reactor, config, &epoll_backend_ops, backend);
  if (status != SALTS_OK) {
    epoll_backend_destroy(backend);
    return status;
  }
  backend->reactor = reactor;
  status = cmeta_thread_create(&backend->thread, epoll_thread_entry, backend);
  if (status != SALTS_OK) {
    (void)cmeta_readiness_reactor_shutdown(reactor);
    (void)cmeta_readiness_reactor_destroy(reactor);
    return status;
  }
  backend->thread_started = 1;
  return SALTS_OK;
}
