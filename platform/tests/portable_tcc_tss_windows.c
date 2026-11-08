#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <platform_thread.h>
#include <stdint.h>
#include <stdio.h>

/* The native provider is LLVM/Clang. Every callback and API invocation here
 * is compiled by real TinyCC and resolved over the explicit C DLL ABI. */
typedef int (SALTS_CORE_PLATFORM_CALL *key_create_fn)(salts_platform_tss **, salts_platform_tss_destructor);
typedef int (SALTS_CORE_PLATFORM_CALL *key_set_fn)(salts_platform_tss *, void *);
typedef int (SALTS_CORE_PLATFORM_CALL *key_get_fn)(salts_platform_tss *, void **);
typedef int (SALTS_CORE_PLATFORM_CALL *key_destroy_fn)(salts_platform_tss **);
typedef int (SALTS_CORE_PLATFORM_CALL *thread_create_fn)(salts_platform_thread **, salts_platform_thread_fn, void *);
typedef int (SALTS_CORE_PLATFORM_CALL *thread_join_fn)(salts_platform_thread **, int *);
typedef int (SALTS_CORE_PLATFORM_CALL *atom_init_fn)(salts_atomic32 *, uint32_t);
typedef int (SALTS_CORE_PLATFORM_CALL *atom_load_fn)(const salts_atomic32 *, salts_memory_order, uint32_t *);
typedef int (SALTS_CORE_PLATFORM_CALL *atom_store_fn)(salts_atomic32 *, uint32_t, salts_memory_order);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_create_fn)(salts_platform_mutex **);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_fn)(salts_platform_mutex *);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_timed_fn)(salts_platform_mutex *, uint64_t);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_destroy_fn)(salts_platform_mutex **);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_create_fn)(salts_platform_condition **);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_timed_fn)(salts_platform_condition *, salts_platform_mutex *, uint64_t);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_destroy_fn)(salts_platform_condition **);
typedef int (SALTS_CORE_PLATFORM_CALL *sleep_fn)(uint64_t);

static salts_platform_tss *key;
static key_set_fn key_set;
static key_get_fn key_get;
static atom_load_fn atom_load;
static atom_store_fn atom_store;
static salts_atomic32 ready = {0}, release_gate = {0};
static int destructor_count;
static int marker = 2026;
static void SALTS_CORE_PLATFORM_CALL on_thread_exit(void *value) {
    if (value == &marker) ++destructor_count; /* read only after join */
}
static int SALTS_CORE_PLATFORM_CALL worker(void *value) {
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
    thread_create_fn thread_create; thread_join_fn thread_join;
    atom_init_fn atom_init;
    mutex_create_fn mutex_create; mutex_fn mutex_lock, mutex_unlock;
    mutex_timed_fn mutex_timed;
    mutex_destroy_fn mutex_destroy; cond_create_fn cond_create;
    cond_timed_fn cond_timed; cond_destroy_fn cond_destroy;
    sleep_fn thread_sleep;
    salts_platform_thread *thread = NULL;
    salts_platform_mutex *mutex = NULL;
    salts_platform_condition *cond = NULL;
    uint32_t started = 0;
    int result, i;
    if (argc != 2 || !argv[1]) return 1;
    lib = LoadLibraryA(argv[1]);
    if (!lib) return 2;
#define LOAD(dest, typ, name) do { dest = (typ)GetProcAddress(lib, name); if (!(dest)) { \
    printf("missing portable symbol: %s\n", name); FreeLibrary(lib); return 3; } } while (0)
    LOAD(key_create, key_create_fn, "salts_platform_tss_create");
    LOAD(key_set, key_set_fn, "salts_platform_tss_set");
    LOAD(key_get, key_get_fn, "salts_platform_tss_get");
    LOAD(key_destroy, key_destroy_fn, "salts_platform_tss_destroy");
    LOAD(thread_create, thread_create_fn, "salts_platform_thread_create");
    LOAD(thread_join, thread_join_fn, "salts_platform_thread_join");
    LOAD(atom_init, atom_init_fn, "salts_atomic32_init");
    LOAD(atom_load, atom_load_fn, "salts_atomic32_load");
    LOAD(atom_store, atom_store_fn, "salts_atomic32_store");
    LOAD(mutex_create, mutex_create_fn, "salts_platform_mutex_create");
    LOAD(mutex_lock, mutex_fn, "salts_platform_mutex_lock");
    LOAD(mutex_unlock, mutex_fn, "salts_platform_mutex_unlock");
    LOAD(mutex_timed, mutex_timed_fn, "salts_platform_mutex_timedlock_ns");
    LOAD(mutex_destroy, mutex_destroy_fn, "salts_platform_mutex_destroy");
    LOAD(cond_create, cond_create_fn, "salts_platform_condition_create");
    LOAD(cond_timed, cond_timed_fn, "salts_platform_condition_timedwait_ns");
    LOAD(cond_destroy, cond_destroy_fn, "salts_platform_condition_destroy");
    LOAD(thread_sleep, sleep_fn, "salts_platform_thread_sleep_ns");
#undef LOAD
    if (atom_init(&ready, 0u) || atom_init(&release_gate, 0u)) return 4;
    if (key_create(&key, on_thread_exit) || !key) return 5;
    if (thread_create(&thread, worker, &marker)) return 6;
    for (i = 0; i < 200000; ++i) {
        if (atom_load(&ready, SALTS_MEMORY_ACQUIRE, &started)) return 7;
        if (started) break;
        Sleep(0);
    }
    if (!started) return 8;
    if (key_destroy(&key) != SALTS_EBUSY || !key) return 9;
    if (atom_store(&release_gate, 1u, SALTS_MEMORY_RELEASE)) return 10;
    if (thread_join(&thread, &result) || result != 43 || thread) return 11;
    if (destructor_count != 1 || key_destroy(&key) || key) return 12;
    if (mutex_create(&mutex) || cond_create(&cond) || mutex_lock(mutex)) return 13;
    if (mutex_timed(mutex, 0) != SALTS_ETIMEDOUT ||
        mutex_timed(mutex, UINT64_MAX) != SALTS_EINVAL) return 19;
    if (cond_timed(cond, mutex, 0u) != SALTS_ETIMEDOUT) return 14;
    if (cond_timed(cond, mutex, UINT64_MAX) != SALTS_EINVAL) return 15;
    if (mutex_unlock(mutex) || cond_destroy(&cond) || mutex_destroy(&mutex)) return 16;
    if (thread_sleep(1000000ull) || thread_sleep(UINT64_MAX) != SALTS_EINVAL) return 17;
    if (mutex_create(&mutex) || mutex_timed(mutex, 0) ||
        mutex_unlock(mutex) || mutex_destroy(&mutex)) return 20;
    if (!FreeLibrary(lib)) return 18;
    puts("TinyCC -> llvm-mingw timed wait/FLS destructor/cdecl ABI PASS");
    return 0;
}
