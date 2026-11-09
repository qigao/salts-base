#include <file/fs_watch.h>
#include <core/error_codes.h>
#include <threads/threads.h>
#include <file/fs.h>
#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

enum {
    WATCH_TEST_EVENT_CAPACITY = 32,
    WATCH_TEST_PATH_CAPACITY = 256
};

typedef struct watch_probe {
    fs_watch_event_kind kinds[WATCH_TEST_EVENT_CAPACITY];
    char paths[WATCH_TEST_EVENT_CAPACITY][WATCH_TEST_PATH_CAPACITY];
    char old_paths[WATCH_TEST_EVENT_CAPACITY][WATCH_TEST_PATH_CAPACITY];
    size_t count;
} watch_probe;

static fs_watch watch;
static watch_probe probe;
static char *fixture_root;
static bool fixture_root_removed;

static void watch_event(void *user, const fs_watch_event *event) {
    watch_probe *probe = (watch_probe *)user;
    if (probe != NULL && event != NULL &&
        probe->count < WATCH_TEST_EVENT_CAPACITY) {
        probe->kinds[probe->count] = event->kind;
        if (event->path != NULL) {
            strncpy(probe->paths[probe->count], event->path,
                    WATCH_TEST_PATH_CAPACITY - 1u);
        }
        if (event->old_path != NULL) {
            strncpy(probe->old_paths[probe->count], event->old_path,
                    WATCH_TEST_PATH_CAPACITY - 1u);
        }
        ++probe->count;
    }
}

static fs_watch_config watch_config(watch_probe *probe,
                                          size_t capacity) {
    fs_watch_config config = {
        .recursive = false,
        .event_capacity = capacity,
        .watch_capacity = 1u,
        .path_capacity = WATCH_TEST_PATH_CAPACITY,
        .native_buffer_capacity = 4096u,
        .event = watch_event,
        .event_user = probe,
    };
    return config;
}

static int watch_drive_until(fs_watch *watch, watch_probe *probe,
                             size_t minimum) {
    size_t attempts = 0u;
    while (probe->count < minimum && attempts++ < 5000u) {
        size_t delivered = 0u;
        int status = fs_watch_run_ready(watch, 8u, &delivered);
        if (status != SALTS_OK)
            return status;
        if (delivered == 0u)
            sleep_ms(1u);
    }
    return probe->count >= minimum ? SALTS_OK : SALTS_ETIMEDOUT;
}

static bool probe_saw(const watch_probe *probe,
                      fs_watch_event_kind kind,
                      const char *path) {
    size_t index;
    for (index = 0u; index < probe->count; ++index) {
        if (probe->kinds[index] == kind &&
            (path == NULL || strcmp(probe->paths[index], path) == 0))
            return true;
    }
    return false;
}

static size_t probe_count_kind(const watch_probe *probe,
                               fs_watch_event_kind kind) {
    size_t count = 0u;
    size_t index;
    for (index = 0u; index < probe->count; ++index) {
        if (probe->kinds[index] == kind)
            ++count;
    }
    return count;
}

static void watch_close_destroy(fs_watch *watch) {
    size_t attempts = 0u;
    int status = fs_watch_close(watch);
    check_true(status == SALTS_OK || status == SALTS_EALREADY);
    while (!fs_watch_is_quiescent(watch) && attempts++ < 5000u) {
        size_t delivered = 0u;
        check_equal(fs_watch_run_ready(watch, 8u, &delivered), SALTS_OK);
        if (delivered == 0u)
            sleep_ms(1u);
    }
    check_true(fs_watch_is_quiescent(watch));
    check_equal(fs_watch_destroy(watch), SALTS_OK);
    check_null(watch->impl);
}

