#ifndef SALTS_PLATFORM_UTILS_H
#define SALTS_PLATFORM_UTILS_H

#include <clock.h>
#include <random.h>
#include <timeval.h>

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

typedef struct cmeta_timezone {
  int tz_minuteswest;
  int tz_dsttime;
} cmeta_timezone_t;

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_gettimeofday(cmeta_timeval_t *tv, cmeta_timezone_t *tz);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_gmtime(time_t value, struct tm *out);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_localtime(time_t value, struct tm *out);
SALTS_CORE_PLATFORM_API time_t SALTS_CORE_PLATFORM_CALL cmeta_timegm(const struct tm *value);
SALTS_CORE_PLATFORM_API time_t SALTS_CORE_PLATFORM_CALL cmeta_mktime(struct tm *value);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_strftime_utc(
    time_t value, const char *format, char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_strftime_local(
    time_t value, const char *format, char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_secure_random(void *buffer, size_t length);

#define SALTS_PLATFORM_INFO_MAX 128

typedef struct cmeta_platform_cpu_info {
  char model[SALTS_PLATFORM_INFO_MAX];
  int core_count;
  double speed_mhz;
} cmeta_platform_cpu_info_t;

typedef struct cmeta_platform_memory_info {
  uint64_t total_memory;
  uint64_t free_memory;
  uint64_t available_memory;
} cmeta_platform_memory_info_t;

typedef struct cmeta_platform_load_average {
  double one_minute;
  double five_minutes;
  double fifteen_minutes;
} cmeta_platform_load_average_t;

typedef struct cmeta_platform_network_interface {
  char name[SALTS_PLATFORM_INFO_MAX];
  char address[SALTS_PLATFORM_INFO_MAX];
  char netmask[SALTS_PLATFORM_INFO_MAX];
  int is_internal;
} cmeta_platform_network_interface_t;

SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_os_name(char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_os_version(char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_arch(char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_username(char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_hostname(char *buffer, size_t buffer_size);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_cpu_info(cmeta_platform_cpu_info_t *info);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_memory_info(cmeta_platform_memory_info_t *info);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_load_average(cmeta_platform_load_average_t *info);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_platform_network_interfaces(
    cmeta_platform_network_interface_t *interfaces,
    size_t max_interfaces, size_t *count);

typedef struct cmeta_native_timer_s cmeta_timer_t;
typedef void (*cmeta_timer_cb)(cmeta_timer_t *timer);

SALTS_CORE_PLATFORM_API cmeta_timer_t *SALTS_CORE_PLATFORM_CALL cmeta_timer_create(void *loop);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_timer_destroy(cmeta_timer_t *timer);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_timer_start(
    cmeta_timer_t *timer, cmeta_timer_cb callback,
    uint64_t timeout_ms, uint64_t repeat_ms);
SALTS_CORE_PLATFORM_API int SALTS_CORE_PLATFORM_CALL cmeta_timer_stop(cmeta_timer_t *timer);
SALTS_CORE_PLATFORM_API void SALTS_CORE_PLATFORM_CALL cmeta_timer_set_data(cmeta_timer_t *timer, void *data);
SALTS_CORE_PLATFORM_API void *SALTS_CORE_PLATFORM_CALL cmeta_timer_get_data(cmeta_timer_t *timer);
SALTS_CORE_PLATFORM_API uint64_t SALTS_CORE_PLATFORM_CALL cmeta_timer_get_repeat(cmeta_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLATFORM_UTILS_H */
