#include <fs_async.h>
#include <error_codes.h>
#include <thread.h>
#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

enum { FS_TEST_COMPLETION_CAPACITY = 32 };

typedef struct fs_completion_probe {
    uint64_t request_ids[FS_TEST_COMPLETION_CAPACITY];
    salts_fs_operation_kind operations[FS_TEST_COMPLETION_CAPACITY];
    int results[FS_TEST_COMPLETION_CAPACITY];
    size_t count;
    salts_fs_service *reentrant_service;
    int reentrant_status;
} fs_completion_probe;

/* Worker borrows must survive TinyTest's failure longjmp until after_each drains. */
static salts_fs_service service;
static fs_completion_probe probe;
static char *fixture_path;
static char fixture_original[1024];
static char *fs_fixture_path(bool directory) {
    fixture_path = directory ? tt_make_temp_dir("salts-fs-") : tt_make_temp_file("salts-fs-", ".txt");
    if (fixture_path == NULL) return NULL;
    size_t length = strlen(fixture_path);
    if (length >= sizeof(fixture_original)) {
        tt_remove_tree(fixture_path);
        free(fixture_path); fixture_path = NULL;
        return NULL;
    }
    memcpy(fixture_original, fixture_path, length + 1u);
    return fixture_path;
}

static void contract_completion(void *user, uint64_t request_id,
                                salts_fs_operation_kind operation,
                                int result) {
    fs_completion_probe *probe = (fs_completion_probe *)user;
    if (probe != NULL && probe->count < FS_TEST_COMPLETION_CAPACITY) {
        probe->request_ids[probe->count] = request_id;
        probe->operations[probe->count] = operation;
        probe->results[probe->count] = result;
        ++probe->count;
    }
    if (probe != NULL && probe->reentrant_service != NULL) {
        salts_fs_service *service = probe->reentrant_service;
        size_t completed = 0u;
        probe->reentrant_service = NULL;
        probe->reentrant_status = salts_fs_run_ready(
            service, 1u, &completed);
    }
}

static salts_fs_config fs_test_config(fs_completion_probe *probe,
                                      size_t capacity) {
    salts_fs_config config = {
        .worker_count = 1u,
        .request_capacity = capacity,
        .path_capacity = 1024u,
        .completion = contract_completion,
        .completion_user = probe,
    };
    return config;
}

static int fs_wait(salts_fs_service *service, fs_completion_probe *probe,
                   size_t expected) {
    size_t attempts = 0u;
    while (probe->count < expected && attempts++ < 5000u) {
        size_t completed = 0u;
        int status = salts_fs_run_ready(service, 8u, &completed);
        if (status != SALTS_OK)
            return status;
        if (completed == 0u)
            cmeta_sleep_ms(1u);
    }
    return probe->count >= expected ? SALTS_OK : SALTS_ETIMEDOUT;
}

static void fs_close_destroy(salts_fs_service *service,
                             fs_completion_probe *probe) {
    size_t attempts = 0u;
    int close_status = salts_fs_service_close(service);
    check_true(close_status == SALTS_OK || close_status == SALTS_EALREADY);
    while (!salts_fs_is_quiescent(service) && attempts++ < 5000u) {
        size_t completed = 0u;
        check_equal(salts_fs_run_ready(service, 8u, &completed), SALTS_OK);
        if (completed == 0u)
            cmeta_sleep_ms(1u);
    }
    check_true(salts_fs_is_quiescent(service));
    check_equal(salts_fs_destroy(service), SALTS_OK);
    check_null(service->impl);
    (void)probe;
}

