#include <fastpath.h>

bool salts_fast_key_read(const salts_fast_key_state *key) {
    return salts_fast_branch(key);
}

int salts_fast_key_set(salts_fast_key_state *key, bool enabled) {
    if (key == NULL)
        return SALTS_EINVAL;
    atomic_store_explicit(&key->enabled, enabled, memory_order_release);
    return SALTS_OK;
}

bool salts_fast_key_consume(salts_fast_key_state *key) {
    if (key == NULL)
        return false;
    return atomic_exchange_explicit(
        &key->enabled, false, memory_order_acq_rel);
}

int salts_fast_enable(salts_fast_key_state *key) {
    return salts_fast_key_set(key, true);
}

int salts_fast_disable(salts_fast_key_state *key) {
    return salts_fast_key_set(key, false);
}
