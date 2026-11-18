#ifndef SALTS_NT_KEYED_API_WIN_H
#define SALTS_NT_KEYED_API_WIN_H

#include "thread_backend.h"
#include <windows.h>

typedef LONG (NTAPI *win_keyed_operation)(HANDLE, PVOID, BOOLEAN, PLARGE_INTEGER);
typedef struct win_keyed_api {
    win_keyed_operation wait, release;
} win_keyed_api;

enum { NT_WAIT_TIMEOUT = 0x102 };
static inline int win_keyed_status(LONG status) {
    if (status == 0) return SALTS_OK;
    if (status == NT_WAIT_TIMEOUT) return SALTS_ETIMEDOUT;
    return SALTS_EIO;
}

static inline int win_bind_keyed_api(win_keyed_api *out, HMODULE module,
                                     FARPROC (WINAPI *resolve)(HMODULE, LPCSTR)) {
    win_keyed_api api;
    if (!module) return SALTS_ENOTSUP;
    api.wait = (win_keyed_operation)resolve(module, "NtWaitForKeyedEvent");
    api.release = (win_keyed_operation)resolve(module, "NtReleaseKeyedEvent");
    if (!api.wait || !api.release) return SALTS_ENOTSUP;
    *out = api;
    return SALTS_OK;
}
#endif
