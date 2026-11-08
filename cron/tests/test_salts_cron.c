#include <tinytest.h>
#include "salts_cron.h"

#include <stdio.h>
#include <string.h>

static time_t make_local_time(int year, int month, int day,
                              int hour, int minute, int second) {
  struct tm tm_value;
  memset(&tm_value, 0, sizeof(tm_value));
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = second;
  tm_value.tm_isdst = -1;
  return mktime(&tm_value);
}

static void expect_local_parts(time_t value,
                               int year, int month, int day,
                               int hour, int minute) {
  struct tm tm_value;
#ifdef _WIN32
  localtime_s(&tm_value, &value);
#else
  localtime_r(&value, &tm_value);
#endif
  check_equal(tm_value.tm_year + 1900, year);
  check_equal(tm_value.tm_mon + 1, month);
  check_equal(tm_value.tm_mday, day);
  check_equal(tm_value.tm_hour, hour);
  check_equal(tm_value.tm_min, minute);
}

static void write_text_file(const char *path, const char *text) {
  FILE *fp = fopen(path, "wb");
  check(fp != NULL);
  check_equal(fwrite(text, 1, strlen(text), fp), (int)strlen(text));
  fclose(fp);
}

typedef struct cron_test_capture_s {
  int count;
  time_t fired[8];
} cron_test_capture_t;

static void capture_callback(const salts_cron_expr_t *expr, time_t scheduled_at, void *user_data) {
  cron_test_capture_t *capture = (cron_test_capture_t *)user_data;
  (void)expr;

  if (capture->count < (int)(sizeof(capture->fired) / sizeof(capture->fired[0]))) {
    capture->fired[capture->count] = scheduled_at;
  }
  capture->count++;
}

