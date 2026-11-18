#include "thread_backend.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <process.h>
#include <stdlib.h>

/* The runtime owns both the native HANDLE and allocated storage.
 * A detached thread still retains its worker reference until it returns.
 */
struct platform_thread {
    HANDLE native;
    unsigned id;
    platform_thread_fn callback;
    void *argument;
    int result;
    atomic32 refs;
};
#if defined(_MSC_VER)
static __declspec(thread) platform_thread *platform_current;
#elif defined(__clang__) || defined(__GNUC__)
static _Thread_local platform_thread *platform_current;
#else
#error "Windows Platform Thread TLS requires supported compiler TLS, not a global fallback"
#endif
uint64_t platform_thread_current_id(void) {
    return (uint64_t)GetCurrentThreadId();
}
uint64_t platform_thread_id(const platform_thread *thread) {
    return thread ? (uint64_t)thread->id : 0u;
}

static void release_thread(platform_thread *thread) {
    uint32_t before = 0;
    (void)atomic32_fetch_add(&thread->refs, UINT32_MAX,
                                    SALTS_MEMORY_SEQ_CST, &before);
    if (before == 1u) free(thread);
}
static unsigned __stdcall thread_entry(void *opaque) {
    platform_thread *thread = (platform_thread *)opaque;
    platform_current = thread;
    thread->result = thread->callback(thread->argument); /* user: default C ABI, OS: stdcall */
    platform_current = NULL;
    release_thread(thread);
    return 0;
}
int platform_thread_create(platform_thread **out,
                              platform_thread_fn fn, void *arg) {
    platform_thread *thread;
    uintptr_t native;
    if (!out || !fn || *out) return SALTS_EINVAL;
    thread = (platform_thread *)calloc(1u, sizeof(*thread));
    if (!thread) return SALTS_ENOMEM;
    thread->callback = fn;
    thread->argument = arg;
    (void)atomic32_init(&thread->refs, 2u);
    native = _beginthreadex(NULL, 0, thread_entry, thread, 0, &thread->id);
    if (!native) { free(thread); return SALTS_EBUSY; }
    thread->native = (HANDLE)native;
    *out = thread;
    return SALTS_OK;
}
int platform_thread_join(platform_thread **owner, int *result) {
    platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (GetCurrentThreadId() == thread->id) return SALTS_EINVAL;
    if (WaitForSingleObject(thread->native, INFINITE) != WAIT_OBJECT_0) return SALTS_EBUSY;
    if (!CloseHandle(thread->native)) return SALTS_EBUSY;
    if (result) *result = thread->result;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
int platform_thread_detach(platform_thread **owner) {
    platform_thread *thread;
    if (!owner || !(thread = *owner)) return SALTS_EINVAL;
    if (!CloseHandle(thread->native)) return SALTS_EBUSY;
    *owner = NULL;
    release_thread(thread);
    return SALTS_OK;
}
void platform_thread_exit(int result) {
    platform_thread *thread = platform_current;
    if (thread) {
        thread->result = result;
        platform_current = NULL;
        release_thread(thread);
    }
    _endthreadex(0);
    for (;;) ExitThread(0); /* The CRT entrypoint must not return. */
}
void platform_thread_yield(void) { (void)SwitchToThread(); }
int platform_thread_sleep_ns(uint64_t duration_ns) {
    if (duration_ns > SALTS_PLATFORM_MAX_RELATIVE_WAIT_NS) return SALTS_EINVAL;
    Sleep((DWORD)((duration_ns + 999999ull) / 1000000ull));
    return SALTS_OK;
}
#endif
