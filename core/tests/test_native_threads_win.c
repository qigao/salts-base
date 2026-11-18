#include <threads/threads.h>
#include "thread_backend.h"
#include "thread_tls_win.h"
#include "nt_keyed_api_win.h"
#include "nt_wait_win.h"
#include <stdlib.h>
#include <string.h>
#define TINYTEST_NO_MAIN
#include <tinytest.h>

enum { WAITERS = 6 };
struct cohort {
    mtx_t mutex;
    cnd_t cond;
    unsigned ready, requeued, finished;
    unsigned wake_order[WAITERS];
    HANDLE enrolled, replies;
};
static int wait_twice(void *arg) {
    struct cohort *g = arg;
    if (mtx_lock(&g->mutex)) return 1;
    if (++g->ready == WAITERS && !SetEvent(g->enrolled)) abort();
    if (cnd_wait(&g->cond, &g->mutex)) return 2;
    /* The mutex orders these tickets exactly as the subsequent registrations.
     * Threads may reacquire in any order after the initial broadcast. */
    unsigned ticket = g->requeued++;
    if (g->requeued == WAITERS && !SetEvent(g->enrolled)) abort();
    if (cnd_wait(&g->cond, &g->mutex)) return 3;
    g->wake_order[g->finished++] = ticket;
    if (!ReleaseSemaphore(g->replies, 1, NULL)) abort();
    return mtx_unlock(&g->mutex);
}
spec("Windows keyed condition selection") {
    it("limits broadcasts to their snapshot and signals requeued waiters in FIFO order") {
        struct cohort g = {0};
        thrd_t workers[WAITERS];
        enum { SINGLE_SIGNALS = WAITERS - 2 };
        unsigned before_signal[SINGLE_SIGNALS], before_broadcast;
        g.enrolled = CreateEventW(NULL, FALSE, FALSE, NULL);
        g.replies = CreateSemaphoreW(NULL, 0, WAITERS, NULL);
        check(g.enrolled && g.replies);
        check_equal(mtx_init(&g.mutex, mtx_plain), thrd_success);
        check_equal(cnd_init(&g.cond), thrd_success);
        for (int i = 0; i < WAITERS; ++i)
            check_equal(thrd_create(workers+i, wait_twice, &g), thrd_success);
        /* Taking the mutex after the event ensures the last worker has
         * actually entered its wait. Release it so workers can requeue
         * even while the broadcaster is still notifying the snapshot. */
        check_equal(WaitForSingleObject(g.enrolled, 5000), WAIT_OBJECT_0);
        check_equal(mtx_lock(&g.mutex), thrd_success);
        check_equal(mtx_unlock(&g.mutex), thrd_success);
        check_equal(cnd_broadcast(&g.cond), thrd_success);
        check_equal(WaitForSingleObject(g.enrolled, 5000), WAIT_OBJECT_0);
        for (unsigned i = 0; i < SINGLE_SIGNALS; ++i) {
            check_equal(mtx_lock(&g.mutex), thrd_success);
            before_signal[i] = g.finished;
            check_equal(cnd_signal(&g.cond), thrd_success);
            check_equal(mtx_unlock(&g.mutex), thrd_success);
            check_equal(WaitForSingleObject(g.replies, 5000), WAIT_OBJECT_0);
        }
        check_equal(mtx_lock(&g.mutex), thrd_success);
        before_broadcast = g.finished;
        check_equal(cnd_broadcast(&g.cond), thrd_success);
        check_equal(mtx_unlock(&g.mutex), thrd_success);
        for (int i = 0; i < WAITERS; ++i) {
            int rc;
            check_equal(thrd_join(workers[i], &rc), thrd_success);
            check_equal(rc, 0);
        }
        CloseHandle(g.replies);
        CloseHandle(g.enrolled);
        cnd_destroy(&g.cond);
        mtx_destroy(&g.mutex);
        for (unsigned i = 0; i < SINGLE_SIGNALS; ++i) {
            check_equal(before_signal[i], i);
            check_equal(g.wake_order[i], i);
        }
        check_equal(before_broadcast, (unsigned)SINGLE_SIGNALS);
        check_equal(g.finished, (unsigned)WAITERS);
    }
}
struct timed_wait {
    mtx_t mutex;
    cnd_t cv;
    int holder_finished;
    HANDLE start;
};
static int hold_past_timeout(void *arg) {
    struct timed_wait *s = arg;
    SetEvent(s->start);
    if (mtx_lock(&s->mutex)) return 1;
    Sleep(125);
    s->holder_finished = 1;
    return mtx_unlock(&s->mutex);
}
static struct timespec after_ms(unsigned ms) {
    FILETIME ft;
    ULARGE_INTEGER ticks;
    struct timespec ts;
    GetSystemTimeAsFileTime(&ft);
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    ticks.QuadPart = ticks.QuadPart - 116444736000000000ULL + ms * 10000ULL;
    ts.tv_sec = (time_t)(ticks.QuadPart / 10000000);
    ts.tv_nsec = (long)(ticks.QuadPart % 10000000) * 100;
    return ts;
}
struct admission_wait {
    mtx_t mutex;
    cnd_t condition;
    HANDLE done, entering;
    int result;
};
static int wait_during_admission(void *opaque) {
    struct admission_wait *s = opaque;
    if (mtx_lock(&s->mutex) || mtx_lock(&s->mutex)) return 1;
    struct timespec due = after_ms(s->entering ? 250 : 10);
    if (s->entering && !SetEvent(s->entering)) abort();
    s->result = cnd_timedwait(&s->condition, &s->mutex, &due);
    if (mtx_unlock(&s->mutex) || mtx_unlock(&s->mutex)) return 2;
    return SetEvent(s->done) ? 0 : 3;
}
spec("Windows condition timeout") {
    it("retires a registered timed wait while the queue lock is held") {
        struct admission_wait s = {0};
        thrd_t worker;
        int result;
        s.done = CreateEventW(NULL, FALSE, FALSE, NULL);
        s.entering = CreateEventW(NULL, FALSE, FALSE, NULL);
        check(s.done && s.entering);
        check_equal(mtx_init(&s.mutex, mtx_recursive), thrd_success);
        check_equal(cnd_init(&s.condition), thrd_success);
        check_equal(thrd_create(&worker, wait_during_admission, &s), thrd_success);
        check_equal(WaitForSingleObject(s.entering, 3000), (DWORD)WAIT_OBJECT_0);
        check_equal(mtx_lock(&s.mutex), thrd_success);
        check_equal(platform_mutex_lock(&s.condition.owner->guard), SALTS_OK);
        check(s.condition.owner->first != NULL);
        check_equal(mtx_unlock(&s.mutex), thrd_success);
        DWORD completed = WaitForSingleObject(s.done, 3000);
        check_equal(platform_mutex_unlock(&s.condition.owner->guard), SALTS_OK);
        check_equal(thrd_join(worker, &result), thrd_success);
        check_equal(completed, (DWORD)WAIT_OBJECT_0);
        check_equal(s.result, thrd_timedout);
        check_equal(result, 0);
        cnd_destroy(&s.condition);
        mtx_destroy(&s.mutex);
        CloseHandle(s.entering);
        CloseHandle(s.done);
    }
    it("expires while another thread still owns the admission guard") {
        struct admission_wait s = {0};
        thrd_t worker;
        int result;
        s.done = CreateEventW(NULL, FALSE, FALSE, NULL);
        check(s.done != NULL);
        check_equal(mtx_init(&s.mutex, mtx_recursive), thrd_success);
        check_equal(cnd_init(&s.condition), thrd_success);
        check_equal(platform_mutex_lock(&s.condition.owner->guard), SALTS_OK);
        check_equal(thrd_create(&worker, wait_during_admission, &s), thrd_success);
        DWORD completed = WaitForSingleObject(s.done, 3000);
        check_equal(platform_mutex_unlock(&s.condition.owner->guard), SALTS_OK);
        check_equal(thrd_join(worker, &result), thrd_success);
        cnd_destroy(&s.condition);
        mtx_destroy(&s.mutex);
        CloseHandle(s.done);
        check_equal(completed, (DWORD)WAIT_OBJECT_0);
        check_equal(result, 0);
        check_equal(s.result, thrd_timedout);
    }
    it("returns with both recursive levels restored after another holder releases") {
        struct timed_wait s = {0};
        thrd_t holder;
        int result;
        s.start = CreateEventW(NULL, TRUE, FALSE, NULL);
        check(s.start != NULL);
        check_equal(mtx_init(&s.mutex, mtx_timed | mtx_recursive), thrd_success);
        check_equal(cnd_init(&s.cv), thrd_success);
        check_equal(mtx_lock(&s.mutex), thrd_success);
        check_equal(mtx_lock(&s.mutex), thrd_success);
        check_equal(thrd_create(&holder, hold_past_timeout, &s), thrd_success);
        check_equal(WaitForSingleObject(s.start, 5000), WAIT_OBJECT_0);
        struct timespec due = after_ms(80);
        check_equal(cnd_timedwait(&s.cv, &s.mutex, &due), thrd_timedout);
        check_equal(s.holder_finished, 1);
        check_equal(mtx_unlock(&s.mutex), thrd_success);
        check_equal(mtx_unlock(&s.mutex), thrd_success);
        check_equal(thrd_join(holder, &result), thrd_success);
        check_equal(result, 0);
        cnd_destroy(&s.cv);
        mtx_destroy(&s.mutex);
        CloseHandle(s.start);
    }
}

