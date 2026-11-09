#ifndef SALTS_PLATFORM_UTILS_H
#define SALTS_PLATFORM_UTILS_H

#include <salts/api.h>

#include <salts/clock.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
  #define SALTS_WIN32 1
  #ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
    #define _SSIZE_T_DEFINED
  #endif
#endif

#ifndef UNUSED
  #define UNUSED(x) (void)(x)
#endif

#if defined(__GNUC__) || defined(__clang__)
  #ifndef likely
    #define likely(x) __builtin_expect(!!(x), 1)
  #endif
  #ifndef unlikely
    #define unlikely(x) __builtin_expect(!!(x), 0)
  #endif
#else
  #ifndef likely
    #define likely(x) (x)
  #endif
  #ifndef unlikely
    #define unlikely(x) (x)
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-width Unix seconds and microseconds, independent of CRT timeval. */
typedef struct wall_time {
  int64_t tv_sec;
  int32_t tv_usec;
} wall_time_t;

typedef struct timezone_info {
  int tz_minuteswest;
  int tz_dsttime;
} timezone_info_t;

SALTS_API int timeval_now(wall_time_t *tv, timezone_info_t *tz);
SALTS_API int time_utc(time_t value, struct tm *out);
SALTS_API int time_local(time_t value, struct tm *out);
SALTS_API time_t time_from_utc(const struct tm *value);
SALTS_API time_t time_from_local(struct tm *value);
SALTS_API int time_format_utc(time_t value,
                                                const char *format,
                                                char *buffer,
                                                size_t buffer_size);
SALTS_API int time_format_local(time_t value,
                                                  const char *format,
                                                  char *buffer,
                                                  size_t buffer_size);
/** Fill a buffer from the OS CSPRNG. Zero length accepts a null buffer.
 * Returns zero on success or a negative error; no process-local fallback.
 */
SALTS_API int secure_random(void *buffer, size_t length);

#define SALTS_PLATFORM_INFO_MAX 128

typedef struct cpu_info {
  char model[SALTS_PLATFORM_INFO_MAX];
  int core_count;
  double speed_mhz;
} cpu_info_t;

typedef struct memory_info {
  uint64_t total_memory;
  uint64_t free_memory;
  uint64_t available_memory;
} memory_info_t;

typedef struct load_average {
  double one_minute;
  double five_minutes;
  double fifteen_minutes;
} load_average_t;

typedef struct network_interface {
  char name[SALTS_PLATFORM_INFO_MAX];
  char address[SALTS_PLATFORM_INFO_MAX];
  char netmask[SALTS_PLATFORM_INFO_MAX];
  int is_internal;
} network_interface_t;

SALTS_API int os_name(char *buffer, size_t buffer_size);
SALTS_API int os_version(char *buffer, size_t buffer_size);
SALTS_API int cpu_arch(char *buffer, size_t buffer_size);
SALTS_API int user_name(char *buffer, size_t buffer_size);
SALTS_API int host_name(char *buffer, size_t buffer_size);
SALTS_API int cpu_info(cpu_info_t *info);
SALTS_API int memory_info(memory_info_t *info);
SALTS_API int load_average(load_average_t *info);
SALTS_API int network_interfaces(
    network_interface_t *interfaces, size_t max_interfaces, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLATFORM_UTILS_H */
