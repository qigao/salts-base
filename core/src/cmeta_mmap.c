/* Original qigao/salts utils/src/cmeta_mmap.c blob 04e87de7bc565d77261e2de66c84380b85a0ac15.
 * Retains native POSIX mmap/Windows FileMapping owner and all original entrypoints.
 * No CMeta/CFlow runtime, compatibility provider, or alternate implementation.
 */
/**
 * @file cmeta_mmap.c
 * @brief Cross-platform Memory-Mapped File I/O Implementation
 *
 * Windows: CreateFileMapping + MapViewOfFile
 * POSIX:   mmap + munmap
 */

#include "cmeta_mmap.h"
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN 1
  #endif
  #include <windows.h>
#endif

#ifdef _WIN32
  #include <io.h>
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

// =============================================================================
// Platform-specific helpers
// =============================================================================

static size_t get_page_size(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwAllocationGranularity;
#else
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (size_t)ps : 4096;
#endif
}

static int64_t align_offset(int64_t offset, size_t page_size) {
    return (offset / (int64_t)page_size) * (int64_t)page_size;
}

static int64_t get_file_size_fd(intptr_t fd) {
#ifdef _WIN32
    LARGE_INTEGER size;
    if (!GetFileSizeEx((HANDLE)fd, &size)) {
        return -1;
    }
    return (int64_t)size.QuadPart;
#else
    struct stat st;
    if (fstat((int)fd, &st) != 0) {
        return -1;
    }
    return (int64_t)st.st_size;
#endif
}

// =============================================================================
// Core Implementation
// =============================================================================

SALTS_C_API void cmeta_mmap_init(cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr) return;
    memset(mmap_ptr, 0, sizeof(*mmap_ptr));
#ifdef _WIN32
    mmap_ptr->file_handle = INVALID_HANDLE_VALUE;
    mmap_ptr->map_handle = NULL;
#else
    mmap_ptr->fd = -1;
    mmap_ptr->owns_fd = false;
#endif
}

SALTS_C_API size_t cmeta_mmap_page_size(void) {
    return get_page_size();
}

SALTS_C_API size_t cmeta_mmap_page_count(size_t size) {
    size_t page_size = get_page_size();
    return (size + page_size - 1) / page_size;
}

SALTS_C_API size_t cmeta_mmap_pages(const cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) return 0;
    size_t page_size = get_page_size();
    return (mmap_ptr->length + page_size - 1) / page_size;
}

SALTS_C_API int cmeta_mmap_open(cmeta_mmap_t *mmap_ptr, const char *path, int access) {
    return cmeta_mmap_open_range(mmap_ptr, path, 0, 0, access);
}

SALTS_C_API int cmeta_mmap_open_range(cmeta_mmap_t *mmap_ptr, const char *path,
                                    int64_t offset, size_t length, int access) {
    if (!mmap_ptr || !path) {
        return SALTS_MMAP_EINVAL;
    }
    if (mmap_ptr->is_mapped) {
        return SALTS_MMAP_EEXIST;
    }

    cmeta_mmap_init(mmap_ptr);

#ifdef _WIN32
    // Open file
    DWORD desired_access = GENERIC_READ;
    DWORD share_mode = FILE_SHARE_READ;
    if (access & SALTS_MMAP_WRITE) {
        desired_access |= GENERIC_WRITE;
        share_mode |= FILE_SHARE_WRITE;
    }

    HANDLE hFile = CreateFileA(path, desired_access, share_mode, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return SALTS_MMAP_ENOENT;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return SALTS_MMAP_EACCES;
        }
        return SALTS_MMAP_EIO;
    }

    mmap_ptr->file_handle = hFile;
    int result = cmeta_mmap_from_fd(mmap_ptr, (intptr_t)hFile, offset, length, access);
    if (result != SALTS_MMAP_OK) {
        CloseHandle(hFile);
        mmap_ptr->file_handle = INVALID_HANDLE_VALUE;
    }
    return result;

#else
    // Open file
    int flags = (access & SALTS_MMAP_WRITE) ? O_RDWR : O_RDONLY;
    int fd = open(path, flags);
    if (fd < 0) {
        if (errno == ENOENT) return SALTS_MMAP_ENOENT;
        if (errno == EACCES) return SALTS_MMAP_EACCES;
        return SALTS_MMAP_EIO;
    }

    mmap_ptr->fd = fd;
    mmap_ptr->owns_fd = true;
    int result = cmeta_mmap_from_fd(mmap_ptr, fd, offset, length, access);
    if (result != SALTS_MMAP_OK) {
        close(fd);
        mmap_ptr->fd = -1;
        mmap_ptr->owns_fd = false;
    }
    return result;