static once_flag once = ONCE_FLAG_INIT;
static sync_once_t extension_once = SALTS_ONCE_INIT;
static volatile LONG entered;
static int published;
static HANDLE release_once;
static void initialize_slowly(void) {
    WaitForSingleObject(release_once, INFINITE);
    ++published;
}
static int use_once(void *unused) {
    (void)unused;
    InterlockedIncrement(&entered);
    call_once(&once, initialize_slowly);
    sync_once(&extension_once, initialize_slowly);
    return published;
}
static once_flag outer = ONCE_FLAG_INIT, inner = ONCE_FLAG_INIT;
static HANDLE exit_gate;
static volatile LONG attempts, entrants;
static int inner_value;
static void exit_nested(void) {
    if (InterlockedIncrement(&attempts) == 1) {
        if (WaitForSingleObject(exit_gate, 5000) != WAIT_OBJECT_0) abort();
        thrd_exit(37);
    }
    inner_value = 41;
}
static void enter_nested(void) { call_once(&inner, exit_nested); }
static int retry_outer(void *unused) {
    (void)unused;
    InterlockedIncrement(&entrants);
    call_once(&outer, enter_nested);
    return inner_value;
}
spec("Windows blocking once") {
    it("hands an abandoned nested initializer to a waiting caller") {
        thrd_t group[WAITERS];
        int exited = 0;
        exit_gate = CreateEventW(NULL, TRUE, FALSE, NULL);
        check(exit_gate != NULL);
        for (int i = 0; i < WAITERS; ++i)
            check_equal(thrd_create(group+i, retry_outer, NULL), thrd_success);
        while (InterlockedCompareExchange(&entrants, 0, 0) != WAITERS) Sleep(1);
        check(SetEvent(exit_gate));
        for (int i = 0; i < WAITERS; ++i) {
            int result;
            check_equal(thrd_join(group[i], &result), thrd_success);
            if (result == 37) ++exited;
            else check_equal(result, 41);
        }
        check_equal(exited, 1);
        check_equal(attempts, 2L);
        CloseHandle(exit_gate);
    }
    it("publishes both initializers to every concurrent caller") {
        thrd_t group[WAITERS];
        release_once = CreateEventW(NULL, TRUE, FALSE, NULL);
        check(release_once != NULL);
        for (int i = 0; i < WAITERS; ++i)
            check_equal(thrd_create(group+i, use_once, NULL), thrd_success);
        while (InterlockedCompareExchange(&entered, 0, 0) != WAITERS) Sleep(1);
        SetEvent(release_once);
        for (int i = 0; i < WAITERS; ++i) {
            int result = 0;
            check_equal(thrd_join(group[i], &result), thrd_success);
            check_equal(result, 2);
        }
        CloseHandle(release_once);
    }
}

