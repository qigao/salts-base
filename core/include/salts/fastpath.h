#ifndef SALTS_FASTPATH_H
#define SALTS_FASTPATH_H

#include <salts/api.h>

#include <salts/error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct fast_key_state fast_key_state;

#ifdef __cplusplus
extern "C" {
#endif

SALTS_API bool fast_key_read(const fast_key_state *key);
SALTS_API int fast_key_set(fast_key_state *key, bool enabled);
SALTS_API bool fast_key_consume(fast_key_state *key);
SALTS_API int fast_enable(fast_key_state *key);
SALTS_API int fast_disable(fast_key_state *key);

#ifdef __cplusplus
}
#else
#include <stdatomic.h>

struct fast_key_state {
    atomic_bool enabled;
};

#define SALTS_FAST_KEY(name_, initial_) \
    fast_key_state name_ = {(initial_)}

static inline bool fast_branch(const fast_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}

#endif

#ifdef __cplusplus
#define fast_branch(key_) fast_key_read(key_)
#endif

#endif
