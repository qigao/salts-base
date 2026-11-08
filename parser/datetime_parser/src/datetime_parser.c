#include "datetime_parser.h"
#include "datetime_lexer.h"
#include "datetime_grammar_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Lemon-generated functions
void *DatetimeParseAlloc(void *(*mallocProc)(size_t));
void DatetimeParse(void *p, int tokenID, datetime_token_t token, datetime_parse_ctx_t *ctx);
void DatetimeParseFree(void *p, void (*freeProc)(void *));

int datetime_parse(const char *str, size_t len, datetime_t *out) {
    if (!str || !out) return -1;

    datetime_lexer_t lexer;
    datetime_lexer_init(&lexer, str, len);

    datetime_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.info.day_of_week = -1;

    void *parser = DatetimeParseAlloc(malloc);
    if (!parser) return -1;

    datetime_token_t token;
    int res;
    while ((res = datetime_lexer_next(&lexer, &token)) > 0) {
        DatetimeParse(parser, token.type, token, &ctx);
        if (ctx.error) break;
    }

    if (res == 0 && !ctx.error) {
        // End of input
        DatetimeParse(parser, 0, token, &ctx);
    }

    DatetimeParseFree(parser, free);

    if (ctx.error || res < 0) return -1;

    out->year = ctx.info.year;
    out->month = ctx.info.month;
    out->day = ctx.info.day;
    out->hour = ctx.info.hour;
    out->minute = ctx.info.minute;
    out->second = ctx.info.second;
    out->millisecond = ctx.info.millisecond;
    out->tz_offset = ctx.info.tz_offset;
    out->has_tz = ctx.info.has_tz;
    out->day_of_week = ctx.info.day_of_week;

    return 0;
}

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static const int DAYS_BEFORE_MONTH[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

time_t datetime_to_time(const datetime_t *dt) {
    if (!dt) return -1;

    long long year = dt->year;
    int month = dt->month;
    int day = dt->day;

    if (month < 1 || month > 12) return -1;

    long long days = (year - 1970) * 365;
    days += (year - 1969) / 4;
    days -= (year - 1901) / 100;
    days += (year - 1601) / 400;

    days += DAYS_BEFORE_MONTH[month - 1];
    if (month > 2 && is_leap(dt->year)) {
        days++;
    }
    days += (day - 1);

    time_t t = (time_t)(days * 86400);
    t += dt->hour * 3600;
    t += dt->minute * 60;
    t += dt->second;

    if (dt->has_tz) {
        t -= dt->tz_offset * 60;
    }

    return t;
}

int datetime_format_rfc822(time_t t, char *buf, size_t buf_len) {
    if (!buf || buf_len < 30) return -1;

    struct tm *tm_val;
#ifdef _WIN32
    struct tm tm_temp;
    if (gmtime_s(&tm_temp, &t) != 0) return -1;
    tm_val = &tm_temp;
#else
    tm_val = gmtime(&t);
    if (!tm_val) return -1;
#endif

    static const char *DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    return snprintf(buf, buf_len, "%s, %02d %s %04d %02d:%02d:%02d GMT",
                    DAYS[tm_val->tm_wday], tm_val->tm_mday, MONTHS[tm_val->tm_mon],
                    tm_val->tm_year + 1900, tm_val->tm_hour, tm_val->tm_min,
                    tm_val->tm_sec);
}
