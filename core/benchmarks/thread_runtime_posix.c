#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#include <threads/threads.h>
#include <tinytest.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

enum { LOCK_ROUNDS = 100000, TRIPS = 8192, TIMEOUTS = 32 };
enum provider { PTHREAD, SALTS_TIMED, SALTS_PLAIN };
struct panel {
    enum provider salts;
    mtx_t mutex;
    cnd_t cond;
    pthread_mutex_t native;
    pthread_cond_t changed;
    int turn;
    unsigned operations;
};

static uint64_t ticks(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock, &t)) abort();
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static once_flag slow_once = ONCE_FLAG_INIT;
static pthread_mutex_t once_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_changed = PTHREAD_COND_INITIALIZER;
static int once_started, waiter_ready, once_value;

static void slow_initializer(void) {
    if (pthread_mutex_lock(&once_mutex)) abort();
    once_started = 1;
    if (pthread_cond_signal(&once_changed)) abort();
    while (!waiter_ready)
        if (pthread_cond_wait(&once_changed, &once_mutex)) abort();
    if (pthread_mutex_unlock(&once_mutex)) abort();
    struct timespec delay = {0, 50000000};
    while (nanosleep(&delay, &delay))
        if (errno != EINTR) abort();
    once_value = 42;
}
static int initialize_once(void *unused) {
    (void)unused;
    call_once(&slow_once, slow_initializer);
    return once_value;
}
static struct timespec deadline(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t)) abort();
    t.tv_nsec += 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}
static void lock(struct panel *p) {
    if (p->salts ? mtx_lock(&p->mutex) : pthread_mutex_lock(&p->native)) abort();
}
static void unlock(struct panel *p) {
    if (p->salts ? mtx_unlock(&p->mutex) : pthread_mutex_unlock(&p->native)) abort();
}
static int take_locks(void *arg) {
    struct panel *p = arg;
    for (unsigned i = 0; i < LOCK_ROUNDS; ++i) {
        lock(p);
        ++p->operations;
        unlock(p);
    }
    return 0;
}
static void exchange(struct panel *p, int turn) {
    lock(p);
    while (p->turn != turn)
        if (p->salts ? cnd_wait(&p->cond, &p->mutex)
                     : pthread_cond_wait(&p->changed, &p->native)) abort();
    p->turn = 1 - turn;
    if (p->salts ? cnd_signal(&p->cond) : pthread_cond_signal(&p->changed)) abort();
    unlock(p);
}
static int respond(void *arg) {
    for (int i = 0; i < TRIPS; ++i) exchange(arg, 1);
    return 0;
}
static int timed_worker(void *arg) {
    struct panel *p = arg;
    for (int i = 0; i < TIMEOUTS; ++i) {
        struct timespec due = deadline();
        if (p->salts) {
            if (mtx_timedlock(&p->mutex, &due) != thrd_timedout) abort();
        }
#ifndef __APPLE__
        else if (pthread_mutex_timedlock(&p->native, &due) != ETIMEDOUT) abort();
#endif
    }
    return 0;
}
static void report(struct panel *p, const char *work, uint64_t wall, uint64_t cpu, unsigned n) {
    static const char *const names[] = {"pthread", "Salts-timed", "Salts-plain"};
    printf("%s %s n=%u wall_ns/op=%.1f cpu_ns/op=%.1f\n", names[p->salts],
           work, n, (double)(ticks(CLOCK_MONOTONIC) - wall) / n,
           (double)(ticks(CLOCK_PROCESS_CPUTIME_ID) - cpu) / n);
}

#ifdef __APPLE__
enum { MUTEX_TRIAL_ROUNDS = 16384, SAMPLE_STRIDE = 64 };
struct mutex_trial {
    struct panel panel;
    pthread_mutex_t start;
    pthread_cond_t ready;
    int running;
    int started;
    int mixed;
    int holds;
    struct timespec due;
};
struct mutex_runner {
    struct mutex_trial *trial;
    uint64_t *samples;
};

