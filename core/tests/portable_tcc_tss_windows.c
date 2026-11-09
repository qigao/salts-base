#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <threads/threads.h>
#include "thread_backend.h"
#include <stdint.h>
#include <stdio.h>

/* The native provider is LLVM/Clang. Every callback and API invocation here
 * is compiled by real TinyCC and resolved over the explicit C DLL ABI. */
typedef int (*key_create_fn)(platform_tss **, platform_tss_destructor);
typedef int (*key_set_fn)(platform_tss *, void *);
typedef int (*key_get_fn)(platform_tss *, void **);
typedef int (*key_destroy_fn)(platform_tss **);
typedef int (*thread_create_fn)(platform_thread **, platform_thread_fn, void *);
typedef int (*thread_join_fn)(platform_thread **, int *);
typedef int (*atom_init_fn)(atomic32 *, uint32_t);
typedef int (*atom_load_fn)(const atomic32 *, atomic_order, uint32_t *);
typedef int (*atom_store_fn)(atomic32 *, uint32_t, atomic_order);
typedef int (*mutex_create_fn)(platform_mutex **);
typedef int (*mutex_fn)(platform_mutex *);
typedef int (*mutex_timed_fn)(platform_mutex *, uint64_t);
typedef int (*mutex_destroy_fn)(platform_mutex **);
typedef int (*cond_create_fn)(platform_condition **);
typedef int (*cond_timed_fn)(platform_condition *, platform_mutex *, uint64_t);
typedef int (*cond_destroy_fn)(platform_condition **);
typedef int (*sleep_fn)(uint64_t);

static platform_tss *key;
static key_set_fn key_set;
static key_get_fn key_get;
static atom_load_fn atom_load;
static atom_store_fn atom_store;
static atomic32 ready = {0}, release_gate = {0};
static int destructor_count;
static int marker = 2026;
static void on_thread_exit(void *value) {
    if (value == &marker) ++destructor_count; /* read only after join */
}
static int worker(void *value) {
    void *out = NULL;
    uint32_t allowed = 0;
    if (key_set(key, value) || key_get(key, &out) || out != value) return -1;
    if (atom_store(&ready, 1u, SALTS_MEMORY_RELEASE)) return -2;
    for (;;) {
        if (atom_load(&release_gate, SALTS_MEMORY_ACQUIRE, &allowed)) return -3;
        if (allowed) return 43;
        Sleep(0);
    }
}

int main(int argc, char **argv) {
    HMODULE lib;
    key_create_fn key_create; key_destroy_fn key_destroy;
    thread_create_fn sync_thread_create; thread_join_fn sync_thread_join;
    atom_init_fn atom_init;
    mutex_create_fn mutex_create; mutex_fn sync_mutex_lock, sync_mutex_unlock;
    mutex_timed_fn mutex_timed;
    mutex_destroy_fn sync_mutex_destroy; cond_create_fn cond_create;
    cond_timed_fn cond_timed; cond_destroy_fn sync_cond_destroy;
    sleep_fn thread_sleep;
    platform_thread *thread = NULL;
    platform_mutex *mutex = NULL;
    platform_condition *cond = NULL;
    uint32_t started = 0;
    int result, i;
    if (argc != 2 || !argv[1]) return 1;
    lib = LoadLibraryA(argv[1]);
    if (!lib) return 2;
#define LOAD(dest, typ, name) do { dest = (typ)GetProcAddress(lib, name); if (!(dest)) { \
    printf("missing portable symbol: %s\n", name); FreeLibrary(lib); return 3; } } while (0)
    LOAD(key_create, key_create_fn, "platform_tss_create");
    LOAD(key_set, key_set_fn, "platform_tss_set");
    LOAD(key_get, key_get_fn, "platform_tss_get");
    LOAD(key_destroy, key_destroy_fn, "platform_tss_destroy");
    LOAD(sync_thread_create, thread_create_fn, "platform_thread_create");
    LOAD(sync_thread_join, thread_join_fn, "platform_thread_join");
    LOAD(atom_init, atom_init_fn, "atomic32_init");
    LOAD(atom_load, atom_load_fn, "atomic32_load");
    LOAD(atom_store, atom_store_fn, "atomic32_store");
    LOAD(mutex_create, mutex_create_fn, "platform_mutex_create");
    LOAD(sync_mutex_lock, mutex_fn, "platform_mutex_lock");
    LOAD(sync_mutex_unlock, mutex_fn, "platform_mutex_unlock");
    LOAD(mutex_timed, mutex_timed_fn, "platform_mutex_timedlock_ns");
    LOAD(sync_mutex_destroy, mutex_destroy_fn, "platform_mutex_destroy");
    LOAD(cond_create, cond_create_fn, "platform_condition_create");
    LOAD(cond_timed, cond_timed_fn, "platform_condition_timedwait_ns");
    LOAD(sync_cond_destroy, cond_destroy_fn, "platform_condition_destroy");
    LOAD(thread_sleep, sleep_fn, "platform_thread_sleep_ns");
#undef LOAD
    if (atom_init(&ready, 0u) || atom_init(&release_gate, 0u)) return 4;
    if (key_create(&key, on_thread_exit) || !key) return 5;
    if (sync_thread_create(&thread, worker, &marker)) return 6;
    for (i = 0; i < 200000; ++i) {
        if (atom_load(&ready, SALTS_MEMORY_ACQUIRE, &started)) return 7;
        if (started) break;
        Sleep(0);
    }
    if (!started) return 8;
    if (key_destroy(&key) != SALTS_EBUSY || !key) return 9;
    if (atom_store(&release_gate, 1u, SALTS_MEMORY_RELEASE)) return 10;
    if (sync_thread_join(&thread, &result) || result != 43 || thread) return 11;
    if (destructor_count != 1 || key_destroy(&key) || key) return 12;
    if (mutex_create(&mutex) || cond_create(&cond) || sync_mutex_lock(mutex)) return 13;
    if (mutex_timed(mutex, 0) != SALTS_ETIMEDOUT ||
        mutex_timed(mutex, UINT64_MAX) != SALTS_EINVAL) return 19;
    if (cond_timed(cond, mutex, 0u) != SALTS_ETIMEDOUT) return 14;
    if (cond_timed(cond, mutex, UINT64_MAX) != SALTS_EINVAL) return 15;
    if (sync_mutex_unlock(mutex) || sync_cond_destroy(&cond) || sync_mutex_destroy(&mutex)) return 16;
    if (thread_sleep(1000000ull) || thread_sleep(UINT64_MAX) != SALTS_EINVAL) return 17;
    if (mutex_create(&mutex) || mutex_timed(mutex, 0) ||
        sync_mutex_unlock(mutex) || sync_mutex_destroy(&mutex)) return 20;
    if (!FreeLibrary(lib)) return 18;
    puts("TinyCC -> llvm-mingw timed wait/FLS destructor/cdecl ABI PASS");
    return 0;
}
