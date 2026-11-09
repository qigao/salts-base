#include <salts/rcu.h>
#include "tinytest.hpp"
#include <type_traits>

SALTS_RCU_TYPE(IntRcu, int);
static_assert(std::is_same_v<IntRcu_value_type, int>);
static_assert(std::is_same_v<decltype(IntRcu_load(nullptr)), const int *>);

suite("Concurrency typed RCU in C++") {
    it("pins canonical snapshots and returns payload ownership explicitly") {
        int first = 1, second = 2;
        IntRcu domain{};
        IntRcu_guard guard{};
        int *out = &second;
        check_equal(IntRcu_init(&domain, &first, size_t{1}), SALTS_OK);
        check_equal(IntRcu_read_lock(&domain, &guard), SALTS_OK);
        check_equal(IntRcu_replace(&domain, &second), SALTS_OK);
        check_true(IntRcu_load(&guard) == &first);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_EBUSY);
        check_true(out == nullptr);
        auto copied = guard;
        check_true(IntRcu_load(&copied) == nullptr);
        check_equal(IntRcu_read_unlock(&copied), SALTS_EINVAL);
        check_equal(IntRcu_read_unlock(&guard), SALTS_OK);
        check_equal(IntRcu_try_reclaim(&domain, &out), SALTS_OK);
        check_true(out == &first);
        check_equal(IntRcu_close(&domain), SALTS_OK);
        check_equal(IntRcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == &second);
        check_equal(IntRcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == nullptr);
    }
    it("rejects invalid admission without acquiring payload ownership") {
        int value = 1;
        IntRcu domain{};
        int *out = &value;
        check_equal(IntRcu_init(&domain, &value, size_t{0}), SALTS_EINVAL);
        check_equal(IntRcu_read_lock(nullptr, nullptr), SALTS_EINVAL);
        check_true(IntRcu_load(nullptr) == nullptr);
        check_equal(IntRcu_try_reclaim(nullptr, &out), SALTS_EINVAL);
        check_true(out == nullptr);
        check_equal(IntRcu_destroy(&domain, nullptr), SALTS_EINVAL);
        check_equal(IntRcu_destroy(&domain, &out), SALTS_OK);
        check_true(out == nullptr);
    }
}
