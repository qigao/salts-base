#include <salts.h>
#include <threads/threads.h>
#include <mutex>
#include <tinytest.hpp>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <type_traits>

static_assert(std::is_standard_layout_v<time_value_t>);
static_assert(std::is_standard_layout_v<timezone_info_t>);
static_assert(std::is_standard_layout_v<cpu_info_t>);
static_assert(std::is_standard_layout_v<memory_info_t>);
static_assert(std::is_standard_layout_v<load_average_t>);
static_assert(std::is_standard_layout_v<network_interface_t>);

spec("Platform native C++17 public ABI") {
  it("retains all exported native function signatures and runtime thread state") {
  int (*gettimeofday_fn)(time_value_t *, timezone_info_t *) =
      &timeval_now;
  int (*gmtime_fn)(std::time_t, std::tm *) = &time_utc;
  int (*localtime_fn)(std::time_t, std::tm *) = &time_local;
  std::time_t (*timegm_fn)(const std::tm *) = &time_from_utc;
  std::time_t (*mktime_fn)(std::tm *) = &time_from_local;
  int (*strftime_utc_fn)(std::time_t, const char *, char *, std::size_t) =
      &time_format_utc;
  int (*strftime_local_fn)(std::time_t, const char *, char *, std::size_t) =
      &time_format_local;
  int (*secure_random_fn)(void *, std::size_t) = &secure_random;
  int (*os_name_fn)(char *, std::size_t) = &os_name;
  int (*os_version_fn)(char *, std::size_t) = &os_version;
  int (*arch_fn)(char *, std::size_t) = &cpu_arch;
  int (*username_fn)(char *, std::size_t) = &user_name;
  int (*hostname_fn)(char *, std::size_t) = &host_name;
  int (*cpu_info_fn)(cpu_info_t *) = &cpu_info;
  int (*memory_info_fn)(memory_info_t *) =
      &memory_info;
  int (*load_fn)(load_average_t *) =
      &load_average;
  int (*interfaces_fn)(network_interface_t *, std::size_t,
                       std::size_t *) =
      &network_interfaces;
  native_timer_t *(*timer_create_fn)(void *) = &native_timer_create;
  void (*timer_destroy_fn)(native_timer_t *) = &native_timer_destroy;
  int (*timer_start_fn)(native_timer_t *, native_timer_cb, std::uint64_t,
                        std::uint64_t) =
      &native_timer_start;
  int (*timer_stop_fn)(native_timer_t *) = &native_timer_stop;
  void (*timer_set_data_fn)(native_timer_t *, void *) = &native_timer_set_data;
  void *(*timer_get_data_fn)(native_timer_t *) = &native_timer_get_data;
  std::uint64_t (*timer_get_repeat_fn)(native_timer_t *) =
      &native_timer_get_repeat;
  void (*single_set_fn)(int) = &sync_set_single_threaded;
  int (*single_get_fn)(void) = &sync_is_single_threaded;
  int (*getpid_fn)(void) = &process_id;

  check(!(!gettimeofday_fn || !gmtime_fn || !localtime_fn || !timegm_fn ||
      !mktime_fn || !strftime_utc_fn || !strftime_local_fn ||
      !secure_random_fn || !os_name_fn || !os_version_fn || !arch_fn ||
      !username_fn || !hostname_fn || !cpu_info_fn || !memory_info_fn ||
      !load_fn || !interfaces_fn || !timer_create_fn || !timer_destroy_fn ||
      !timer_start_fn || !timer_stop_fn || !timer_set_data_fn ||
      !timer_get_data_fn || !timer_get_repeat_fn || !single_set_fn ||
      !single_get_fn || !getpid_fn));

  single_set_fn(1);
  check(single_get_fn() == 1 && getpid_fn() > 0);
  single_set_fn(0);
  }
  it("keeps C++ once APIs intact when mutex is included after the merged thread header") {
    std::once_flag guard;
    int calls = 0;
    std::call_once(guard, [&] { ++calls; });
    std::call_once(guard, [&] { ++calls; });
    check_equal(calls, 1);
  }
}
