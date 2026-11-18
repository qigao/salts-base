#include <threads/threads_types.h>
#include <tinytest.h>
#include <windows.h>
#include <stdlib.h>

/* This executable only links TinyTest. All tested entry points and FLS
 * callbacks belong to the module loaded for that iteration. */
struct tss_client {
    tss_t key;
    int (*set)(tss_t, void *);
    LONG callbacks;
};
static void increment(void *arg) {
    struct tss_client *client = arg;
    InterlockedIncrement(&client->callbacks);
}
static DWORD WINAPI foreign_worker(void *arg) {
    struct tss_client *client = arg;
    return (DWORD)client->set(client->key, client);
}
static void load_and_release(void) {
    HMODULE dll = LoadLibraryW(SALTS_TEST_CORE_DLL);
    check(dll != NULL);
    int (*create)(tss_t *, tss_dtor_t) = (int (*)(tss_t *, tss_dtor_t))GetProcAddress(dll, "tss_create");
    void (*retire)(tss_t) = (void (*)(tss_t))GetProcAddress(dll, "tss_delete");
    struct tss_client client = {0};
    client.set = (int (*)(tss_t, void *))GetProcAddress(dll, "tss_set");
    check(create && retire && client.set);
    check_equal(create(&client.key, increment), thrd_success);
    HANDLE worker = CreateThread(NULL, 0, foreign_worker, &client, 0, NULL);
    check(worker != NULL);
    check_equal(WaitForSingleObject(worker, 5000), WAIT_OBJECT_0);
    DWORD result;
    check(GetExitCodeThread(worker, &result));
    check_equal(result, (DWORD)thrd_success);
    check_equal(client.callbacks, 1L);
    CloseHandle(worker);
    retire(client.key);
    check(FreeLibrary(dll));
}
static size_t available_fls_slots(void) {
    size_t count = 0, capacity = 64;
    DWORD *indices = malloc(capacity * sizeof(*indices));
    if (!indices) abort();
    for (;;) {
        DWORD slot = FlsAlloc(NULL);
        if (slot == FLS_OUT_OF_INDEXES) break;
        if (count == capacity) {
            capacity *= 2;
            indices = realloc(indices, capacity * sizeof(*indices));
            if (!indices) abort();
        }
        indices[count++] = slot;
    }
    for (size_t i = 0; i < count; ++i)
        if (!FlsFree(indices[i])) abort();
    free(indices);
    return count;
}
spec("Core Windows TSS unload") {
    it("returns the exit-hook slot after foreign threads have finished") {
        load_and_release();
        size_t before = available_fls_slots();
        for (unsigned i = 0; i < 8; ++i) load_and_release();
        check_equal(available_fls_slots(), before);
    }
}
