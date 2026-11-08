/**
 * @file salts_fs.c
 * @brief Salts File System - Synchronous cross-platform I/O, zero dependencies.
 *
 * "Simple, direct, no bullshit."
 * No libuv. Just standard POSIX / Win32 syscalls.
 * Error codes: negative errno values (e.g. -EINVAL, -ENOMEM), mirroring POSIX convention.
 */
#include <fs.h>
#ifdef _WIN32
#include <windows.h>
/* Native MSVC CRT has no POSIX ssize_t; use signed pointer width. */
#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EOVERFLOW
  #define EOVERFLOW ERANGE
#endif

/* ── Platform headers ─────────────────────────────────────────────────────── */
#ifdef _WIN32
  #include <share.h>
  #include <direct.h>
  #include <fcntl.h>
  #include <io.h>
  #include <sys/stat.h>
  #include <sys/types.h>

  /* POSIX-style open/read/write/close live in <io.h> on MSVC */
  #define fs_open    _open
  #define fs_read    _read
  #define fs_write   _write
  #define fs_close   _close
  #define fs_lseek   _lseeki64
  #define fs_fstat   _fstat64
  #define fs_stat_t  struct __stat64
  #define fs_ftrunc  _chsize_s
  #define fs_fsync   _commit
  #define fs_unlink  _unlink
  #define fs_chmod   _chmod
  #define fs_access  _access

  /* Win32 does not have S_ISxxx macros by default */
  #ifndef S_ISREG
    #define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
  #endif
  #ifndef S_ISDIR
    #define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
  #endif
  #ifndef S_ISLNK
    #define S_ISLNK(m)  0
  #endif

  /* Time fields: MSVC uses st_atime (time_t), not st_atim (timespec) */
  #define FS_ST_ATIME_SEC(s)  ((s).st_atime)
  #define FS_ST_MTIME_SEC(s)  ((s).st_mtime)
  #define FS_ST_CTIME_SEC(s)  ((s).st_ctime)
  #define FS_ST_XTIME_NSEC    0   /* MSVC stat has no nanosecond field */
  #ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
  #endif

#else  /* POSIX */
  #include <dirent.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>

  #define fs_open    open
  #define fs_read    read
  #define fs_write   write
  #define fs_close   close
  #define fs_lseek   lseek
  #define fs_fstat   fstat
  #define fs_stat_t  struct stat
  #define fs_ftrunc  ftruncate
  #define fs_fsync   fsync
  #define fs_unlink  unlink
  #define fs_chmod   chmod
  #define fs_access  access

  #if defined(__APPLE__)
    #define FS_ST_ATIME_SEC(s)  ((s).st_atimespec.tv_sec)
    #define FS_ST_MTIME_SEC(s)  ((s).st_mtimespec.tv_sec)
    #define FS_ST_CTIME_SEC(s)  ((s).st_ctimespec.tv_sec)
    #define FS_ST_ATIME_NSEC(s) ((s).st_atimespec.tv_nsec)
    #define FS_ST_MTIME_NSEC(s) ((s).st_mtimespec.tv_nsec)
    #define FS_ST_CTIME_NSEC(s) ((s).st_ctimespec.tv_nsec)
  #else
    #define FS_ST_ATIME_SEC(s)  ((s).st_atim.tv_sec)
    #define FS_ST_MTIME_SEC(s)  ((s).st_mtim.tv_sec)
    #define FS_ST_CTIME_SEC(s)  ((s).st_ctim.tv_sec)
    #define FS_ST_ATIME_NSEC(s) ((s).st_atim.tv_nsec)
    #define FS_ST_MTIME_NSEC(s) ((s).st_mtim.tv_nsec)
    #define FS_ST_CTIME_NSEC(s) ((s).st_ctim.tv_nsec)
  #endif
#endif

struct salts_fs_dir_s {
#ifdef _WIN32
  HANDLE handle;
  WIN32_FIND_DATAA entry;
  bool first_pending;
#else
  DIR *handle;
#endif
};

/* Helper: map errno to negative return value */
static inline int err_from_errno(void) { return -errno; }

#ifdef _WIN32
static int err_from_win32(DWORD error) {
  switch (error) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return -ENOENT;
  case ERROR_ACCESS_DENIED:
  case ERROR_PRIVILEGE_NOT_HELD:
    return -EACCES;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS:
    return -EEXIST;
  case ERROR_INVALID_PARAMETER:
  case ERROR_INVALID_NAME:
    return -EINVAL;
  case ERROR_DIRECTORY:
    return -ENOTDIR;
  case ERROR_FILENAME_EXCED_RANGE:
    return -ENAMETOOLONG;
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
    return -ENOMEM;
  case ERROR_LOCK_VIOLATION:
  case ERROR_IO_PENDING:
    return -EAGAIN;
  case ERROR_NOT_SAME_DEVICE:
    return -EXDEV;
  default:
    return -EIO;
  }
}