#endif
}

SALTS_C_API int cmeta_mmap_from_fd(cmeta_mmap_t *mmap_ptr, intptr_t fd,
                                 int64_t offset, size_t length, int access) {
    if (!mmap_ptr) {
        return SALTS_MMAP_EINVAL;
    }

    // Get file size
    int64_t file_size = get_file_size_fd(fd);
    if (file_size < 0) {
        return SALTS_MMAP_EIO;
    }
    if (file_size == 0) {
        return SALTS_MMAP_EEMPTY;
    }

    // Calculate mapping range
    if (offset < 0 || offset >= file_size) {
        return SALTS_MMAP_EINVAL;
    }

    if (length == 0) {
        length = (size_t)(file_size - offset);
    }
    if (offset + (int64_t)length > file_size) {
        length = (size_t)(file_size - offset);
    }

    // Align offset to page boundary
    size_t page_size = get_page_size();
    int64_t aligned_offset = align_offset(offset, page_size);
    size_t offset_diff = (size_t)(offset - aligned_offset);
    size_t mapped_length = length + offset_diff;

    mmap_ptr->offset = offset;
    mmap_ptr->aligned_offset = aligned_offset;
    mmap_ptr->length = length;
    mmap_ptr->mapped_length = mapped_length;
    mmap_ptr->access = access;

#ifdef _WIN32
    // Create file mapping
    DWORD protect = (access & SALTS_MMAP_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
    HANDLE hMap = CreateFileMappingA((HANDLE)fd, NULL, protect, 0, 0, NULL);
    if (!hMap) {
        return SALTS_MMAP_ENOMEM;
    }

    // Map view
    DWORD map_access = (access & SALTS_MMAP_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
    DWORD offset_high = (DWORD)(aligned_offset >> 32);
    DWORD offset_low = (DWORD)(aligned_offset & 0xFFFFFFFF);

    void *ptr = MapViewOfFile(hMap, map_access, offset_high, offset_low, mapped_length);
    if (!ptr) {
        CloseHandle(hMap);
        return SALTS_MMAP_ENOMEM;
    }

    mmap_ptr->map_handle = hMap;
    mmap_ptr->data = (char *)ptr + offset_diff;
    mmap_ptr->is_mapped = true;

#else
    // POSIX mmap
    int prot = PROT_READ;
    if (access & SALTS_MMAP_WRITE) prot |= PROT_WRITE;
    if (access & SALTS_MMAP_EXEC) prot |= PROT_EXEC;

    void *ptr = mmap(NULL, mapped_length, prot, MAP_SHARED, (int)fd, aligned_offset);
    if (ptr == MAP_FAILED) {
        if (errno == ENOMEM) return SALTS_MMAP_ENOMEM;
        if (errno == EACCES) return SALTS_MMAP_EACCES;
        return SALTS_MMAP_EIO;
    }

    mmap_ptr->data = (char *)ptr + offset_diff;
    mmap_ptr->is_mapped = true;
#endif

    return SALTS_MMAP_OK;
}

SALTS_C_API int cmeta_mmap_sync(cmeta_mmap_t *mmap_ptr, bool async) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return SALTS_MMAP_EINVAL;
    }
    return cmeta_mmap_sync_range(mmap_ptr, 0, mmap_ptr->length, async);
}

SALTS_C_API int cmeta_mmap_sync_range(cmeta_mmap_t *mmap_ptr, size_t offset,
                                    size_t length, bool async) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return SALTS_MMAP_EINVAL;
    }
    if (offset + length > mmap_ptr->length) {
        return SALTS_MMAP_EINVAL;
    }

    // Get actual mapped base (before user offset adjustment)
    size_t offset_diff = (size_t)(mmap_ptr->offset - mmap_ptr->aligned_offset);
    char *base = (char *)mmap_ptr->data - offset_diff;

#ifdef _WIN32
    (void)async; // Windows FlushViewOfFile is always sync
    if (!FlushViewOfFile(base + offset_diff + offset, length)) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#else
    int flags = async ? MS_ASYNC : MS_SYNC;
    if (msync(base + offset_diff + offset, length, flags) != 0) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#endif
}

SALTS_C_API void cmeta_mmap_unmap(cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return;
    }

    // Get actual mapped base
    size_t offset_diff = (size_t)(mmap_ptr->offset - mmap_ptr->aligned_offset);
    char *base = (char *)mmap_ptr->data - offset_diff;

#ifdef _WIN32
    UnmapViewOfFile(base);
    if (mmap_ptr->map_handle) {
        CloseHandle(mmap_ptr->map_handle);
        mmap_ptr->map_handle = NULL;
    }
