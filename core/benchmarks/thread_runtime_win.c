#include <threads/threads.h>
#include <tinytest.h>
#include <windows.h>
#include <winternl.h>
#include "thread_tls_win.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef BENCH_PTHREAD
#include <pthread.h>
#endif

enum { SAMPLES = 4096, WORKERS = 8, RUN_MS = 350 };
enum { CONDITION_WARMUP = 1024, CONDITION_ROUNDS = 65536 };
enum { DELAYED_WARMUP = 8, DELAYED_ROUNDS = 256 };
enum backend { SALTS, SRW, PTHREAD };
struct run {
    enum backend api;
    mtx_t mutex;
    SRWLOCK srw;
#ifdef BENCH_PTHREAD
    pthread_mutex_t pthread;
#endif
    HANDLE start, finish;
    volatile LONG ready, done;
    LONGLONG end;
    uint64_t protected_count;
};
struct worker {
    struct run *run;
    uint64_t operations, contended;
    uint32_t random;
    LONGLONG samples[SAMPLES];
};
static LONGLONG clock_ticks(void) {
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) abort();
    return counter.QuadPart;
}
static ULONGLONG cpu_ticks(void) {
    FILETIME created, ended, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) abort();
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}

/* Take both snapshots while all benchmark threads are alive. The SDK names
 * the NT thread record's context-switch counter Reserved3. */
