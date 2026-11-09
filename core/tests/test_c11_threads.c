/* Standard C11 threads.h client of the Salts implementation.
 * Compiles with GCC/MSVC/Clang and makes no native _Atomic layout assumptions.
 */
#include <tinytest.h>
#include <threads/threads.h>
#include "thread_backend.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum { WORKERS=4, LOOPS=1200 };
static mtx_t gate;
static cnd_t ready_condition;
static int go;
static once_flag once = ONCE_FLAG_INIT;
static int once_count;
static atomic32 finished = {0};
static atomic32 increments = {0};

static void initialize_once(void) { ++once_count; }
static int worker(void *arg) {
    uintptr_t index = (uintptr_t)arg;
    unsigned i;
    uint32_t previous;
    thrd_t me = thrd_current();
    if (!me || !thrd_equal(me,thrd_current())) return -1;
    call_once(&once,initialize_once);
    if (mtx_lock(&gate) != thrd_success) return -2;
    while (!go)
        if (cnd_wait(&ready_condition,&gate) != thrd_success) return -3;
    if (mtx_unlock(&gate) != thrd_success) return -4;
    for (i=0;i<LOOPS;i++)
        if (atomic32_fetch_add(&increments,1u,SALTS_MEMORY_RELAXED,&previous))
            return -5;
    return (int)(index+17u);
}
static int early(void *arg) {
    (void)arg;
    thrd_exit(81);
}
static int detached(void *arg) {
    (void)arg;
    if (atomic32_store(&finished,1u,SALTS_MEMORY_RELEASE)) return -1;
    return 0;
}
static int saltscore_legacy_entry_test_c11_threads_c(void) {
    thrd_t threads[WORKERS], one, loose;
    uint32_t count=0, done=0;
    struct timespec expired={0,0};
    struct timespec sleep_time={0,2000000L}, remaining={0,0};
    struct timespec deadline;
    int i, result;
    if (mtx_init(&gate,mtx_timed) || cnd_init(&ready_condition)) return 2;
    if (mtx_lock(&gate)) return 3;
    if (mtx_trylock(&gate) != thrd_busy) return 4;
    if (mtx_timedlock(&gate,&expired) != thrd_timedout) return 5;
    if (cnd_timedwait(&ready_condition,&gate,&expired) != thrd_timedout) return 6;
    if (mtx_unlock(&gate)) return 7;
    if (mtx_trylock(&gate) || mtx_unlock(&gate)) return 8;
    deadline.tv_sec=time(NULL)+3;
    deadline.tv_nsec=0;
    if (mtx_timedlock(&gate,&deadline) || mtx_unlock(&gate)) return 9;
    for (i=0;i<WORKERS;i++) {
        int j;
        threads[i]=NULL;
        if (thrd_create(&threads[i],worker,(void *)(uintptr_t)i)) return 10;
        if (thrd_equal(threads[i], thrd_current())) return 22;
        for (j=0; j<i; j++)
            if (thrd_equal(threads[i],threads[j])) return 23;
    }
    if (mtx_lock(&gate)) return 11;
    go=1;
    if (cnd_broadcast(&ready_condition) || mtx_unlock(&gate)) return 12;
    for (i=0;i<WORKERS;i++) {
        if (thrd_join(threads[i],&result) || result!=i+17) return 13;
    }
    if (once_count!=1 || atomic32_load(&increments,SALTS_MEMORY_ACQUIRE,&count) ||
        count != WORKERS*LOOPS) return 14;
    if (thrd_create(&one,early,NULL)) return 15;
    if (thrd_join(one,&result) || result!=81) return 16;
    if (thrd_create(&loose,detached,NULL) || thrd_detach(loose)) return 17;
    for (i=0;i<200000;i++) {
        if (atomic32_load(&finished,SALTS_MEMORY_ACQUIRE,&done)) return 18;
        if (done) break;
        thrd_yield();
    }
    if (!done) return 19;
    if (thrd_sleep(&sleep_time,&remaining) || remaining.tv_sec || remaining.tv_nsec) return 20;
    if (thrd_sleep(NULL,&remaining) != -2) return 21;
    cnd_destroy(&ready_condition);
    mtx_destroy(&gate);
    puts("C11 threads: join/detach/early-exit/current/equal/mutex/condition/once PASS");
    return 0;
}

/* Keep the native contract's original exit codes and cleanup paths intact;
 * surface its exact nonzero result as one registered TinyTest failure. */
spec("C11 native thread synchronization") {
  it("preserves the original native exit and lifetime contract") {
    const int result = saltscore_legacy_entry_test_c11_threads_c();
    check(result == 0, "original native contract returned %d", result);
  }
}

