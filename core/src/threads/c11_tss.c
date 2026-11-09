#include <threads/threads.h>
#include "c11_threads_internal.h"

#include <stdlib.h>

/* One native exit hook per thread, shared by all C11 keys. Each slot retains
 * key metadata, not its borrowed value. Delete retires the key immediately;
 * outstanding thread slots keep metadata alive and suppress its destructor.
 * At most 1024 live/retired keys (and therefore slots per thread) may exist.
 */
enum { C11_TSS_KEY_LIMIT = 1024 };
struct tss_key {
    atomic32 refs;
    atomic32 deleted;
    tss_dtor_t destructor;
};
struct c11_tss_slot {
    struct c11_tss_slot *next;
    tss_t key;
    void *value;
};
struct c11_tss_state {
    struct c11_tss_slot *slots;
    int cleaning;
};
static atomic32 key_count = {0};
static platform_once exit_key_once = SALTS_PLATFORM_ONCE_INIT;
static platform_tss *exit_key;
static int exit_key_status;
static SALTS_THREAD_LOCAL struct c11_tss_state *current_state;

static int key_deleted(tss_t key) {
    uint32_t deleted;
    if (atomic32_load(&key->deleted, SALTS_MEMORY_ACQUIRE, &deleted) != SALTS_OK)
        abort();
    return deleted != 0;
}
static int retain_key(tss_t key) {
    uint32_t refs, expected;
    int swapped;
    for (;;) {
        if (atomic32_load(&key->refs, SALTS_MEMORY_ACQUIRE, &refs) != SALTS_OK)
            abort();
        if (refs == UINT32_MAX) return thrd_error;
        expected = refs;
        if (atomic32_cas(&key->refs, &expected, refs + 1u,
                SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &swapped) != SALTS_OK)
            abort();
        if (swapped) return thrd_success;
    }
}
static void release_key(tss_t key) {
    uint32_t before;
    if (atomic32_fetch_add(&key->refs, UINT32_MAX,
            SALTS_MEMORY_ACQ_REL, &before) != SALTS_OK) abort();
    if (before == 1u) {
        free(key);
        if (atomic32_fetch_add(&key_count, UINT32_MAX,
                SALTS_MEMORY_ACQ_REL, &before) != SALTS_OK) abort();
    }
}
static void prune_deleted(struct c11_tss_state *state) {
    struct c11_tss_slot **link = &state->slots;
    if (state->cleaning) return;
    while (*link) {
        struct c11_tss_slot *slot = *link;
        if (!key_deleted(slot->key)) { link = &slot->next; continue; }
        *link = slot->next;
        release_key(slot->key);
        free(slot);
    }
}
static void cleanup_state(void *opaque) {
    struct c11_tss_state *state = (struct c11_tss_state *)opaque;
    struct c11_tss_slot *slot;
    current_state = state;
    state->cleaning = 1;
    for (unsigned pass = 0; pass < TSS_DTOR_ITERATIONS; ++pass) {
        int called = 0;
        /* Callbacks may prepend slots, delete keys, or reset values. Nodes
         * stay retained throughout cleanup; newly prepended slots are seen
         * on the next pass, with the same bounded iteration count.
         */
        for (slot = state->slots; slot; slot = slot->next) {
            void *value = slot->value;
            slot->value = NULL;
            if (value && !key_deleted(slot->key) && slot->key->destructor) {
                called = 1;
                slot->key->destructor(value);
            }
        }
        if (!called) break;
    }
    while ((slot = state->slots) != NULL) {
        state->slots = slot->next;
        release_key(slot->key);
        free(slot);
    }
    current_state = NULL;
    free(state);
}
static void create_exit_key(void) {
    exit_key_status = platform_tss_create(&exit_key, cleanup_state);
}
static struct c11_tss_state *get_state(void) {
    struct c11_tss_state *state;
    if (current_state) return current_state;
    if (platform_call_once(&exit_key_once, create_exit_key) != SALTS_OK ||
        exit_key_status != SALTS_OK) return NULL;
    state = (struct c11_tss_state *)calloc(1u, sizeof(*state));
    if (!state) return NULL;
    if (platform_tss_set(exit_key, state) != SALTS_OK) {
        free(state);
        return NULL;
    }
    current_state = state;
    return state;
}
void tss_cleanup_current(void) {
    struct c11_tss_state *state = current_state;
    if (!state || state->cleaning) return;
    /* Remove the native exit registration before running callbacks, avoiding
     * a second cleanup after C11 thrd_exit or normal callback return.
     */
    if (platform_tss_set(exit_key, NULL) != SALTS_OK) abort();
    cleanup_state(state);
}
int tss_create(tss_t *out, tss_dtor_t destructor) {
    uint32_t count, expected;
    int swapped;
    tss_t key;
    if (!out) return thrd_error;
    *out = NULL;
    for (;;) {
        if (atomic32_load(&key_count, SALTS_MEMORY_ACQUIRE, &count) != SALTS_OK)
            return thrd_error;
        if (count >= C11_TSS_KEY_LIMIT) return thrd_error;
        expected = count;
        if (atomic32_cas(&key_count, &expected, count + 1u,
                SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &swapped) != SALTS_OK)
            return thrd_error;
        if (swapped) break;
    }
    key = (tss_t)calloc(1u, sizeof(*key));
    if (!key) {
        (void)atomic32_fetch_add(&key_count, UINT32_MAX,
                                      SALTS_MEMORY_ACQ_REL, &count);
        return thrd_error;
    }
    if (atomic32_init(&key->refs, 1u) != SALTS_OK ||
        atomic32_init(&key->deleted, 0u) != SALTS_OK) abort();
    key->destructor = destructor;
    *out = key;
    return thrd_success;
}
void tss_delete(tss_t key) {
    if (!key) return;
    if (atomic32_store(&key->deleted, 1u, SALTS_MEMORY_RELEASE) != SALTS_OK)
        abort();
    if (current_state) prune_deleted(current_state);
    release_key(key);
}
void *tss_get(tss_t key) {
    struct c11_tss_slot *slot;
    if (!key || !current_state) return NULL;
    for (slot = current_state->slots; slot; slot = slot->next)
        if (slot->key == key) return slot->value;
    return NULL;
}
int tss_set(tss_t key, void *value) {
    struct c11_tss_state *state;
    struct c11_tss_slot **link, *slot;
    if (!key || key_deleted(key)) return thrd_error;
    if (!current_state && !value) return thrd_success;
    state = get_state();
    if (!state) return thrd_error;
    prune_deleted(state);
    for (link = &state->slots; (slot = *link) != NULL; link = &slot->next) {
        if (slot->key != key) continue;
        slot->value = value;
        if (!value && !state->cleaning) {
            *link = slot->next;
            release_key(slot->key);
            free(slot);
        }
        return thrd_success;
    }
    if (!value) return thrd_success;
    slot = (struct c11_tss_slot *)malloc(sizeof(*slot));
    if (!slot) return thrd_error;
    if (retain_key(key) != thrd_success) { free(slot); return thrd_error; }
    slot->key = key;
    slot->value = value;
    slot->next = state->slots;
    state->slots = slot;
    return thrd_success;
}
