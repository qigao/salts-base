#ifndef SALTS_CLOCK_H
#define SALTS_CLOCK_H

#include <core/api.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

SALTS_API uint64_t hrtime(void);
SALTS_API uint64_t monotonic_ms(void);
SALTS_API uint64_t realtime_ms(void);
SALTS_API uint64_t uptime_ms(void);

typedef struct native_timer_s native_timer_t;
typedef void (*native_timer_cb)(native_timer_t *timer);

SALTS_API native_timer_t *native_timer_create(void *loop);
SALTS_API void native_timer_destroy(native_timer_t *timer);
SALTS_API int native_timer_start(native_timer_t *timer,
                                 native_timer_cb callback,
                                 uint64_t timeout_ms,
                                 uint64_t repeat_ms);
SALTS_API int native_timer_stop(native_timer_t *timer);
SALTS_API void native_timer_set_data(native_timer_t *timer, void *data);
SALTS_API void *native_timer_get_data(native_timer_t *timer);
SALTS_API uint64_t native_timer_get_repeat(native_timer_t *timer);

static inline uint64_t ns_to_ms(uint64_t ns) { return ns / 1000000ULL; }
static inline uint64_t ms_to_ns(uint64_t ms) { return ms * 1000000ULL; }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CLOCK_H */