spec("Native filesystem watch") {
    before_each() {
        memset(&probe, 0, sizeof(probe));
        fixture_root_removed = false;
    }
    after_each() {
        if (watch.impl != NULL) watch_close_destroy(&watch);
        if (fixture_root != NULL) {
            int removed = fixture_root_removed ? 0 : tt_remove_tree(fixture_root);
            free(fixture_root); fixture_root = NULL;
            check_equal(removed, 0);
        }
    }
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    it("reports create rename and remove through the driver") {
        char *root = fixture_root = tt_make_temp_dir("cflow-watch-");
        char first[1024];
        char second[1024];
        fs_watch_config config = watch_config(&probe, 8u);

        check_not_null(root);
        check_equal(fs_path_join(first, sizeof(first), root,
                                       "first.txt"), SALTS_OK);
        check_equal(fs_path_join(second, sizeof(second), root,
                                       "second.txt"), SALTS_OK);
        check_equal(fs_watch_open(NULL, root, &config), SALTS_EINVAL);
        check_equal(fs_watch_open(&watch, root, &config), SALTS_OK);
        check_equal(tt_write_file(first, "value", 5u), SALTS_OK);
        check_equal(watch_drive_until(&watch, &probe, 1u), SALTS_OK);
        check_true(probe_saw(&probe, SALTS_FS_WATCH_CREATED, "first.txt") ||
                   probe_saw(&probe, SALTS_FS_WATCH_MODIFIED, "first.txt"));

        check_equal(fs_rename(first, second), SALTS_OK);
        check_equal(watch_drive_until(&watch, &probe, 2u), SALTS_OK);
#if defined(_WIN32) || defined(__linux__)
        {
            size_t attempts = 0u;
            while (!probe_saw(&probe, SALTS_FS_WATCH_RENAMED, "second.txt") &&
                   attempts++ < 5000u) {
                size_t delivered = 0u;
                check_equal(fs_watch_run_ready(
                                &watch, 8u, &delivered), SALTS_OK);
                if (delivered == 0u)
                    sleep_ms(1u);
            }
            check_true(probe_saw(&probe, SALTS_FS_WATCH_RENAMED, "second.txt"));
        }
#elif defined(__APPLE__)
        {
            size_t attempts = 0u;
            while (!probe_saw(&probe,
                              SALTS_FS_WATCH_RESCAN_REQUIRED, NULL) &&
                   attempts++ < 5000u) {
                size_t delivered = 0u;
                check_equal(fs_watch_run_ready(
                                &watch, 8u, &delivered), SALTS_OK);
                if (delivered == 0u)
                    sleep_ms(1u);
            }
            check_true(probe_saw(&probe,
                                 SALTS_FS_WATCH_RESCAN_REQUIRED, NULL));
            check_equal(fs_watch_acknowledge_rescan(&watch), SALTS_OK);
        }
#endif
        check_equal(tt_remove_file(second), SALTS_OK);
        {
            size_t attempts = 0u;
#if defined(__APPLE__)
            const size_t rescans_before_remove = probe_count_kind(
                &probe, SALTS_FS_WATCH_RESCAN_REQUIRED);
            while (!probe_saw(&probe, SALTS_FS_WATCH_REMOVED, "second.txt") &&
                   probe_count_kind(&probe, SALTS_FS_WATCH_RESCAN_REQUIRED) ==
                       rescans_before_remove &&
                   attempts++ < 5000u) {
#else
            while (!probe_saw(&probe, SALTS_FS_WATCH_REMOVED, "second.txt") &&
                   attempts++ < 5000u) {
#endif
                size_t delivered = 0u;
                check_equal(fs_watch_run_ready(
                                &watch, 8u, &delivered), SALTS_OK);
                if (delivered == 0u)
                    sleep_ms(1u);
            }
#if defined(__APPLE__)
            check_true(probe_saw(&probe, SALTS_FS_WATCH_REMOVED, "second.txt") ||
                       probe_count_kind(&probe,
                           SALTS_FS_WATCH_RESCAN_REQUIRED) >
                           rescans_before_remove);
            if (probe_count_kind(&probe, SALTS_FS_WATCH_RESCAN_REQUIRED) >
                rescans_before_remove) {
                check_equal(fs_watch_acknowledge_rescan(&watch), SALTS_OK);
            }
#else
            check_true(probe_saw(&probe, SALTS_FS_WATCH_REMOVED, "second.txt"));
#endif
        }

        watch_close_destroy(&watch);
    }

    it("turns bounded queue loss into one rescan-required event") {
        char *root = fixture_root = tt_make_temp_dir("cflow-watch-overflow-");
        char first[1024];
        char second[1024];
        char during_rescan[1024];
        fs_watch_config config = watch_config(&probe, 1u);
        fs_watch_stats stats = {0};
        size_t attempts = 0u;

        check_not_null(root);
        check_equal(fs_path_join(first, sizeof(first), root,
                                       "one.txt"), SALTS_OK);
        check_equal(fs_path_join(second, sizeof(second), root,
                                       "two.txt"), SALTS_OK);
        check_equal(fs_path_join(during_rescan, sizeof(during_rescan),
                                       root, "during-rescan.txt"), SALTS_OK);
        check_equal(fs_watch_open(&watch, root, &config), SALTS_OK);
        check_equal(tt_write_file(first, "1", 1u), SALTS_OK);
        check_equal(tt_write_file(second, "2", 1u), SALTS_OK);
        while (attempts++ < 5000u) {
            check_true(fs_watch_get_stats(&watch, &stats));
            if (stats.awaiting_rescan)
                break;
            sleep_ms(1u);
        }
        check_true(stats.awaiting_rescan);
        check_equal(watch_drive_until(&watch, &probe, 2u), SALTS_OK);
        check_true(probe_saw(&probe, SALTS_FS_WATCH_RESCAN_REQUIRED, NULL));
        check_true(fs_watch_get_stats(&watch, &stats));
        {
            const size_t suppressed_at_delivery = stats.suppressed;
            attempts = 0u;
            check_equal(tt_write_file(during_rescan, "3", 1u), SALTS_OK);
            while (attempts++ < 5000u) {
                check_true(fs_watch_get_stats(&watch, &stats));
                if (stats.suppressed > suppressed_at_delivery)
                    break;
                sleep_ms(1u);
            }
            check_true(stats.suppressed > suppressed_at_delivery);
        }
        check_equal(fs_watch_acknowledge_rescan(&watch), SALTS_OK);
        check_true(fs_watch_get_stats(&watch, &stats));
        check_true(stats.awaiting_rescan);
        check_equal(watch_drive_until(&watch, &probe, 3u), SALTS_OK);
        check_equal(probe_count_kind(&probe,
                        SALTS_FS_WATCH_RESCAN_REQUIRED), (size_t)2u);
        check_equal(fs_watch_acknowledge_rescan(&watch), SALTS_OK);
        check_true(fs_watch_get_stats(&watch, &stats));

        /* Native inotify events already queued when the second marker was
         * delivered can still arrive before ACK. The contract must publish
         * another marker for that new loss rather than silently clearing it.
         * Drain those finite, already-triggered losses without assuming the
         * second ACK is necessarily the final one. */
        {
            size_t retries = 0u;
            bool settled = true;
            while (stats.awaiting_rescan &&
                   retries++ < WATCH_TEST_EVENT_CAPACITY - 2u) {
                const size_t before = probe.count;
                const size_t markers = probe_count_kind(
                    &probe, SALTS_FS_WATCH_RESCAN_REQUIRED);
                const size_t required = stats.rescan_required;
                if (before >= WATCH_TEST_EVENT_CAPACITY ||
                    watch_drive_until(&watch, &probe, before + 1u) != SALTS_OK ||
                    probe_count_kind(
                        &probe, SALTS_FS_WATCH_RESCAN_REQUIRED) != markers + 1u ||
                    fs_watch_acknowledge_rescan(&watch) != SALTS_OK ||
                    !fs_watch_get_stats(&watch, &stats) ||
                    stats.rescan_required < required) {
                    settled = false;
                    break;
                }
            }
            settled = settled && !stats.awaiting_rescan;
            watch_close_destroy(&watch);
            check_true(settled);
        }
    }

    it("reports deletion of the watched root") {
        char *root = fixture_root = tt_make_temp_dir("cflow-watch-root-");
        fs_watch_config config = watch_config(&probe, 8u);

        check_not_null(root);
        check_equal(fs_watch_open(&watch, root, &config), SALTS_OK);
        check_equal(tt_remove_tree(root), SALTS_OK);
        fixture_root_removed = true;
        check_equal(watch_drive_until(&watch, &probe, 1u), SALTS_OK);
        check_true(probe_saw(&probe, SALTS_FS_WATCH_ROOT_CHANGED, NULL));

        watch_close_destroy(&watch);
    }

#else
    it("fails fast when the native watch backend is unavailable") {
        char *root = fixture_root = tt_make_temp_dir("cflow-watch-unsupported-");
        fs_watch_config config = watch_config(&probe, 1u);

        check_not_null(root);
        check_equal(fs_watch_open(&watch, root, &config), SALTS_ENOTSUP);
        check_null(watch.impl);
    }
#endif

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    it("reports recursive descendants with normalized relative paths") {
        char *root = fixture_root = tt_make_temp_dir("cflow-watch-recursive-");
        char nested[1024];
        char child[1024];
        char dynamic[1024];
        char dynamic_child[1024];
        fs_watch_config config = watch_config(&probe, 8u);
        size_t attempts = 0u;

        config.recursive = true;
        config.watch_capacity = 8u;
        check_not_null(root);
        check_equal(fs_path_join(nested, sizeof(nested), root,
                                       "nested"), SALTS_OK);
        check_equal(fs_path_join(child, sizeof(child), nested,
                                       "child.txt"), SALTS_OK);
        check_equal(fs_path_join(dynamic, sizeof(dynamic), root,
                                       "dynamic"), SALTS_OK);
        check_equal(fs_path_join(dynamic_child, sizeof(dynamic_child),
                                       dynamic, "later.txt"), SALTS_OK);
        check_equal(tt_make_dir(nested), SALTS_OK);
        check_equal(fs_watch_open(&watch, root, &config), SALTS_OK);
        check_equal(tt_write_file(child, "x", 1u), SALTS_OK);
        while (!probe_saw(&probe, SALTS_FS_WATCH_CREATED,
                          "nested/child.txt") && attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(fs_watch_run_ready(
                            &watch, 8u, &delivered), SALTS_OK);
            if (delivered == 0u)
                sleep_ms(1u);
        }
        check_true(probe_saw(&probe, SALTS_FS_WATCH_CREATED,
                             "nested/child.txt"));
        check_equal(tt_make_dir(dynamic), SALTS_OK);
        attempts = 0u;
        while (!probe_saw(&probe, SALTS_FS_WATCH_CREATED, "dynamic") &&
               attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(fs_watch_run_ready(
                            &watch, 8u, &delivered), SALTS_OK);
            if (delivered == 0u)
                sleep_ms(1u);
        }
        check_true(probe_saw(&probe, SALTS_FS_WATCH_CREATED, "dynamic"));
        check_equal(tt_write_file(dynamic_child, "y", 1u), SALTS_OK);
        attempts = 0u;
        while (!probe_saw(&probe, SALTS_FS_WATCH_CREATED,
                          "dynamic/later.txt") && attempts++ < 5000u) {
            size_t delivered = 0u;
            check_equal(fs_watch_run_ready(
                            &watch, 8u, &delivered), SALTS_OK);
            if (delivered == 0u)
                sleep_ms(1u);
        }
        check_true(probe_saw(&probe, SALTS_FS_WATCH_CREATED,
                             "dynamic/later.txt"));
        watch_close_destroy(&watch);
    }
#endif
}