static int trial_locks(void *arg) {
    struct mutex_runner *worker = arg;
    struct mutex_trial *trial = worker->trial;
    struct panel *p = &trial->panel;
    if (pthread_mutex_lock(&trial->start)) abort();
    ++trial->started;
    if (pthread_cond_broadcast(&trial->ready)) abort();
    while (!trial->running)
        if (pthread_cond_wait(&trial->ready, &trial->start)) abort();
    if (pthread_mutex_unlock(&trial->start)) abort();
    for (unsigned i = 0; i < MUTEX_TRIAL_ROUNDS; ++i) {
        unsigned sample_index = i / SAMPLE_STRIDE;
        int sample = i % SAMPLE_STRIDE == (trial->mixed ? sample_index % 2 : 0);
        uint64_t begin = sample ? ticks(CLOCK_UPTIME_RAW) : 0;
        if (trial->mixed && (i & 1u)) {
            if (mtx_timedlock(&p->mutex, &trial->due)) abort();
        } else {
            lock(p);
        }
        if (sample) worker->samples[sample_index] = ticks(CLOCK_UPTIME_RAW) - begin;
        ++p->operations;
        if (trial->holds && p->operations % 16 == 0) {
            struct timespec hold = {0, 5000};
            while (nanosleep(&hold, &hold)) if (errno != EINTR) abort();
        }
        unlock(p);
    }
    return 0;
}

