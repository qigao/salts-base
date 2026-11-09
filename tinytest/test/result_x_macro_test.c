#include "tinytest.h"

suite("test result x-macro mapping") {
  it("keeps stable enum values") {
    check_equal(TTEST_RESULT_PENDING__, 0);
    check_equal(TTEST_RESULT_PASSED__, 1);
    check_equal(TTEST_RESULT_FAILED__, 2);
    check_equal(TTEST_RESULT_SKIPPED__, 3);
    check_equal(TTEST_RESULT_EXPECTED_FAIL__, 4);
    check_equal(TTEST_RESULT_UNEXPECTED_PASS__, 5);
    check_equal(TTEST_RESULT_FILTERED__, 6);
  }

  it("maps results to display names") {
    check_equal(ttest_test_result_name__(TTEST_RESULT_PENDING__), "pending");
    check_equal(ttest_test_result_name__(TTEST_RESULT_PASSED__), "passed");
    check_equal(ttest_test_result_name__(TTEST_RESULT_FAILED__), "failed");
    check_equal(ttest_test_result_name__(TTEST_RESULT_SKIPPED__), "skipped");
    check_equal(ttest_test_result_name__(TTEST_RESULT_EXPECTED_FAIL__), "expected_fail");
    check_equal(ttest_test_result_name__(TTEST_RESULT_UNEXPECTED_PASS__), "unexpected_pass");
    check_equal(ttest_test_result_name__(TTEST_RESULT_FILTERED__), "filtered");
  }

  it("maps results to summary categories") {
    check_equal(ttest_test_result_category__(TTEST_RESULT_PASSED__),
                 TTEST_RESULT_CATEGORY_PASS__);
    check_equal(ttest_test_result_category__(TTEST_RESULT_EXPECTED_FAIL__),
                 TTEST_RESULT_CATEGORY_PASS__);
    check_equal(ttest_test_result_category__(TTEST_RESULT_FAILED__),
                 TTEST_RESULT_CATEGORY_FAIL__);
    check_equal(ttest_test_result_category__(TTEST_RESULT_SKIPPED__),
                 TTEST_RESULT_CATEGORY_SKIP__);
    check_equal(ttest_test_result_category__(TTEST_RESULT_FILTERED__),
                 TTEST_RESULT_CATEGORY_FILTER__);
    check_equal(ttest_test_result_category__(TTEST_RESULT_UNEXPECTED_PASS__),
                 TTEST_RESULT_CATEGORY_TODO__);

    check_true(ttest_test_result_is_skip__(TTEST_RESULT_SKIPPED__));
    check_true(ttest_test_result_is_skip__(TTEST_RESULT_FILTERED__));
    check_true(ttest_test_result_is_fail__(TTEST_RESULT_FAILED__));
    check_true(ttest_test_result_is_fail__(TTEST_RESULT_UNEXPECTED_PASS__));
  }
}
