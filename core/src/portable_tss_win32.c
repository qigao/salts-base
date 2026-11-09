#include <platform_thread.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <stdlib.h>
#define TSS_ACTIVE_LIMIT 65535u
struct salts_platform_tss {
    DWORD index;
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
/* Must actually use WINAPI/stdcall; never cast a cdecl user callback to it. */
static VOID WINAPI tss_cleanup(PVOID opaque) {
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
    key->index = FlsAlloc(tss_cleanup);
    if (key->index == FLS_OUT_OF_INDEXES) { free(key); return SALTS_EBUSY; }
    *out = key;
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_set(salts_platform_tss *key, void *value) {
    struct tss_slot *slot;
    int rc;
    if (!key) return SALTS_EINVAL;
    slot = (struct tss_slot *)FlsGetValue(key->index);
    if (slot) {
        if (value) { slot->value = value; return SALTS_OK; }
        if (!FlsSetValue(key->index, NULL)) return SALTS_EBUSY;
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
    if (!FlsSetValue(key->index, slot)) {
        free(slot); release(key); return SALTS_EBUSY;
    }
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_get(salts_platform_tss *key, void **out) {
    struct tss_slot *slot;
    if (!key || !out) return SALTS_EINVAL;
    slot = (struct tss_slot *)FlsGetValue(key->index);
    *out = slot ? slot->value : NULL;
    return SALTS_OK;
}
int SALTS_CORE_PLATFORM_CALL salts_platform_tss_destroy(salts_platform_tss **owner) {
    salts_platform_tss *key;
    uint32_t active;
    if (!owner || !(key = *owner)) return SALTS_EINVAL;
    if (salts_atomic32_load(&key->active, SALTS_MEMORY_ACQUIRE, &active)) return SALTS_EINVAL;
    if (active != 0u) return SALTS_EBUSY;
    if (!FlsFree(key->index)) return SALTS_EBUSY;
    *owner = NULL;
    free(key);
    return SALTS_OK;
}
#endif