static uint64_t context_switches(void) {
    ULONG size = 0;
    NTSTATUS status;
    SYSTEM_PROCESS_INFORMATION *record;
    unsigned char *buffer = NULL;
    uint64_t count = 0;
    do {
        free(buffer);
        buffer = size ? malloc(size) : NULL;
        if (size && !buffer) abort();
        status = NtQuerySystemInformation(SystemProcessInformation, buffer, size, &size);
    } while ((ULONG)status == 0xc0000004u);
    if (status != 0) abort();
    record = (SYSTEM_PROCESS_INFORMATION *)buffer;
    while ((ULONG_PTR)record->UniqueProcessId != GetCurrentProcessId()) {
        if (!record->NextEntryOffset) abort();
        record = (SYSTEM_PROCESS_INFORMATION *)((char *)record + record->NextEntryOffset);
    }
    const SYSTEM_THREAD_INFORMATION *threads = (const SYSTEM_THREAD_INFORMATION *)(record + 1);
    for (ULONG i = 0; i < record->NumberOfThreads; ++i) count += threads[i].Reserved3;
    free(buffer);
    return count;
}
static int take(struct run *r) {
    if (r->api == SALTS) {
        int rc = mtx_trylock(&r->mutex);
        if (rc == thrd_success) return 0;
        if (rc != thrd_busy || mtx_lock(&r->mutex)) abort();
    } else if (r->api == SRW) {
        if (TryAcquireSRWLockExclusive(&r->srw)) return 0;
        AcquireSRWLockExclusive(&r->srw);
#ifdef BENCH_PTHREAD
    } else {
        int rc = pthread_mutex_trylock(&r->pthread);
        if (!rc) return 0;
        if (rc != EBUSY || pthread_mutex_lock(&r->pthread)) abort();
#endif
    }
    return 1;
}
static void give(struct run *r) {
    if (r->api == SALTS) {
        if (mtx_unlock(&r->mutex)) abort();
    } else if (r->api == SRW) {
        ReleaseSRWLockExclusive(&r->srw);
#ifdef BENCH_PTHREAD
    } else {
        if (pthread_mutex_unlock(&r->pthread)) abort();
#endif
    }
}
static int exercise(void *arg) {
    struct worker *w = arg;
    struct run *r = w->run;
    InterlockedIncrement(&r->ready);
    WaitForSingleObject(r->start, INFINITE);
    for (;;) {
        LONGLONG begin = clock_ticks();
        if (begin >= r->end) break;
        w->contended += take(r);
        LONGLONG latency = clock_ticks() - begin;
        ++r->protected_count;
        give(r);
        uint64_t slot = w->operations++;
        if (slot >= SAMPLES) {
            w->random = w->random * 1664525u + 1013904223u;
            slot = ((uint64_t)w->random * w->operations) >> 32;
        }
        if (slot < SAMPLES) w->samples[slot] = latency;
    }
    InterlockedIncrement(&r->done);
    WaitForSingleObject(r->finish, INFINITE);
    return 0;
}
static int compare_ticks(const void *left, const void *right) {
    LONGLONG a = *(const LONGLONG *)left, b = *(const LONGLONG *)right;
    return (a > b) - (a < b);
}
static void measure(enum backend api, int n, LONGLONG frequency) {
    const char *names[] = {"Salts", "SRW", "pthread"};
    struct run r = {0};
    struct worker *workers = calloc(n, sizeof(*workers));
    LONGLONG *samples = malloc(n * SAMPLES * sizeof(*samples));
    thrd_t threads[WORKERS];
    size_t used = 0;
    uint64_t total = 0, busy = 0, least = UINT64_MAX, most = 0;
    if (!workers || !samples) abort();
    r.api = api;
    if (mtx_init(&r.mutex, mtx_plain)) abort();
    InitializeSRWLock(&r.srw);
#ifdef BENCH_PTHREAD
    if (pthread_mutex_init(&r.pthread, NULL)) abort();
#endif
    r.start = CreateEventW(NULL, TRUE, FALSE, NULL);
    r.finish = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!r.start || !r.finish) abort();
    for (int i = 0; i < n; ++i) {
        workers[i].run = &r;
        workers[i].random = (uint32_t)i + 1;
        if (thrd_create(&threads[i], exercise, workers + i)) abort();
    }
    while (InterlockedCompareExchange(&r.ready, 0, 0) != n) Sleep(1);
    uint64_t switches = context_switches(), cpu = cpu_ticks();
    LONGLONG begin = clock_ticks();
    r.end = begin + frequency * RUN_MS / 1000;
    SetEvent(r.start);
    while (InterlockedCompareExchange(&r.done, 0, 0) != n) Sleep(1);
    double elapsed = (double)(clock_ticks() - begin) / frequency;
    cpu = cpu_ticks() - cpu;
    switches = context_switches() - switches;
    SetEvent(r.finish);
    for (int i = 0; i < n; ++i) {
        if (thrd_join(threads[i], NULL)) abort();
        uint64_t ops = workers[i].operations;
        size_t count = ops < SAMPLES ? (size_t)ops : SAMPLES;
        memcpy(samples + used, workers[i].samples, count * sizeof(*samples));
        used += count;
        total += ops;
        busy += workers[i].contended;
        if (ops < least) least = ops;
        if (ops > most) most = ops;
    }
    if (total != r.protected_count || !used) abort();
    qsort(samples, used, sizeof(*samples), compare_ticks);
    double ns = 1e9 / frequency;
    printf("%s n=%d ops/s=%.0f p50/p95/p99/p999(ns)=%.0f/%.0f/%.0f/%.0f "
           "cpu(ms)=%.1f cs=%llu busy=%.1f%% min/max=%.3f\n", names[api], n,
           total / elapsed, samples[used/2]*ns, samples[used*95/100]*ns,
           samples[used*99/100]*ns, samples[used*999/1000]*ns, cpu/10000.0,
           (unsigned long long)switches, busy*100.0/total, (double)least/most);
    CloseHandle(r.start);
    CloseHandle(r.finish);
    mtx_destroy(&r.mutex);
#ifdef BENCH_PTHREAD
    pthread_mutex_destroy(&r.pthread);