#ifdef SALTS_PRIVATE_TEB_ACCESS
struct tls_probe { DWORD slot; const void *parent_token; };
static DWORD WINAPI foreign_tls(void *opaque) {
    struct tls_probe *p = opaque;
    if (sync_thread_current_token() == p->parent_token || win_tls_read(p->slot)) return 1;
    if (!TlsSetValue(p->slot, p) || win_tls_read(p->slot) != p) return 2;
    return 0;
}
spec("Windows private thread identity and TLS") {
    it("isolates foreign threads and reads OS-owned expansion slots") {
        DWORD slots[TLS_MINIMUM_AVAILABLE + 1], result;
        size_t count = 0;
        struct tls_probe p;
        do {
            p.slot = TlsAlloc();
            check(p.slot != TLS_OUT_OF_INDEXES);
            slots[count++] = p.slot;
            check(TlsSetValue(p.slot, &slots));
            check(win_tls_read(p.slot) == &slots);
        } while (p.slot < TLS_MINIMUM_AVAILABLE);
        p.parent_token = sync_thread_current_token();
        HANDLE thread = CreateThread(NULL, 0, foreign_tls, &p, 0, NULL);
        check(thread != NULL);
        check_equal(WaitForSingleObject(thread, 5000), WAIT_OBJECT_0);
        check(GetExitCodeThread(thread, &result));
        check_equal(result, (DWORD)0);
        check(win_tls_read(p.slot) == &slots);
        CloseHandle(thread);
        while (count) check(TlsFree(slots[--count]));
    }
}
#endif

