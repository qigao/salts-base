#include "readiness_internal.h"

#include <error_codes.h>
#include <thread.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

enum { KQUEUE_CONTROL_IDENT = 1 };

_Static_assert(sizeof(uintptr_t) >= sizeof(uint64_t),
               "kqueue readiness tokens require 64-bit udata");

typedef struct cmeta_readiness_kqueue_record {
  int fd;
  uint64_t registration_token;
  uint64_t event_token;
  cmeta_readiness_events watched_events;
  int active;
  int armed;
} cmeta_readiness_kqueue_record;

typedef struct cmeta_readiness_kqueue_backend {
  cmeta_readiness_reactor *reactor;
  cmeta_readiness_kqueue_record *records;
  struct kevent *event_batch;
  size_t capacity;
  size_t event_batch_capacity;
  cmeta_mutex_t mutex;
  cmeta_cond_t changed;
  cmeta_thread_t thread;
  int kqueue_fd;
  int thread_started;
  int thread_exited;
  atomic_int stopping;
} cmeta_readiness_kqueue_backend;

static uint32_t kqueue_token_index(uint64_t token) { return (uint32_t)token; }

static cmeta_readiness_events kqueue_native_events(
    cmeta_readiness_events events) {
  cmeta_readiness_events native_events =
      events & (SALTS_READINESS_EVENT_READ | SALTS_READINESS_EVENT_WRITE);
  if (native_events == 0u &&
      (events & (SALTS_READINESS_EVENT_ERROR |
                 SALTS_READINESS_EVENT_HANGUP)) != 0u)
    native_events = SALTS_READINESS_EVENT_READ;
  return native_events;
}

static int kqueue_fd_valid(int fd) {
  int status;
  do {
    status = fcntl(fd, F_GETFD);
  } while (status < 0 && errno == EINTR);
  return status >= 0 ? SALTS_OK : -errno;
}

