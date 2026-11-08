#include <platform_thread.h>

/* No implicit OS atomics in the client; a single C ABI owns the transition.
 * A recursive callback on the same flag would deadlock and is prohibited.
 */
int SALTS_CORE_PLATFORM_CALL salts_platform_call_once(salts_platform_once *flag,
                                       salts_platform_once_fn callback) {
    uint32_t expected = 0, state = 0;
    int swapped = 0;
    if (!flag || !callback) return SALTS_EINVAL;
    if (salts_atomic32_cas(&flag->state, &expected, 1u,
                           SALTS_MEMORY_SEQ_CST, SALTS_MEMORY_SEQ_CST,
                           &swapped) != SALTS_OK) return SALTS_EINVAL;
    if (swapped) {
        callback();
        return salts_atomic32_store(&flag->state, 2u, SALTS_MEMORY_RELEASE);
    }
    for (;;) {
        if (salts_atomic32_load(&flag->state, SALTS_MEMORY_ACQUIRE, &state) != SALTS_OK)
            return SALTS_EINVAL;
        if (state == 2u) return SALTS_OK;
        if (state != 1u) return SALTS_EINVAL;
        salts_platform_thread_yield();
    }
}
