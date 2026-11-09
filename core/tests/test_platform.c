#include <salts/clock.h>
#include <tinytest.h>
#include <salts/error_codes.h>
#include <salts/platform.h>
#include <threads/threads.h>

#include <atomic/atomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static void increment(void *arg) {
  int *value = (int *)arg;
  ++*value;
}

static atomic_int timer_repeat_count;
static atomic_int timer_oneshot_count;
static atomic_int timer_destroy_race_count;

static void repeating_timer_cb(native_timer_t *timer) {
  int value = atomic_fetch_add(&timer_repeat_count, 1) + 1;
  if (value >= 5) {
    (void)native_timer_stop(timer);
  }
}

static void oneshot_timer_cb(native_timer_t *timer) {
  (void)timer;
  atomic_fetch_add(&timer_oneshot_count, 1);
}

static void destroy_race_timer_cb(native_timer_t *timer) {
  atomic_int *counter = (atomic_int *)native_timer_get_data(timer);
  if (counter) atomic_fetch_add(counter, 1);
  sleep_ms(1u);
}

static int test_original_time_and_system_surface(void) {
  time_value_t tv = {0};
  timezone_info_t tz = {0};
  struct tm utc = {0};
  struct tm local = {0};
  struct tm input = {0};
  char buffer[SALTS_PLATFORM_INFO_MAX] = {0};
  cpu_info_t cpu = {0};
  memory_info_t memory = {0};
  load_average_t load = {0};
  network_interface_t interfaces[16];
  size_t count = 0;
  time_t epoch;
  int status;

  if (timeval_now(&tv, &tz) != 0 || tv.tv_sec <= 0) return 10;
  if (time_utc((time_t)1704110400, &utc) != 0 ||
      utc.tm_year + 1900 != 2024 || utc.tm_mon != 0 || utc.tm_mday != 1 ||
      utc.tm_hour != 12)
    return 11;
  if (time_local((time_t)1704110400, &local) != 0) return 12;

  input.tm_year = 2024 - 1900;
  input.tm_mon = 0;
  input.tm_mday = 1;
  input.tm_hour = 12;
  epoch = time_from_utc(&input);
  if (epoch != (time_t)1704110400) return 13;
  if (time_from_utc(NULL) != (time_t)-1) return 14;

  if (time_from_local(&local) == (time_t)-1) return 15;

  memset(buffer, 0, sizeof(buffer));
  if (time_format_utc(epoch, "%Y-%m-%dT%H:%M:%SZ",
                         buffer, sizeof(buffer)) <= 0 ||
      strcmp(buffer, "2024-01-01T12:00:00Z") != 0)
    return 16;
  if (time_format_local(epoch, "%Y", buffer, sizeof(buffer)) <= 0)
    return 17;

  if (os_name(buffer, sizeof(buffer)) != 0 || buffer[0] == '\0')
    return 18;
  if (os_version(buffer, sizeof(buffer)) != 0 || buffer[0] == '\0')
    return 19;
  if (cpu_arch(buffer, sizeof(buffer)) != 0 || buffer[0] == '\0')
    return 20;
  if (user_name(buffer, sizeof(buffer)) != 0 || buffer[0] == '\0')
    return 21;
  if (host_name(buffer, sizeof(buffer)) != 0 || buffer[0] == '\0')
    return 22;
  if (cpu_info(&cpu) != 0 || cpu.core_count < 1 ||
      cpu.model[0] == '\0')
    return 23;
  if (memory_info(&memory) != 0 || memory.total_memory == 0u)
    return 24;
  if (load_average(&load) != 0) return 25;

  memset(interfaces, 0, sizeof(interfaces));
  status = network_interfaces(
      interfaces, sizeof(interfaces) / sizeof(interfaces[0]), &count);
  if (status == 0) {
    if (count == 0u || count > sizeof(interfaces) / sizeof(interfaces[0]) ||
        interfaces[0].name[0] == '\0')
      return 26;
  } else if (count != 0u) {
    return 27;
  }

  if (secure_random(NULL, 0u) != SALTS_OK) return 28;
  return 0;
}

