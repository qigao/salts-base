#include "thread_backend.h"

/* No implicit OS atomics in the client; a single C ABI owns the transition.
 * A recursive callback on the same flag would deadlock and is prohibited.
 */
int platform_call_once(platform_once *flag,
                                       platform_once_fn callback) {
    uint32_t expected = 0, state = 0;
    int swapped = 0;
    if (!flag || !callback) return SALTS_EINVAL;
    if (atomic32_cas(&flag->state, &expected, 1u,
                           SALTS_MEMORY_SEQ_CST, SALTS_MEMORY_SEQ_CST,
                           &swapped) != SALTS_OK) return SALTS_EINVAL;
    if (swapped) {
        callback();
        return atomic32_store(&flag->state, 2u, SALTS_MEMORY_RELEASE);
    }
    for (;;) {
        if (atomic32_load(&flag->state, SALTS_MEMORY_ACQUIRE, &state) != SALTS_OK)
            return SALTS_EINVAL;
        if (state == 2u) return SALTS_OK;
        if (state != 1u) return SALTS_EINVAL;
        platform_thread_yield();
    }
}
