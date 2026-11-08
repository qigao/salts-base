#ifndef SALTS_SIMD_SCAN_H
#define SALTS_SIMD_SCAN_H

#include "cmeta_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

SALTS_C_API const char *cmeta_scan_char(const char *p, const char *end, char needle);

SALTS_C_API const char *cmeta_scan_to_char(const char *p, const char *end, char needle);

SALTS_C_API const char *cmeta_scan_to_any2(const char *p,
                                         const char *end,
                                         char a,
                                         char b);

SALTS_C_API const char *cmeta_scan_to_any3(const char *p,
                                         const char *end,
                                         char a,
                                         char b,
                                         char c);

SALTS_C_API const char *cmeta_scan_skip_sp_tab(const char *p, const char *end);

SALTS_C_API const char *cmeta_scan_skip_sp_tab_cr_lf(const char *p, const char *end);

SALTS_C_API const char *cmeta_scan_mem(const char *haystack,
                                     size_t haystack_len,
                                     const char *needle,
                                     size_t needle_len);

#ifdef __cplusplus
}
#endif

#endif