static uint64_t filetime_to_unix_us(const FILETIME *ft) {
  ULARGE_INTEGER t;
  t.LowPart = ft->dwLowDateTime;
  t.HighPart = ft->dwHighDateTime;
  if (t.QuadPart < 116444736000000000ULL) {
    return 0;
  }
  return (t.QuadPart - 116444736000000000ULL) / 10ULL;
}
#endif

static void salts_fs_stat_from_native(const fs_stat_t *st, salts_fs_stat_t *out) {
  out->size = (uint64_t)st->st_size;
  out->mode = (int)st->st_mode;
  out->is_file = S_ISREG(st->st_mode) ? true : false;
  out->is_directory = S_ISDIR(st->st_mode) ? true : false;
  out->is_symlink = S_ISLNK(st->st_mode) ? true : false;

#ifdef _WIN32
  out->atime = (uint64_t)st->st_atime * 1000000ULL;
  out->mtime = (uint64_t)st->st_mtime * 1000000ULL;
  out->ctime = (uint64_t)st->st_ctime * 1000000ULL;
#else
  out->atime = (uint64_t)FS_ST_ATIME_SEC(*st) * 1000000ULL + (uint64_t)FS_ST_ATIME_NSEC(*st) / 1000;
  out->mtime = (uint64_t)FS_ST_MTIME_SEC(*st) * 1000000ULL + (uint64_t)FS_ST_MTIME_NSEC(*st) / 1000;
  out->ctime = (uint64_t)FS_ST_CTIME_SEC(*st) * 1000000ULL + (uint64_t)FS_ST_CTIME_NSEC(*st) / 1000;
#endif
}

#ifdef _WIN32
static int salts_fs_lstat_win32(const char *path, salts_fs_stat_t *out) {
  WIN32_FIND_DATAA data;
  HANDLE find = FindFirstFileA(path, &data);
  if (find == INVALID_HANDLE_VALUE) {
    return err_from_win32(GetLastError());
  }
  FindClose(find);

  memset(out, 0, sizeof(*out));
  out->size = ((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow;
  out->is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  out->is_file = !out->is_directory;
  out->is_symlink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                    data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
  out->mode = out->is_directory ? _S_IFDIR : _S_IFREG;
  out->mode |= _S_IREAD;
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0) {
    out->mode |= _S_IWRITE;
  }
  out->atime = filetime_to_unix_us(&data.ftLastAccessTime);
  out->mtime = filetime_to_unix_us(&data.ftLastWriteTime);
  out->ctime = filetime_to_unix_us(&data.ftCreationTime);
  return 0;
}
#endif

#ifdef _WIN32
static SRWLOCK g_salts_fs_position_lock = SRWLOCK_INIT;

static inline void salts_fs_lock_position(void) { AcquireSRWLockExclusive(&g_salts_fs_position_lock); }
static inline void salts_fs_unlock_position(void) { ReleaseSRWLockExclusive(&g_salts_fs_position_lock); }
#endif

static inline size_t salts_fs_io_chunk(size_t len) {
  return len > (size_t)INT_MAX ? (size_t)INT_MAX : len;
}

static inline int salts_fs_stream_len_ok(size_t len) {
  return len > (size_t)INT_MAX ? -EOVERFLOW : 0;
}

static ssize_t salts_fs_read_once(salts_file_t fd, char *buf, size_t len) {
  size_t chunk = salts_fs_io_chunk(len);
  for (;;) {
#ifdef _WIN32
    salts_fs_lock_position();
    ssize_t n = fs_read(fd, buf, (unsigned int)chunk);
    int e = errno;
    salts_fs_unlock_position();
    errno = e;
#else
    ssize_t n = fs_read(fd, buf, chunk);
#endif
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return n;
  }
}

static ssize_t salts_fs_write_once(salts_file_t fd, const char *data, size_t len) {
  size_t chunk = salts_fs_io_chunk(len);
  for (;;) {
#ifdef _WIN32
    salts_fs_lock_position();
    ssize_t n = fs_write(fd, data, (unsigned int)chunk);
    int e = errno;
    salts_fs_unlock_position();
    errno = e;
#else
    ssize_t n = fs_write(fd, data, chunk);
#endif
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return n;
  }
}

static int salts_fs_read_exact(salts_file_t fd, char *buf, size_t len, size_t *read_out) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = salts_fs_read_once(fd, buf + total, len - total);
    if (n < 0) {
      if (read_out) *read_out = total;
      return err_from_errno();
    }
    if (n == 0) {
      if (read_out) *read_out = total;
      return -EIO;
    }
    total += (size_t)n;
  }
  if (read_out) *read_out = total;
  return 0;
}

static int salts_fs_write_all(salts_file_t fd, const char *data, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = salts_fs_write_once(fd, data + total, len - total);
    if (n < 0) {
      return err_from_errno();
    }
    if (n == 0) {
      return -EIO;
    }
    total += (size_t)n;
  }
  return 0;
}

static int salts_fs_offset_ok(int64_t offset, size_t len) {
  if (offset < 0) {
    return -EINVAL;
  }
  if (len > (size_t)(INT64_MAX - offset)) {
    return -EOVERFLOW;
  }
  return 0;
}

