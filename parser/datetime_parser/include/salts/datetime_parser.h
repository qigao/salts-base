#ifndef DATETIME_PARSER_H
#define DATETIME_PARSER_H

#include <time.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Multi-format Date-Time Parser
 *
 * Supported formats (total 48 variations):
 * - Compact: YYYYMMDD, YYYYDDMM, HHMM, HHMMSS, HHMMSSmss
 * - Slash:   YYYY/MM/DD, DD/MM/YYYY, MM/DD/YYYY, ...
 * - Hyphen:  YYYY-MM-DD, DD-MM-YYYY, MM-DD-YYYY, ...
 * - Dot:     DD.MM.YYYY, HH.MM.SS, HH.MM.SS.mss, ...
 * - Space:   HH MM SS, HH MM SS mss
 * - Colon:   HH:MM, HH:MM:SS, HH:MM:SS.mss
 * - Names:   DD-Mon-YY, DD-Mon-YYYY, DD/Mon/YYYY
 * - RFC 822: Sun, 06 Nov 1994 08:49:37 GMT
 * - NCSA:    04/Mar/2006:13:27:54
 * - ISO-8601: 2006-03-14T13:27:54+03:45
 *
 * Timezone support: GMT, UTC, Z, +HHMM, -HHMM, +HH:MM, -HH:MM.
 */

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int millisecond;
    int tz_offset; // in minutes from UTC
    int has_tz;
    int day_of_week; // 0-6 (Sun-Sat), -1 if not set
} datetime_t;

/**
 * @brief Parses a date-time string in various formats (RFC-822, ISO-8601, etc.).
 *
 * @param str The date string to parse.
 * @param len Length of the string.
 * @param out Output datetime structure.
 * @return 0 on success, -1 on failure.
 */
int datetime_parse(const char *str, size_t len, datetime_t *out);

/**
 * @brief Converts datetime_t to time_t (UTC).
 *
 * @param dt Input datetime structure.
 * @return time_t value (seconds since epoch), or -1 on error.
 */
time_t datetime_to_time(const datetime_t *dt);

/**
 * @brief Formats a time_t as an RFC 7231 / RFC 822 HTTP date-time string.
 *
 * @param t Time to format.
 * @param buf Output buffer (at least 30 bytes).
 * @param buf_len Size of the buffer.
 * @return Number of characters written, or -1 on failure.
 */
int datetime_format_rfc822(time_t t, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // DATETIME_PARSER_H
