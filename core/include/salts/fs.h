/**
 * @file fs.h
 * @brief Salts File System Utilities
 * @author "Simple, direct, no bullshit" - Linus philosophy applied to file I/O
 *
 * This header provides simple synchronous file system utilities without exposing libuv.
 */

#ifndef SALTS_FS_H
#define SALTS_FS_H

#include <salts/api.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Caller-owned byte buffer with no legacy format.
typedef struct {
  char *base;
  size_t len;
} fs_buf_t;

// File information structure
typedef struct {
  uint64_t size;
  uint64_t atime; // Access time (microseconds since epoch)
  uint64_t mtime; // Modify time (microseconds since epoch)
  uint64_t ctime; // Change time (microseconds since epoch)
  int mode;       // File permissions
  bool is_file;
  bool is_directory;
  bool is_symlink;
} fs_stat_t;

// Opaque directory enumeration handle
typedef struct fs_dir_s fs_dir_t;

/*
 * Secure root-relative filesystem capabilities.
 *
 * These identities intentionally hide native fd/HANDLE values. The admitted
 * host root is resolved once; all subsequent path operations are relative to
 * that retained capability and never to process cwd.
 */
typedef struct fs_root_s fs_root_t;
typedef struct fs_root_file_s fs_root_file_t;
typedef struct fs_root_dir_s fs_root_dir_t;

typedef enum {
  SALTS_FS_DIRENT_UNKNOWN = 0,
  SALTS_FS_DIRENT_FILE,
  SALTS_FS_DIRENT_DIRECTORY,
  SALTS_FS_DIRENT_SYMLINK,
  SALTS_FS_DIRENT_OTHER
} fs_dirent_type_t;

typedef struct {
  const char *name;
  fs_dirent_type_t type;
} fs_dirent_t;

// =============================================================================
// Synchronous File Operations - simple blocking I/O
// =============================================================================

/**
 * @brief Read an entire file
 *
 * This function reads the entire contents of a file into a buffer.
 * Memory is allocated automatically and must be freed with fs_buf_free().
 *
 * @param path File path to read
 * @param buf Pointer to fs_buf_t where file contents will be stored
 * @return 0 on success, negative error code on failure
 *
 * @note Blocks until the entire file is read or an error occurs
 * @note Use fs_buf_free() to release the buffer when done
 */
SALTS_API int fs_read_file(const char *path, fs_buf_t *buf);

/**
 * @brief Write data to a file
 *
 * This function writes a buffer of data to a file, creating it if necessary.
 *
 * @param path File path to write to
 * @param buf Pointer to fs_buf_t containing data to write
 * @return 0 on success, negative error code on failure
 *
 * @note Blocks until the entire buffer is written or an error occurs
 * @note Creates the file if it doesn't exist, overwrites if it does
 */
SALTS_API int fs_write_file(const char *path, const fs_buf_t *buf);

/**
 * @brief Get file information
 *
 * This function retrieves metadata about a file or directory.
 *
 * @param path Path to file or directory to stat
 * @param stat Pointer to fs_stat_t to fill with file information
 * @return 0 on success, negative error code on failure
 *
 * @note Works on both files and directories
 * @note Provides size, permissions, timestamps, and file type information
 */
SALTS_API int fs_stat(const char *path, fs_stat_t *stat);

/**
 * @brief Get file information without following symbolic links when supported
 *
 * @param path Path to file, directory, or symbolic link
 * @param stat Pointer to fs_stat_t to fill with file information
 * @return 0 on success, negative error code on failure
 *
 * @note On POSIX this uses lstat(). On Windows this reports reparse-point
 *       symlinks using Win32 file attributes.
 */
SALTS_API int fs_lstat(const char *path, fs_stat_t *stat);

/**
 * @brief Change file permissions
 *
 * @param path Path to file
 * @param mode Permission bits, e.g. 0644
 * @return 0 on success, negative error code on failure
 *
 * @note Windows maps this to read-only/read-write CRT permissions.
 */
SALTS_API int fs_chmod(const char *path, int mode);

