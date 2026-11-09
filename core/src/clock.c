#include <clock.h>

#ifdef _WIN32
  #include <windows.h>
  #include <platform_atomic.h>
#else
  #include <time.h>
#endif

#ifdef _WIN32
uint64_t cmeta_hrtime(void) {
  /* QueryPerformanceFrequency is stable, but publishing through a volatile
   * global was still a C data race. Use our ONE Platform Atomic provider.
   * Never fall back to native compiler _Atomic or unsynchronized writes.
   */
  static salts_atomic64 frequency = {0};
  uint64_t f = 0, expected = 0;
  int exchanged = 0;
  LARGE_INTEGER counter;

  if (salts_atomic64_load(&frequency, SALTS_MEMORY_ACQUIRE, &f) != SALTS_OK)
    return 0;
  if (f == 0) {
    LARGE_INTEGER candidate;
    if (!QueryPerformanceFrequency(&candidate) || candidate.QuadPart <= 0)
      return 0;
    if (salts_atomic64_cas(&frequency, &expected,
          (uint64_t)candidate.QuadPart,
          SALTS_MEMORY_ACQ_REL, SALTS_MEMORY_ACQUIRE, &exchanged) != SALTS_OK)
      return 0;
    f = exchanged ? (uint64_t)candidate.QuadPart : expected;
  }

  if (!QueryPerformanceCounter(&counter) || f == 0) return 0;

  uint64_t ticks = (uint64_t)counter.QuadPart;
  uint64_t whole = (ticks / f) * 1000000000ULL;
  uint64_t part = (ticks % f) * 1000000000ULL / f;
  return whole + part;
}

uint64_t cmeta_realtime_ms(void) {
  FILETIME ft;
  ULARGE_INTEGER value;

  GetSystemTimeAsFileTime(&ft);
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}
#else
uint64_t cmeta_hrtime(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t cmeta_realtime_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
#endif

uint64_t cmeta_monotonic_ms(void) { return cmeta_ns_to_ms(cmeta_hrtime()); }

uint64_t cmeta_uptime_ms(void) {
  /* Benign race: worst case two threads choose nearly identical start times. */
  static volatile uint64_t start_time = 0;
  uint64_t st = start_time;
  if (st == 0) {
    st = cmeta_hrtime();
    start_time = st;
  }
  return cmeta_ns_to_ms(cmeta_hrtime() - st);
}
