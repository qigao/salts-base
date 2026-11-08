/* Original qigao/salts utils/include/cmeta_mmap.h blob ba7aecd588492814ebca196d66023dfc9f24801c.
 * Mechanically replace only the obsolete CMeta platform umbrella with the
 * current plain C11 Core export contract. All 18 original mmap entrypoints,
 * types, data accessors and error/status semantics remain here.
 */
/**
 * @file cmeta_mmap.h
 * @brief Cross-platform Memory-Mapped File I/O
 * @author Inspired by vimpunk/mio, implemented in pure C11
 *
 * Zero-copy file access - map files directly into process address space.
 * Like mio but without C++ templates and Boost dependencies.
 */

#ifndef SALTS_MMAP_H
#define SALTS_MMAP_H

#include "cmeta_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Types
// =============================================================================

/** Access mode for memory mapping */
typedef enum {
    SALTS_MMAP_READ  = 0x01,  /**< Read-only access */
    SALTS_MMAP_WRITE = 0x02,  /**< Write access (implies read) */
    SALTS_MMAP_EXEC  = 0x04,  /**< Execute permission (rare) */
} cmeta_mmap_access_t;

/** Mapping advice hints for optimization */
typedef enum {
    SALTS_MMAP_NORMAL     = 0,  /**< No special treatment */
    SALTS_MMAP_SEQUENTIAL = 1,  /**< Sequential access pattern */
    SALTS_MMAP_RANDOM     = 2,  /**< Random access pattern */
    SALTS_MMAP_WILLNEED   = 3,  /**< Will need this data soon */
    SALTS_MMAP_DONTNEED   = 4,  /**< Won't need this data soon */
} cmeta_mmap_advice_t;

/** Memory-mapped file handle */
typedef struct {
    void    *data;           /**< Mapped memory address (user-visible) */
    size_t   length;         /**< User-requested length */
    size_t   mapped_length;  /**< Actual mapped length (page-aligned) */
    int64_t  offset;         /**< User-requested offset */
    int64_t  aligned_offset; /**< Page-aligned offset */
    int      access;         /**< Access mode (SALTS_MMAP_*) */
    bool     is_mapped;      /**< True if currently mapped */
#ifdef _WIN32
    void    *file_handle;    /**< HANDLE to file */
    void    *map_handle;     /**< HANDLE to file mapping object */
#else
    int      fd;             /**< File descriptor */
    bool     owns_fd;        /**< True if we should close fd */
#endif
} cmeta_mmap_t;

// =============================================================================
// Core API
// =============================================================================

/**
 * @brief Initialize a mmap handle to empty state
 * @param mmap Handle to initialize
 */
SALTS_C_API void cmeta_mmap_init(cmeta_mmap_t *mmap);

/**
 * @brief Map an entire file into memory
 *
 * @param mmap Handle to store mapping
 * @param path File path to map
 * @param access Access mode (SALTS_MMAP_READ or SALTS_MMAP_WRITE)
 * @return 0 on success, negative error code on failure
 *
 * @note File must exist. For write access, file must not be empty.
 */
SALTS_C_API int cmeta_mmap_open(cmeta_mmap_t *mmap, const char *path, int access);

/**
 * @brief Map a portion of a file into memory
 *
 * @param mmap Handle to store mapping
 * @param path File path to map
 * @param offset Byte offset into file (will be aligned to page boundary)
 * @param length Number of bytes to map (0 = entire file from offset)
 * @param access Access mode (SALTS_MMAP_READ or SALTS_MMAP_WRITE)
 * @return 0 on success, negative error code on failure
 *
 * @note Offset is automatically aligned to system page boundary.
 *       The returned data pointer accounts for this alignment.
 */
SALTS_C_API int cmeta_mmap_open_range(cmeta_mmap_t *mmap, const char *path,
                                    int64_t offset, size_t length, int access);

/**
 * @brief Map using an existing file descriptor (POSIX) or handle (Windows)
 *
 * @param mmap Handle to store mapping
 * @param fd File descriptor (POSIX) or HANDLE cast to intptr_t (Windows)
 * @param offset Byte offset into file
 * @param length Number of bytes to map (0 = entire file from offset)
 * @param access Access mode
 * @return 0 on success, negative error code on failure
 *
 * @note Does NOT take ownership of fd/handle - caller must close it.
 */
SALTS_C_API int cmeta_mmap_from_fd(cmeta_mmap_t *mmap, intptr_t fd,
                                  int64_t offset, size_t length, int access);

/**
 * @brief Sync mapped memory to disk
 *
 * @param mmap Mapping to sync
 * @param async If true, return immediately (async flush)
 * @return 0 on success, negative error code on failure
 */
SALTS_C_API int cmeta_mmap_sync(cmeta_mmap_t *mmap, bool async);

/**
 * @brief Sync a specific range to disk
 *
 * @param mmap Mapping to sync
 * @param offset Offset within mapped region
 * @param length Length to sync
 * @param async If true, return immediately
 * @return 0 on success, negative error code on failure
 */
SALTS_C_API int cmeta_mmap_sync_range(cmeta_mmap_t *mmap, size_t offset,
                                     size_t length, bool async);

/**
 * @brief Unmap the file from memory
 *
 * @param mmap Mapping to unmap
 *
 * @note Safe to call on already-unmapped or uninitialized handle.
 *       Does NOT sync - call cmeta_mmap_sync() first if needed.
 */