#endif
    free(samples);
    free(workers);
}
struct condition_sample {
    LONGLONG observed, lock_ticks, wait_ticks, signal_ticks;
};
struct ping_pong {
    struct run lock;
    cnd_t cv;
    CONDITION_VARIABLE native_cv;
#ifdef BENCH_PTHREAD
    pthread_cond_t pthread_cv;
#endif
    int turn, exchanges;
    DWORD reply_delay_ms;
    DWORD_PTR reply_mask;
    struct condition_sample *initiator_samples, *reply_samples;
};
static LONGLONG exchange(struct ping_pong *p, int side, struct condition_sample *sample) {
    LONGLONG before_lock = sample ? clock_ticks() : 0;
    take(&p->lock);
    LONGLONG locked = sample ? clock_ticks() : 0;
    while (p->turn != side) {
        if (p->lock.api == SALTS) {
            if (cnd_wait(&p->cv, &p->lock.mutex)) abort();
        } else if (p->lock.api == SRW) {
            if (!SleepConditionVariableSRW(&p->native_cv, &p->lock.srw, INFINITE, 0)) abort();
#ifdef BENCH_PTHREAD
        } else {
            if (pthread_cond_wait(&p->pthread_cv, &p->lock.pthread)) abort();
#endif
        }
    }
    /* Under the mutex, side zero has observed the preceding reply and is
     * about to publish its next request. Consecutive marks span one trip. */
    LONGLONG mark = side == 0 || sample ? clock_ticks() : 0;
    p->turn = 1 - side;
    if (p->lock.api == SALTS) {
        if (cnd_signal(&p->cv)) abort();
    } else if (p->lock.api == SRW) {
        WakeConditionVariable(&p->native_cv);
#ifdef BENCH_PTHREAD
    } else {
        if (pthread_cond_signal(&p->pthread_cv)) abort();
#endif
    }
    LONGLONG signaled = sample ? clock_ticks() : 0;
    give(&p->lock);
    if (sample) {
        *sample = (struct condition_sample){mark, locked - before_lock,
                                           mark - locked, signaled - mark};
    }
    return mark;
}
static int reply(void *arg) {
    struct ping_pong *p = arg;
    if (p->reply_mask && !SetThreadAffinityMask(GetCurrentThread(), p->reply_mask)) abort();
    for (int i = 0; i < p->exchanges; ++i) {
        if (p->reply_delay_ms) Sleep(p->reply_delay_ms);
        exchange(p, 1, p->reply_samples ? &p->reply_samples[i] : NULL);
    }
    return 0;
}
static void report_condition_band(const struct ping_pong *p, const char *name,
                                  const char *placement, const char *band, int warmup,
                                  LONGLONG low, LONGLONG high, double ns) {
    unsigned count = 0;
    LONGLONG request = 0, response = 0, peak = 0;
    LONGLONG lock[2] = {0}, wait[2] = {0}, signal[2] = {0};
    for (int i = warmup; i < p->exchanges; ++i) {
        const struct condition_sample *previous = &p->initiator_samples[i - 1];
        const struct condition_sample *reply = &p->reply_samples[i - 1];
        const struct condition_sample *current = &p->initiator_samples[i];
        LONGLONG trip = current->observed - previous->observed;
        if (trip < low || trip > high) continue;
        ++count;
        request += reply->observed - previous->observed;
        response += current->observed - reply->observed;
        if (trip > peak) peak = trip;
        lock[0] += current->lock_ticks;
        lock[1] += reply->lock_ticks;
        wait[0] += current->wait_ticks;
        wait[1] += reply->wait_ticks;
        signal[0] += previous->signal_ticks;
        signal[1] += reply->signal_ticks;
    }
    if (!count) abort();
    double mean_ns = ns / count;
    printf("%s condition phases placement=%s band=%s n=%u trip_max_ns=%.0f "
           "request/reply_ns=%.0f/%.0f initiator_lock/wait/signal_ns=%.0f/%.0f/%.0f "
           "responder_lock/wait/signal_ns=%.0f/%.0f/%.0f\n",
           name, placement, band, count, peak * ns, request * mean_ns, response * mean_ns,
           lock[0] * mean_ns, wait[0] * mean_ns, signal[0] * mean_ns,
           lock[1] * mean_ns, wait[1] * mean_ns, signal[1] * mean_ns);
}
static void measure_condition(enum backend api, LONGLONG frequency, unsigned cpus,
                              DWORD reply_delay_ms, DWORD_PTR split_mask, BOOL trace) {
    const char *names[] = {"Salts", "SRW/CV", "pthread"};
    struct ping_pong p = {0};
    thrd_t worker;
    int rounds = reply_delay_ms ? DELAYED_ROUNDS : CONDITION_ROUNDS;
    int warmup = reply_delay_ms ? DELAYED_WARMUP : CONDITION_WARMUP;
    LONGLONG *latency = malloc(rounds * sizeof(*latency));
    LONGLONG *trips = malloc(rounds * sizeof(*trips));
    LONGLONG previous = 0;
    DWORD_PTR old_mask = 0;
    ULONG64 cycles_before, cycles_after;
    ULONG64 waiter_before, waiter_after;
    if (!latency || !trips) abort();
    p.lock.api = api;
    p.exchanges = warmup + rounds;
    p.reply_delay_ms = reply_delay_ms;
    if (trace) {
        p.initiator_samples = malloc(p.exchanges * sizeof(*p.initiator_samples));
        p.reply_samples = malloc(p.exchanges * sizeof(*p.reply_samples));
        if (!p.initiator_samples || !p.reply_samples) abort();
    }
    if (split_mask) {
        DWORD_PTR initiator_mask = split_mask & (0 - split_mask);
        p.reply_mask = split_mask ^ initiator_mask;
        old_mask = SetThreadAffinityMask(GetCurrentThread(), initiator_mask);
        if (!old_mask) abort();
    }
    if (mtx_init(&p.lock.mutex, mtx_plain) || cnd_init(&p.cv)) abort();
    InitializeSRWLock(&p.lock.srw);
    InitializeConditionVariable(&p.native_cv);
#ifdef BENCH_PTHREAD
    if (pthread_mutex_init(&p.lock.pthread, NULL) || pthread_cond_init(&p.pthread_cv, NULL)) abort();
#endif
    if (thrd_create(&worker, reply, &p)) abort();
    for (int i = 0; i < warmup; ++i)
        previous = exchange(&p, 0, trace ? &p.initiator_samples[i] : NULL);
    if (!QueryProcessCycleTime(GetCurrentProcess(), &cycles_before)) abort();
    if (!QueryThreadCycleTime(GetCurrentThread(), &waiter_before)) abort();
    LONGLONG start = clock_ticks();
    for (int i = 0; i < rounds; ++i) {
        LONGLONG before = clock_ticks();
        LONGLONG mark = exchange(&p, 0, trace ? &p.initiator_samples[warmup + i] : NULL);
        latency[i] = clock_ticks() - before;
        trips[i] = mark - previous;
        previous = mark;
    }
    if (thrd_join(worker, NULL) || p.turn != 0) abort();
    double seconds = (double)(clock_ticks() - start) / frequency;
    if (!QueryProcessCycleTime(GetCurrentProcess(), &cycles_after)) abort();
    if (!QueryThreadCycleTime(GetCurrentThread(), &waiter_after)) abort();
    qsort(latency, rounds, sizeof(*latency), compare_ticks);
    qsort(trips, rounds, sizeof(*trips), compare_ticks);
    double ns = 1e9 / frequency;
    const char *placement = split_mask ? "split" : "shared";
    printf("%s condition cpus=%u reply_sleep_ms=%lu round trips/s=%.0f "
           "exchange p50/p95/p99/p999(ns)=%.0f/%.0f/%.0f/%.0f trip p99/p999(ns)=%.0f/%.0f "
           "process cycles/trip=%.0f waiter cycles/trip=%.0f placement=%s trace=%d\n",
           names[api], cpus, (unsigned long)reply_delay_ms, rounds / seconds,
           latency[rounds/2]*ns, latency[rounds*95/100]*ns,
           latency[rounds*99/100]*ns, latency[rounds*999/1000]*ns,
           trips[rounds*99/100]*ns, trips[rounds*999/1000]*ns,
           (double)(cycles_after - cycles_before) / rounds,
           (double)(waiter_after - waiter_before) / rounds, placement, trace);
    if (trace) {
        report_condition_band(&p, names[api], placement, "near-p99", warmup,
                              trips[rounds * 985 / 1000], trips[rounds * 995 / 1000], ns);
        report_condition_band(&p, names[api], placement, "p999-plus", warmup,
                              trips[rounds * 999 / 1000], trips[rounds - 1], ns);
        free(p.reply_samples);
        free(p.initiator_samples);
    }
    if (old_mask && !SetThreadAffinityMask(GetCurrentThread(), old_mask)) abort();
    free(trips);
    free(latency);
    cnd_destroy(&p.cv);
    mtx_destroy(&p.lock.mutex);
#ifdef BENCH_PTHREAD
    pthread_cond_destroy(&p.pthread_cv);
    pthread_mutex_destroy(&p.lock.pthread);
#endif
}
struct reader_load {
    sync_rwlock_t lock;
    volatile LONG ready, stop;
    HANDLE start;
};
static int read_until_stopped(void *arg) {
    struct reader_load *load = arg;
    InterlockedIncrement(&load->ready);
    if (WaitForSingleObject(load->start, INFINITE) != WAIT_OBJECT_0) abort();
    unsigned count = 0;
    while (!InterlockedCompareExchange(&load->stop, 0, 0)) {
        sync_rwlock_rdlock(&load->lock);
        ++count;
        sync_rwlock_rdunlock(&load->lock);
    }
    return (int)count;
}
struct writer_sample {
    struct reader_load *load;
    unsigned completed;
    LONGLONG longest;
};
static int sample_writer(void *arg) {
    struct writer_sample *w = arg;
    InterlockedIncrement(&w->load->ready);
    if (WaitForSingleObject(w->load->start, INFINITE) != WAIT_OBJECT_0) abort();
    while (!InterlockedCompareExchange(&w->load->stop, 0, 0)) {
        LONGLONG before = clock_ticks();
        sync_rwlock_wrlock(&w->load->lock);
        LONGLONG elapsed = clock_ticks() - before;
        if (elapsed > w->longest) w->longest = elapsed;
        if (!InterlockedCompareExchange(&w->load->stop, 0, 0)) ++w->completed;
        sync_rwlock_wrunlock(&w->load->lock);
    }
    return 0;
}
spec("Windows mutex contention") {
    it("records writer progress with concurrent SRW readers") {
        struct reader_load load = {0};
        struct writer_sample writer = {&load, 0, 0};
        thrd_t threads[WORKERS];
        unsigned reads = 0;
        LARGE_INTEGER frequency;
        check(QueryPerformanceFrequency(&frequency));
        check_equal(sync_rwlock_init(&load.lock), 0);
        load.start = CreateEventW(NULL, TRUE, FALSE, NULL);
        check(load.start != NULL);
        for (int i = 1; i < WORKERS; ++i)
            check_equal(thrd_create(&threads[i], read_until_stopped, &load), thrd_success);
        check_equal(thrd_create(&threads[0], sample_writer, &writer), thrd_success);
        while (InterlockedCompareExchange(&load.ready, 0, 0) != WORKERS) Sleep(1);
        check(SetEvent(load.start));
        Sleep(RUN_MS);
        InterlockedExchange(&load.stop, 1);
        check_equal(thrd_join(threads[0], NULL), thrd_success);
        for (int i = 1; i < WORKERS; ++i) {
            int count;
            check_equal(thrd_join(threads[i], &count), thrd_success);
            reads += (unsigned)count;
        }
        printf("Salts SRW readers=%d reads=%u writes_in_window=%u max_writer_wait_us=%.1f\n",
               WORKERS - 1, reads, writer.completed, writer.longest * 1e6 / frequency.QuadPart);
        CloseHandle(load.start);
        sync_rwlock_destroy(&load.lock);
    }
    it("measures uncontended, two-worker and oversubscribed acquisition") {
        DWORD_PTR available, system, mask;
        LARGE_INTEGER frequency;
        check(GetProcessAffinityMask(GetCurrentProcess(), &available, &system));
        mask = available & (0 - available);
        DWORD_PTR rest = available ^ mask;
        mask |= rest & (0 - rest);
        check(SetProcessAffinityMask(GetCurrentProcess(), mask));
        check(QueryPerformanceFrequency(&frequency));
        const int counts[] = {1, 2, WORKERS};
        for (size_t i = 0; i < sizeof(counts)/sizeof(counts[0]); ++i) {
            measure(SALTS, counts[i], frequency.QuadPart);
            measure(SRW, counts[i], frequency.QuadPart);
#ifdef BENCH_PTHREAD
            measure(PTHREAD, counts[i], frequency.QuadPart);
#endif
        }
        check(SetProcessAffinityMask(GetCurrentProcess(), available));
    }
    it("measures condition handoff with one and two available logical processors") {
        DWORD_PTR available, system;
        LARGE_INTEGER frequency;
        check(GetProcessAffinityMask(GetCurrentProcess(), &available, &system));
        check(QueryPerformanceFrequency(&frequency));
        DWORD_PTR mask = 0, remaining = available;
        for (unsigned cpus = 1; cpus <= 2 && remaining; ++cpus) {
            mask |= remaining & (0 - remaining);
            remaining = available ^ mask;
            check(SetProcessAffinityMask(GetCurrentProcess(), mask));
#if defined(__aarch64__) || defined(_M_ARM64)
            const unsigned repeats = 4;
#else
            const unsigned repeats = 1;
#endif
            for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                enum backend first = repeat % 2 == 0 ? SALTS : SRW;
                enum backend second = first == SALTS ? SRW : SALTS;
                measure_condition(first, frequency.QuadPart, cpus, 0, 0, FALSE);
                measure_condition(second, frequency.QuadPart, cpus, 0, 0, FALSE);
#if defined(__aarch64__) || defined(_M_ARM64)
                if (cpus == 2) {
                    measure_condition(second, frequency.QuadPart, cpus, 0, mask, FALSE);
                    measure_condition(first, frequency.QuadPart, cpus, 0, mask, FALSE);
                }
#endif
            }
#if defined(__aarch64__) || defined(_M_ARM64)
            if (cpus == 2) {
                measure_condition(SALTS, frequency.QuadPart, cpus, 0, 0, TRUE);
                measure_condition(SRW, frequency.QuadPart, cpus, 0, 0, TRUE);
                measure_condition(SRW, frequency.QuadPart, cpus, 0, mask, TRUE);
                measure_condition(SALTS, frequency.QuadPart, cpus, 0, mask, TRUE);
            }
#endif
#ifdef BENCH_PTHREAD
            measure_condition(PTHREAD, frequency.QuadPart, cpus, 0, 0, FALSE);
#endif
            measure_condition(SALTS, frequency.QuadPart, cpus, 1, 0, FALSE);
        }
        check(SetProcessAffinityMask(GetCurrentProcess(), available));
    }
}

