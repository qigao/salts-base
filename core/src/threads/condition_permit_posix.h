#ifndef SALTS_CONDITION_PERMIT_POSIX_H
#define SALTS_CONDITION_PERMIT_POSIX_H

#include "thread_backend.h"
#include <time.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t native_permit;
#else
typedef int native_permit;
#endif

struct wait_group;
struct permit_condition {
    native_permit guard;
    struct wait_group *open, *sealed;
};

#pragma GCC visibility push(hidden)
int permit_init(native_permit *permit, unsigned value);
void permit_destroy(native_permit *permit);
void permit_post(native_permit *permit, unsigned count);
int permit_wait(native_permit *permit, const struct timespec *due, clockid_t clock);
int permit_condition_wait(struct permit_condition *c, platform_mutex *m,
                          const struct timespec *due, clockid_t clock);
int permit_condition_notify(struct permit_condition *c, int all);
void permit_condition_destroy(struct permit_condition *c);
#pragma GCC visibility pop

#endif