static int compare_latency(const void *a, const void *b) {
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static void measure_mutex_trial(enum provider provider, unsigned repeat, int workers,
                                int holds, int mixed) {
    enum { SAMPLES_PER_WORKER = MUTEX_TRIAL_ROUNDS / SAMPLE_STRIDE };
    struct mutex_trial trial = {
        .panel.salts = provider,
        .start = PTHREAD_MUTEX_INITIALIZER,
        .ready = PTHREAD_COND_INITIALIZER,
        .holds = holds,
        .mixed = mixed,
    };
    uint64_t samples[2 * SAMPLES_PER_WORKER];
    struct mutex_runner args[] = {{&trial, samples}, {&trial, samples + SAMPLES_PER_WORKER}};
    thrd_t threads[2];
    if (provider == PTHREAD ? pthread_mutex_init(&trial.panel.native, NULL) :
        mtx_init(&trial.panel.mutex, provider == SALTS_TIMED ? mtx_timed : mtx_plain)) abort();
    for (int i = 0; i < workers; ++i)
        if (thrd_create(&threads[i], trial_locks, &args[i])) abort();
    if (pthread_mutex_lock(&trial.start)) abort();
    while (trial.started != workers)
        if (pthread_cond_wait(&trial.ready, &trial.start)) abort();
    if (clock_gettime(CLOCK_REALTIME, &trial.due)) abort();
    trial.due.tv_sec += 30;
    trial.running = 1;
    uint64_t wall = ticks(CLOCK_UPTIME_RAW), cpu = ticks(CLOCK_PROCESS_CPUTIME_ID);
    if (pthread_cond_broadcast(&trial.ready) || pthread_mutex_unlock(&trial.start)) abort();
    for (int i = 0; i < workers; ++i)
        if (thrd_join(threads[i], NULL)) abort();
    wall = ticks(CLOCK_UPTIME_RAW) - wall;
    cpu = ticks(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    if (trial.panel.operations != (unsigned)workers * MUTEX_TRIAL_ROUNDS) abort();

    size_t count = (size_t)workers * SAMPLES_PER_WORKER;
    qsort(samples, count, sizeof(*samples), compare_latency);
    static const char *const names[] = {"pthread", "Salts-timed", "Salts-plain"};
    printf("Darwin mutex repeat=%u provider=%s workers=%d hold=%d mixed=%d "
           "wall_ns/op=%.1f cpu_ns/op=%.1f ops/s=%.0f p99_ns=%llu\n",
           repeat, names[provider], workers, holds, mixed,
           (double)wall / trial.panel.operations, (double)cpu / trial.panel.operations,
           (double)trial.panel.operations * 1e9 / wall,
           (unsigned long long)samples[(count * 99 + 99) / 100 - 1]);
    if (provider == PTHREAD) {
        if (pthread_mutex_destroy(&trial.panel.native)) abort();
    } else {
        mtx_destroy(&trial.panel.mutex);
    }
    if (pthread_cond_destroy(&trial.ready) || pthread_mutex_destroy(&trial.start)) abort();
}
#endif

spec("POSIX thread runtime comparison") {
    it("measures waiter CPU time during a long once initializer") {
        thrd_t owner;
        if (thrd_create(&owner, initialize_once, NULL)) abort();
        if (pthread_mutex_lock(&once_mutex)) abort();
        while (!once_started)
            if (pthread_cond_wait(&once_changed, &once_mutex)) abort();
        waiter_ready = 1;
        uint64_t wall = ticks(CLOCK_MONOTONIC), cpu = ticks(CLOCK_THREAD_CPUTIME_ID);
        if (pthread_cond_signal(&once_changed) || pthread_mutex_unlock(&once_mutex)) abort();
        call_once(&slow_once, slow_initializer);
        cpu = ticks(CLOCK_THREAD_CPUTIME_ID) - cpu;
        wall = ticks(CLOCK_MONOTONIC) - wall;
        printf("Salts once-wait wall_ns=%.0f waiter_cpu_ns=%.0f\n", (double)wall, (double)cpu);
        check_equal(once_value, 42);
        if (thrd_join(owner, NULL)) abort();
    }
    it("measures mutex, condition handoff and timed waiting against pthreads") {
        for (enum provider salts = PTHREAD; salts <= SALTS_PLAIN; ++salts) {
            struct panel p = {0};
            p.salts = salts;
            thrd_t worker;
            if (mtx_init(&p.mutex, salts == SALTS_TIMED ? mtx_timed : mtx_plain) || cnd_init(&p.cond) ||
                pthread_mutex_init(&p.native, NULL) || pthread_cond_init(&p.changed, NULL)) abort();
            for (unsigned n = 1; n <= 2; ++n) {
                p.operations = 0;
                uint64_t wall = ticks(CLOCK_MONOTONIC), cpu = ticks(CLOCK_PROCESS_CPUTIME_ID);
                if (n == 2 && thrd_create(&worker, take_locks, &p)) abort();
                take_locks(&p);
                if (n == 2 && thrd_join(worker, NULL)) abort();
                report(&p, n == 1 ? "mutex-1" : "mutex-2", wall, cpu, p.operations);
                check_equal(p.operations, n * LOCK_ROUNDS);
            }
            uint64_t wall = ticks(CLOCK_MONOTONIC), cpu = ticks(CLOCK_PROCESS_CPUTIME_ID);
            if (thrd_create(&worker, respond, &p)) abort();
            for (int i = 0; i < TRIPS; ++i) exchange(&p, 0);
            if (thrd_join(worker, NULL)) abort();
            report(&p, "condition-round-trip", wall, cpu, TRIPS);
            lock(&p);
            wall = ticks(CLOCK_MONOTONIC); cpu = ticks(CLOCK_PROCESS_CPUTIME_ID);
            for (int i = 0; i < TIMEOUTS; ++i) {
                struct timespec due = deadline();
                int rc;
                do {
                    rc = salts ? cnd_timedwait(&p.cond, &p.mutex, &due)
                               : pthread_cond_timedwait(&p.changed, &p.native, &due);
                } while (!rc);
                if (rc != (salts ? thrd_timedout : ETIMEDOUT)) abort();
            }
            report(&p, "condition-timeout", wall, cpu, TIMEOUTS);
            /* A plain C11 mutex has no timed-lock operation. Darwin also
             * has no pthread timed mutex baseline. */
            if (salts == SALTS_TIMED
#ifndef __APPLE__
                || salts == PTHREAD
#endif
            ) {
                wall = ticks(CLOCK_MONOTONIC); cpu = ticks(CLOCK_PROCESS_CPUTIME_ID);
                if (thrd_create(&worker, timed_worker, &p) || thrd_join(worker, NULL)) abort();
                report(&p, "mutex-timeout", wall, cpu, TIMEOUTS);
            }
            unlock(&p);
            if (pthread_cond_destroy(&p.changed) || pthread_mutex_destroy(&p.native)) abort();
            cnd_destroy(&p.cond);
            mtx_destroy(&p.mutex);
        }
    }
#ifdef __APPLE__
    it("repeats Darwin acquisitions with short sections, held locks and mixed timed calls") {
        for (unsigned repeat = 0; repeat < 4; ++repeat) {
            for (unsigned i = 0; i < 3; ++i) {
                enum provider provider = (enum provider)((i + repeat) % 3);
                measure_mutex_trial(provider, repeat, 1, 0, 0);
                measure_mutex_trial(provider, repeat, 2, 0, 0);
                measure_mutex_trial(provider, repeat, 2, 1, 0);
                if (provider == SALTS_TIMED) measure_mutex_trial(provider, repeat, 2, 1, 1);
            }
        }
    }
#endif
}
