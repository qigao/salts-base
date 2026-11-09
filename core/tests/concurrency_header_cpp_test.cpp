#include <deadline_queue.h>
#include <disruptor.h>
#include <spsc_ring.h>
#include <thread_pool.h>
#include <rcu.h>
#include <tinytest.hpp>
#include <type_traits>

static void lf_increment(void *arg) { ++*static_cast<int *>(arg); }

suite("Concurrency C++ headers") {
    group("compile-time API contracts") {
        it("preserves public identifier and return types") {
            static_assert(std::is_same_v<disruptor_stage_t, uint32_t>);
            static_assert(std::is_same_v<decltype(disruptor_capacity(nullptr)), uint64_t>);
            static_assert(std::is_same_v<decltype(cmeta_threadpool_size(nullptr)), int>);
            static_assert(std::is_same_v<cmeta_deadline_id, uint64_t>);
            static_assert(std::is_same_v<decltype(cmeta_spsc_ring_read_available(nullptr)), size_t>);
            static_assert(std::is_same_v<decltype(cmeta_rcu_read_unlock(nullptr)), int>);
        }
    }
    it("runs an independent C++17 consumer through the optional LF backend") {
        const cmeta_threadpool_lf_config_t config{
            sizeof(config), SALTS_THREADPOOL_LF_VERSION, 2, 3, 1};
        cmeta_threadpool_t *pool = nullptr;
        check_equal(cmeta_threadpool_create_leader_followers(&config, &pool), SALTS_OK);
        int value = 0;
        const cmeta_threadpool_task_t task{lf_increment, nullptr, nullptr, &value};
        const int submitted = cmeta_threadpool_submit_task(pool, &task);
        const int waited = cmeta_threadpool_wait_status(pool);
        cmeta_threadpool_destroy(pool);
        check_equal(submitted, SALTS_OK);
        check_equal(waited, SALTS_OK);
        check_equal(value, 1);
    }
}