// Access check flags for fs_access()
#define SALTS_FS_ACCESS_EXISTS 0x00
#define SALTS_FS_ACCESS_READ   0x01
#define SALTS_FS_ACCESS_WRITE  0x02
#define SALTS_FS_ACCESS_EXEC   0x04

/**
 * @brief Check file accessibility
 *
 * @param path Path to check
 * @param mode SALTS_FS_ACCESS_* bitmask, or SALTS_FS_ACCESS_EXISTS
 * @return 0 if accessible, negative error code otherwise
 *
 * @note Windows CRT access checks read/write/existence. Execute is treated as
 *       existence on Windows because executable permission is not a mode bit.
 */
SALTS_API int fs_access(const char *path, int mode);

/**
 * @brief Create a symbolic link
 *
 * @param target Link target path
 * @param link_path Path of the symlink to create
 * @param is_directory Non-zero when target is a directory
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_symlink(const char *target, const char *link_path, int is_directory);

/**
 * @brief Read a symbolic link target into buffer
 *
 * @param path Symlink path
 * @param buffer Destination buffer
 * @param buffer_size Destination size including trailing NUL
 * @return Number of bytes written, or negative error code on failure
 *
 * @note POSIX returns the symlink payload. Windows returns the resolved final
 *       path for the reparse point because Win32 does not expose POSIX readlink
 *       semantics through the CRT.
 */
