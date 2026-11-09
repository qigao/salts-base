#ifndef SALTS_CLOCK_H
#define SALTS_CLOCK_H


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

 uint64_t SALTS_CORE_PLATFORM_CALL cmeta_hrtime(void);
 uint64_t SALTS_CORE_PLATFORM_CALL cmeta_monotonic_ms(void);
 uint64_t SALTS_CORE_PLATFORM_CALL cmeta_realtime_ms(void);
 uint64_t SALTS_CORE_PLATFORM_CALL cmeta_uptime_ms(void);

static inline uint64_t cmeta_ns_to_ms(uint64_t ns) { return ns / 1000000ULL; }
static inline uint64_t cmeta_ms_to_ns(uint64_t ms) { return ms * 1000000ULL; }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CLOCK_H */
