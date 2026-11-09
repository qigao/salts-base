#include "c11_threads_internal.h"

#include <errno.h>
#include <time.h>

int thrd_sleep_native(uint64_t seconds, uint32_t nanos,
                           uint64_t *remaining_seconds, uint32_t *remaining_nanos) {
    struct timespec requested, remaining;
    requested.tv_sec = (time_t)seconds;
    requested.tv_nsec = (long)nanos;
    if (requested.tv_sec < 0 || (uint64_t)requested.tv_sec != seconds)
        return -2;
    if (nanosleep(&requested, &remaining) == 0) return 0;
    if (errno != EINTR) return -2;
    *remaining_seconds = (uint64_t)remaining.tv_sec;
    *remaining_nanos = (uint32_t)remaining.tv_nsec;
    return -1;
}
