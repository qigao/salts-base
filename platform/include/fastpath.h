#ifndef SALTS_FASTPATH_H
#define SALTS_FASTPATH_H

#include <platform_api.h>
#include <error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct salts_fast_key_state salts_fast_key_state;

#ifdef __cplusplus
extern "C" {
#endif

SALTS_CORE_PLATFORM_API bool SALTS_CORE_PLATFORM_CALL salts_fast_key_read(const salts_fast_key_state *key);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_fast_key_set(salts_fast_key_state *key, bool enabled);
SALTS_CORE_PLATFORM_API bool SALTS_CORE_PLATFORM_CALL salts_fast_key_consume(salts_fast_key_state *key);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_fast_enable(salts_fast_key_state *key);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_fast_disable(salts_fast_key_state *key);

#ifdef __cplusplus
}
#else
#include <stdatomic.h>

struct salts_fast_key_state {
    atomic_bool enabled;
};

#define SALTS_FAST_KEY(name_, initial_) \
    salts_fast_key_state name_ = {(initial_)}

static inline bool salts_fast_branch(const salts_fast_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}

#endif

#ifdef __cplusplus
#define salts_fast_branch(key_) salts_fast_key_read(key_)
#endif

#endif
