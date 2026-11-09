# Salts Cron

Cross-platform cron expression parser and minute-based background runner for Salts.

## Features

- 5-field cron parsing: minute, hour, day of month, month, day of week
- `re2c` lexer for fast tokenization
- Cross-platform shared library API
- Next-fire calculation with local wall-clock semantics
- Next-N fire calculation for debugging and UI display
- Background runner for Windows and Unix
- Deterministic runner advancement for `tinytest.h` unit tests
- Crontab-like table loader for many scheduled payloads

## Supported Syntax

Each expression has 5 fields:

```text
* * * * *
| | | | |
| | | | +-- day of week (0-6, Sunday = 0, 7 also accepted)
| | | +---- month (1-12 or jan-dec)
| | +------ day of month (1-31)
| +-------- hour (0-23)
+---------- minute (0-59)
```

Supported operators:

- `*` any value
- `1,2,3` list
- `1-5` range
- `*/15` step
- `1-10/2` ranged step
- month names: `jan` .. `dec`
- weekday names: `sun` .. `sat`

Supported aliases:

- `@hourly`
- `@daily`
- `@weekly`
- `@monthly`
- `@yearly`
- `@annually`
- `@midnight`

## Semantics

This module follows classic crontab behavior for day-of-month and day-of-week:

- if both fields are `*`, the date always matches
- if one field is `*`, only the other field matters
- if both fields are restricted, the match rule is `day_of_month OR day_of_week`

This is important. `0 0 13 * 5` means "the 13th of the month, or every Friday", not "Friday the 13th only".

## Quick Start

```c
#include <salts/cron.h>

cron_expr_t expr;
time_t next_fire;

if (cron_parse("*/15 9-17 * * 1-5", &expr) == SALTS_CRON_OK &&
    cron_next(&expr, time(NULL), &next_fire) == SALTS_CRON_OK) {
  /* next_fire now holds the next matching local time */
}
```

## Next N Fire Times

```c
#include <salts/cron.h>

cron_expr_t expr;
time_t next_times[5];
int count;

if (cron_parse("*/20 9-10 * * *", &expr) == SALTS_CRON_OK) {
  count = cron_next_n(&expr, time(NULL), next_times, 5);
  if (count > 0) {
    /* next_times[0..count-1] now holds future matching local times */
  }
}
```

`cron_next_n()` is useful when:

- building a debug screen
- previewing a user's schedule before saving it
- showing the next few runs in logs or admin UI

## Formatting Helper

If thou merely needest a stable local-time string, use `cron_format_time()`:

```c
char buf[32];
cron_format_time(next_times[0], buf, sizeof(buf), NULL);
/* buf -> "2024-01-02 03:04" */
```

Pass a custom `strftime` format when needed:

```c
cron_format_time(next_times[0], buf, sizeof(buf), "%H:%M");
```

## Crontab-Like Table Loader

The loader accepts lines in this shape:

```text
minute hour day month weekday payload...
```

Examples:

```text
# comment
*/15 9-17 * * 1-5 send-report
0 0 * * * rotate-logs --force
```

Use it like this:

```c
cron_table_t table;
cron_table_init(&table);

if (cron_table_load_file("jobs.cron", &table, NULL, 0) == SALTS_CRON_OK) {
  /* table.entries[i].expr + table.entries[i].payload */
}

cron_table_free(&table);
```

This module does not execute payloads. It only parses and stores them. The
upper layer decides whether the payload is a command, task name, route id, or
anything else.

## Background Runner

```c
static void on_fire(const cron_expr_t *expr, time_t scheduled_at, void *user_data) {
  (void)expr;
  (void)scheduled_at;
  (void)user_data;
}

cron_runner_t *runner =
    cron_runner_create("0 * * * *", on_fire, NULL);

cron_runner_start(runner);
/* ... */
cron_runner_stop(runner);
cron_runner_destroy(runner);
```

See [cron_example.c](examples/cron_example.c) for a full example.

## Deterministic Testing

For unit tests, use `cron_runner_advance()` instead of sleeping on real time:

```c
cron_runner_advance(runner, fake_now);
```

This advances the runner to a chosen local minute and fires every due callback exactly once.

## Build

Link against:

```cmake
target_link_libraries(your_target PRIVATE Salts::Cron)
```

## Tests

The unit tests use `tinytest.h` and cover:

- parser success cases
- parser failure cases
- next-fire calculation
- `DOM/DOW` semantics
- crontab-like table loading
- deterministic runner catch-up behavior