static const char *missing;
static FARPROC WINAPI omit_export(HMODULE module, LPCSTR name) {
    return strcmp(name, missing) ? GetProcAddress(module, name) : NULL;
}
spec("Windows NT capability admission") {
    it("distinguishes an NT timeout from unexpected success and failure statuses") {
        check_equal(win_keyed_status(0), SALTS_OK);
        check_equal(win_keyed_status(0x102), SALTS_ETIMEDOUT);
        check_equal(win_keyed_status(0x101), SALTS_EIO);
        check_equal(win_keyed_status((LONG)0xc000000d), SALTS_EIO);
    }
    it("requires both keyed operations and the native object wait") {
        win_keyed_api api = {0};
        HMODULE module = GetModuleHandleW(L"ntdll.dll");
        check_equal(win_bind_keyed_api(&api, NULL, GetProcAddress), SALTS_ENOTSUP);
        missing = "NtWaitForKeyedEvent";
        check_equal(win_bind_keyed_api(&api, module, omit_export), SALTS_ENOTSUP);
        missing = "NtReleaseKeyedEvent";
        check_equal(win_bind_keyed_api(&api, module, omit_export), SALTS_ENOTSUP);
        missing = "NtWaitForSingleObject";
        check_equal(win_bind_keyed_api(&api, module, omit_export), SALTS_ENOTSUP);
        check(!api.wait && !api.release && !api.wait_object);
        check_equal(win_bind_keyed_api(&api, module, GetProcAddress), SALTS_OK);
        check(api.wait && api.release);
    }
}

struct timed_race {
    mtx_t m;
    cnd_t cv;
    HANDLE start, registered, finished;
    int use_condition;
    unsigned completed;
};
enum { RACE_ROUNDS = 192 };
static int untimed_contender(void *arg) {
    mtx_t *mutex = arg;
    if (mtx_lock(mutex)) return 1;
    return mtx_unlock(mutex);
}
static int race_waiter(void *arg) {
    struct timed_race *r = arg;
    for (unsigned i = 0; i < RACE_ROUNDS; ++i) {
        if (WaitForSingleObject(r->start, 5000) != WAIT_OBJECT_0) return 1;
        if (r->use_condition && mtx_lock(&r->m)) return 2;
        struct timespec due = after_ms((i % 3) ? 1 : 0);
        SetEvent(r->registered);
        int rc = r->use_condition ? cnd_timedwait(&r->cv, &r->m, &due)
                                  : mtx_timedlock(&r->m, &due);
        if (rc != thrd_success && rc != thrd_timedout) return 3;
        if ((r->use_condition || rc == thrd_success) && mtx_unlock(&r->m)) return 4;
        ++r->completed;
        SetEvent(r->finished);
    }
    return 0;
}
spec("Windows deadline and release arbitration") {
    it("retires every registration across repeated mutex and condition reuse") {
        for (int mode = 0; mode != 2; ++mode) {
            struct timed_race r = {0};
            thrd_t worker;
            int result;
            r.use_condition = mode;
            r.start = CreateEventW(NULL, FALSE, FALSE, NULL);
            r.registered = CreateEventW(NULL, FALSE, FALSE, NULL);
            r.finished = CreateEventW(NULL, FALSE, FALSE, NULL);
            check(r.start && r.registered && r.finished);
            check_equal(mtx_init(&r.m, mtx_timed), thrd_success);
            check_equal(cnd_init(&r.cv), thrd_success);
            check_equal(thrd_create(&worker, race_waiter, &r), thrd_success);
            for (unsigned i = 0; i < RACE_ROUNDS; ++i) {
                thrd_t untimed;
                if (!mode) check_equal(mtx_lock(&r.m), thrd_success);
                SetEvent(r.start);
                check_equal(WaitForSingleObject(r.registered, 5000), WAIT_OBJECT_0);
                /* A single unlock must make progress for both a racing
                 * timed waiter and a peer with no timeout of its own. */
                if (!mode) check_equal(thrd_create(&untimed, untimed_contender, &r.m), thrd_success);
                if ((i % 3) == 1) Sleep(1);
                if (mode) {
                    check_equal(mtx_lock(&r.m), thrd_success);
                    check_equal(cnd_signal(&r.cv), thrd_success);
                }
                check_equal(mtx_unlock(&r.m), thrd_success);
                if (!mode) {
                    check_equal(thrd_join(untimed, &result), thrd_success);
                    check_equal(result, thrd_success);
                }
                check_equal(WaitForSingleObject(r.finished, 5000), WAIT_OBJECT_0);
            }
            check_equal(thrd_join(worker, &result), thrd_success);
            check_equal(result, 0);
            check_equal(r.completed, (unsigned)RACE_ROUNDS);
            check_equal(mtx_trylock(&r.m), thrd_success);
            check_equal(mtx_unlock(&r.m), thrd_success);
            cnd_destroy(&r.cv);
            mtx_destroy(&r.m);
            CloseHandle(r.start);
            CloseHandle(r.registered);
            CloseHandle(r.finished);
        }
    }
}