static int try_mutex(void *arg) {
    mtx_t *mutex = (mtx_t *)arg;
    int result = mtx_trylock(mutex);
    if (result == thrd_success && mtx_unlock(mutex) != thrd_success)
        return thrd_error;
    return result;
}

struct recursive_wait {
    mtx_t mutex;
    cnd_t condition;
    int published;
};
static int publish_recursive(void *arg) {
    struct recursive_wait *state = (struct recursive_wait *)arg;
    if (mtx_lock(&state->mutex) != thrd_success) return 1;
    state->published = 1;
    if (cnd_signal(&state->condition) != thrd_success) return 2;
    return mtx_unlock(&state->mutex);
}

static tss_t destructor_key;
static int destructor_calls;
static int destructor_saw_clear;
static int destructor_set_failed;
static int destructor_payload;
static thread_local int local_marker = 17;

static void counting_destructor(void *value) {
    if (value == &destructor_payload) ++destructor_calls;
}
static void reinstall_destructor(void *value) {
    ++destructor_calls;
    if (tss_get(destructor_key) == NULL) ++destructor_saw_clear;
    if (tss_set(destructor_key, value) != thrd_success) ++destructor_set_failed;
}
static void delete_self_destructor(void *value) {
    if (value == &destructor_payload) ++destructor_calls;
    tss_delete(destructor_key);
}
static int use_tss(void *arg) {
    if (tss_get(destructor_key) != NULL || local_marker != 17) return -1;
    local_marker = 29;
    if (tss_set(destructor_key, &destructor_payload) != thrd_success) return -2;
    if (arg) thrd_exit(42);
    return 41;
}

struct delete_tss_state {
    mtx_t mutex;
    cnd_t condition;
    tss_t key;
    int ready;
    int done;
};
static int hold_deleted_key(void *arg) {
    struct delete_tss_state *state = (struct delete_tss_state *)arg;
    if (tss_set(state->key, &destructor_payload) != thrd_success) return 1;
    if (mtx_lock(&state->mutex) != thrd_success) return 2;
    state->ready = 1;
    if (cnd_signal(&state->condition) != thrd_success) return 3;
    while (!state->done)
        if (cnd_wait(&state->condition, &state->mutex) != thrd_success) return 4;
    return mtx_unlock(&state->mutex);
}

