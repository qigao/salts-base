#ifndef SALTS_TIMEVAL_H
#define SALTS_TIMEVAL_H
#include <stdint.h>
/* Original cross-platform timezone-independent Y2038-safe time value.
 * This is a plain C11 POD; no reflection or dynamic CMeta dependency. */
typedef struct cmeta_timeval {
  int64_t tv_sec;
  int32_t tv_usec;
} cmeta_timeval_t;
#endif
