/* Explicit opt-in C11 threads source facade, not a libc/header replacement.
 * This M2 implementation is still experimental, notably for recursive
 * mutexes and live TSS deletion. Never undefine __STDC_NO_THREADS__.
 */
#ifndef SALTS_CORE_C11_THREADS_H
#define SALTS_CORE_C11_THREADS_H
#ifndef SALTS_C11_THREADS_EXPERIMENTAL
#error "C11 threads adapter requires SALTS_C11_THREADS_EXPERIMENTAL=1"
#endif
#include <platform_thread.h>
#include <stdint.h>
#include <time.h>
/* glibc stdlib.h declares its own once_flag; include it BEFORE aliasing. */
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Opaque pointer across TinyCC/LLVM/MSVC; no struct-return ABI hazards. */
typedef struct salts_c11_thread *thrd_t;
typedef int (SALTS_CORE_PLATFORM_CALL *thrd_start_t)(void *);
typedef struct { salts_platform_mutex *owner; int type; } mtx_t;
typedef struct { salts_platform_condition *owner; } cnd_t;
typedef salts_platform_once salts_c11_once_flag;
#define once_flag salts_c11_once_flag
#define ONCE_FLAG_INIT SALTS_PLATFORM_ONCE_INIT
enum { thrd_success=0, thrd_nomem=1, thrd_timedout=2, thrd_busy=3, thrd_error=4 };
enum { mtx_plain=0, mtx_recursive=1, mtx_timed=2 };

/* Link names remain prefixed to avoid collisions with UCRT/libc exports. */
#define thrd_create salts_c11_thrd_create
#define thrd_join salts_c11_thrd_join
#define thrd_detach salts_c11_thrd_detach
#define thrd_current salts_c11_thrd_current
#define thrd_equal salts_c11_thrd_equal
#define thrd_yield salts_c11_thrd_yield
#define thrd_exit salts_c11_thrd_exit
#define thrd_sleep salts_c11_thrd_sleep
#define mtx_init salts_c11_mtx_init
#define mtx_lock salts_c11_mtx_lock
#define mtx_trylock salts_c11_mtx_trylock
#define mtx_timedlock salts_c11_mtx_timedlock
#define mtx_unlock salts_c11_mtx_unlock
#define mtx_destroy salts_c11_mtx_destroy
#define cnd_init salts_c11_cnd_init
#define cnd_wait salts_c11_cnd_wait
#define cnd_timedwait salts_c11_cnd_timedwait
#define cnd_signal salts_c11_cnd_signal
#define cnd_broadcast salts_c11_cnd_broadcast
#define cnd_destroy salts_c11_cnd_destroy
#define call_once salts_c11_call_once

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL thrd_create(thrd_t *thr, thrd_start_t fn, void *arg);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL thrd_join(thrd_t thr, int *result);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL thrd_detach(thrd_t thr);
SALTS_CORE_PLATFORM_API thrd_t SALTS_CORE_PLATFORM_CALL thrd_current(void);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL thrd_equal(thrd_t a, thrd_t b);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL thrd_yield(void);
/* CRT struct timespec must NEVER cross a TinyCC/MSVC/LLVM DLL boundary.
 * Convert it here in the consumer, then use fixed-width values only.
 */
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_c11_thrd_sleep_abi(uint64_t seconds, uint32_t nanos);
static inline int SALTS_CORE_PLATFORM_CALL thrd_sleep(
    const struct timespec *duration, struct timespec *remaining) {
    int rc;
    if (!duration || duration->tv_sec < 0 ||
        duration->tv_nsec < 0 || duration->tv_nsec >= 1000000000L)
        return -1;
    rc = salts_c11_thrd_sleep_abi((uint64_t)duration->tv_sec,
                                   (uint32_t)duration->tv_nsec);
    if (rc == 0 && remaining) { remaining->tv_sec=0; remaining->tv_nsec=0; }
    return rc;
}
#if defined(_MSC_VER)
__declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL thrd_exit(int result);

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL mtx_init(mtx_t *m, int type);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL mtx_lock(mtx_t *m);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL mtx_trylock(mtx_t *m);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_c11_mtx_timedlock_abi(mtx_t *m, int64_t seconds, uint32_t nanos);
static inline int SALTS_CORE_PLATFORM_CALL mtx_timedlock(
    mtx_t *m, const struct timespec *utc_deadline) {
    if (!utc_deadline || utc_deadline->tv_nsec < 0 ||
        utc_deadline->tv_nsec >= 1000000000L) return thrd_error;
    return salts_c11_mtx_timedlock_abi(m, (int64_t)utc_deadline->tv_sec,
                                        (uint32_t)utc_deadline->tv_nsec);
}
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL mtx_unlock(mtx_t *m);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL mtx_destroy(mtx_t *m);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cnd_init(cnd_t *c);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cnd_wait(cnd_t *c, mtx_t *m);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL salts_c11_cnd_timedwait_abi(cnd_t *c, mtx_t *m, int64_t seconds, uint32_t nanos);
static inline int SALTS_CORE_PLATFORM_CALL cnd_timedwait(
    cnd_t *c, mtx_t *m, const struct timespec *utc_deadline) {
    if (!utc_deadline || utc_deadline->tv_nsec < 0 ||
        utc_deadline->tv_nsec >= 1000000000L) return thrd_error;
    return salts_c11_cnd_timedwait_abi(c, m,
                (int64_t)utc_deadline->tv_sec, (uint32_t)utc_deadline->tv_nsec);
}
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cnd_signal(cnd_t *c);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cnd_broadcast(cnd_t *c);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cnd_destroy(cnd_t *c);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL call_once(once_flag *flag, void (SALTS_CORE_PLATFORM_CALL *callback)(void));

#ifdef __cplusplus
}
#endif
#endif
