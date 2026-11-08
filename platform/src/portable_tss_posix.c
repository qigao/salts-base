#include <platform_thread.h>
#ifndef _WIN32
#include <pthread.h>
#include <stdlib.h>
#define TSS_ACTIVE_LIMIT 65535u
struct salts_platform_tss {
    pthread_key_t native;
    salts_platform_tss_destructor destructor;
    salts_atomic32 active;
};
struct tss_slot { salts_platform_tss *key; void *value; };
static int reserve(salts_platform_tss *key) {
    uint32_t count, expected;
    int swapped;
    for (;;) {
        if (salts_atomic32_load(&key->active, SALTS_MEMORY_ACQUIRE, &count)) return SALTS_EINVAL;
        if (count >= TSS_ACTIVE_LIMIT) return SALTS_EBUSY;
        expected = count;
        if (salts_atomic32_cas(&key->active, &expected, count + 1u,
                              SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &swapped)) return SALTS_EINVAL;
        if (swapped) return SALTS_OK;
    }
}
static void release(salts_platform_tss *key) {
    uint32_t previous = 0;
    (void)salts_atomic32_fetch_add(&key->active, UINT32_MAX, SALTS_MEMORY_ACQ_REL, &previous);
}
static void cleanup(void *opaque) {
    struct tss_slot *slot = (struct tss_slot *)opaque;
    salts_platform_tss *key;
    void *value;
    if (!slot) return;
    key = slot->key;
    value = slot->value;
    free(slot);
    if (key->destructor && value) key->destructor(value);
    release(key);
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_create(salts_platform_tss **out,
                                                        salts_platform_tss_destructor destructor) {
    salts_platform_tss *key;
    if (!out || *out) return SALTS_EINVAL;
    key = (salts_platform_tss *)calloc(1u, sizeof(*key));
    if (!key) return SALTS_ENOMEM;
    key->destructor = destructor;
    if (salts_atomic32_init(&key->active, 0u)) { free(key); return SALTS_EINVAL; }
    if (pthread_key_create(&key->native, cleanup) != 0) { free(key); return SALTS_EBUSY; }
    *out = key;
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_set(salts_platform_tss *key, void *value) {
    struct tss_slot *slot;
    int rc;
    if (!key) return SALTS_EINVAL;
    slot = (struct tss_slot *)pthread_getspecific(key->native);
    if (slot) {
        if (value) { slot->value = value; return SALTS_OK; }
        if (pthread_setspecific(key->native, NULL) != 0) return SALTS_EBUSY;
        free(slot);
        release(key);
        return SALTS_OK;
    }
    if (!value) return SALTS_OK;
    slot = (struct tss_slot *)calloc(1u, sizeof(*slot));
    if (!slot) return SALTS_ENOMEM;
    rc = reserve(key);
    if (rc != SALTS_OK) { free(slot); return rc; }
    slot->key = key;
    slot->value = value;
    if (pthread_setspecific(key->native, slot) != 0) {
        free(slot); release(key); return SALTS_EBUSY;
    }
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_get(salts_platform_tss *key, void **out) {
    struct tss_slot *slot;
    if (!key || !out) return SALTS_EINVAL;
    slot = (struct tss_slot *)pthread_getspecific(key->native);
    *out = slot ? slot->value : NULL;
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_destroy(salts_platform_tss **owner) {
    salts_platform_tss *key;
    uint32_t active;
    if (!owner || !(key = *owner)) return SALTS_EINVAL;
    if (salts_atomic32_load(&key->active, SALTS_MEMORY_ACQUIRE, &active)) return SALTS_EINVAL;
    if (active != 0u) return SALTS_EBUSY;
    if (pthread_key_delete(key->native) != 0) return SALTS_EBUSY;
    *owner = NULL;
    free(key);
    return SALTS_OK;
}
#endif
