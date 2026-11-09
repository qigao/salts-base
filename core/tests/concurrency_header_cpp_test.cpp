#include <mutex>
#include <salts.h>
#include <threads/threads.h>
#include <queue/deadline_queue.h>
#include <queue/disruptor.h>
#include <queue/spsc_ring.h>
#include <threads/thread_pool.h>
#include <core/rcu.h>
#include <tinytest.hpp>
#include <type_traits>

static void lf_increment(void *arg) { ++*static_cast<int *>(arg); }
static int merged_once_calls;
static void increment_merged_once() { ++merged_once_calls; }

suite("Concurrency C++ headers") {
    it("combines Salts and C11 primitives without changing previously included C++ once APIs") {
        std::once_flag standard;
        once_flag c11 = ONCE_FLAG_INIT;
        sync_once_t native = SALTS_ONCE_INIT;
        int standard_calls = 0;
        merged_once_calls = 0;
        std::call_once(standard, [&] { ++standard_calls; });
        std::call_once(standard, [&] { ++standard_calls; });
        ::call_once(&c11, increment_merged_once);
        ::call_once(&c11, increment_merged_once);
        sync_once(&native, increment_merged_once);
        sync_once(&native, increment_merged_once);
        check_equal(standard_calls, 1);
        check_equal(merged_once_calls, 2);
        check_equal(thrd_equal(thrd_current(), thrd_current()), 1);
        sync_mutex_t mutex = nullptr;
        sync_mutex_init(&mutex);
        check(mutex != nullptr);
        sync_mutex_lock(&mutex);
        sync_mutex_unlock(&mutex);
        sync_mutex_destroy(&mutex);
        check(mutex == nullptr);
    }
    group("compile-time API contracts") {
        it("preserves public identifier and return types") {
            static_assert(std::is_same_v<disruptor_stage_t, uint32_t>);
            static_assert(std::is_same_v<decltype(disruptor_capacity(nullptr)), uint64_t>);
            static_assert(std::is_same_v<decltype(threadpool_size(nullptr)), int>);
            static_assert(std::is_same_v<deadline_id, uint64_t>);
            static_assert(std::is_same_v<decltype(spsc_ring_read_available(nullptr)), size_t>);
            static_assert(std::is_same_v<decltype(rcu_read_unlock(nullptr)), int>);
        }
    }
    it("runs an independent C++17 consumer through the optional LF backend") {
        const threadpool_lf_config_t config{
            sizeof(config), SALTS_THREADPOOL_LF_VERSION, 2, 3, 1};
        threadpool_t *pool = nullptr;
        check_equal(threadpool_create_leader_followers(&config, &pool), SALTS_OK);
        int value = 0;
        const threadpool_task_t task{lf_increment, nullptr, nullptr, &value};
        const int submitted = threadpool_submit_task(pool, &task);
        const int waited = threadpool_wait_status(pool);
        threadpool_destroy(pool);
        check_equal(submitted, SALTS_OK);
        check_equal(waited, SALTS_OK);
        check_equal(value, 1);
    }
}
