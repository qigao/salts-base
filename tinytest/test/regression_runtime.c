/* The symlink regression needs POSIX declarations before any libc header. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "tinytest.h"

#if defined(columns) || defined(lines) || defined(buttons) || defined(tab)
#error "tinytest.h must not leak terminal capability macros"
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef _WIN32
static char *tt_join_path__(const char *dir, const char *name) {
  size_t len = strlen(dir) + strlen(name) + 2;
  char *path = (char *)malloc(len);
  if (!path) return NULL;
#ifdef _WIN32
  snprintf(path, len, "%s\\%s", dir, name);
#else
  snprintf(path, len, "%s/%s", dir, name);
#endif
  return path;
}
#endif

static int fixture_body_runs;
static int fixture_cleanup_runs;

spec("tinytest runtime regression") {
  it("check_equal_warn should evaluate operands once") {
    int value = 1;
    check_equal_warn(value++, 1);
    check_equal(value, 2);
  }

  it("check_equal_warn should evaluate length once") {
    unsigned char actual[2] = {0, 0};
    unsigned char expected[2] = {0, 0};
    size_t len = 1;
    check_equal_warn(actual, expected, len++);
    check_equal(len, 2);
  }

  it("check_warn should behave as a single statement") {
    if (1)
      check_warn(1);
    else
      check_warn(0);
    check_true(1);
  }

  it("benchmark metrics should keep samples, operations, and bytes distinct") {
    ttest_bench_entry__ entry =
        ttest_bench_make_entry__("batched io", 4, 8.0, 1.5, 2.5, 10, 1024 * 1024, true);

    check_equal(entry.samples, 4);
    check_equal(entry.operations_per_sample, 10);
    check_equal(entry.bytes_per_sample, 1024 * 1024);
    check_true(entry.tracks_bytes);
    check_within(entry.avg_op_us, 200.0, 0.001);
    check_within(entry.avg_sample_us, 2000.0, 0.001);
    check_within(entry.min_sample_us, 1500.0, 0.001);
    check_within(entry.max_sample_us, 2500.0, 0.001);
    check_within(entry.ops_s, 5000.0, 0.001);
    check_within(entry.mib_s, 500.0, 0.001);
  }

  group("fixture assertion handling") {
    before_each() { check_equal(1, 2); }
    after_each() { ++fixture_cleanup_runs; }

    it_should_fail("should attribute setup failures to the test") { ++fixture_body_runs; }
  }

  it("should skip a body after setup failure and still run cleanup") {
    check_equal(fixture_body_runs, 0);
    check_equal(fixture_cleanup_runs, 1);
  }

  group("cleanup assertion handling") {
    after_each() { check_equal(1, 2); }

    it_should_fail("should attribute cleanup failures to the test") { check_true(1); }
  }

#ifndef _WIN32
  it("tt_remove_tree should not follow directory symlinks") {
    char *root = tt_make_temp_dir("ttroot");
    char *outside = tt_make_temp_dir("ttout");
    char *link = NULL;
    char *sentinel = NULL;
    size_t size = 0;
    char *content = NULL;

    check_not_null(root);
    check_not_null(outside);

    link = tt_join_path__(root, "linked");
    sentinel = tt_join_path__(outside, "sentinel.txt");

    check_not_null(link);
    check_not_null(sentinel);
    check_equal(tt_write_file(sentinel, "x", 1), 0);
    check_equal(symlink(outside, link), 0);
    check_equal(tt_remove_tree(root), 0);

    content = tt_read_file(sentinel, &size);
    check_not_null(content);
    check_equal(size, 1);

    free(content);
    check_equal(tt_remove_tree(outside), 0);
    free(root);
    free(outside);
    free(link);
    free(sentinel);
  }
#else
  it_skip("tt_remove_tree should not follow directory symlinks") {
    check_true(1);
  }
#endif
}
