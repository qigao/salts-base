#ifndef SALTS_SCAN_H
#define SALTS_SCAN_H

#include <salts/api.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-range scans borrow [p, end) from one live array, including its one-past
 * endpoint. No alignment or NUL termination is required; NUL is an ordinary
 * byte. They retain no storage, allocate nothing, and take O(n) time/O(1) space.
 * Null endpoints or reversed ranges return end, except scan_char returns NULL.
 * Pointers into unrelated objects are outside the range contract.
 */

/* Return the first matching byte, or NULL if no match is found. */
SALTS_C_API const char *scan_char(const char *p, const char *end, char needle);

SALTS_C_API const char *scan_to_char(const char *p, const char *end, char needle);

SALTS_C_API const char *scan_to_any2(const char *p,
                                    const char *end, char a, char b);

SALTS_C_API const char *scan_to_any3(const char *p,
                                    const char *end, char a, char b, char c);

SALTS_C_API const char *scan_skip_sp_tab(const char *p, const char *end);

SALTS_C_API const char *scan_skip_sp_tab_cr_lf(const char *p, const char *end);

/* Borrow counted ranges and return the first substring, or NULL on a miss.
 * Compatibility behavior: NULL haystack/needle or an empty needle returns
 * haystack. Otherwise ranges must be valid for the supplied byte counts.
 * Worst-case time O(haystack_len * needle_len), additional space O(1).
 */
SALTS_C_API const char *scan_mem(const char *haystack, size_t haystack_len,
                                const char *needle, size_t needle_len);

/* Return the first matching delimiter, or end when none is present. */
SALTS_C_API const char *scan_to_any4(const char *p, const char *end,
                                   char a, char b, char c, char d);
SALTS_C_API const char *scan_to_any5(const char *p, const char *end,
                                   char a, char b, char c, char d, char e);

#ifdef __cplusplus
}
#endif

#endif