#ifndef _WIN32
static int salts_fs_offset_to_off_t(int64_t offset, off_t *out) {
  off_t converted = (off_t)offset;
  if ((int64_t)converted != offset) {
    return -EOVERFLOW;
  }
  *out = converted;
  return 0;
}
#endif

static ssize_t salts_fs_pread_once(salts_file_t fd, char *buf, size_t len, int64_t offset) {
  size_t chunk = salts_fs_io_chunk(len);
#ifdef _WIN32
  salts_fs_lock_position();
  int64_t saved = _lseeki64(fd, 0, SEEK_CUR);
  if (saved < 0) {
    int e = errno;
    salts_fs_unlock_position();
    errno = e;
    return -1;
  }
  if (_lseeki64(fd, offset, SEEK_SET) < 0) {
    int e = errno;
    _lseeki64(fd, saved, SEEK_SET);
    salts_fs_unlock_position();
    errno = e;
    return -1;
  }
  ssize_t n = fs_read(fd, buf, (unsigned int)chunk);
  int e = errno;
  if (_lseeki64(fd, saved, SEEK_SET) < 0 && n >= 0) {
    e = errno;
    n = -1;
  }
  salts_fs_unlock_position();
  errno = e;
  return n;
#else
  off_t pos;
  int rc = salts_fs_offset_to_off_t(offset, &pos);
  if (rc != 0) {
    errno = -rc;
    return -1;
  }
  for (;;) {
    ssize_t n = pread(fd, buf, chunk, pos);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return n;
  }
#endif
}

static ssize_t salts_fs_pwrite_once(salts_file_t fd, const char *data, size_t len,
                                    int64_t offset) {
  size_t chunk = salts_fs_io_chunk(len);
#ifdef _WIN32
  salts_fs_lock_position();
  int64_t saved = _lseeki64(fd, 0, SEEK_CUR);
  if (saved < 0) {
    int e = errno;
    salts_fs_unlock_position();
    errno = e;
    return -1;
  }
  if (_lseeki64(fd, offset, SEEK_SET) < 0) {
    int e = errno;
    _lseeki64(fd, saved, SEEK_SET);
    salts_fs_unlock_position();
    errno = e;
    return -1;
  }
  ssize_t n = fs_write(fd, data, (unsigned int)chunk);
  int e = errno;
  if (_lseeki64(fd, saved, SEEK_SET) < 0 && n >= 0) {
    e = errno;
    n = -1;
  }
  salts_fs_unlock_position();
  errno = e;
  return n;
#else
  off_t pos;
  int rc = salts_fs_offset_to_off_t(offset, &pos);
  if (rc != 0) {
    errno = -rc;
    return -1;
  }
  for (;;) {
    ssize_t n = pwrite(fd, data, chunk, pos);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return n;
  }
#endif
}

static int salts_fs_pwrite_all(salts_file_t fd, const char *data, size_t len, int64_t offset) {
  size_t total = 0;
  int rc = salts_fs_offset_ok(offset, len);
  if (rc != 0) {
    return rc;
  }
  while (total < len) {
    ssize_t n = salts_fs_pwrite_once(fd, data + total, len - total, offset + (int64_t)total);
    if (n < 0) {
      return err_from_errno();
    }
    if (n == 0) {
      return -EIO;
    }
    total += (size_t)n;
  }
  return 0;
}

/* ── Flag translation: SALTS_FS_O_* → native O_* ─────────────────────────── */
static int salts_fs_flags_to_native(int flags) {
  int native = 0;

  /* Access mode: exactly one of RDONLY / WRONLY / RDWR must be set */
  if (flags & SALTS_FS_O_RDWR)
    native |= O_RDWR;
  else if (flags & SALTS_FS_O_WRONLY)
    native |= O_WRONLY;
  else
    native |= O_RDONLY;

  if (flags & SALTS_FS_O_CREAT)
    native |= O_CREAT;
  if (flags & SALTS_FS_O_TRUNC)
    native |= O_TRUNC;
  if (flags & SALTS_FS_O_APPEND)
    native |= O_APPEND;

#ifdef _WIN32
  /* Windows: always open in binary mode to avoid CRLF surprises */
  native |= O_BINARY;
#endif

  return native;
}

/* ── Synchronous File Operations ─────────────────────────────────────────── */

int salts_fs_read_file(const char *path, salts_fs_buf_t *buf) {
  if (!path || !buf) {
    return -EINVAL;
  }

  /* stat to get size */
  fs_stat_t st;
#ifdef _WIN32
  if (_stat64(path, &st) != 0) {
#else
  if (stat(path, &st) != 0) {
#endif
    int e = err_from_errno();
    return e;
  }

  if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)SIZE_MAX - 1) {
    return -EOVERFLOW;
  }
  size_t file_size = (size_t)st.st_size;

  /* open */
  int fd = fs_open(path, O_RDONLY
#ifdef _WIN32
                   | O_BINARY
#endif
                   , 0);
  if (fd < 0) {
    int e = err_from_errno();
    return e;
  }

  /* allocate */
  char *base = (char *)malloc(file_size + 1);
  if (!base) {
    fs_close(fd);
    return -ENOMEM;
  }

  /* read */
  size_t bytes_read = 0;
  int rc = salts_fs_read_exact(fd, base, file_size, &bytes_read);
  int close_rc = fs_close(fd);
  if (rc != 0) {
    free(base);
    return rc;
  }
  if (close_rc != 0) {
    int e = err_from_errno();
    free(base);
    return e;
  }

  buf->base = base;
  buf->len = bytes_read;
  buf->base[bytes_read] = '\0';
  return 0;
}

