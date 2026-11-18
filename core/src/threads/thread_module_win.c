#include "c11_threads_internal.h"
#include <windows.h>
#include <stdlib.h>

platform_tss *c11_exit_key;

/* Explicit unload requires every Core user and foreign-thread exit hook to
 * have finished. With no live FLS values, freeing the index cannot dispatch
 * user destructors under the loader lock. Process termination owns its cleanup. */
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    if (reason == DLL_PROCESS_DETACH && reserved == NULL && c11_exit_key) {
        if (platform_tss_destroy(&c11_exit_key) != SALTS_OK) abort();
    }
    return TRUE;
}