static int test_original_thread_policy(void) {
  sync_set_single_threaded(0);
  if (sync_is_single_threaded() != 0) return 30;
  sync_set_single_threaded(1);
  if (sync_is_single_threaded() != 1) return 31;
  sync_set_single_threaded(0);
  if (process_id() <= 0) return 32;
  return 0;
}

static int test_original_native_timer(void) {
  native_timer_t *timer;
  int marker = 7;

  atomic_store(&timer_repeat_count, 0);
  timer = native_timer_create(NULL);
  if (!timer) return 40;
  native_timer_set_data(timer, &marker);
  if (native_timer_get_data(timer) != &marker ||
      native_timer_get_repeat(timer) != 0u)
    return 41;
  if (native_timer_start(timer, repeating_timer_cb, 100u, 100u) != 0 ||
      native_timer_get_repeat(timer) != 100u)
    return 42;
  sleep_ms(1000u);
  if (atomic_load(&timer_repeat_count) < 5) return 43;
  native_timer_destroy(timer);

  atomic_store(&timer_oneshot_count, 0);
  timer = native_timer_create(NULL);
  if (!timer) return 44;
  if (native_timer_start(timer, oneshot_timer_cb, 100u, 0u) != 0) return 45;
  sleep_ms(300u);
  if (atomic_load(&timer_oneshot_count) != 1) return 46;
  native_timer_destroy(timer);

  atomic_store(&timer_destroy_race_count, 0);
  for (int i = 0; i < 256; ++i) {
    timer = native_timer_create(NULL);
    if (!timer) return 47;
    native_timer_set_data(timer, &timer_destroy_race_count);
    if (native_timer_start(timer, destroy_race_timer_cb, 1u, 0u) != 0)
      return 48;
    sleep_ms(1u);
    native_timer_destroy(timer);
  }
  if (atomic_load(&timer_destroy_race_count) < 0) return 49;
  return 0;
}

static int saltscore_legacy_entry_test_platform_c(void) {
  uint8_t random_bytes[32] = {0};
  sync_thread_t thread = NULL;
  sync_mutex_t lock = NULL;
  sync_thread_affine_state affinity = {0};
  int value = 0;
  int status;

  if (secure_random(NULL, 0u) != SALTS_OK ||
      secure_random(NULL, 1u) != SALTS_EINVAL ||
      secure_random(random_bytes, sizeof(random_bytes)) != SALTS_OK)
    return 1;
  if (hrtime() == 0u || realtime_ms() == 0u ||
      monotonic_ms() == 0u || cpu_count() < 1)
    return 2;

  sync_mutex_init(&lock);
  if (lock == NULL) return 3;
  sync_mutex_lock(&lock);
  sync_mutex_unlock(&lock);
  sync_mutex_destroy(&lock);
  if (lock != NULL) return 4;

  if (sync_thread_affine_init(&affinity, &affinity) != SALTS_OK ||
      sync_thread_affine_check(&affinity, &affinity) != SALTS_OK ||
      sync_thread_affine_set_busy(&affinity, &affinity, true) != SALTS_OK ||
      sync_thread_affine_check(&affinity, &affinity) != SALTS_EBUSY ||
      sync_thread_affine_set_busy(&affinity, &affinity, false) != SALTS_OK ||
      sync_thread_affine_reset(&affinity, &affinity) != SALTS_OK)
    return 5;

  if (sync_thread_create(&thread, increment, &value) != 0 || thread == NULL)
    return 6;
  if (sync_thread_join(&thread) != 0 || thread != NULL || value != 1)
    return 7;

  status = test_original_time_and_system_surface();
  if (status != 0) return status;
  status = test_original_thread_policy();
  if (status != 0) return status;
  status = test_original_native_timer();
  if (status != 0) return status;
  return 0;
}

/* Keep the native contract's original exit codes and cleanup paths intact;
 * surface its exact nonzero result as one registered TinyTest failure. */
spec("Platform lifecycle and clock") {
  it("preserves the original native exit and lifetime contract") {
    const int result = saltscore_legacy_entry_test_platform_c();
    check(result == 0, "original native contract returned %d", result);
  }
}