spec("Native filesystem service") {
    before_each() { memset(&probe, 0, sizeof(probe)); }
    after_each() {
        if (service.impl != NULL) fs_close_destroy(&service, &probe);
        if (fixture_path != NULL) {
            int removed = tt_remove_tree(fixture_original);
            free(fixture_path); fixture_path = NULL;
            check_equal(removed, 0);
        }
    }
describe("lifecycle and metadata") {
    it("validates configuration and asynchronously stats copied paths") {
        char *path = fs_fixture_path(false);
        salts_fs_config config = fs_test_config(&probe, 2u);
        static salts_fs_stat_t stat_result = {0};
        salts_fs_submit_result submitted;

        check_not_null(path);
        check_equal(tt_write_file(path, "value", 5u), 0);
        check_equal(salts_fs_service_init(NULL, &config), SALTS_EINVAL);
        check_equal(salts_fs_service_init(&service, NULL), SALTS_EINVAL);
        config.worker_count = 0u;
        check_equal(salts_fs_service_init(&service, &config), SALTS_EINVAL);
        config = fs_test_config(&probe, 2u);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);
        check_equal(salts_fs_destroy(&service), SALTS_EBUSY);

        submitted = salts_fs_try_stat(&service, path, &stat_result);
        check_equal(submitted.status, SALTS_FS_SUBMIT_ACCEPTED);
        memset(path, 'x', strlen(path));
        check_equal(fs_wait(&service, &probe, 1u), SALTS_OK);
        check_equal(probe.operations[0], SALTS_FS_STAT);
        check_equal(probe.results[0], SALTS_OK);
        check_equal(stat_result.size, (uint64_t)5u);
        check_true(stat_result.is_file);

        fs_close_destroy(&service, &probe);
    }

    it("rejects over-capacity paths before admission") {
        salts_fs_config config = fs_test_config(&probe, 1u);
        static salts_fs_stat_t stat_result = {0};
        char path[4] = {'a', 'b', 'c', '\0'};

        config.path_capacity = sizeof(path);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);
        check_equal(salts_fs_try_stat(&service, path, &stat_result).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), SALTS_OK);
        path[3] = 'd';
        check_equal(salts_fs_try_stat(&service, path, &stat_result).status,
                    SALTS_FS_SUBMIT_INVALID_ARGUMENT);
        fs_close_destroy(&service, &probe);
    }

    it("settles an accepted close race exactly once") {
        char *path = fs_fixture_path(false);
        salts_fs_config config = fs_test_config(&probe, 1u);
        static salts_fs_stat_t stat_result = {0};
        salts_fs_submit_result submitted;
        size_t attempts = 0u;

        check_not_null(path);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);
        submitted = salts_fs_try_stat(&service, path, &stat_result);
        check_equal(submitted.status, SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(salts_fs_service_close(&service), SALTS_OK);
        while (!salts_fs_is_quiescent(&service) && attempts++ < 5000u) {
            size_t completed = 0u;
            check_equal(salts_fs_run_ready(&service, 1u, &completed), SALTS_OK);
            if (completed == 0u)
                cmeta_sleep_ms(1u);
        }
        check_equal(probe.count, (size_t)1u);
        check_equal(probe.request_ids[0], submitted.request_id);
        check_true(probe.results[0] == SALTS_OK ||
                   probe.results[0] == SALTS_ECANCELED);
        check_true(salts_fs_is_quiescent(&service));
        check_equal(salts_fs_destroy(&service), SALTS_OK);
    }

    it("keeps capacity occupied until the driver delivers completion") {
        char *path = fs_fixture_path(false);
        salts_fs_config config = fs_test_config(&probe, 1u);
        static salts_fs_stat_t first = {0};
        static salts_fs_stat_t second = {0};
        salts_fs_submit_result submitted;
        salts_fs_stats stats = {0};

        check_not_null(path);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);
        submitted = salts_fs_try_stat(&service, path, &first);
        check_equal(submitted.status, SALTS_FS_SUBMIT_ACCEPTED);
        probe.reentrant_service = &service;
        submitted = salts_fs_try_lstat(&service, path, &second);
        check_equal(submitted.status, SALTS_FS_SUBMIT_FULL);
        check_equal(fs_wait(&service, &probe, 1u), SALTS_OK);
        check_equal(probe.reentrant_status, SALTS_EBUSY);
        submitted = salts_fs_try_lstat(&service, path, &second);
        check_equal(submitted.status, SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), SALTS_OK);
        check_equal(probe.operations[1], SALTS_FS_LSTAT);
        check_equal(probe.results[1], SALTS_OK);
        check_true(salts_fs_get_stats(&service, &stats));
        check_equal(stats.capacity, (size_t)1u);
        check_equal(stats.accepted, (size_t)2u);
        check_equal(stats.completed, (size_t)2u);
        check_equal(stats.in_use, (size_t)0u);
        check_equal(stats.rejected_full, (size_t)1u);

        check_equal(salts_fs_service_close(&service), SALTS_OK);
        check_equal(salts_fs_service_close(&service), SALTS_EALREADY);
        submitted = salts_fs_try_stat(&service, path, &first);
        check_equal(submitted.status, SALTS_FS_SUBMIT_CLOSED);

        fs_close_destroy(&service, &probe);
    }
}

