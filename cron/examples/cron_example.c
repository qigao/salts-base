#include "salts_cron.h"
#include "salts/thread.h"

#include <stdio.h>

static void print_local_time(const char *prefix, time_t value) {
  char buf[32];
  if (salts_cron_format_time(value, buf, sizeof(buf), NULL) < 0) {
    printf("%s<format-error>\n", prefix);
    return;
  }
  printf("%s%s\n", prefix, buf);
}

static void print_fire(const salts_cron_expr_t *expr, time_t scheduled_at, void *user_data) {
  (void)expr;
  (void)user_data;
  print_local_time("cron fired at ", scheduled_at);
}

int main(void) {
  salts_cron_expr_t expr_obj;
  salts_cron_runner_t *runner;
  const char *expr = "*/2 * * * *";
  time_t next_times[5];
  int count;
  int i;

  if (salts_cron_parse(expr, &expr_obj) != SALTS_CRON_OK) {
    fprintf(stderr, "failed to parse cron expression '%s'\n", expr);
    return 1;
  }

  count = salts_cron_next_n(&expr_obj, time(NULL), next_times, 5);
  if (count < 0) {
    fprintf(stderr, "failed to compute future fire times for '%s'\n", expr);
    return 1;
  }

  printf("next %d fire times for '%s':\n", count, expr);
  for (i = 0; i < count; ++i) {
    print_local_time("  - ", next_times[i]);
  }

  runner = salts_cron_runner_create(expr, print_fire, NULL);
  if (!runner) {
    fprintf(stderr, "failed to create cron runner for '%s'\n", expr);
    return 1;
  }

  if (salts_cron_runner_start(runner) != SALTS_CRON_OK) {
    fprintf(stderr, "failed to start cron runner\n");
    salts_cron_runner_destroy(runner);
    return 1;
  }

  printf("running cron '%s' for about 5 minutes\n", expr);
  cmeta_sleep_ms(5 * 60 * 1000);

  salts_cron_runner_stop(runner);
  salts_cron_runner_destroy(runner);
  return 0;
}
