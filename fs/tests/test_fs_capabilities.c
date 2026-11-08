#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define TINYTEST_NO_MAIN
#include <tinytest.h>
#include <fs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

enum { ENTRY_COUNT = 240 };
static char workspace[1600];
static salts_fs_root_t *root;
static salts_fs_root_file_t *file;
static salts_fs_root_dir_t *first_dir;
static salts_fs_root_dir_t *second_dir;
static unsigned created_entries;
static bool workspace_created;
static bool child_directory_created;
#ifndef _WIN32
static bool fifo_created;
#endif

static int entry_name(char *buffer, size_t size, unsigned index) {
  /* Long names force multiple native directory-query buffers on Windows. */
  int count = snprintf(buffer, size,
      "entry-%03u-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz.dat", index);
  return count >= 0 && (size_t)count < size ? 0 : -1;
}

static int enumerate(salts_fs_root_dir_t *dir, uint64_t cookie,
                     unsigned char seen[ENTRY_COUNT], unsigned *count) {
  for (;;) {
    char name[SALTS_FS_ROOT_DIRENT_NAME_MAX + 1u];
    salts_fs_root_dirent_t entry;
    unsigned index = ENTRY_COUNT;
    int rc = salts_fs_root_readdir(dir, cookie, name, sizeof(name), &entry);
    if (rc <= 0) return rc;
    if (sscanf(name, "entry-%u-", &index) != 1 || index >= ENTRY_COUNT ||
        seen[index] || (entry.type != SALTS_FS_DIRENT_FILE &&
                        entry.type != SALTS_FS_DIRENT_UNKNOWN))
      return -1;
    seen[index] = 1u;
    ++*count;
    cookie = entry.next_cookie;
  }
}

spec("FS regular-file capabilities and independent enumeration") {
  before_each() {
    char temporary[1024];
    unsigned long pid;
#ifdef _WIN32
    pid = GetCurrentProcessId();
#else
    pid = (unsigned long)getpid();
#endif
    root = NULL;
    file = NULL;
    first_dir = second_dir = NULL;
    created_entries = 0u;
    workspace_created = child_directory_created = false;
#ifndef _WIN32
    fifo_created = false;
#endif
    check_equal(salts_fs_get_tmpdir(temporary, sizeof(temporary)), 0);
    int length = snprintf(workspace, sizeof(workspace),
                          "%s/saltscore-fs-capabilities-%lu", temporary, pid);
    check(length > 0 && (size_t)length < sizeof(workspace));
    check_equal(salts_fs_mkdir(workspace, 0700), 0);
    workspace_created = true;
    check_equal(salts_fs_root_open(workspace, &root), 0);
  }
  after_each() {
    int cleanup = 0;
    if (file && salts_fs_root_file_close(file) != 0) cleanup = 1;
    if (first_dir && salts_fs_root_closedir(first_dir) != 0) cleanup = 1;
    if (second_dir && salts_fs_root_closedir(second_dir) != 0) cleanup = 1;
    if (root) {
      for (unsigned index = 0u; index < created_entries; ++index) {
        char name[128];
        if (entry_name(name, sizeof(name), index) != 0 ||
            salts_fs_root_unlink(root, name) != 0) cleanup = 1;
      }
      if (child_directory_created && salts_fs_root_rmdir(root, "directory") != 0)
        cleanup = 1;
#ifndef _WIN32
      if (fifo_created && salts_fs_root_unlink(root, "fifo") != 0) cleanup = 1;
#endif
      if (salts_fs_root_close(root) != 0) cleanup = 1;
    }
    if (workspace_created && salts_fs_rmdir(workspace) != 0) cleanup = 1;
    check_equal(cleanup, 0);
  }
  it("keeps two root scans independent across native buffer boundaries") {
    unsigned char first_seen[ENTRY_COUNT] = {0};
    unsigned char second_seen[ENTRY_COUNT] = {0};
    unsigned first_count = 0u, second_count = 0u;
    for (unsigned index = 0u; index < ENTRY_COUNT; ++index) {
      char name[128];
      check_equal(entry_name(name, sizeof(name), index), 0);
      check_equal(salts_fs_root_file_open(root, name,
          SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_ROOT_O_EXCL, 0600, &file), 0);
      ++created_entries;
      int rc = salts_fs_root_file_close(file);
      file = NULL;
      check_equal(rc, 0);
    }
    check_equal(salts_fs_root_opendir_self(root, &first_dir), 0);
    char name[128];
    salts_fs_root_dirent_t entry;
    check_equal(salts_fs_root_readdir(first_dir, 0u, name, sizeof(name), &entry), 1);
    unsigned index = ENTRY_COUNT;
    check_equal(sscanf(name, "entry-%u-", &index), 1);
    check(index < ENTRY_COUNT);
    first_seen[index] = 1u;
    first_count = 1u;
    uint64_t resume = entry.next_cookie;

    check_equal(salts_fs_root_opendir_self(root, &second_dir), 0);
    check_equal(enumerate(second_dir, 0u, second_seen, &second_count), 0);
    check_equal(second_count, (unsigned)ENTRY_COUNT);
    check_equal(enumerate(first_dir, resume, first_seen, &first_count), 0);
    check_equal(first_count, (unsigned)ENTRY_COUNT);
    for (unsigned item = 0u; item < ENTRY_COUNT; ++item) {
      check_equal(first_seen[item], 1u);
      check_equal(second_seen[item], 1u);
    }
  }
  it("rejects a directory without publishing a regular-file capability") {
    check_equal(salts_fs_root_mkdir(root, "directory", 0700), 0);
    child_directory_created = true;
    int rc = salts_fs_root_file_open(root, "directory", SALTS_FS_O_RDONLY, 0, &file);
    check_less(rc, 0);
    check_null(file);
  }
  it("enumerates the admitted identity after its host directory is renamed") {
    char name[128], renamed[sizeof(workspace)];
    check_equal(entry_name(name, sizeof(name), 0u), 0);
    check_equal(salts_fs_root_file_open(root, name,
        SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_ROOT_O_EXCL, 0600, &file), 0);
    created_entries = 1u;
    int rc = salts_fs_root_file_close(file);
    file = NULL;
    check_equal(rc, 0);
    int length = snprintf(renamed, sizeof(renamed), "%s-renamed", workspace);
    check(length > 0 && (size_t)length < sizeof(renamed));
    check_equal(salts_fs_rename(workspace, renamed), 0);
    memcpy(workspace, renamed, (size_t)length + 1u);
    check_equal(salts_fs_root_opendir_self(root, &first_dir), 0);
    char actual[128];
    salts_fs_root_dirent_t entry;
    check_equal(salts_fs_root_readdir(first_dir, 0u, actual, sizeof(actual), &entry), 1);
    check_equal(actual, name);
    check_equal(salts_fs_root_readdir(first_dir, entry.next_cookie,
                                     actual, sizeof(actual), &entry), 0);
  }
#ifndef _WIN32
  it("rejects a FIFO without waiting for a writer") {
    char path[1700];
    check_equal(salts_fs_path_join(path, sizeof(path), workspace, "fifo"), 0);
    check_equal(mkfifo(path, 0600), 0);
    fifo_created = true;
    check_equal(salts_fs_root_file_open(root, "fifo", SALTS_FS_O_RDONLY, 0, &file),
                -EINVAL);
    check_null(file);
  }
#endif
}