#ifdef SALTS_PRIVATE_TEB_ACCESS
static DWORD tls_index, fls_index;
static SALTS_THREAD_LOCAL void *compiler_value;
static void *read_compiler(void) { return compiler_value; }
static void *read_teb(void) { return win_tls_read(tls_index); }
static void *read_tls(void) { return TlsGetValue(tls_index); }
static void *read_fls(void) { return FlsGetValue(fls_index); }
static void *compiler_identity(void) { return &compiler_value; }
static void *teb_identity(void) { return (void *)win_thread_token(); }
static void *(*volatile read_value)(void);
static void *volatile observed;
spec("Windows private per-thread pointer") {
    it("measures the same value through compiler TLS, TEB, TLS and FLS") {
        static int marker;
        LARGE_INTEGER frequency;
        void *(*variants[])(void) = {read_compiler, read_teb, read_tls, read_fls,
                                     compiler_identity, teb_identity};
        const char *names[] = {"compiler TLS", "TEB slot", "TlsGetValue", "FlsGetValue",
                               "compiler identity", "TEB identity"};
        tls_index = TlsAlloc();
        fls_index = FlsAlloc(NULL);
        check(tls_index != TLS_OUT_OF_INDEXES && fls_index != FLS_OUT_OF_INDEXES);
        compiler_value = &marker;
        check(TlsSetValue(tls_index, &marker) && FlsSetValue(fls_index, &marker));
        check(QueryPerformanceFrequency(&frequency));
        for (unsigned v = 0; v < sizeof(variants)/sizeof(variants[0]); ++v) {
            read_value = variants[v];
            void *expected = v < 4 ? &marker : variants[v]();
            LONGLONG times[9];
            for (unsigned run = 0; run < 9; ++run) {
                LONGLONG start = clock_ticks();
                for (unsigned i = 0; i < 500000; ++i) observed = read_value();
                times[run] = clock_ticks() - start;
            }
            qsort(times, 9, sizeof(*times), compare_ticks);
            check(observed == expected);
            printf("%s median ns/read=%.2f\n", names[v], times[4] * 2000.0 / frequency.QuadPart);
        }
        check(TlsFree(tls_index) && FlsFree(fls_index));
    }
}
#endif