describe("directory and path mutations") {
    it("enumerates into caller-owned bounded storage transactionally") {
        char *root = fs_fixture_path(true);
        char child[1024];
        char nested[1024];
        static salts_fs_dirent_t entries[4] = {0};
        static char names[128] = {0};
        static salts_fs_dir_buffer directory;
        directory = (salts_fs_dir_buffer){
            .entries = entries,
            .entry_capacity = 4u,
            .names = names,
            .names_capacity = sizeof(names),
        };
        salts_fs_config config = fs_test_config(&probe, 2u);
        bool saw_child = false;
        bool saw_nested = false;
        size_t index;

        check_not_null(root);
        check_equal(salts_fs_path_join(child, sizeof(child), root,
                                       "child.txt"), 0);
        check_equal(salts_fs_path_join(nested, sizeof(nested), root,
                                       "nested"), 0);
        check_equal(tt_write_file(child, "x", 1u), 0);
        check_equal(tt_make_dir(nested), 0);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);
        check_equal(salts_fs_try_read_directory(&service, root, &directory).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), SALTS_OK);
        check_equal(probe.results[0], SALTS_OK);
        check_equal(directory.entry_count, (size_t)2u);
        for (index = 0u; index < directory.entry_count; ++index) {
            if (strcmp(directory.entries[index].name, "child.txt") == 0)
                saw_child = true;
            if (strcmp(directory.entries[index].name, "nested") == 0)
                saw_nested = true;
        }
        check_true(saw_child);
        check_true(saw_nested);

        directory.names_capacity = 1u;
        directory.entry_count = 99u;
        directory.names_used = 99u;
        check_equal(salts_fs_try_read_directory(&service, root, &directory).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), SALTS_OK);
        check_equal(probe.results[1], SALTS_ENOBUFS);
        check_equal(directory.entry_count, (size_t)0u);
        check_equal(directory.names_used, (size_t)0u);

        fs_close_destroy(&service, &probe);
    }

    it("creates renames deletes and removes filesystem objects") {
        char *root = fs_fixture_path(true);
        char directory[1024];
        char source[1024];
        char destination[1024];
        salts_fs_config config = fs_test_config(&probe, 2u);
        static salts_fs_stat_t stat_result = {0};

        check_not_null(root);
        check_equal(salts_fs_path_join(directory, sizeof(directory), root,
                                       "created"), 0);
        check_equal(salts_fs_path_join(source, sizeof(source), root,
                                       "source.txt"), 0);
        check_equal(salts_fs_path_join(destination, sizeof(destination), root,
                                       "destination.txt"), 0);
        check_equal(salts_fs_service_init(&service, &config), SALTS_OK);

        check_equal(salts_fs_try_mkdir(&service, directory, 0755).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 1u), SALTS_OK);
        check_equal(probe.results[0], SALTS_OK);
        check_equal(salts_fs_stat(directory, &stat_result), SALTS_OK);
        check_true(stat_result.is_directory);

        check_equal(tt_write_file(source, "move", 4u), 0);
        check_equal(salts_fs_try_rename(&service, source, destination).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 2u), SALTS_OK);
        check_equal(probe.results[1], SALTS_OK);
        check_less(salts_fs_stat(source, &stat_result), 0);
        check_equal(salts_fs_stat(destination, &stat_result), SALTS_OK);

        check_equal(salts_fs_try_unlink(&service, destination).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 3u), SALTS_OK);
        check_equal(probe.results[2], SALTS_OK);
        check_equal(salts_fs_try_rmdir(&service, directory).status,
                    SALTS_FS_SUBMIT_ACCEPTED);
        check_equal(fs_wait(&service, &probe, 4u), SALTS_OK);
        check_equal(probe.results[3], SALTS_OK);

        fs_close_destroy(&service, &probe);
    }
}
}
