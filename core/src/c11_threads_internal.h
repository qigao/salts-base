#ifndef SALTS_C11_THREADS_INTERNAL_H
#define SALTS_C11_THREADS_INTERNAL_H

#include <stdint.h>

/* Native sleep preserves interruption instead of silently restarting it.
 * Fixed-width transport keeps the consumer's CRT timespec inside its DLL.
 */
int thrd_sleep_native(uint64_t seconds, uint32_t nanos,
                           uint64_t *remaining_seconds, uint32_t *remaining_nanos);
void tss_cleanup_current(void);

#endif
