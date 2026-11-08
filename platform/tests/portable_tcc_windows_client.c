/* True TinyCC / llvm-mingw cross-compiler C ABI consumer.
 * No compiler-native _Atomic storage or TCC object linked into provider.
 * Function lookup is strict: one ABI, one spelling, no fallback.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <platform_thread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef int (SALTS_CORE_PLATFORM_CALL *check_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef int (SALTS_CORE_PLATFORM_CALL *init32_fn)(salts_atomic32 *, uint32_t);
typedef int (SALTS_CORE_PLATFORM_CALL *fetch32_fn)(salts_atomic32 *, uint32_t, salts_memory_order, uint32_t *);
typedef int (SALTS_CORE_PLATFORM_CALL *load32_fn)(const salts_atomic32 *, salts_memory_order, uint32_t *);
typedef int (SALTS_CORE_PLATFORM_CALL *once_fn)(salts_platform_once *, salts_platform_once_fn);
typedef int (SALTS_CORE_PLATFORM_CALL *create_fn)(salts_platform_thread **, salts_platform_thread_fn, void *);
typedef int (SALTS_CORE_PLATFORM_CALL *join_fn)(salts_platform_thread **, int *);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_create_fn)(salts_platform_mutex **);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_fn)(salts_platform_mutex *);
typedef int (SALTS_CORE_PLATFORM_CALL *mutex_destroy_fn)(salts_platform_mutex **);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_create_fn)(salts_platform_condition **);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_wait_fn)(salts_platform_condition *, salts_platform_mutex *);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_signal_fn)(salts_platform_condition *);
typedef int (SALTS_CORE_PLATFORM_CALL *cond_destroy_fn)(salts_platform_condition **);

static fetch32_fn g_fetch32;
static once_fn g_once;
static mutex_fn g_lock, g_unlock;
static cond_wait_fn g_wait;
static salts_atomic32 counter = {0};
static salts_platform_once once = SALTS_PLATFORM_ONCE_INIT;
static int once_count;
static salts_platform_mutex *gate_mutex;
static salts_platform_condition *gate_cond;
static int gate_ready, gate_payload;
static void init_once(void) { ++once_count; }
static int worker(void *arg) {
    unsigned i;
    uint32_t old = 0;
    (void)arg;
    if (g_once(&once, init_once) != SALTS_OK) return -1;
    for (i = 0; i < 3000u; ++i)
        if (g_fetch32(&counter, 1u, SALTS_MEMORY_RELAXED, &old) != SALTS_OK) return -2;
    return 17;
}
static int wait_worker(void *arg) {
    int value;
    (void)arg;
    if (g_lock(gate_mutex)) return -3;
    while (!gate_ready)
        if (g_wait(gate_cond, gate_mutex)) return -4;
    value = gate_payload;
    if (g_unlock(gate_mutex)) return -5;
    return value;
}
int main(int argc, char **argv) {
    HMODULE lib;
    check_fn check;
    init32_fn init32;
    load32_fn load32;
    create_fn create;
    join_fn join;
    mutex_create_fn new_mutex;
    mutex_destroy_fn destroy_mutex;
    cond_create_fn new_condition;
    cond_signal_fn broadcast;
    cond_destroy_fn destroy_condition;
    salts_platform_thread *threads[4] = {0}, *waiter = NULL;
    uint32_t actual = 0;
    unsigned i;
    int result;
    struct abi_slot { char pad; salts_atomic64 value; };
    if (argc != 2 || !argv[1]) return 1;
    lib = LoadLibraryA(argv[1]);
    if (!lib) { printf("portable LoadLibrary failed %lu\n", GetLastError()); return 2; }
#define GET(field, typ, name) do { field = (typ)GetProcAddress(lib, name); if (!(field)) { \
    printf("missing ABI symbol %s\n", name); FreeLibrary(lib); return 3; } } while (0)
    GET(check, check_fn, "salts_platform_portable_abi_verify");
    GET(init32, init32_fn, "salts_atomic32_init");
    GET(g_fetch32, fetch32_fn, "salts_atomic32_fetch_add");
    GET(load32, load32_fn, "salts_atomic32_load");
    GET(g_once, once_fn, "salts_platform_call_once");
    GET(create, create_fn, "salts_platform_thread_create");
    GET(join, join_fn, "salts_platform_thread_join");
    GET(new_mutex, mutex_create_fn, "salts_platform_mutex_create");
    GET(g_lock, mutex_fn, "salts_platform_mutex_lock");
    GET(g_unlock, mutex_fn, "salts_platform_mutex_unlock");
    GET(destroy_mutex, mutex_destroy_fn, "salts_platform_mutex_destroy");
    GET(new_condition, cond_create_fn, "salts_platform_condition_create");
    GET(g_wait, cond_wait_fn, "salts_platform_condition_wait");
    GET(broadcast, cond_signal_fn, "salts_platform_condition_broadcast");
    GET(destroy_condition, cond_destroy_fn, "salts_platform_condition_destroy");
#undef GET
    if (check(SALTS_PLATFORM_PORTABLE_ABI_VERSION,
              (uint32_t)sizeof(salts_atomic64),
              (uint32_t)offsetof(struct abi_slot, value),
              (uint32_t)sizeof(salts_atomic_ptr)) != SALTS_OK) return 4;
    if (check(999u, 8u, 8u, (uint32_t)sizeof(salts_atomic_ptr)) != SALTS_EINVAL) return 5;
    if (init32(&counter, 0u)) return 6;
    for (i = 0; i < 4; ++i)
        if (create(&threads[i], worker, NULL)) return 7;
    for (i = 0; i < 4; ++i)
        if (join(&threads[i], &result) || threads[i] || result != 17) return 8;
    if (once_count != 1 || load32(&counter, SALTS_MEMORY_ACQUIRE, &actual) ||
        actual != 12000u) return 9;

    if (new_mutex(&gate_mutex) || new_condition(&gate_cond)) return 10;
    if (create(&waiter, wait_worker, NULL)) return 11;
    if (g_lock(gate_mutex)) return 12;
    gate_payload = 42; gate_ready = 1;
    if (broadcast(gate_cond) || g_unlock(gate_mutex)) return 13;
    if (join(&waiter, &result) || waiter || result != 42) return 14;
    if (destroy_condition(&gate_cond) || gate_cond ||
        destroy_mutex(&gate_mutex) || gate_mutex) return 15;
    if (!FreeLibrary(lib)) return 16;
    puts("TinyCC -> llvm-mingw Salts::Platform Atomic+Thread C ABI PASS");
    return 0;
}