#else
    munmap(base, mmap_ptr->mapped_length);
#endif

    mmap_ptr->data = NULL;
    mmap_ptr->is_mapped = false;
}

SALTS_C_API void cmeta_mmap_close(cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr) return;

    cmeta_mmap_unmap(mmap_ptr);

#ifdef _WIN32
    if (mmap_ptr->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(mmap_ptr->file_handle);
        mmap_ptr->file_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (mmap_ptr->owns_fd && mmap_ptr->fd >= 0) {
        close(mmap_ptr->fd);
    }
    mmap_ptr->fd = -1;
    mmap_ptr->owns_fd = false;
#endif
}

// =============================================================================
// Utilities
// =============================================================================

SALTS_C_API int cmeta_mmap_advise(cmeta_mmap_t *mmap_ptr, cmeta_mmap_advice_t advice) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return SALTS_MMAP_EINVAL;
    }

#ifdef _WIN32
    // Windows doesn't have madvise equivalent
    (void)advice;
    return SALTS_MMAP_OK;
#else
    int posix_advice;
    switch (advice) {
        case SALTS_MMAP_SEQUENTIAL: posix_advice = MADV_SEQUENTIAL; break;
        case SALTS_MMAP_RANDOM:     posix_advice = MADV_RANDOM; break;
        case SALTS_MMAP_WILLNEED:   posix_advice = MADV_WILLNEED; break;
        case SALTS_MMAP_DONTNEED:   posix_advice = MADV_DONTNEED; break;
        default:                    posix_advice = MADV_NORMAL; break;
    }

    size_t offset_diff = (size_t)(mmap_ptr->offset - mmap_ptr->aligned_offset);
    char *base = (char *)mmap_ptr->data - offset_diff;

    if (madvise(base, mmap_ptr->mapped_length, posix_advice) != 0) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#endif
}

SALTS_C_API int cmeta_mmap_lock(cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return SALTS_MMAP_EINVAL;
    }

    size_t offset_diff = (size_t)(mmap_ptr->offset - mmap_ptr->aligned_offset);
    char *base = (char *)mmap_ptr->data - offset_diff;

#ifdef _WIN32
    if (!VirtualLock(base, mmap_ptr->mapped_length)) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#else
    if (mlock(base, mmap_ptr->mapped_length) != 0) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#endif
}

SALTS_C_API int cmeta_mmap_unlock(cmeta_mmap_t *mmap_ptr) {
    if (!mmap_ptr || !mmap_ptr->is_mapped) {
        return SALTS_MMAP_EINVAL;
    }

    size_t offset_diff = (size_t)(mmap_ptr->offset - mmap_ptr->aligned_offset);
    char *base = (char *)mmap_ptr->data - offset_diff;

#ifdef _WIN32
    if (!VirtualUnlock(base, mmap_ptr->mapped_length)) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#else
    if (munlock(base, mmap_ptr->mapped_length) != 0) {
        return SALTS_MMAP_EIO;
    }
    return SALTS_MMAP_OK;
#endif
}

// =============================================================================
// Group Mapping Implementation
// =============================================================================

SALTS_C_API void cmeta_mmap_group_init(cmeta_mmap_group_t *group) {
    if (!group) return;
    memset(group, 0, sizeof(*group));
}