int salts_fs_write_file(const char *path, const salts_fs_buf_t *buf) {
  if (!path || !buf || (!buf->base && buf->len > 0)) {
    return -EINVAL;
  }

  int fd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC
#ifdef _WIN32
                   | O_BINARY
#endif
                   , SALTS_FS_DEFAULT_MODE);
  if (fd < 0) {
    int e = err_from_errno();
    return e;
  }

  int rc = salts_fs_write_all(fd, buf->base, buf->len);
  int close_rc = fs_close(fd);
  if (rc != 0) {
    return rc;
  }
  if (close_rc != 0) {
    return err_from_errno();
  }

  return 0;
}

int salts_fs_stat(const char *path, salts_fs_stat_t *stat_out) {
  if (!path || !stat_out) {
    return -EINVAL;
  }

  fs_stat_t st;
#ifdef _WIN32
  if (_stat64(path, &st) != 0) {
#else
  if (stat(path, &st) != 0) {
#endif
    int e = err_from_errno();
    return e;
  }

  salts_fs_stat_from_native(&st, stat_out);

  return 0;
}

int salts_fs_lstat(const char *path, salts_fs_stat_t *stat_out) {
  if (!path || !stat_out) {
    return -EINVAL;
  }

#ifdef _WIN32
  int rc = salts_fs_lstat_win32(path, stat_out);
  if (rc != 0) {
    return rc;
  }
#else
  fs_stat_t st;
  if (lstat(path, &st) != 0) {
    int e = err_from_errno();
    return e;
  }
  salts_fs_stat_from_native(&st, stat_out);
#endif

  return 0;
}

int salts_fs_chmod(const char *path, int mode) {
  if (!path) {
    return -EINVAL;
  }
  if (fs_chmod(path, mode) != 0) {
    return err_from_errno();
  }
  return 0;
}

int salts_fs_access(const char *path, int mode) {
  if (!path) {
    return -EINVAL;
  }

#ifdef _WIN32
  int native = 0;
  if (mode & SALTS_FS_ACCESS_READ) native |= 4;
  if (mode & SALTS_FS_ACCESS_WRITE) native |= 2;
  if ((mode & ~(SALTS_FS_ACCESS_READ | SALTS_FS_ACCESS_WRITE | SALTS_FS_ACCESS_EXEC)) != 0) {
    return -EINVAL;
  }
#else
  int native = F_OK;
  if ((mode & ~(SALTS_FS_ACCESS_READ | SALTS_FS_ACCESS_WRITE | SALTS_FS_ACCESS_EXEC)) != 0) {
    return -EINVAL;
  }
  if (mode != SALTS_FS_ACCESS_EXISTS) {
    native = 0;
    if (mode & SALTS_FS_ACCESS_READ) native |= R_OK;
    if (mode & SALTS_FS_ACCESS_WRITE) native |= W_OK;
    if (mode & SALTS_FS_ACCESS_EXEC) native |= X_OK;
  }
#endif

  if (fs_access(path, native) != 0) {
    return err_from_errno();
  }
  return 0;
}

int salts_fs_symlink(const char *target, const char *link_path, int is_directory) {
  if (!target || !link_path) {
    return -EINVAL;
  }

#ifdef _WIN32
  DWORD flags = is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (!CreateSymbolicLinkA(link_path, target, flags)) {
    return err_from_win32(GetLastError());
  }
  return 0;
#else
  (void)is_directory;
  if (symlink(target, link_path) != 0) {
    return err_from_errno();
  }
  return 0;
#endif
}

int salts_fs_readlink(const char *path, char *buffer, size_t buffer_size) {
  if (!path || !buffer || buffer_size == 0) {
    return -EINVAL;
  }

#ifdef _WIN32
  HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    return err_from_win32(GetLastError());
  }

  DWORD cap = buffer_size > (size_t)0xffffffffu ? 0xffffffffu : (DWORD)buffer_size;
  DWORD n = GetFinalPathNameByHandleA(h, buffer, cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  DWORD err = GetLastError();
  CloseHandle(h);
  if (n == 0) {
    return err_from_win32(err);
  }
  if ((size_t)n >= buffer_size) {
    buffer[0] = '\0';
    return -ENAMETOOLONG;
  }
  buffer[n] = '\0';
  return (int)n;
#else
  ssize_t n = readlink(path, buffer, buffer_size - 1);
  if (n < 0) {
    return err_from_errno();
  }
  if ((size_t)n >= buffer_size - 1) {
    buffer[0] = '\0';
    return -ENAMETOOLONG;
  }
  buffer[n] = '\0';
  return (int)n;
#endif
}

int salts_fs_mkdir(const char *path, int mode) {
  if (!path) {
    return -EINVAL;
  }

#ifdef _WIN32
  int err = _mkdir(path);
#else
  int err = mkdir(path, (mode_t)mode);
#endif

  if (err != 0) {
    int e = err_from_errno();
    return e;
  }

  return 0;
}

int salts_fs_rmdir(const char *path) {
  if (!path) {
    return -EINVAL;
  }

#ifdef _WIN32
  int err = _rmdir(path);
#else
  int err = rmdir(path);
#endif

  if (err != 0) {
    int e = err_from_errno();
    return e;
  }

  return 0;
}

static bool salts_fs_dirent_is_dot(const char *name) {
  return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

#ifdef _WIN32
static salts_fs_dirent_type_t salts_fs_dirent_type_win32(const WIN32_FIND_DATAA *entry) {
  if ((entry->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
      entry->dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
    return SALTS_FS_DIRENT_SYMLINK;
  }
  if ((entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return SALTS_FS_DIRENT_DIRECTORY;
  }
  return SALTS_FS_DIRENT_FILE;
}
#else
#ifdef DT_UNKNOWN
static salts_fs_dirent_type_t salts_fs_dirent_type_posix(unsigned char type) {
#ifdef DT_REG
  if (type == DT_REG) {
    return SALTS_FS_DIRENT_FILE;
  }
#endif
#ifdef DT_DIR
  if (type == DT_DIR) {
    return SALTS_FS_DIRENT_DIRECTORY;
  }
#endif
#ifdef DT_LNK
  if (type == DT_LNK) {
    return SALTS_FS_DIRENT_SYMLINK;
  }
#endif
#ifdef DT_UNKNOWN
  if (type == DT_UNKNOWN) {
    return SALTS_FS_DIRENT_UNKNOWN;
  }
#endif
  return SALTS_FS_DIRENT_OTHER;
}
#endif
#endif

int salts_fs_opendir(const char *path, salts_fs_dir_t **dir_out) {
  if (!dir_out) {
    return -EINVAL;
  }
  *dir_out = NULL;
  if (!path || path[0] == '\0') {
    return -EINVAL;
  }

  salts_fs_dir_t *dir = (salts_fs_dir_t *)calloc(1, sizeof(*dir));
  if (!dir) {
    return -ENOMEM;
  }

#ifdef _WIN32
  size_t path_len = strlen(path);
  bool needs_separator = path[path_len - 1] != '\\' && path[path_len - 1] != '/';
  if (path_len > SIZE_MAX - (needs_separator ? 3u : 2u)) {
    free(dir);
    return -ENAMETOOLONG;
  }

  size_t pattern_size = path_len + (needs_separator ? 3u : 2u);
  char *pattern = (char *)malloc(pattern_size);
  if (!pattern) {
    free(dir);
    return -ENOMEM;
  }
  memcpy(pattern, path, path_len);
  size_t pos = path_len;
  if (needs_separator) {
    pattern[pos++] = '\\';
  }
  pattern[pos++] = '*';
  pattern[pos] = '\0';

  dir->handle = FindFirstFileA(pattern, &dir->entry);
  DWORD error = GetLastError();
  free(pattern);
  if (dir->handle == INVALID_HANDLE_VALUE) {
    free(dir);
    return err_from_win32(error);
  }
  dir->first_pending = true;
#else
  dir->handle = opendir(path);
  if (!dir->handle) {
    int error = errno;
    free(dir);
    return -error;
  }
#endif

  *dir_out = dir;
  return 0;
}

int salts_fs_readdir(salts_fs_dir_t *dir, salts_fs_dirent_t *entry_out) {
  if (!dir || !entry_out) {
    return -EINVAL;
  }

#ifdef _WIN32
  for (;;) {
    if (dir->first_pending) {
      dir->first_pending = false;
    } else if (!FindNextFileA(dir->handle, &dir->entry)) {
      DWORD error = GetLastError();
      return error == ERROR_NO_MORE_FILES ? 0 : err_from_win32(error);
    }

    if (salts_fs_dirent_is_dot(dir->entry.cFileName)) {
      continue;
    }
    entry_out->name = dir->entry.cFileName;
    entry_out->type = salts_fs_dirent_type_win32(&dir->entry);
    return 1;
  }
#else
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir->handle);
    if (!entry) {
      return errno == 0 ? 0 : err_from_errno();
    }
    if (salts_fs_dirent_is_dot(entry->d_name)) {
      continue;
    }
    entry_out->name = entry->d_name;
#ifdef DT_UNKNOWN
    entry_out->type = salts_fs_dirent_type_posix(entry->d_type);
#else
    entry_out->type = SALTS_FS_DIRENT_UNKNOWN;
#endif
    return 1;
  }
#endif
}

int salts_fs_closedir(salts_fs_dir_t *dir) {
  if (!dir) {
    return -EINVAL;
  }

#ifdef _WIN32
  BOOL closed = FindClose(dir->handle);
  DWORD error = closed ? ERROR_SUCCESS : GetLastError();
  free(dir);
  return closed ? 0 : err_from_win32(error);
#else
  int closed = closedir(dir->handle);
  int error = errno;
  free(dir);
  return closed == 0 ? 0 : -error;
#endif
}

int salts_fs_unlink(const char *path) {
  if (!path) {
    return -EINVAL;
  }

  if (fs_unlink(path) != 0) {
    int e = err_from_errno();
    return e;
  }

  return 0;
}

/* ── Buffer Utilities ────────────────────────────────────────────────────── */

salts_fs_buf_t salts_fs_buf_init(char *base, size_t len) {
  salts_fs_buf_t buf;
  buf.base = base;
  buf.len  = len;
  return buf;
}

void salts_fs_buf_free(salts_fs_buf_t *buf) {
  if (buf && buf->base) {
    free(buf->base);
    buf->base = NULL;
    buf->len  = 0;
  }
}

int salts_fs_get_tmpdir(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return -EINVAL;
  }

#ifdef _WIN32
  DWORD n = GetTempPathA((DWORD)buffer_size, buffer);
  if (n == 0 || n >= buffer_size) {
    return -ERANGE;
  }
  /* Strip trailing backslash for consistency */
  if (n > 1 && buffer[n - 1] == '\\') {
    buffer[n - 1] = '\0';
  }
#else
  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = getenv("TEMP");
  if (!tmp) tmp = "/tmp";

  size_t len = strlen(tmp);
  if (len >= buffer_size) {
    return -ERANGE;
  }
  memcpy(buffer, tmp, len + 1);
#endif

  return 0;
}

/* ── Path Utilities ──────────────────────────────────────────────────────── */

bool salts_fs_path_is_absolute(const char *path) {
  if (!path || path[0] == '\0') {
    return false;
  }
#ifdef _WIN32
  return (path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
         (path[0] == '\\' && path[1] == '\\');
#else
  return path[0] == '/';
#endif
}

int salts_fs_path_join(char *result, size_t result_size, const char *base, const char *path) {
  if (!result || !base || !path || result_size == 0) {
    return -EINVAL;
  }

  size_t base_len = strlen(base);
  size_t path_len = strlen(path);
  bool need_sep   = false;

  if (base_len > 0) {
    char last = base[base_len - 1];
#ifdef _WIN32
    need_sep = (last != '\\' && last != '/');
#else
    need_sep = (last != '/');
#endif
  }

  size_t required = base_len + (need_sep ? 1 : 0) + path_len + 1;
  if (required > result_size) {
    return -ENAMETOOLONG;
  }

  strcpy(result, base);
  if (need_sep) {
#ifdef _WIN32
    strcat(result, "\\");
#else
    strcat(result, "/");
#endif
  }
  strcat(result, path);
  return 0;
}

int salts_fs_path_dirname(const char *path, char *dirname, size_t dirname_size) {
  if (!path || !dirname || dirname_size == 0) {
    return -EINVAL;
  }

  size_t path_len = strlen(path);
  if (path_len == 0) {
    if (dirname_size < 2) return -ENOMEM;
    strcpy(dirname, ".");
    return 0;
  }

  const char *last_sep = NULL;
  for (size_t i = 0; i < path_len; i++) {
#ifdef _WIN32
    if (path[i] == '\\' || path[i] == '/') {
#else
    if (path[i] == '/') {
#endif
      last_sep = &path[i];
    }
  }

  if (!last_sep) {
    if (dirname_size < 2) return -ENOMEM;
    strcpy(dirname, ".");
    return 0;
  }

  size_t dlen = (size_t)(last_sep - path);
  if (dlen == 0) dlen = 1; /* root: "/" or "\" */

  if (dlen + 1 > dirname_size) return -ENAMETOOLONG;

  strncpy(dirname, path, dlen);
  dirname[dlen] = '\0';
  return 0;
}

int salts_fs_path_basename(const char *path, char *basename, size_t basename_size) {
  if (!path || !basename || basename_size == 0) {
    return -EINVAL;
  }

  size_t path_len = strlen(path);
  if (path_len == 0) {
    if (basename_size < 2) return -ENOMEM;
    strcpy(basename, ".");
    return 0;
  }

  const char *last_sep = NULL;
  for (size_t i = 0; i < path_len; i++) {
#ifdef _WIN32
    if (path[i] == '\\' || path[i] == '/') {
#else
    if (path[i] == '/') {
#endif
      last_sep = &path[i];
    }
  }

  const char *base_start = last_sep ? last_sep + 1 : path;
  size_t      base_len   = path_len - (size_t)(base_start - path);

  if (base_len + 1 > basename_size) return -ENAMETOOLONG;

  strcpy(basename, base_start);
  return 0;
}

/* ── Streaming File Operations ───────────────────────────────────────────── */

salts_file_t salts_fs_open(const char *path, int flags, int mode) {
  if (!path) {
    return SALTS_INVALID_FILE;
  }

  int native_flags = salts_fs_flags_to_native(flags);
  int fd           = fs_open(path, native_flags, mode);

  if (fd < 0) {
    return SALTS_INVALID_FILE;
  }

  return (salts_file_t)fd;
}

int salts_fs_ftruncate(salts_file_t fd, int64_t length) {
  if (fd == SALTS_INVALID_FILE) {
    return -EINVAL;
  }

  if (fs_ftrunc(fd, length) != 0) {
    return err_from_errno();
  }
  return 0;
}

int salts_fs_read(salts_file_t fd, char *buf, size_t len) {
  if (fd == SALTS_INVALID_FILE || !buf) {
    return -EINVAL;
  }
  int rc = salts_fs_stream_len_ok(len);
  if (rc != 0) {
    return rc;
  }

  ssize_t n = salts_fs_read_once(fd, buf, len);
  if (n < 0) {
    return err_from_errno();
  }
  return (int)n;
}

int salts_fs_pread(salts_file_t fd, char *buf, size_t len, int64_t offset) {
  if (fd == SALTS_INVALID_FILE || !buf) {
    return -EINVAL;
  }
  int rc = salts_fs_stream_len_ok(len);
  if (rc != 0) return rc;
  rc = salts_fs_offset_ok(offset, len);
  if (rc != 0) return rc;

  ssize_t n = salts_fs_pread_once(fd, buf, len, offset);
  if (n < 0) return err_from_errno();
  return (int)n;
}

int salts_fs_pwrite(salts_file_t fd, const char *data, size_t len, int64_t offset) {
  if (fd == SALTS_INVALID_FILE || !data) {
    return -EINVAL;
  }
  int rc = salts_fs_stream_len_ok(len);
  if (rc != 0) return rc;

  rc = salts_fs_pwrite_all(fd, data, len, offset);
  if (rc != 0) return rc;
  return (int)len;
}

int salts_fs_write(salts_file_t fd, const char *data, size_t len) {
  if (fd == SALTS_INVALID_FILE || !data) {
    return -EINVAL;
  }
  int rc = salts_fs_stream_len_ok(len);
  if (rc != 0) return rc;

  rc = salts_fs_write_all(fd, data, len);
  if (rc != 0) return rc;
  return (int)len;
}

int salts_fs_close(salts_file_t fd) {
  if (fd == SALTS_INVALID_FILE) {
    return -EINVAL;
  }

  if (fs_close(fd) != 0) {
    return err_from_errno();
  }
  return 0;
}

int salts_fs_fsync(salts_file_t fd) {
  if (fd == SALTS_INVALID_FILE) {
    return -EINVAL;
  }

  if (fs_fsync(fd) != 0) {
    return err_from_errno();
  }
  return 0;
}

int salts_fs_lock(salts_file_t fd, int flags, int64_t offset, uint64_t len) {
  if (fd == SALTS_INVALID_FILE || offset < 0) {
    return -EINVAL;
  }
  bool shared = (flags & SALTS_FS_LOCK_SHARED) != 0;
  bool exclusive = (flags & SALTS_FS_LOCK_EXCLUSIVE) != 0;
  if (shared == exclusive || (flags & ~(SALTS_FS_LOCK_SHARED | SALTS_FS_LOCK_EXCLUSIVE |
                                        SALTS_FS_LOCK_NONBLOCK)) != 0) {
    return -EINVAL;
  }

#ifdef _WIN32
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1) {
    return -EBADF;
  }
  OVERLAPPED ov;
  memset(&ov, 0, sizeof(ov));
  uint64_t uoffset = (uint64_t)offset;
  ov.Offset = (DWORD)(uoffset & 0xffffffffu);
  ov.OffsetHigh = (DWORD)(uoffset >> 32);

  uint64_t lock_len = len == 0 ? UINT64_MAX : len;
  DWORD low = (DWORD)(lock_len & 0xffffffffu);
  DWORD high = (DWORD)(lock_len >> 32);
  DWORD lock_flags = 0;
  if (exclusive) lock_flags |= LOCKFILE_EXCLUSIVE_LOCK;
  if (flags & SALTS_FS_LOCK_NONBLOCK) lock_flags |= LOCKFILE_FAIL_IMMEDIATELY;

  if (!LockFileEx((HANDLE)os_handle, lock_flags, 0, low, high, &ov)) {
    return err_from_win32(GetLastError());
  }
  return 0;
#else
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = shared ? F_RDLCK : F_WRLCK;
  fl.l_whence = SEEK_SET;
  int rc = salts_fs_offset_to_off_t(offset, &fl.l_start);
  if (rc != 0) return rc;
  if (len == 0) {
    fl.l_len = 0;
  } else if (len > (uint64_t)INT64_MAX || (uint64_t)(off_t)len != len) {
    return -EOVERFLOW;
  } else {
    fl.l_len = (off_t)len;
  }
  if (fcntl(fd, (flags & SALTS_FS_LOCK_NONBLOCK) ? F_SETLK : F_SETLKW, &fl) != 0) {
    return err_from_errno();
  }
  return 0;
#endif
}

int salts_fs_unlock(salts_file_t fd, int64_t offset, uint64_t len) {
  if (fd == SALTS_INVALID_FILE || offset < 0) {
    return -EINVAL;
  }

#ifdef _WIN32
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1) {
    return -EBADF;
  }
  OVERLAPPED ov;
  memset(&ov, 0, sizeof(ov));
  uint64_t uoffset = (uint64_t)offset;
  ov.Offset = (DWORD)(uoffset & 0xffffffffu);
  ov.OffsetHigh = (DWORD)(uoffset >> 32);

  uint64_t lock_len = len == 0 ? UINT64_MAX : len;
  DWORD low = (DWORD)(lock_len & 0xffffffffu);
  DWORD high = (DWORD)(lock_len >> 32);
  if (!UnlockFileEx((HANDLE)os_handle, 0, low, high, &ov)) {
    return err_from_win32(GetLastError());
  }
  return 0;
#else
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = F_UNLCK;
  fl.l_whence = SEEK_SET;
  int rc = salts_fs_offset_to_off_t(offset, &fl.l_start);
  if (rc != 0) return rc;
  if (len == 0) {
    fl.l_len = 0;
  } else if (len > (uint64_t)INT64_MAX || (uint64_t)(off_t)len != len) {
    return -EOVERFLOW;
  } else {
    fl.l_len = (off_t)len;
  }
  if (fcntl(fd, F_SETLK, &fl) != 0) {
    return err_from_errno();
  }
  return 0;
#endif
}

int salts_fs_rename(const char *old_path, const char *new_path) {
  if (!old_path || !new_path) {
    return -EINVAL;
  }

#ifdef _WIN32
  if (!MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING)) {
    return err_from_win32(GetLastError());
  }
#else
  if (rename(old_path, new_path) != 0) {
    return err_from_errno();
  }
#endif
  return 0;
}

int salts_fs_replace_durable(const char *staging_path,
                             const char *destination_path,
                             salts_fs_replace_state_t *state) {
  salts_file_t staging;
  int rc;

  if (state == NULL)
    return -EINVAL;
  *state = SALTS_FS_REPLACE_NOT_PUBLISHED;

  if (staging_path == NULL || destination_path == NULL ||
      staging_path[0] == '\0' || destination_path[0] == '\0' ||
      strcmp(staging_path, destination_path) == 0)
    return -EINVAL;

#ifndef _WIN32
  char parent[SALTS_FS_MAX_PATH];
  rc = salts_fs_path_dirname(destination_path, parent, sizeof(parent));
  if (rc != 0)
    return rc;
#endif

  {
    const int native_flags = salts_fs_flags_to_native(SALTS_FS_O_RDWR);
    const int native_fd = fs_open(staging_path, native_flags, 0);
    if (native_fd < 0)
      return err_from_errno();
    staging = (salts_file_t)native_fd;
  }

  rc = salts_fs_fsync(staging);
  if (rc != 0) {
    (void)salts_fs_close(staging);
    return rc;
  }
  rc = salts_fs_close(staging);
  if (rc != 0)
    return rc;

#ifdef _WIN32
  if (!MoveFileExA(staging_path, destination_path,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return err_from_win32(GetLastError());

  *state = SALTS_FS_REPLACE_PUBLISHED_DURABLE;
  return 0;
#else
  if (rename(staging_path, destination_path) != 0)
    return err_from_errno();

  *state = SALTS_FS_REPLACE_DURABILITY_UNKNOWN;

  {
    int dir_flags = O_RDONLY;
    int parent_fd;
#ifdef O_DIRECTORY
    dir_flags |= O_DIRECTORY;
#endif
    parent_fd = open(parent, dir_flags);
    if (parent_fd < 0)
      return err_from_errno();

    if (fsync(parent_fd) != 0) {
      const int error = err_from_errno();
      (void)close(parent_fd);
      return error;
    }
    (void)close(parent_fd);
  }

  *state = SALTS_FS_REPLACE_PUBLISHED_DURABLE;
  return 0;
#endif
}

int64_t salts_fs_tell(salts_file_t fd) {
  if (fd == SALTS_INVALID_FILE) {
    return -EINVAL;
  }

#ifdef _WIN32
  salts_fs_lock_position();
#endif
  int64_t pos = fs_lseek(fd, 0, SEEK_CUR);
  int e = errno;
#ifdef _WIN32
  salts_fs_unlock_position();
#endif
  errno = e;
  if (pos < 0) return err_from_errno();
  return pos;
}

int64_t salts_fs_seek(salts_file_t fd, int64_t offset, int whence) {
  if (fd == SALTS_INVALID_FILE) {
    return -EINVAL;
  }

#ifdef _WIN32
  salts_fs_lock_position();
#endif
  int64_t pos = fs_lseek(fd, offset, whence);
  int e = errno;
#ifdef _WIN32
  salts_fs_unlock_position();
#endif
  errno = e;
  if (pos < 0) return err_from_errno();
  return pos;
}
