#include "c11_threads_internal.h"

#include <windows.h>

int thrd_sleep_native(uint64_t seconds, uint32_t nanos,
                           uint64_t *remaining_seconds, uint32_t *remaining_nanos) {
    /* Sleep's DWORD milliseconds reserve INFINITE. Split long C11 durations
     * without imposing the platform relative-wait API's 49-day maximum.
     */
    const uint64_t chunk_seconds = ((uint64_t)INFINITE - 1u) / 1000u - 1u;
    (void)remaining_seconds;
    (void)remaining_nanos;
    while (seconds > chunk_seconds) {
        Sleep((DWORD)(chunk_seconds * 1000u));
        seconds -= chunk_seconds;
    }
    Sleep((DWORD)(seconds * 1000u + ((uint64_t)nanos + 999999u) / 1000000u));
    return 0;
}