SALTS_C_API int cmeta_mmap_group_open(cmeta_mmap_group_t *group, const char **paths,
                                     size_t count, int access) {
    if (!group || !paths || count == 0) return SALTS_MMAP_EINVAL;
    if (group->data) return SALTS_MMAP_EEXIST;

    size_t page_size = get_page_size();
    size_t total_mapped_size = 0;
    int result = SALTS_MMAP_OK;
    
    // First pass: calculate total size and get file handles
    group->mappings = (cmeta_mmap_t *)calloc(count, sizeof(cmeta_mmap_t));
    if (!group->mappings) return SALTS_MMAP_ENOMEM;

    for (size_t i = 0; i < count; i++) {
        cmeta_mmap_init(&group->mappings[i]);
#ifdef _WIN32
        HANDLE hFile = CreateFileA(paths[i], GENERIC_READ | (access & SALTS_MMAP_WRITE ? GENERIC_WRITE : 0),
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            result = SALTS_MMAP_ENOENT;
            goto cleanup;
        }
        group->mappings[i].file_handle = hFile;
        int64_t size = get_file_size_fd((intptr_t)hFile);
#else
        int fd = open(paths[i], (access & SALTS_MMAP_WRITE ? O_RDWR : O_RDONLY));
        if (fd < 0) {
            result = SALTS_MMAP_ENOENT;
            goto cleanup;
        }
        group->mappings[i].fd = fd;
        group->mappings[i].owns_fd = true;
        int64_t size = get_file_size_fd(fd);
#endif
        if (size <= 0) {
            result = SALTS_MMAP_EEMPTY;
            goto cleanup;
        }
        
        group->mappings[i].length = (size_t)size;
        // Each mapping must start on a page boundary for the next one to be aligned
        group->mappings[i].mapped_length = (size_t)((size + page_size - 1) & ~(page_size - 1));
        total_mapped_size += group->mappings[i].mapped_length;
    }

    // Second pass: Reserve address space to find a contiguous hole
#ifdef _WIN32
    void *base = VirtualAlloc(NULL, total_mapped_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) {
        result = SALTS_MMAP_ENOMEM;
        goto cleanup;
    }
    // Paradoxically, on Windows we MUST release the reservation before MapViewOfFileEx can use the address.
    // The reservation just served to "find" a suitable hole of total_mapped_size.
    VirtualFree(base, 0, MEM_RELEASE);
#else
    void *base = mmap(NULL, total_mapped_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        result = SALTS_MMAP_ENOMEM;
        goto cleanup;
    }
#endif

    group->data = base;
    group->total_size = total_mapped_size;
    group->count = count;

    // Third pass: Map each file into the reserved space
    char *curr = (char *)base;
    for (size_t i = 0; i < count; i++) {
        cmeta_mmap_t *m = &group->mappings[i];
#ifdef _WIN32
        DWORD protect = (access & SALTS_MMAP_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        m->map_handle = CreateFileMappingA(m->file_handle, NULL, protect, 0, 0, NULL);
        if (!m->map_handle) {
            result = SALTS_MMAP_ENOMEM;
            goto cleanup;
        }

        DWORD map_access = (access & SALTS_MMAP_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
        // MapViewOfFileEx will fail if the address is already "owned" (even if just reserved),
        // which is why we freed it above.
        void *view = MapViewOfFileEx(m->map_handle, map_access, 0, 0, m->length, curr);
        if (!view) {
            result = SALTS_MMAP_ENOMEM;
            goto cleanup;
        }
        m->data = view;
#else
        int prot = PROT_READ | (access & SALTS_MMAP_WRITE ? PROT_WRITE : 0);
        // On POSIX, MAP_FIXED will overwrite the anonymous mapping created earlier.
        void *view = mmap(curr, m->length, prot, MAP_SHARED | MAP_FIXED, m->fd, 0);
        if (view == MAP_FAILED) {
            result = SALTS_MMAP_EIO;
            goto cleanup;
        }
        m->data = view;
#endif
        m->is_mapped = true;
        curr += m->mapped_length;
    }

    return SALTS_MMAP_OK;

cleanup:
    // If we have group->data and are on POSIX, we might need to munmap the whole thing
    // if we haven't mapped segments over it yet.
#ifndef _WIN32
    if (group->data && group->total_size > 0) {
        munmap(group->data, group->total_size);
    }
#endif

    if (group->mappings) {
        for (size_t i = 0; i < count; i++) {
            cmeta_mmap_close(&group->mappings[i]);
        }
        free(group->mappings);
        group->mappings = NULL;
    }
    group->data = NULL;
    group->total_size = 0;
    group->count = 0;
    return result;
}

SALTS_C_API void cmeta_mmap_group_close(cmeta_mmap_group_t *group) {
    if (!group || !group->data) return;

    for (size_t i = 0; i < group->count; i++) {
        cmeta_mmap_close(&group->mappings[i]);
    }

    // On Windows, VirtualFree is only needed if not all views are unmapped, 
    // but here we unmapped them all via cmeta_mmap_close.
    // However, the reservation itself might need release.
#ifdef _WIN32
    VirtualFree(group->data, 0, MEM_RELEASE);
#else
    munmap(group->data, group->total_size);
#endif

    free(group->mappings);
    memset(group, 0, sizeof(*group));
}

SALTS_C_API const char *cmeta_mmap_strerror(int err) {
    switch (err) {
        case SALTS_MMAP_OK:     return "Success";
        case SALTS_MMAP_EINVAL: return "Invalid argument";
        case SALTS_MMAP_ENOENT: return "File not found";
        case SALTS_MMAP_EACCES: return "Permission denied";
        case SALTS_MMAP_ENOMEM: return "Out of memory";
        case SALTS_MMAP_EEXIST: return "Already mapped";
        case SALTS_MMAP_EIO:    return "I/O error";
        case SALTS_MMAP_ENOSYS: return "Not supported";
        case SALTS_MMAP_EEMPTY: return "File is empty";
        default:               return "Unknown error";
    }
}
