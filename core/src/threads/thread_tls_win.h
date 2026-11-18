#ifndef SALTS_THREAD_TLS_WIN_H
#define SALTS_THREAD_TLS_WIN_H

#include <windows.h>
#include <winternl.h>

/* The OS owns the TEB. Its address is a live thread identity, including for
 * foreign threads. Only read TLS slots allocated to this module via TlsAlloc. */
#if (defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)) && (defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__))
#define SALTS_PRIVATE_TEB_ACCESS 1
static inline const void *win_thread_token(void) { return NtCurrentTeb(); }
static inline void *win_tls_read(DWORD index) {
    PTEB teb = NtCurrentTeb();
    if (index < TLS_MINIMUM_AVAILABLE) return teb->TlsSlots[index];
    void **extra = (void **)teb->TlsExpansionSlots;
    return extra ? extra[index - TLS_MINIMUM_AVAILABLE] : NULL;
}
#endif
#endif