SALTS_API int fs_readlink(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Create a directory
 *
 * @param path Directory path to create
 * @param mode Directory permissions (e.g., 0755)
 * @return 0 on success, negative error code on failure
 *
 * @note Parent directories must exist
 * @note Permissions are ignored on Windows platforms
 */
SALTS_API int fs_mkdir(const char *path, int mode);

/**
 * @brief Remove a directory
 *
 * @param path Directory path to remove
 * @return 0 on success, negative error code on failure
 *
 * @note Directory must be empty
 */
SALTS_API int fs_rmdir(const char *path);

/**
 * @brief Open a directory for enumeration
 *
 * @param path Directory path to enumerate
 * @param dir Pointer that receives an owned directory handle
 * @return 0 on success, negative error code on failure
 *
 * @note The returned handle must be released with fs_closedir().
 * @note A directory handle is not safe for concurrent reads.
 */
SALTS_API int fs_opendir(const char *path, fs_dir_t **dir);

/**
 * @brief Read the next directory entry
 *
 * @param dir Directory handle from fs_opendir()
 * @param entry Pointer that receives the next entry
 * @return 1 when an entry is returned, 0 at end of directory, or a negative
 *         error code on failure
 *
 * @note The "." and ".." entries are skipped.
 * @note entry->name is borrowed from dir and remains valid only until the next
 *       fs_readdir() call or fs_closedir(). Copy it to retain it.
 * @note type may be SALTS_FS_DIRENT_UNKNOWN when the platform does not expose
 *       a file type during enumeration. Use fs_lstat() when required.
 */
SALTS_API int fs_readdir(fs_dir_t *dir, fs_dirent_t *entry);

/**
 * @brief Close a directory enumeration handle
 *
 * @param dir Directory handle from fs_opendir()
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_closedir(fs_dir_t *dir);

/**
 * @brief Delete a file
 *
 * @param path File path to delete
 * @return 0 on success, negative error code on failure
 *
 * @note Only works on files, not directories
 */
SALTS_API int fs_unlink(const char *path);

// =============================================================================
// File System Utilities - cross-platform helpers
// =============================================================================

/**
 * @brief Initialize a file system buffer
 *
 * @param base Pointer to the buffer memory
 * @param len Length of the buffer in bytes
 * @return Initialized fs_buf_t structure
 */
SALTS_API fs_buf_t fs_buf_init(char *base, size_t len);

/**
 * @brief Free a file system buffer allocated by Salts
 *
 * @param buf Pointer to fs_buf_t to free
 *
 * @note Only call this on buffers allocated by Salts functions
 */
SALTS_API void fs_buf_free(fs_buf_t *buf);

/**
 * @brief Get temporary directory path (cross-platform)
 *
 * @param buffer Buffer to store the directory path
 * @param buffer_size Size of the buffer in bytes
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_get_tmpdir(char *buffer, size_t buffer_size);

/**
 * @brief Check if a path is absolute
 *
 * @param path Path to check
 * @return true if path is absolute, false if relative
 */
SALTS_API bool fs_path_is_absolute(const char *path);

/**
 * @brief Join two path components into a single path
 *
 * @param result Buffer to store the joined path
 * @param result_size Size of the result buffer in bytes
 * @param base Base path component
 * @param path Relative path component to append
 * @return 0 on success, -1 if result buffer is too small
 */
SALTS_API int fs_path_join(char *result, size_t result_size, const char *base,
                                 const char *path);

/**
 * @brief Extract directory component from a path
 *
 * @param path Original path
 * @param dirname Buffer to store the directory component
 * @param dirname_size Size of the dirname buffer in bytes
 * @return 0 on success, -1 if dirname buffer is too small
 */
SALTS_API int fs_path_dirname(const char *path, char *dirname, size_t dirname_size);

/**
 * @brief Extract filename component from a path
 *
 * @param path Original path
 * @param basename Buffer to store the filename component
 * @param basename_size Size of the basename buffer in bytes
 * @return 0 on success, -1 if basename buffer is too small
 */
SALTS_API int fs_path_basename(const char *path, char *basename, size_t basename_size);

// File system constants
#define SALTS_FS_MAX_PATH 260
#define SALTS_FS_DEFAULT_MODE 0644

// File open flags
#define SALTS_FS_O_RDONLY 0x0001
#define SALTS_FS_O_WRONLY 0x0002
#define SALTS_FS_O_RDWR 0x0004
#define SALTS_FS_O_CREAT 0x0100
#define SALTS_FS_O_TRUNC 0x0200
#define SALTS_FS_O_APPEND 0x0400

// Advisory file lock flags for fs_lock()
#define SALTS_FS_LOCK_SHARED    0x01
#define SALTS_FS_LOCK_EXCLUSIVE 0x02
#define SALTS_FS_LOCK_NONBLOCK  0x04

// File handle type
typedef int file_t;
#define SALTS_INVALID_FILE (-1)

// =============================================================================
// Streaming File Operations - for incremental I/O
// =============================================================================

/**
 * @brief Open a file
 *
 * @param path File path to open
 * @param flags Open flags (SALTS_FS_O_*)
 * @param mode File permissions for new files (e.g., 0644)
 * @return File handle on success, SALTS_INVALID_FILE on failure
 */
SALTS_API file_t fs_open(const char *path, int flags, int mode);

/**
 * @brief Read from an open file
 *
 * @param fd File handle from fs_open
 * @param buf Buffer to store read data
 * @param len Maximum number of bytes to read
 * @return Number of bytes read, or negative error code
 *
 * @note len must be <= INT_MAX because the return type is int
 */
SALTS_API int fs_read(file_t fd, char *buf, size_t len);

/**
 * @brief Read from an open file at a specific offset (thread-safe)
 *
 * Uses positional read — does not modify the file descriptor's position.
 * Safe to call concurrently from multiple threads on the same fd.
 *
 * @param fd File handle from fs_open
 * @param buf Buffer to store read data
 * @param len Maximum number of bytes to read
 * @param offset Byte offset in the file to read from
 * @return Number of bytes read, or negative error code
 *
 * @note len must be <= INT_MAX because the return type is int
 */
SALTS_API int fs_pread(file_t fd, char *buf, size_t len, int64_t offset);

/**
 * @brief Write to an open file at a specific offset
 *
 * @param fd File handle from fs_open
 * @param data Data to write
 * @param len Number of bytes to write
 * @param offset Byte offset in the file to write to
 * @return Number of bytes written, or negative error code
 *
 * @note len must be <= INT_MAX because the return type is int
 */
SALTS_API int fs_pwrite(file_t fd, const char *data, size_t len, int64_t offset);

/**
 * @brief Write to an open file
 *
 * @param fd File handle from fs_open
 * @param data Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or negative error code
 *
 * @note len must be <= INT_MAX because the return type is int
 */
SALTS_API int fs_write(file_t fd, const char *data, size_t len);

/**
 * @brief Close an open file
 *
 * @param fd File handle to close
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_close(file_t fd);

/**
 * @brief Truncate or extend a file to a specified length
 *
 * @param fd File handle
 * @param length New length in bytes
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_ftruncate(file_t fd, int64_t length);

/**
 * @brief Flush file buffers to disk
 *
 * @param fd File handle to flush
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_fsync(file_t fd);

/**
 * @brief Acquire an advisory byte-range lock
 *
 * @param fd File handle
 * @param flags SALTS_FS_LOCK_SHARED or SALTS_FS_LOCK_EXCLUSIVE, optionally
 *              OR'd with SALTS_FS_LOCK_NONBLOCK
 * @param offset Start offset
 * @param len Length to lock; 0 means to EOF
 * @return 0 on success, negative error code on failure
 *
 * @note POSIX uses fcntl record locks. Windows uses LockFileEx. Locks are
 *       advisory and process/OS semantics differ across platforms.
 */
SALTS_API int fs_lock(file_t fd, int flags, int64_t offset, uint64_t len);

/**
 * @brief Release an advisory byte-range lock
 *
 * @param fd File handle
 * @param offset Start offset
 * @param len Length to unlock; 0 means to EOF
 * @return 0 on success, negative error code on failure
 */
SALTS_API int fs_unlock(file_t fd, int64_t offset, uint64_t len);

/**
 * @brief Rename/move a file, replacing an existing destination file
 *
 * @param old_path Current file path
 * @param new_path New file path
 * @return 0 on success, negative error code on failure
 *
 * @note The source and destination must be on the same filesystem.
 */
SALTS_API int fs_rename(const char *old_path, const char *new_path);

/**
 * @brief Publication state returned by fs_replace_durable().
 *
 * NOT_PUBLISHED means Salts did not perform the namespace replacement.
 * PUBLISHED_DURABLE means the replacement completed and the platform durability
 * step succeeded.
 * DURABILITY_UNKNOWN means the namespace replacement completed, but a later
 * durability step failed so callers must not assume that retrying the same
 * publication is safe.
 */
typedef enum fs_replace_state_e {
  SALTS_FS_REPLACE_NOT_PUBLISHED = 0,
  SALTS_FS_REPLACE_PUBLISHED_DURABLE = 1,
  SALTS_FS_REPLACE_DURABILITY_UNKNOWN = 2
} fs_replace_state_t;

/**
 * @brief Durably publish a completed staging file over a destination file.
 *
 * The staging file and destination must be on the same filesystem/volume.
 * Cross-filesystem copy is never used as a fallback.
 *
 * POSIX: flush staging, rename, then fsync the destination parent directory.
 * Windows: flush staging, then MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH).
 *
 * @param staging_path Completed staging file. Consumed by a successful
 *                     namespace replacement.
 * @param destination_path Destination file to replace/create.
 * @param state Required output publication state; initialized to
 *              SALTS_FS_REPLACE_NOT_PUBLISHED before validation.
 * @return 0 only when state is SALTS_FS_REPLACE_PUBLISHED_DURABLE. A negative
 *         error may accompany NOT_PUBLISHED or DURABILITY_UNKNOWN.
 */
SALTS_API int fs_replace_durable(
    const char *staging_path, const char *destination_path,
    fs_replace_state_t *state);

/**
 * @brief Get current file position
 *
 * @param fd File handle
 * @return Current position, or negative error code
 */
SALTS_API int64_t fs_tell(file_t fd);

/**
 * @brief Seek to position in file
 *
 * @param fd File handle
 * @param offset Offset from whence
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New position, or negative error code
 */
SALTS_API int64_t fs_seek(file_t fd, int64_t offset, int whence);

// =============================================================================
// Secure root-relative filesystem capability
// =============================================================================

/*
 * Root-relative paths use '/' or '\\' as separators on every platform and
 * must consist only of non-empty ordinary components. Absolute paths, ".",
 * "..", repeated/trailing separators and Windows drive/ADS ':' components are
 * rejected. V1 never follows symlinks/reparse points below the admitted root:
 * lstat may observe a final link, while stat/open reject it.
 *
 * Component traversal is handle/fd-relative. This is a security boundary; do
 * not emulate it with root + relative_path string concatenation.
 */
#define SALTS_FS_ROOT_COMPONENT_MAX 1024u
#define SALTS_FS_ROOT_DEPTH_MAX 64u
#define SALTS_FS_ROOT_DIRENT_NAME_MAX 1024u

typedef struct fs_root_dirent_s {
  uint64_t next_cookie;
  size_t name_length;
  fs_dirent_type_t type;
} fs_root_dirent_t;

/* Admit one host directory as an opaque capability. */
SALTS_API int fs_root_open(
    const char *host_root, fs_root_t **out_root);
SALTS_API int fs_root_close(fs_root_t *root);

/* Inspect the admitted root itself. */
SALTS_API int fs_root_fstat(
    const fs_root_t *root, fs_stat_t *out_stat);

/* Open/operate on a regular file without exposing a native handle.
 * Non-regular entries are rejected; opening a FIFO does not wait for a peer.
 */
SALTS_API int fs_root_file_open(
    const fs_root_t *root, const char *relative_path,
    int flags, int mode, fs_root_file_t **out_file);
SALTS_API int fs_root_file_close(fs_root_file_t *file);
SALTS_API int fs_root_file_read(
    fs_root_file_t *file, char *buffer, size_t length);
SALTS_API int fs_root_file_write(
    fs_root_file_t *file, const char *data, size_t length);
SALTS_API int64_t fs_root_file_seek(
    fs_root_file_t *file, int64_t offset, int whence);
SALTS_API int64_t fs_root_file_tell(fs_root_file_t *file);
SALTS_API int fs_root_file_stat(
    const fs_root_file_t *file, fs_stat_t *out_stat);

#define SALTS_FS_ROOT_MUTATION_VERSION 1
/* Root-file-open only: requires O_CREAT, fails with EEXIST without truncation. */
#define SALTS_FS_ROOT_O_EXCL 0x0800
/* Toggle append without reopening. Single owner; returns 0 or negative errno,
 * preserving flags on failure. Each append write targets EOF atomically. */
SALTS_API int fs_root_file_set_append(fs_root_file_t *file, bool append);
/* Atomically rename/replace between admitted roots. Parent components never
 * follow links; a final link is moved as an entry. Returns 0 or negative errno,
 * including -EXDEV across filesystems. Open handles retain their identities. */
SALTS_API int fs_root_rename(
    const fs_root_t *source_root, const char *source_path,
    const fs_root_t *target_root, const char *target_path);

/*
 * stat is secure no-follow in capability v1: a final symlink/reparse point
 * returns -ELOOP. lstat reports the final link itself. Intermediate links are
 * always rejected.
 */
SALTS_API int fs_root_stat(
    const fs_root_t *root, const char *relative_path,
    fs_stat_t *out_stat);
SALTS_API int fs_root_lstat(
    const fs_root_t *root, const char *relative_path,
    fs_stat_t *out_stat);

SALTS_API int fs_root_mkdir(
    const fs_root_t *root, const char *relative_path, int mode);
SALTS_API int fs_root_rmdir(
    const fs_root_t *root, const char *relative_path);
SALTS_API int fs_root_unlink(
    const fs_root_t *root, const char *relative_path);

/*
 * Bounded directory enumeration. Names are copied into caller-owned storage.
 * cookie 0 starts/restarts enumeration; next_cookie resumes from the returned
 * entry. Cookies are stable only while the directory contents are unchanged.
 */
/* Enumerate the admitted root directory itself without inventing "." syntax. */
SALTS_API int fs_root_opendir_self(
    const fs_root_t *root, fs_root_dir_t **out_dir);
SALTS_API int fs_root_opendir(
    const fs_root_t *root, const char *relative_path,
    fs_root_dir_t **out_dir);
SALTS_API int fs_root_readdir(
    fs_root_dir_t *dir, uint64_t cookie,
    char *name_buffer, size_t name_buffer_size,
    fs_root_dirent_t *out_entry);
SALTS_API int fs_root_closedir(fs_root_dir_t *dir);

#ifdef __cplusplus
}
#endif

#endif // SALTS_FS_H