suite("salts_cron") {
  it("parses lists ranges steps and names") {
    salts_cron_expr_t expr;
    int rc = salts_cron_parse("*/15 9-17 * jan,mar mon-fri", &expr);

    check_equal(rc, SALTS_CRON_OK);
    check((expr.minute_bits & (1ULL << 0)) != 0);
    check((expr.minute_bits & (1ULL << 15)) != 0);
    check((expr.hour_bits & (1ULL << 9)) != 0);
    check((expr.hour_bits & (1ULL << 17)) != 0);
    check((expr.month_bits & (1ULL << 1)) != 0);
    check((expr.month_bits & (1ULL << 3)) != 0);
    check((expr.day_of_week_bits & (1ULL << 1)) != 0);
    check((expr.day_of_week_bits & (1ULL << 5)) != 0);
  }

  it("parses aliases") {
    salts_cron_expr_t expr;
    int rc = salts_cron_parse("@daily", &expr);

    check_equal(rc, SALTS_CRON_OK);
    check((expr.minute_bits & (1ULL << 0)) != 0);
    check((expr.hour_bits & (1ULL << 0)) != 0);
    check(expr.day_of_month_any == 1);
    check(expr.day_of_week_any == 1);
  }

  it("rejects invalid steps") {
    salts_cron_expr_t expr;
    char error_buf[128];
    int rc = salts_cron_parse_ex("*/0 * * * *", &expr, error_buf, sizeof(error_buf));

    check_equal(rc, SALTS_CRON_EPARSE);
    check(strstr(error_buf, "step") != NULL);
  }

  it("computes next fire time") {
    salts_cron_expr_t expr;
    time_t after = make_local_time(2024, 1, 1, 14, 1, 20);
    time_t next_fire = 0;
    int rc = salts_cron_parse("30 14 * * 1-5", &expr);

    check_equal(rc, SALTS_CRON_OK);
    rc = salts_cron_next(&expr, after, &next_fire);
    check_equal(rc, SALTS_CRON_OK);
    expect_local_parts(next_fire, 2024, 1, 1, 14, 30);
  }

  it("maps sunday 7 to sunday 0") {
    salts_cron_expr_t expr;
    time_t sunday = make_local_time(2024, 1, 7, 8, 0, 0);
    int rc = salts_cron_parse("0 8 * * 7", &expr);

    check_equal(rc, SALTS_CRON_OK);
    check_equal(salts_cron_matches(&expr, sunday), 1);
  }

  it("reports impossible schedules") {
    salts_cron_expr_t expr;
    time_t next_fire = 0;
    int rc = salts_cron_parse("0 0 31 2 *", &expr);

    check_equal(rc, SALTS_CRON_OK);
    rc = salts_cron_next(&expr, make_local_time(2024, 1, 1, 0, 0, 0), &next_fire);
    check_equal(rc, SALTS_CRON_ENEXT);
  }

  it("computes multiple future fire times") {
    salts_cron_expr_t expr;
    time_t times[3];
    int rc = salts_cron_parse("*/20 9-10 * * *", &expr);

    check_equal(rc, SALTS_CRON_OK);
    rc = salts_cron_next_n(&expr, make_local_time(2024, 1, 1, 9, 5, 0), times, 3);
    check_equal(rc, 3);
    expect_local_parts(times[0], 2024, 1, 1, 9, 20);
    expect_local_parts(times[1], 2024, 1, 1, 9, 40);
    expect_local_parts(times[2], 2024, 1, 1, 10, 0);
  }

  it("computes multiple future fire times across day boundaries") {
    salts_cron_expr_t expr;
    time_t times[2];
    int rc = salts_cron_parse("0 23 * * *", &expr);

    check_equal(rc, SALTS_CRON_OK);
    rc = salts_cron_next_n(&expr, make_local_time(2024, 1, 1, 23, 0, 1), times, 2);
    check_equal(rc, 2);
    expect_local_parts(times[0], 2024, 1, 2, 23, 0);
    expect_local_parts(times[1], 2024, 1, 3, 23, 0);
  }

  it("reports no future fire times for impossible schedules") {
    salts_cron_expr_t expr;
    time_t times[2];
    int rc = salts_cron_parse("0 0 31 2 *", &expr);

    check_equal(rc, SALTS_CRON_OK);
    rc = salts_cron_next_n(&expr, make_local_time(2024, 1, 1, 0, 0, 0), times, 2);
    check_equal(rc, SALTS_CRON_ENEXT);
  }

  it("formats time using default layout") {
    char buf[32];
    int rc = salts_cron_format_time(make_local_time(2024, 1, 2, 3, 4, 5),
                                    buf, sizeof(buf), NULL);

    check(rc > 0);
    check_equal(buf, "2024-01-02 03:04");
  }

  it("formats time using caller layout") {
    char buf[32];
    int rc = salts_cron_format_time(make_local_time(2024, 1, 2, 3, 4, 5),
                                    buf, sizeof(buf), "%H:%M");

    check(rc > 0);
    check_equal(buf, "03:04");
  }

  it("loads crontab-like entries from text") {
    static const char *text =
        "# comment\n"
        "\n"
        "*/15 9-17 * * 1-5 send-report\n"
        "0 0 * * * rotate-logs --force\n";
    salts_cron_table_t table;
    int rc;

    salts_cron_table_init(&table);
    rc = salts_cron_table_load_string(text, &table, NULL, 0);
    check_equal(rc, SALTS_CRON_OK);
    check_equal((int)table.count, 2);
    check_equal(table.entries[0].payload, "send-report");
    check_equal(table.entries[1].payload, "rotate-logs --force");
    salts_cron_table_free(&table);
  }

  it("loads crontab-like entries from file") {
    static const char *path = "cron_table_test.txt";
    static const char *text =
        "*/10 * * * * sync-cache\n"
        "0 6 * * 1 weekly-job\n";
    salts_cron_table_t table;
    int rc;

    write_text_file(path, text);
    salts_cron_table_init(&table);
    rc = salts_cron_table_load_file(path, &table, NULL, 0);
    check_equal(rc, SALTS_CRON_OK);
    check_equal((int)table.count, 2);
    check_equal(table.entries[0].payload, "sync-cache");
    check_equal(table.entries[1].payload, "weekly-job");
    salts_cron_table_free(&table);
    remove(path);
  }

  it("rejects crontab-like lines without payload") {
    salts_cron_table_t table;
    char error_buf[128];
    int rc;

    salts_cron_table_init(&table);
    rc = salts_cron_table_load_string("0 0 * * *\n", &table, error_buf, sizeof(error_buf));
    check_equal(rc, SALTS_CRON_EPARSE);
    check(strstr(error_buf, "payload") != NULL);
    salts_cron_table_free(&table);
  }

  it("runner advance fires matching current minute once") {
    cron_test_capture_t capture = {0};
    salts_cron_runner_t *runner =
        salts_cron_runner_create("0 10 * * *", capture_callback, &capture);
    int fired;

    check(runner != NULL);
    fired = salts_cron_runner_advance(runner, make_local_time(2024, 1, 1, 10, 0, 5));
    check_equal(fired, 1);
    check_equal(capture.count, 1);
    expect_local_parts(capture.fired[0], 2024, 1, 1, 10, 0);
    salts_cron_runner_destroy(runner);
  }

  it("runner advance catches up missed minutes") {
    cron_test_capture_t capture = {0};
    salts_cron_runner_t *runner =
        salts_cron_runner_create("* * * * *", capture_callback, &capture);
    int fired;

    check(runner != NULL);
    fired = salts_cron_runner_advance(runner, make_local_time(2024, 1, 1, 10, 0, 1));
    check_equal(fired, 1);

    fired = salts_cron_runner_advance(runner, make_local_time(2024, 1, 1, 10, 3, 59));
    check_equal(fired, 3);
    check_equal(capture.count, 4);
    expect_local_parts(capture.fired[1], 2024, 1, 1, 10, 1);
    expect_local_parts(capture.fired[2], 2024, 1, 1, 10, 2);
    expect_local_parts(capture.fired[3], 2024, 1, 1, 10, 3);
    salts_cron_runner_destroy(runner);
  }

  it("runner advance does not refire the same minute") {
    cron_test_capture_t capture = {0};
    salts_cron_runner_t *runner =
        salts_cron_runner_create("* * * * *", capture_callback, &capture);
    int fired;

    check(runner != NULL);
    fired = salts_cron_runner_advance(runner, make_local_time(2024, 1, 1, 10, 0, 10));
    check_equal(fired, 1);

    fired = salts_cron_runner_advance(runner, make_local_time(2024, 1, 1, 10, 0, 50));
    check_equal(fired, 0);
    check_equal(capture.count, 1);
    salts_cron_runner_destroy(runner);
  }
}