SALTS_C_API void cmeta_mmap_unmap(cmeta_mmap_t *mmap);

/**
 * @brief Close the mapping and release all resources
 *
 * @param mmap Mapping to close
 *
 * @note Calls unmap internally. Safe to call multiple times.
 */
SALTS_C_API void cmeta_mmap_close(cmeta_mmap_t *mmap);

// =============================================================================
// Accessors
// =============================================================================

/** Get pointer to mapped data */
static inline void *cmeta_mmap_data(const cmeta_mmap_t *mmap) {
    return mmap ? mmap->data : NULL;
}

/** Get length of mapped region */
static inline size_t cmeta_mmap_size(const cmeta_mmap_t *mmap) {
    return mmap ? mmap->length : 0;
}

/** Check if mapping is valid */
static inline bool cmeta_mmap_is_open(const cmeta_mmap_t *mmap) {
    return mmap && mmap->is_mapped;
}

/** Get byte at offset (no bounds checking) */
static inline uint8_t cmeta_mmap_get(const cmeta_mmap_t *mmap, size_t offset) {
    return ((const uint8_t *)mmap->data)[offset];
}

/** Set byte at offset (no bounds checking, requires write access) */
static inline void cmeta_mmap_set(cmeta_mmap_t *mmap, size_t offset, uint8_t value) {
    ((uint8_t *)mmap->data)[offset] = value;
}

// =============================================================================
// Utilities
// =============================================================================

/**
 * @brief Get system page size
 * @return Page size in bytes
 */
SALTS_C_API size_t cmeta_mmap_page_size(void);

/**
 * @brief Calculate number of pages needed for a given size
 * @param size Size in bytes
 * @return Number of pages (rounded up)
 */
SALTS_C_API size_t cmeta_mmap_page_count(size_t size);

/**
 * @brief Get number of pages for a mapped region
 * @param mmap Mapping handle
 * @return Number of pages, or 0 if not mapped
 */
SALTS_C_API size_t cmeta_mmap_pages(const cmeta_mmap_t *mmap);

/**
 * @brief Advise kernel about access pattern
 *
 * @param mmap Mapping to advise
 * @param advice Access pattern hint
 * @return 0 on success, negative error code on failure
 *
 * @note This is a hint only - kernel may ignore it.
 */
SALTS_C_API int cmeta_mmap_advise(cmeta_mmap_t *mmap, cmeta_mmap_advice_t advice);

/**
 * @brief Lock mapped pages in physical memory (prevent swapping)
 *
 * @param mmap Mapping to lock
 * @return 0 on success, negative error code on failure
 *
 * @note Requires appropriate privileges on most systems.
 */
SALTS_C_API int cmeta_mmap_lock(cmeta_mmap_t *mmap);

/**
 * @brief Unlock mapped pages (allow swapping)
 *
 * @param mmap Mapping to unlock
 * @return 0 on success, negative error code on failure
 */
SALTS_C_API int cmeta_mmap_unlock(cmeta_mmap_t *mmap);

// =============================================================================
// Group Mapping (Map multiple files into one contiguous address space)
// =============================================================================

/**
 * @brief Group of mapped files, appearing as one contiguous buffer
 */
typedef struct {
    void    *data;           /**< Start of contiguous virtual memory */
    size_t   total_size;     /**< Total size of all mapped files combined */
    size_t   count;          /**< Number of files in the group */
    cmeta_mmap_t *mappings;  /**< Individual mappings */
} cmeta_mmap_group_t;

/**
 * @brief Initialize a group handle
 */
SALTS_C_API void cmeta_mmap_group_init(cmeta_mmap_group_t *group);

/**
 * @brief Map several files into one contiguous block of virtual memory
 *
 * @param group Group handle to initialize
 * @param paths Array of file paths
 * @param count Number of paths
 * @param access Access mode (READ or WRITE)
 * @return 0 on success, negative error code on failure
 *
 * @note This is highly efficient for segmented logs or datasets.
 *       The resulting 'group->data' can be treated as one huge array.
 */
SALTS_C_API int cmeta_mmap_group_open(cmeta_mmap_group_t *group, const char **paths,
                                     size_t count, int access);

/**
 * @brief Close all mappings in the group and release the address space
 */
SALTS_C_API void cmeta_mmap_group_close(cmeta_mmap_group_t *group);

// =============================================================================
// Error codes (negative values)
// =============================================================================

#define SALTS_MMAP_OK           0
#define SALTS_MMAP_EINVAL      -1   /**< Invalid argument */
#define SALTS_MMAP_ENOENT      -2   /**< File not found */
#define SALTS_MMAP_EACCES      -3   /**< Permission denied */
#define SALTS_MMAP_ENOMEM      -4   /**< Out of memory */
#define SALTS_MMAP_EEXIST      -5   /**< Already mapped */
#define SALTS_MMAP_EIO         -6   /**< I/O error */
#define SALTS_MMAP_ENOSYS      -7   /**< Not supported */
#define SALTS_MMAP_EEMPTY      -8   /**< File is empty */

/**
 * @brief Get error message for error code
 * @param err Error code
 * @return Human-readable error message
 */
SALTS_C_API const char *cmeta_mmap_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif // SALTS_MMAP_H
