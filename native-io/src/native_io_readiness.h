#ifndef SALTS_NATIVE_IO_READINESS_H
#define SALTS_NATIVE_IO_READINESS_H

#include "native_io_internal.h"

enum {
  SALTS_IO_READY_READ = 1u,
  SALTS_IO_READY_WRITE = 2u,
  SALTS_IO_READY_ERROR = 4u,
  SALTS_IO_READY_WAKE = 8u
};

typedef struct cmeta_io_ready_event {
  uint64_t token;
  uint32_t interests;
  uint32_t native_status;
} cmeta_io_ready_event;

typedef struct cmeta_io_readiness_driver_ops {
  int (*init)(void *state, size_t batch_capacity);
  int (*update)(void *state, int fd, uint64_t token, uint32_t old_interests,
                uint32_t new_interests);
  int (*wait)(void *state, cmeta_io_ready_event *events, size_t event_capacity, uint32_t timeout_ms,
              size_t *out_count);
  int (*wake)(void *state);
  void (*destroy)(void *state);
  /* ET registrations may outlive the current request lane. New requests must
   * still try I/O before waiting: retained readiness is not a promise of data. */
  bool persistent_interests;
} cmeta_io_readiness_driver_ops;

int cmeta_io_readiness_backend_init(native_io_backend *backend,
                                    const native_io_backend_config *config,
                                    const cmeta_io_readiness_driver_ops *driver_ops,
                                    size_t driver_state_size);

#endif /* SALTS_NATIVE_IO_READINESS_H */