spec("C11 standard thread contracts") {
    it("supports every standard mutex type and balances recursive ownership") {
        const int types[] = {mtx_plain, mtx_timed,
            mtx_plain | mtx_recursive, mtx_timed | mtx_recursive};
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            mtx_t mutex;
            thrd_t observer;
            int result = -1;
            const struct timespec expired = {0, 0};
            check_equal(mtx_init(&mutex, types[i]), thrd_success);
            check_equal(mtx_lock(&mutex), thrd_success);
            if ((types[i] & mtx_recursive) != 0) {
                check_equal(mtx_lock(&mutex), thrd_success);
                check_equal(mtx_trylock(&mutex), thrd_success);
                check_equal(mtx_unlock(&mutex), thrd_success);
                if ((types[i] & mtx_timed) != 0) {
                    check_equal(mtx_timedlock(&mutex, &expired), thrd_success);
                    check_equal(mtx_unlock(&mutex), thrd_success);
                }
                check_equal(mtx_unlock(&mutex), thrd_success);
            }
            check_equal(thrd_create(&observer, try_mutex, &mutex), thrd_success);
            check_equal(thrd_join(observer, &result), thrd_success);
            check_equal(result, thrd_busy);
            check_equal(mtx_unlock(&mutex), thrd_success);
            check_equal(thrd_create(&observer, try_mutex, &mutex), thrd_success);
            check_equal(thrd_join(observer, &result), thrd_success);
            check_equal(result, thrd_success);
            if ((types[i] & mtx_timed) != 0) {
                struct timespec distant = {0, 0};
                distant.tv_sec = time(NULL) + 365 * 24 * 60 * 60;
                check_equal(mtx_timedlock(&mutex, &distant), thrd_success);
                check_equal(mtx_unlock(&mutex), thrd_success);
            }
            mtx_destroy(&mutex);
        }
    }
    it("waits with recursive ownership and restores the acquisition count") {
        struct recursive_wait state = {0};
        thrd_t publisher;
        int result = -1;
        struct timespec distant = {0, 0};
        distant.tv_sec = time(NULL) + 365 * 24 * 60 * 60;
        check_equal(mtx_init(&state.mutex, mtx_recursive | mtx_timed), thrd_success);
        check_equal(cnd_init(&state.condition), thrd_success);
        check_equal(mtx_lock(&state.mutex), thrd_success);
        check_equal(mtx_lock(&state.mutex), thrd_success);
        check_equal(thrd_create(&publisher, publish_recursive, &state), thrd_success);
        while (!state.published)
            check_equal(cnd_timedwait(&state.condition, &state.mutex, &distant), thrd_success);
        check_equal(mtx_unlock(&state.mutex), thrd_success);
        check_equal(mtx_unlock(&state.mutex), thrd_success);
        check_equal(thrd_join(publisher, &result), thrd_success);
        check_equal(result, thrd_success);
        cnd_destroy(&state.condition);
        mtx_destroy(&state.mutex);
    }
    it("replaces and clears TSS values without invoking the destructor") {
        int replacement = 0;
        destructor_calls = 0;
        check_equal(tss_create(&destructor_key, counting_destructor), thrd_success);
        check(tss_get(destructor_key) == NULL);
        check_equal(tss_set(destructor_key, &destructor_payload), thrd_success);
        check_equal(tss_set(destructor_key, &replacement), thrd_success);
        check(tss_get(destructor_key) == &replacement);
        check_equal(tss_set(destructor_key, NULL), thrd_success);
        check(tss_get(destructor_key) == NULL);
        check_equal(destructor_calls, 0);
        tss_delete(destructor_key);
    }
    it("runs and bounds destructors on both return and thrd_exit") {
        for (int early_exit = 0; early_exit < 2; ++early_exit) {
            thrd_t thread;
            int result = -1;
            destructor_calls = destructor_saw_clear = destructor_set_failed = 0;
            check_equal(tss_create(&destructor_key, reinstall_destructor), thrd_success);
            check_equal(thrd_create(&thread, use_tss, early_exit ? &result : NULL), thrd_success);
            check_equal(thrd_join(thread, &result), thrd_success);
            check_equal(result, early_exit ? 42 : 41);
            check_equal(destructor_calls, TSS_DTOR_ITERATIONS);
            check_equal(destructor_saw_clear, TSS_DTOR_ITERATIONS);
            check_equal(destructor_set_failed, 0);
            check_equal(local_marker, 17);
            tss_delete(destructor_key);
        }
    }
    it("also cleans TSS installed by a thread created through the platform API") {
        platform_thread *thread = NULL;
        int result = -1;
        destructor_calls = destructor_saw_clear = destructor_set_failed = 0;
        check_equal(tss_create(&destructor_key, reinstall_destructor), thrd_success);
        check_equal(platform_thread_create(&thread, use_tss, NULL), SALTS_OK);
        check_equal(platform_thread_join(&thread, &result), SALTS_OK);
        check_equal(result, 41);
        check_equal(destructor_calls, TSS_DTOR_ITERATIONS);
        check_equal(destructor_saw_clear, TSS_DTOR_ITERATIONS);
        check_equal(destructor_set_failed, 0);
        tss_delete(destructor_key);
    }
    it("deletes a key with values on a live thread without running its destructor") {
        struct delete_tss_state state = {0};
        thrd_t thread;
        int result = -1;
        destructor_calls = 0;
        check_equal(mtx_init(&state.mutex, mtx_plain), thrd_success);
        check_equal(cnd_init(&state.condition), thrd_success);
        check_equal(tss_create(&state.key, counting_destructor), thrd_success);
        check_equal(mtx_lock(&state.mutex), thrd_success);
        check_equal(thrd_create(&thread, hold_deleted_key, &state), thrd_success);
        while (!state.ready)
            check_equal(cnd_wait(&state.condition, &state.mutex), thrd_success);
        tss_delete(state.key);
        check_equal(destructor_calls, 0);
        state.done = 1;
        check_equal(cnd_signal(&state.condition), thrd_success);
        check_equal(mtx_unlock(&state.mutex), thrd_success);
        check_equal(thrd_join(thread, &result), thrd_success);
        check_equal(result, thrd_success);
        check_equal(destructor_calls, 0);
        cnd_destroy(&state.condition);
        mtx_destroy(&state.mutex);
    }
    it("keeps key metadata alive when its destructor deletes the key") {
        thrd_t thread;
        int result = -1;
        destructor_calls = 0;
        check_equal(tss_create(&destructor_key, delete_self_destructor), thrd_success);
        check_equal(thrd_create(&thread, use_tss, NULL), thrd_success);
        check_equal(thrd_join(thread, &result), thrd_success);
        check_equal(result, 41);
        check_equal(destructor_calls, 1);
    }
    it("supports NULL destructors and reclaims deleted current-thread slots") {
        for (unsigned i = 0; i < 2048; ++i) {
            tss_t key;
            check_equal(tss_create(&key, NULL), thrd_success);
            check_equal(tss_set(key, &destructor_payload), thrd_success);
            check(tss_get(key) == &destructor_payload);
            tss_delete(key);
        }
    }
}