static int kqueue_change(cmeta_readiness_kqueue_backend *backend,
                         uintptr_t ident, int16_t filter, uint16_t flags,
                         uint32_t fflags, intptr_t data, void *user) {
  struct kevent change;
  int status;
  EV_SET(&change, ident, filter, flags, fflags, data, user);
  do {
    status = kevent(backend->kqueue_fd, &change, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  return status == 0 ? SALTS_OK : -errno;
}

static int kqueue_delete_filter(cmeta_readiness_kqueue_backend *backend,
                                int fd, int16_t filter) {
  int status = kqueue_change(backend, (uintptr_t)fd, filter, EV_DELETE,
                             0u, 0, NULL);
  return status == -ENOENT ? SALTS_OK : status;
}

static int kqueue_remove_filters(cmeta_readiness_kqueue_backend *backend,
                                 int fd,
                                 cmeta_readiness_events events) {
  const cmeta_readiness_events native_events = kqueue_native_events(events);
  int read_status = SALTS_OK;
  int write_status = SALTS_OK;
  if ((native_events & SALTS_READINESS_EVENT_READ) != 0)
    read_status = kqueue_delete_filter(backend, fd, EVFILT_READ);
  if ((native_events & SALTS_READINESS_EVENT_WRITE) != 0)
    write_status = kqueue_delete_filter(backend, fd, EVFILT_WRITE);
  return read_status != SALTS_OK ? read_status : write_status;
}

static int kqueue_register_resource(void *user, intptr_t native_resource,
                                    uint64_t token) {
  cmeta_readiness_kqueue_backend *backend =
      (cmeta_readiness_kqueue_backend *)user;
  uint32_t index = kqueue_token_index(token);
  int fd;
  int status;

  if (native_resource < 0 || native_resource > INT_MAX ||
      (uint32_t)(token >> 32) == 0u || (size_t)index >= backend->capacity)
    return SALTS_EINVAL;
  fd = (int)native_resource;
  status = kqueue_fd_valid(fd);
  if (status != SALTS_OK) return status;

  cmeta_mutex_lock(&backend->mutex);
  if (backend->records[index].active) {
    cmeta_mutex_unlock(&backend->mutex);
    return SALTS_EALREADY;
  }
  for (size_t i = 0u; i < backend->capacity; ++i) {
    if (backend->records[i].active && backend->records[i].fd == fd) {
      cmeta_mutex_unlock(&backend->mutex);
      return SALTS_EALREADY;
    }
  }
  backend->records[index].fd = fd;
  backend->records[index].registration_token = token;
  backend->records[index].active = 1;
  cmeta_mutex_unlock(&backend->mutex);
  return SALTS_OK;
}

static int kqueue_arm(void *user, uint64_t token, uint64_t arm_token,
                      cmeta_readiness_events events) {
  cmeta_readiness_kqueue_backend *backend =
      (cmeta_readiness_kqueue_backend *)user;
  uint32_t index = kqueue_token_index(token);
  int fd;
  struct kevent changes[2];
  int change_count = 0;
  int status;
  uint64_t previous_event_token;
  cmeta_readiness_events previous_watched_events;
  int previous_armed;
  const cmeta_readiness_events native_events = kqueue_native_events(events);

  cmeta_mutex_lock(&backend->mutex);
  if ((size_t)index >= backend->capacity ||
      !backend->records[index].active ||
      backend->records[index].registration_token != token) {
    cmeta_mutex_unlock(&backend->mutex);
    return SALTS_EINVAL;
  }
  fd = backend->records[index].fd;
  previous_event_token = backend->records[index].event_token;
  previous_watched_events = backend->records[index].watched_events;
  previous_armed = backend->records[index].armed;
  backend->records[index].event_token = arm_token;
  backend->records[index].watched_events = events;
  backend->records[index].armed = 1;
  cmeta_mutex_unlock(&backend->mutex);

  if ((native_events & SALTS_READINESS_EVENT_READ) != 0) {
    const int synthetic_read =
        (events & SALTS_READINESS_EVENT_READ) == 0u;
    EV_SET(&changes[change_count++], (uintptr_t)fd, EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_ONESHOT,
           synthetic_read ? NOTE_LOWAT : 0u,
           synthetic_read ? INTPTR_MAX : 0,
           (void *)(uintptr_t)arm_token);
  }
  if ((native_events & SALTS_READINESS_EVENT_WRITE) != 0) {
    EV_SET(&changes[change_count++], (uintptr_t)fd, EVFILT_WRITE,
           EV_ADD | EV_ENABLE | EV_ONESHOT, 0u, 0,
           (void *)(uintptr_t)arm_token);
  }
  do {
    status = kevent(backend->kqueue_fd, changes, change_count,
                    NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  status = status == 0 ? SALTS_OK : -errno;
  if (status != SALTS_OK) {
    (void)kqueue_remove_filters(backend, fd, events);
    cmeta_mutex_lock(&backend->mutex);
    if (backend->records[index].active &&
        backend->records[index].registration_token == token &&
        backend->records[index].event_token == arm_token) {
      backend->records[index].event_token = previous_event_token;
      backend->records[index].watched_events = previous_watched_events;
      backend->records[index].armed = previous_armed;
    }
    cmeta_mutex_unlock(&backend->mutex);
    return status;
  }
  return SALTS_OK;
}

static int kqueue_remove_watch(cmeta_readiness_kqueue_backend *backend,
                               uint64_t token, int close_record) {
  uint32_t index = kqueue_token_index(token);
  int fd;
  cmeta_readiness_events watched;
  int status;

  cmeta_mutex_lock(&backend->mutex);
  if ((size_t)index >= backend->capacity ||
      !backend->records[index].active ||
      backend->records[index].registration_token != token) {
    cmeta_mutex_unlock(&backend->mutex);
    return SALTS_EINVAL;
  }
  fd = backend->records[index].fd;
  watched = backend->records[index].watched_events;
  cmeta_mutex_unlock(&backend->mutex);

  status = kqueue_remove_filters(backend, fd, watched);
  if (status != SALTS_OK) return status;

  cmeta_mutex_lock(&backend->mutex);
  backend->records[index].watched_events = 0u;
  backend->records[index].event_token = 0u;
  backend->records[index].armed = 0;
  if (close_record) {
    backend->records[index].fd = -1;
    backend->records[index].registration_token = 0u;
    backend->records[index].active = 0;
  }
  cmeta_mutex_unlock(&backend->mutex);
  return SALTS_OK;
}

static int kqueue_unarm(void *user, uint64_t token) {
  return kqueue_remove_watch((cmeta_readiness_kqueue_backend *)user,
                             token, 0);
}

static int kqueue_close_registration(void *user, uint64_t token) {
  return kqueue_remove_watch((cmeta_readiness_kqueue_backend *)user,
                             token, 1);
}

static int kqueue_control_trigger(cmeta_readiness_kqueue_backend *backend) {
  return kqueue_change(backend, KQUEUE_CONTROL_IDENT, EVFILT_USER, 0u,
                       NOTE_TRIGGER, 0, NULL);
}

static cmeta_readiness_events kqueue_translate_events(
    const struct kevent *event) {
  cmeta_readiness_events events = 0u;
  if (event->filter == EVFILT_READ) events |= SALTS_READINESS_EVENT_READ;
  if (event->filter == EVFILT_WRITE) events |= SALTS_READINESS_EVENT_WRITE;
  if ((event->flags & EV_EOF) != 0) events |= SALTS_READINESS_EVENT_HANGUP;
  if ((event->flags & EV_EOF) != 0 && event->fflags != 0u)
    events |= SALTS_READINESS_EVENT_ERROR;
  if ((event->flags & EV_ERROR) != 0) events |= SALTS_READINESS_EVENT_ERROR;
  return events;
}

static void kqueue_thread_mark_exited(
    cmeta_readiness_kqueue_backend *backend) {
  cmeta_mutex_lock(&backend->mutex);
  backend->thread_exited = 1;
  cmeta_cond_broadcast(&backend->changed);
  cmeta_mutex_unlock(&backend->mutex);
}

static void kqueue_thread_entry(void *user) {
  cmeta_readiness_kqueue_backend *backend =
      (cmeta_readiness_kqueue_backend *)user;
  int terminal_status = SALTS_OK;
  for (;;) {
    int ready;
    int control_seen = 0;
    do {
      ready = kevent(backend->kqueue_fd, NULL, 0, backend->event_batch,
                     (int)backend->event_batch_capacity, NULL);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) {
      terminal_status = -errno;
      break;
    }

    for (int i = 0; i < ready; ++i) {
      const struct kevent *event = &backend->event_batch[i];
      uint64_t event_token;
      uint64_t registration_token = 0u;
      cmeta_readiness_events watched = 0u;
      uint32_t index;
      int event_status = SALTS_OK;
      cmeta_readiness_events delivered;

      if (event->filter == EVFILT_USER &&
          event->ident == KQUEUE_CONTROL_IDENT) {
        control_seen = 1;
        continue;
      }
      event_token = (uint64_t)(uintptr_t)event->udata;
      index = kqueue_token_index(event_token);
      cmeta_mutex_lock(&backend->mutex);
      if ((size_t)index < backend->capacity &&
          backend->records[index].active &&
          backend->records[index].armed &&
          backend->records[index].event_token == event_token) {
        registration_token = backend->records[index].registration_token;
        watched = backend->records[index].watched_events;
        backend->records[index].watched_events = 0u;
        backend->records[index].armed = 0;
      }
      cmeta_mutex_unlock(&backend->mutex);
      if (registration_token == 0u) continue;
      (void)kqueue_remove_filters(backend, (int)event->ident, watched);
      if ((event->flags & EV_ERROR) != 0 && event->data != 0)
        event_status = -(int)event->data;
      delivered = kqueue_translate_events(event);
      if ((watched & SALTS_READINESS_EVENT_READ) == 0u)
        delivered &= ~SALTS_READINESS_EVENT_READ;
      if ((watched & SALTS_READINESS_EVENT_WRITE) == 0u)
        delivered &= ~SALTS_READINESS_EVENT_WRITE;
      (void)cmeta_readiness_backend_dispatch_generation(
          backend->reactor, registration_token, event_token,
          delivered, event_status);
    }
    if (control_seen &&
        atomic_load_explicit(&backend->stopping, memory_order_acquire))
      break;
  }
  if (terminal_status != SALTS_OK)
    (void)cmeta_readiness_backend_fail(backend->reactor, terminal_status);
  kqueue_thread_mark_exited(backend);
}

static int kqueue_shutdown(void *user) {
  cmeta_readiness_kqueue_backend *backend =
      (cmeta_readiness_kqueue_backend *)user;
  int status;
  if (!backend->thread_started) return SALTS_OK;
  atomic_store_explicit(&backend->stopping, 1, memory_order_release);
  status = kqueue_control_trigger(backend);
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

static void kqueue_backend_destroy(void *user) {
  cmeta_readiness_kqueue_backend *backend =
      (cmeta_readiness_kqueue_backend *)user;
  if (backend == NULL) return;
  if (backend->kqueue_fd >= 0) (void)close(backend->kqueue_fd);
  cmeta_cond_destroy(&backend->changed);
  cmeta_mutex_destroy(&backend->mutex);
  free(backend->event_batch);
  free(backend->records);
  free(backend);
}

static const cmeta_readiness_backend_ops kqueue_backend_ops = {
    kqueue_register_resource, kqueue_arm, kqueue_unarm,
    kqueue_close_registration, kqueue_shutdown, kqueue_backend_destroy};

int cmeta_readiness_kqueue_init(cmeta_readiness_reactor *reactor,
                                const cmeta_readiness_config *config) {
  cmeta_readiness_kqueue_backend *backend;
  int status;
  if (config == NULL || config->registration_capacity == 0u ||
      config->event_batch_capacity == 0u)
    return SALTS_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u ||
      config->event_batch_capacity > (size_t)INT_MAX)
    return SALTS_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u)
    return SALTS_EINVAL;
  if (config->registration_capacity >
          SIZE_MAX / sizeof(cmeta_readiness_kqueue_record) ||
      config->event_batch_capacity > SIZE_MAX / sizeof(struct kevent))
    return SALTS_ERANGE;
  backend = (cmeta_readiness_kqueue_backend *)calloc(1u, sizeof(*backend));
  if (backend == NULL) return SALTS_ENOMEM;
  backend->kqueue_fd = -1;
  backend->capacity = config->registration_capacity;
  backend->event_batch_capacity = config->event_batch_capacity;
  atomic_init(&backend->stopping, 0);
  backend->records = (cmeta_readiness_kqueue_record *)calloc(
      backend->capacity, sizeof(*backend->records));
  backend->event_batch = (struct kevent *)calloc(
      backend->event_batch_capacity, sizeof(*backend->event_batch));
  cmeta_mutex_init(&backend->mutex);
  cmeta_cond_init(&backend->changed);
  if (backend->records == NULL || backend->event_batch == NULL ||
      backend->mutex == NULL || backend->changed == NULL) {
    kqueue_backend_destroy(backend);
    return SALTS_ENOMEM;
  }
  backend->kqueue_fd = kqueue();
  if (backend->kqueue_fd < 0) {
    status = -errno;
    kqueue_backend_destroy(backend);
    return status;
  }
  status = kqueue_change(backend, KQUEUE_CONTROL_IDENT, EVFILT_USER,
                         EV_ADD | EV_CLEAR, 0u, 0, NULL);
  if (status != SALTS_OK) {
    kqueue_backend_destroy(backend);
    return status;
  }
  status = cmeta_readiness_reactor_init_backend(
      reactor, config, &kqueue_backend_ops, backend);
  if (status != SALTS_OK) {
    kqueue_backend_destroy(backend);
    return status;
  }
  backend->reactor = reactor;
  status = cmeta_thread_create(&backend->thread, kqueue_thread_entry, backend);
  if (status != SALTS_OK) {
    (void)cmeta_readiness_reactor_shutdown(reactor);
    (void)cmeta_readiness_reactor_destroy(reactor);
    return status;
  }
  backend->thread_started = 1;
  return SALTS_OK;
}
