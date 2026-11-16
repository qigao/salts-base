#ifndef DATETIME_TYPES_H
#define DATETIME_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int year;           // e.g., 2006
    int month;          // 1-12
    int day;            // 1-31
    int hour;           // 0-23
    int minute;         // 0-59
    int second;         // 0-59
    int millisecond;    // 0-999
    int tz_offset;      // in minutes from UTC (e.g., +03:45 -> 225)
    int has_tz;         // bool
    int day_of_week;    // 0-6 (Sun-Sat), optional
} datetime_info_t;

typedef struct {
    datetime_info_t info;
    int error;
    char error_msg[128];
} datetime_parse_ctx_t;

typedef struct {
    int type;
    int int_value;
    const char *value;
    size_t length;
} datetime_token_t;

#endif // DATETIME_TYPES_H
