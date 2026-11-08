#include "salts_cron.h"
#include "cron_lexer.h"
#include "salts/thread.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRON_FIELD_COUNT 5
#define CRON_SEARCH_LIMIT_MINUTES (366 * 24 * 60 * 5)

typedef enum cron_field_kind_e {
  CRON_FIELD_MINUTE = 0,
  CRON_FIELD_HOUR,
  CRON_FIELD_DAY_OF_MONTH,
  CRON_FIELD_MONTH,
  CRON_FIELD_DAY_OF_WEEK
} cron_field_kind_t;

typedef struct cron_field_spec_s {
  int min_value;
  int max_value;
  const char *name;
  cron_field_kind_t kind;
} cron_field_spec_t;

typedef struct cron_name_map_s {
  const char *name;
  int value;
} cron_name_map_t;

typedef struct cron_field_parser_s {
  cron_lexer_t lexer;
  cron_token_t current;
  cron_field_spec_t spec;
  char *error_buf;
  size_t error_buf_len;
} cron_field_parser_t;

struct salts_cron_runner_s {
  salts_cron_expr_t expr;
  salts_cron_callback_t callback;
  void *user_data;
  cmeta_mutex_t lock;
  cmeta_cond_t cond;
  cmeta_thread_t thread;
  time_t cursor_minute;
  int thread_active;
  int stop_requested;
};

static const cron_field_spec_t CRON_SPECS[CRON_FIELD_COUNT] = {
    {0, 59, "minute", CRON_FIELD_MINUTE},
    {0, 23, "hour", CRON_FIELD_HOUR},
    {1, 31, "day-of-month", CRON_FIELD_DAY_OF_MONTH},
    {1, 12, "month", CRON_FIELD_MONTH},
    {0, 7, "day-of-week", CRON_FIELD_DAY_OF_WEEK},
};

static const cron_name_map_t CRON_MONTH_NAMES[] = {
    {"jan", 1}, {"feb", 2}, {"mar", 3}, {"apr", 4}, {"may", 5}, {"jun", 6},
    {"jul", 7}, {"aug", 8}, {"sep", 9}, {"oct", 10}, {"nov", 11}, {"dec", 12},
};

static const cron_name_map_t CRON_WEEKDAY_NAMES[] = {
    {"sun", 0}, {"mon", 1}, {"tue", 2}, {"wed", 3},
    {"thu", 4}, {"fri", 5}, {"sat", 6},
};

static const struct {
  const char *alias;
  const char *expression;
} CRON_ALIASES[] = {
    {"@yearly", "0 0 1 1 *"},
    {"@annually", "0 0 1 1 *"},
    {"@monthly", "0 0 1 * *"},
    {"@weekly", "0 0 * * 0"},
    {"@daily", "0 0 * * *"},
    {"@midnight", "0 0 * * *"},
    {"@hourly", "0 * * * *"},
};

static void cron_set_error(char *buffer, size_t buffer_len, const char *fmt, ...) {
  va_list ap;

  if (!buffer || buffer_len == 0) {
    return;
  }

  va_start(ap, fmt);
  vsnprintf(buffer, buffer_len, fmt, ap);
  va_end(ap);
}

static int cron_ascii_tolower(int ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A' + 'a';
  }
  return ch;
}

static int cron_text_ieq(const char *lhs, size_t lhs_len, const char *rhs) {
  size_t rhs_len = strlen(rhs);
  size_t i;

  if (lhs_len != rhs_len) {
    return 0;
  }

  for (i = 0; i < lhs_len; ++i) {
    if (cron_ascii_tolower((unsigned char)lhs[i]) !=
        cron_ascii_tolower((unsigned char)rhs[i])) {
      return 0;
    }
  }

  return 1;
}

static const char *cron_expand_alias(const char *expression) {
  size_t i;

  for (i = 0; i < sizeof(CRON_ALIASES) / sizeof(CRON_ALIASES[0]); ++i) {
    if (strcmp(expression, CRON_ALIASES[i].alias) == 0) {
      return CRON_ALIASES[i].expression;
    }
  }

  return expression;
}

static uint64_t cron_mask_range(int start, int end) {
  uint64_t mask = 0;
  int value;

  for (value = start; value <= end; ++value) {
    mask |= (1ULL << value);
  }

  return mask;
}

static int cron_localtime_safe(time_t when, struct tm *out_tm) {
#ifdef _WIN32
  return localtime_s(out_tm, &when) == 0 ? 0 : -1;
#else
  return localtime_r(&when, out_tm) != NULL ? 0 : -1;
#endif
}

static time_t cron_floor_minute(time_t when) {
  return when - (when % 60);
}

static char *cron_dup_range(const char *start, size_t length) {
  char *copy = (char *)malloc(length + 1);

  if (!copy) {
    return NULL;
  }

  if (length > 0) {
    memcpy(copy, start, length);
  }
  copy[length] = '\0';
  return copy;
}

static void cron_trim_span(const char **start, const char **end) {
  while (*start < *end &&
         ((*start)[0] == ' ' || (*start)[0] == '\t' ||
          (*start)[0] == '\r' || (*start)[0] == '\n')) {
    (*start)++;
  }

  while (*end > *start &&
         ((*end)[-1] == ' ' || (*end)[-1] == '\t' ||
          (*end)[-1] == '\r' || (*end)[-1] == '\n')) {
    (*end)--;
  }
}

static int cron_table_reserve(salts_cron_table_t *table, size_t min_capacity) {
  salts_cron_entry_t *entries;
  size_t new_capacity;

  if (table->capacity >= min_capacity) {
    return SALTS_CRON_OK;
  }

  new_capacity = table->capacity > 0 ? table->capacity * 2 : 4;
  if (new_capacity < min_capacity) {
    new_capacity = min_capacity;
  }

  entries = (salts_cron_entry_t *)realloc(table->entries,
                                          new_capacity * sizeof(*entries));
  if (!entries) {
    return SALTS_CRON_ENOMEM;
  }

  table->entries = entries;
  table->capacity = new_capacity;
  return SALTS_CRON_OK;
}

static int cron_split_entry_line(const char *line_start,
                                 const char *line_end,
                                 const char **expr_start_out,
                                 size_t *expr_len_out,
                                 const char **payload_start_out,
                                 size_t *payload_len_out) {
  const char *p = line_start;
  const char *expr_start = NULL;
  const char *payload_start = NULL;
  int field_count = 0;

  while (p < line_end) {
    const char *field_start;

    while (p < line_end && (*p == ' ' || *p == '\t')) {
      ++p;
    }
    if (p >= line_end) {
      break;
    }

    field_start = p;
    while (p < line_end && *p != ' ' && *p != '\t') {
      ++p;
    }

    if (field_count == 0) {
      expr_start = field_start;
    }

    ++field_count;
    if (field_count == CRON_FIELD_COUNT) {
      const char *expr_end = p;
      while (p < line_end && (*p == ' ' || *p == '\t')) {
        ++p;
      }
      payload_start = p;
      *expr_start_out = expr_start;
      *expr_len_out = (size_t)(expr_end - expr_start);
      *payload_start_out = payload_start;
      *payload_len_out = (size_t)(line_end - payload_start);
      return SALTS_CRON_OK;
    }
  }

  return SALTS_CRON_EPARSE;
}

void salts_cron_table_init(salts_cron_table_t *table) {
  if (!table) {
    return;
  }

  memset(table, 0, sizeof(*table));
}

void salts_cron_table_free(salts_cron_table_t *table) {
  size_t i;

  if (!table) {
    return;
  }

  for (i = 0; i < table->count; ++i) {
    free(table->entries[i].payload);
    table->entries[i].payload = NULL;
  }

  free(table->entries);
  salts_cron_table_init(table);
}

static int cron_split_fields(const char *expression,
                             const char **field_starts,
                             size_t *field_lengths,
                             char *error_buf,
                             size_t error_buf_len) {
  const char *p = expression;
  int count = 0;

  while (*p != '\0') {
    const char *start;

    while (*p == ' ' || *p == '\t') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    if (count >= CRON_FIELD_COUNT) {
      cron_set_error(error_buf, error_buf_len, "expected 5 cron fields, got more");
      return SALTS_CRON_EPARSE;
    }

    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      ++p;
    }

    field_starts[count] = start;
    field_lengths[count] = (size_t)(p - start);
    ++count;
  }

  if (count != CRON_FIELD_COUNT) {
    cron_set_error(error_buf, error_buf_len, "expected 5 cron fields, got %d", count);
    return SALTS_CRON_EPARSE;
  }

  return SALTS_CRON_OK;
}

static int cron_lookup_name(const cron_field_spec_t *spec,
                            const char *text,
                            size_t length,
                            int *value_out) {
  const cron_name_map_t *table = NULL;
  size_t count = 0;
  size_t i;

  if (spec->kind == CRON_FIELD_MONTH) {
    table = CRON_MONTH_NAMES;
    count = sizeof(CRON_MONTH_NAMES) / sizeof(CRON_MONTH_NAMES[0]);
  } else if (spec->kind == CRON_FIELD_DAY_OF_WEEK) {
    table = CRON_WEEKDAY_NAMES;
    count = sizeof(CRON_WEEKDAY_NAMES) / sizeof(CRON_WEEKDAY_NAMES[0]);
  }

  if (!table) {
    return SALTS_CRON_EPARSE;
  }

  for (i = 0; i < count; ++i) {
    if (cron_text_ieq(text, length, table[i].name)) {
      *value_out = table[i].value;
      return SALTS_CRON_OK;
    }
  }

  return SALTS_CRON_EPARSE;
}

static int cron_normalize_value(const cron_field_spec_t *spec,
                                int raw_value,
                                int *normalized_out) {
  if (spec->kind == CRON_FIELD_DAY_OF_WEEK) {
    if (raw_value < 0 || raw_value > 7) {
      return SALTS_CRON_EPARSE;
    }
    *normalized_out = (raw_value == 7) ? 0 : raw_value;
    return SALTS_CRON_OK;
  }

  if (raw_value < spec->min_value || raw_value > spec->max_value) {
    return SALTS_CRON_EPARSE;
  }

  *normalized_out = raw_value;
  return SALTS_CRON_OK;
}

static int cron_apply_raw_value(const cron_field_spec_t *spec,
                                uint64_t *bits,
                                int raw_value) {
  int normalized = 0;
  int rc = cron_normalize_value(spec, raw_value, &normalized);

  if (rc != SALTS_CRON_OK) {
    return rc;
  }

  *bits |= (1ULL << normalized);
  return SALTS_CRON_OK;
}

static int cron_field_is_full(const cron_field_spec_t *spec, uint64_t bits) {
  if (spec->kind == CRON_FIELD_DAY_OF_WEEK) {
    return bits == cron_mask_range(0, 6);
  }
  return bits == cron_mask_range(spec->min_value, spec->max_value);
}

static int cron_parser_advance(cron_field_parser_t *parser) {
  int rc = cron_lexer_next(&parser->lexer, &parser->current);

  if (rc >= 0) {
    return SALTS_CRON_OK;
  }

  cron_set_error(parser->error_buf, parser->error_buf_len,
                 "invalid token in %s field near '%.*s'",
                 parser->spec.name,
                 (int)parser->current.length,
                 parser->current.text);
  return SALTS_CRON_EPARSE;
}

static int cron_parse_value(cron_field_parser_t *parser, int *value_out) {
  int raw_value = 0;
  int normalized = 0;
  int rc;

  if (parser->current.kind == CRON_TOKEN_NUMBER) {
    raw_value = parser->current.number;
  } else if (parser->current.kind == CRON_TOKEN_NAME) {
    rc = cron_lookup_name(&parser->spec, parser->current.text, parser->current.length, &raw_value);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(parser->error_buf, parser->error_buf_len,
                     "unknown %s name '%.*s'",
                     parser->spec.name,
                     (int)parser->current.length,
                     parser->current.text);
      return rc;
    }
  } else {
    cron_set_error(parser->error_buf, parser->error_buf_len,
                   "expected value in %s field near '%.*s'",
                   parser->spec.name,
                   (int)parser->current.length,
                   parser->current.text);
    return SALTS_CRON_EPARSE;
  }

  rc = cron_normalize_value(&parser->spec, raw_value, &normalized);
  if (rc != SALTS_CRON_OK) {
    cron_set_error(parser->error_buf, parser->error_buf_len,
                   "value %d out of range for %s field",
                   raw_value,
                   parser->spec.name);
    return rc;
  }

  *value_out = normalized;
  return cron_parser_advance(parser);
}

static int cron_parse_step(cron_field_parser_t *parser, int *step_out) {
  if (parser->current.kind != CRON_TOKEN_NUMBER) {
    cron_set_error(parser->error_buf, parser->error_buf_len,
                   "expected numeric step in %s field",
                   parser->spec.name);
    return SALTS_CRON_EPARSE;
  }

  if (parser->current.number <= 0) {
    cron_set_error(parser->error_buf, parser->error_buf_len,
                   "step must be positive in %s field",
                   parser->spec.name);
    return SALTS_CRON_EPARSE;
  }

  *step_out = parser->current.number;
  return cron_parser_advance(parser);
}

static int cron_apply_range(const cron_field_spec_t *spec,
                            uint64_t *bits,
                            int start,
                            int end,
                            int step,
                            char *error_buf,
                            size_t error_buf_len) {
  int raw;

  if (step <= 0) {
    cron_set_error(error_buf, error_buf_len, "step must be positive in %s field", spec->name);
    return SALTS_CRON_EPARSE;
  }
  if (start > end) {
    cron_set_error(error_buf, error_buf_len, "range start exceeds end in %s field", spec->name);
    return SALTS_CRON_EPARSE;
  }

  for (raw = start; raw <= end; raw += step) {
    int rc = cron_apply_raw_value(spec, bits, raw);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(error_buf, error_buf_len,
                     "value %d out of range for %s field", raw, spec->name);
      return rc;
    }
  }

  return SALTS_CRON_OK;
}

static int cron_parse_term(cron_field_parser_t *parser, uint64_t *bits) {
  int start = 0;
  int end = 0;
  int step = 1;
  int rc;

  if (parser->current.kind == CRON_TOKEN_STAR) {
    start = parser->spec.min_value;
    end = (parser->spec.kind == CRON_FIELD_DAY_OF_WEEK) ? 6 : parser->spec.max_value;
    rc = cron_parser_advance(parser);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }
  } else {
    rc = cron_parse_value(parser, &start);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }

    end = start;
    if (parser->current.kind == CRON_TOKEN_DASH) {
      rc = cron_parser_advance(parser);
      if (rc != SALTS_CRON_OK) {
        return rc;
      }

      rc = cron_parse_value(parser, &end);
      if (rc != SALTS_CRON_OK) {
        return rc;
      }
    }
  }

  if (parser->current.kind == CRON_TOKEN_SLASH) {
    rc = cron_parser_advance(parser);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }

    rc = cron_parse_step(parser, &step);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }

    if (start == end) {
      end = (parser->spec.kind == CRON_FIELD_DAY_OF_WEEK) ? 7 : parser->spec.max_value;
    }
  }

  return cron_apply_range(&parser->spec, bits, start, end, step,
                          parser->error_buf, parser->error_buf_len);
}

static int cron_parse_field(const char *field_text,
                            size_t field_len,
                            const cron_field_spec_t *spec,
                            uint64_t *bits_out,
                            uint8_t *any_out,
                            char *error_buf,
                            size_t error_buf_len) {
  cron_field_parser_t parser;
  uint64_t bits = 0;
  int rc;

  memset(&parser, 0, sizeof(parser));
  parser.spec = *spec;
  parser.error_buf = error_buf;
  parser.error_buf_len = error_buf_len;

  cron_lexer_init(&parser.lexer, field_text, field_len);
  rc = cron_parser_advance(&parser);
  if (rc != SALTS_CRON_OK) {
    return rc;
  }

  if (parser.current.kind == CRON_TOKEN_EOF) {
    cron_set_error(error_buf, error_buf_len, "empty %s field", spec->name);
    return SALTS_CRON_EPARSE;
  }

  while (parser.current.kind != CRON_TOKEN_EOF) {
    rc = cron_parse_term(&parser, &bits);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }

    if (parser.current.kind == CRON_TOKEN_COMMA) {
      rc = cron_parser_advance(&parser);
      if (rc != SALTS_CRON_OK) {
        return rc;
      }
      continue;
    }

    if (parser.current.kind != CRON_TOKEN_EOF) {
      cron_set_error(error_buf, error_buf_len,
                     "unexpected token in %s field near '%.*s'",
                     spec->name,
                     (int)parser.current.length,
                     parser.current.text);
      return SALTS_CRON_EPARSE;
    }
  }

  *bits_out = bits;
  *any_out = (uint8_t)cron_field_is_full(spec, bits);
  return SALTS_CRON_OK;
}

static void cron_assign_bits(salts_cron_expr_t *expr,
                             cron_field_kind_t field,
                             uint64_t bits,
                             uint8_t any_flag) {
  switch (field) {
    case CRON_FIELD_MINUTE:
      expr->minute_bits = bits;
      break;
    case CRON_FIELD_HOUR:
      expr->hour_bits = bits;
      break;
    case CRON_FIELD_DAY_OF_MONTH:
      expr->day_of_month_bits = bits;
      expr->day_of_month_any = any_flag;
      break;
    case CRON_FIELD_MONTH:
      expr->month_bits = bits;
      break;
    case CRON_FIELD_DAY_OF_WEEK:
      expr->day_of_week_bits = bits;
      expr->day_of_week_any = any_flag;
      break;
  }
}

void salts_cron_expr_init(salts_cron_expr_t *expr) {
  if (!expr) {
    return;
  }
  memset(expr, 0, sizeof(*expr));
}

int salts_cron_parse(const char *expression, salts_cron_expr_t *out_expr) {
  return salts_cron_parse_ex(expression, out_expr, NULL, 0);
}

int salts_cron_parse_ex(const char *expression,
                        salts_cron_expr_t *out_expr,
                        char *error_buf,
                        size_t error_buf_len) {
  const char *fields[CRON_FIELD_COUNT];
  size_t lengths[CRON_FIELD_COUNT];
  const char *canonical;
  salts_cron_expr_t parsed;
  int i;
  int rc;

  if (!expression || !out_expr) {
    cron_set_error(error_buf, error_buf_len, "expression and output are required");
    return SALTS_CRON_EINVAL;
  }

  canonical = cron_expand_alias(expression);
  rc = cron_split_fields(canonical, fields, lengths, error_buf, error_buf_len);
  if (rc != SALTS_CRON_OK) {
    return rc;
  }

  salts_cron_expr_init(&parsed);
  for (i = 0; i < CRON_FIELD_COUNT; ++i) {
    uint64_t bits = 0;
    uint8_t any_flag = 0;

    rc = cron_parse_field(fields[i], lengths[i], &CRON_SPECS[i],
                          &bits, &any_flag, error_buf, error_buf_len);
    if (rc != SALTS_CRON_OK) {
      return rc;
    }

    cron_assign_bits(&parsed, CRON_SPECS[i].kind, bits, any_flag);
  }

  *out_expr = parsed;
  return SALTS_CRON_OK;
}

int salts_cron_table_load_string(const char *text,
                                 salts_cron_table_t *out_table,
                                 char *error_buf,
                                 size_t error_buf_len) {
  salts_cron_table_t table;
  const char *cursor;
  size_t line_no = 0;

  if (!text || !out_table) {
    cron_set_error(error_buf, error_buf_len, "text and output are required");
    return SALTS_CRON_EINVAL;
  }

  salts_cron_table_init(&table);
  cursor = text;

  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_end;
    const char *trimmed_start;
    const char *trimmed_end;
    const char *expr_start = NULL;
    const char *payload_start = NULL;
    size_t expr_len = 0;
    size_t payload_len = 0;
    char *expr_copy = NULL;
    char *payload_copy = NULL;
    salts_cron_entry_t *entry;
    int rc;

    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
    line_end = cursor;
    if (*cursor == '\n') {
      ++cursor;
    }

    ++line_no;
    trimmed_start = line_start;
    trimmed_end = line_end;
    cron_trim_span(&trimmed_start, &trimmed_end);

    if (trimmed_start == trimmed_end || *trimmed_start == '#') {
      continue;
    }

    rc = cron_split_entry_line(trimmed_start, trimmed_end,
                               &expr_start, &expr_len,
                               &payload_start, &payload_len);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: expected 5 cron fields followed by payload",
                     line_no);
      salts_cron_table_free(&table);
      return rc;
    }

    if (payload_len == 0) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: missing payload after cron expression",
                     line_no);
      salts_cron_table_free(&table);
      return SALTS_CRON_EPARSE;
    }

    rc = cron_table_reserve(&table, table.count + 1);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: out of memory", line_no);
      salts_cron_table_free(&table);
      return rc;
    }

    expr_copy = cron_dup_range(expr_start, expr_len);
    payload_copy = cron_dup_range(payload_start, payload_len);
    if (!expr_copy || !payload_copy) {
      free(expr_copy);
      free(payload_copy);
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: out of memory", line_no);
      salts_cron_table_free(&table);
      return SALTS_CRON_ENOMEM;
    }

    entry = &table.entries[table.count];
    salts_cron_expr_init(&entry->expr);
    rc = salts_cron_parse_ex(expr_copy, &entry->expr, error_buf, error_buf_len);
    free(expr_copy);
    if (rc != SALTS_CRON_OK) {
      if (error_buf && error_buf_len > 0) {
        char inner_error[256];
        snprintf(inner_error, sizeof(inner_error), "%s", error_buf);
        cron_set_error(error_buf, error_buf_len,
                       "line %zu: %s", line_no, inner_error);
      }
      free(payload_copy);
      salts_cron_table_free(&table);
      return rc;
    }

    entry->payload = payload_copy;
    ++table.count;
  }

  *out_table = table;
  return SALTS_CRON_OK;
}

int salts_cron_table_load_file(const char *path,
                               salts_cron_table_t *out_table,
                               char *error_buf,
                               size_t error_buf_len) {
  FILE *fp;
  char line_buf[2048];
  size_t line_no = 0;
  salts_cron_table_t table;

  if (!path || !out_table) {
    cron_set_error(error_buf, error_buf_len, "path and output are required");
    return SALTS_CRON_EINVAL;
  }

  fp = fopen(path, "r");
  if (!fp) {
    cron_set_error(error_buf, error_buf_len, "failed to open file '%s'", path);
    return SALTS_CRON_EINVAL;
  }

  salts_cron_table_init(&table);

  while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
    const char *line_start = line_buf;
    const char *line_end = line_buf + strlen(line_buf);
    const char *trimmed_start;
    const char *trimmed_end;
    const char *expr_start = NULL;
    const char *payload_start = NULL;
    size_t expr_len = 0;
    size_t payload_len = 0;
    char *expr_copy = NULL;
    char *payload_copy = NULL;
    salts_cron_entry_t *entry;
    int rc;

    ++line_no;
    trimmed_start = line_start;
    trimmed_end = line_end;
    cron_trim_span(&trimmed_start, &trimmed_end);

    if (trimmed_start == trimmed_end || *trimmed_start == '#') {
      continue;
    }

    rc = cron_split_entry_line(trimmed_start, trimmed_end,
                               &expr_start, &expr_len,
                               &payload_start, &payload_len);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: expected 5 cron fields followed by payload",
                     line_no);
      salts_cron_table_free(&table);
      fclose(fp);
      return rc;
    }

    if (payload_len == 0) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: missing payload after cron expression",
                     line_no);
      salts_cron_table_free(&table);
      fclose(fp);
      return SALTS_CRON_EPARSE;
    }

    rc = cron_table_reserve(&table, table.count + 1);
    if (rc != SALTS_CRON_OK) {
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: out of memory", line_no);
      salts_cron_table_free(&table);
      fclose(fp);
      return rc;
    }

    expr_copy = cron_dup_range(expr_start, expr_len);
    payload_copy = cron_dup_range(payload_start, payload_len);
    if (!expr_copy || !payload_copy) {
      free(expr_copy);
      free(payload_copy);
      cron_set_error(error_buf, error_buf_len,
                     "line %zu: out of memory", line_no);
      salts_cron_table_free(&table);
      fclose(fp);
      return SALTS_CRON_ENOMEM;
    }

    entry = &table.entries[table.count];
    salts_cron_expr_init(&entry->expr);
    rc = salts_cron_parse_ex(expr_copy, &entry->expr, error_buf, error_buf_len);
    free(expr_copy);
    if (rc != SALTS_CRON_OK) {
      if (error_buf && error_buf_len > 0) {
        char inner_error[256];
        snprintf(inner_error, sizeof(inner_error), "%s", error_buf);
        cron_set_error(error_buf, error_buf_len,
                       "line %zu: %s", line_no, inner_error);
      }
      free(payload_copy);
      salts_cron_table_free(&table);
      fclose(fp);
      return rc;
    }

    entry->payload = payload_copy;
    ++table.count;
  }

  fclose(fp);
  *out_table = table;
  return SALTS_CRON_OK;
}

int salts_cron_matches(const salts_cron_expr_t *expr, time_t when) {
  struct tm local_tm;
  int dom_match;
  int dow_match;

  if (!expr) {
    return 0;
  }
  if (cron_localtime_safe(when, &local_tm) != 0) {
    return 0;
  }

  if ((expr->minute_bits & (1ULL << local_tm.tm_min)) == 0) {
    return 0;
  }
  if ((expr->hour_bits & (1ULL << local_tm.tm_hour)) == 0) {
    return 0;
  }
  if ((expr->month_bits & (1ULL << (local_tm.tm_mon + 1))) == 0) {
    return 0;
  }

  dom_match = ((expr->day_of_month_bits & (1ULL << local_tm.tm_mday)) != 0);
  dow_match = ((expr->day_of_week_bits & (1ULL << local_tm.tm_wday)) != 0);

  if (expr->day_of_month_any && expr->day_of_week_any) {
    return 1;
  }
  if (expr->day_of_month_any) {
    return dow_match;
  }
  if (expr->day_of_week_any) {
    return dom_match;
  }
  return dom_match || dow_match;
}

int salts_cron_next(const salts_cron_expr_t *expr, time_t after, time_t *next_out) {
  struct tm local_tm;
  time_t candidate;
  int iters = 0;

  if (!expr || !next_out) {
    return SALTS_CRON_EINVAL;
  }

  candidate = cron_floor_minute(after) + 60;
  if (candidate <= after) {
    candidate += 60;
  }

  if (cron_localtime_safe(candidate, &local_tm) != 0) {
    return SALTS_CRON_ESTATE;
  }

  while (iters < 5000) {
    int jump = 0;
    uint64_t remaining;
    iters++;

    // 1. Check Month
    remaining = expr->month_bits >> (local_tm.tm_mon + 1);
    if (remaining == 0) {
      local_tm.tm_year++;
      local_tm.tm_mon = 0;
      local_tm.tm_mday = 1;
      local_tm.tm_hour = 0;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    } else if ((remaining & 1) == 0) {
      while ((remaining & 1) == 0) { jump++; remaining >>= 1; }
      local_tm.tm_mon += jump;
      local_tm.tm_mday = 1;
      local_tm.tm_hour = 0;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    }

    // 2. Check Day
    int dom_match = ((expr->day_of_month_bits & (1ULL << local_tm.tm_mday)) != 0);
    int dow_match = ((expr->day_of_week_bits & (1ULL << local_tm.tm_wday)) != 0);
    int day_match = 0;

    if (expr->day_of_month_any && expr->day_of_week_any) {
      day_match = 1;
    } else if (expr->day_of_month_any) {
      day_match = dow_match;
    } else if (expr->day_of_week_any) {
      day_match = dom_match;
    } else {
      day_match = dom_match || dow_match;
    }

    if (!day_match) {
      local_tm.tm_mday++;
      local_tm.tm_hour = 0;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    }

    // 3. Check Hour
    remaining = expr->hour_bits >> local_tm.tm_hour;
    if (remaining == 0) {
      local_tm.tm_mday++;
      local_tm.tm_hour = 0;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    } else if ((remaining & 1) == 0) {
      while ((remaining & 1) == 0) { jump++; remaining >>= 1; }
      local_tm.tm_hour += jump;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    }

    // 4. Check Minute
    remaining = expr->minute_bits >> local_tm.tm_min;
    if (remaining == 0) {
      local_tm.tm_hour++;
      local_tm.tm_min = 0;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    } else if ((remaining & 1) == 0) {
      while ((remaining & 1) == 0) { jump++; remaining >>= 1; }
      local_tm.tm_min += jump;
      local_tm.tm_isdst = -1;
      mktime(&local_tm);
      continue;
    }

    // Match found!
    local_tm.tm_isdst = -1;
    candidate = mktime(&local_tm);
    if (candidate != (time_t)-1) {
      *next_out = candidate;
      return SALTS_CRON_OK;
    }
    return SALTS_CRON_ESTATE;
  }

  return SALTS_CRON_ENEXT;
}

int salts_cron_next_n(const salts_cron_expr_t *expr,
                      time_t after,
                      time_t *next_out,
                      size_t max_count) {
  time_t candidate;
  size_t written = 0;
  int i;

  if (!expr) {
    return SALTS_CRON_EINVAL;
  }
  if (max_count == 0) {
    return 0;
  }
  if (!next_out) {
    return SALTS_CRON_EINVAL;
  }

  candidate = after;
  for (i = 0; i < (int)max_count; ++i) {
    int rc = salts_cron_next(expr, candidate, &next_out[written]);
    if (rc != SALTS_CRON_OK) {
      return written > 0 ? (int)written : rc;
    }
    candidate = next_out[written];
    ++written;
  }

  return (int)written;
}

int salts_cron_format_time(time_t when,
                           char *buffer,
                           size_t buffer_len,
                           const char *format) {
  struct tm local_tm;
  const char *active_format = format;
  size_t written;

  if (!buffer || buffer_len == 0) {
    return SALTS_CRON_EINVAL;
  }

  if (!active_format || active_format[0] == '\0') {
    active_format = "%Y-%m-%d %H:%M";
  }

  if (cron_localtime_safe(when, &local_tm) != 0) {
    return SALTS_CRON_ESTATE;
  }

  written = strftime(buffer, buffer_len, active_format, &local_tm);
  if (written == 0) {
    if (buffer_len > 0) {
      buffer[0] = '\0';
    }
    return SALTS_CRON_EINVAL;
  }

  return (int)written;
}

int salts_cron_runner_advance(salts_cron_runner_t *runner, time_t now) {
  salts_cron_callback_t callback;
  void *user_data;
  time_t current_minute;
  time_t start_minute;
  time_t check;
  int fired = 0;

  if (!runner) {
    return SALTS_CRON_EINVAL;
  }

  current_minute = cron_floor_minute(now);

  cmeta_mutex_lock(&runner->lock);
  callback = runner->callback;
  user_data = runner->user_data;

  if (runner->cursor_minute == 0) {
    start_minute = current_minute;
  } else {
    start_minute = runner->cursor_minute + 60;
  }

  if (current_minute < runner->cursor_minute) {
    cmeta_mutex_unlock(&runner->lock);
    return 0;
  }

  runner->cursor_minute = current_minute;
  cmeta_mutex_unlock(&runner->lock);

  check = start_minute - 60;
  while (1) {
    time_t next_fire = 0;
    int rc = salts_cron_next(&runner->expr, check, &next_fire);
    if (rc != SALTS_CRON_OK || next_fire > current_minute) {
      break;
    }
    if (callback) {
      callback(&runner->expr, next_fire, user_data);
    }
    ++fired;
    check = next_fire;
  }

  return fired;
}

static void salts_cron_runner_thread(void *arg) {
  salts_cron_runner_t *runner = (salts_cron_runner_t *)arg;

  for (;;) {
    time_t now;
    time_t next_fire = 0;
    uint64_t wait_ns;
    int rc;

    cmeta_mutex_lock(&runner->lock);
    if (runner->stop_requested) {
      break;
    }

    now = time(NULL);
    rc = salts_cron_next(&runner->expr, now, &next_fire);
    if (rc != SALTS_CRON_OK) {
      runner->stop_requested = 1;
      break;
    }

    wait_ns = (uint64_t)(next_fire - now) * 1000000000ULL;
    if (wait_ns == 0) {
      wait_ns = 1000000ULL;
    }

    rc = cmeta_cond_timedwait(&runner->cond, &runner->lock, wait_ns);
    if (runner->stop_requested) {
      break;
    }
    if (rc == SALTS_CRON_OK) {
      cmeta_mutex_unlock(&runner->lock);
      continue;
    }

    cmeta_mutex_unlock(&runner->lock);
    (void)salts_cron_runner_advance(runner, time(NULL));
  }

  runner->thread_active = 0;
  cmeta_cond_broadcast(&runner->cond);
  cmeta_mutex_unlock(&runner->lock);
}

salts_cron_runner_t *salts_cron_runner_create(const char *expression,
                                              salts_cron_callback_t callback,
                                              void *user_data) {
  salts_cron_runner_t *runner;

  if (!expression || !callback) {
    return NULL;
  }

  runner = (salts_cron_runner_t *)calloc(1, sizeof(*runner));
  if (!runner) {
    return NULL;
  }

  if (salts_cron_parse(expression, &runner->expr) != SALTS_CRON_OK) {
    free(runner);
    return NULL;
  }

  runner->callback = callback;
  runner->user_data = user_data;
  cmeta_mutex_init(&runner->lock);
  cmeta_cond_init(&runner->cond);
  return runner;
}

int salts_cron_runner_start(salts_cron_runner_t *runner) {
  time_t now;
  time_t next_fire = 0;
  int rc;

  if (!runner) {
    return SALTS_CRON_EINVAL;
  }

  now = time(NULL);
  rc = salts_cron_next(&runner->expr, now, &next_fire);
  if (rc != SALTS_CRON_OK) {
    return rc;
  }

  cmeta_mutex_lock(&runner->lock);
  if (runner->thread_active) {
    cmeta_mutex_unlock(&runner->lock);
    return SALTS_CRON_ESTATE;
  }

  runner->stop_requested = 0;
  runner->cursor_minute = cron_floor_minute(now);
  runner->thread_active = 1;
  cmeta_mutex_unlock(&runner->lock);

  rc = cmeta_thread_create(&runner->thread, salts_cron_runner_thread, runner);
  if (rc != 0) {
    cmeta_mutex_lock(&runner->lock);
    runner->thread_active = 0;
    cmeta_mutex_unlock(&runner->lock);
    return SALTS_CRON_ESTATE;
  }

  return SALTS_CRON_OK;
}

int salts_cron_runner_stop(salts_cron_runner_t *runner) {
  if (!runner) {
    return SALTS_CRON_EINVAL;
  }

  cmeta_mutex_lock(&runner->lock);
  if (!runner->thread_active) {
    cmeta_mutex_unlock(&runner->lock);
    return SALTS_CRON_OK;
  }
  runner->stop_requested = 1;
  cmeta_cond_broadcast(&runner->cond);
  cmeta_mutex_unlock(&runner->lock);

  cmeta_thread_join(&runner->thread);
  return SALTS_CRON_OK;
}

void salts_cron_runner_destroy(salts_cron_runner_t *runner) {
  if (!runner) {
    return;
  }

  salts_cron_runner_stop(runner);
  cmeta_cond_destroy(&runner->cond);
  cmeta_mutex_destroy(&runner->lock);
  free(runner);
}

const salts_cron_expr_t *salts_cron_runner_expr(const salts_cron_runner_t *runner) {
  return runner ? &runner->expr : NULL;
}

const char *salts_cron_strerror(int code) {
  switch (code) {
    case SALTS_CRON_OK:
      return "ok";
    case SALTS_CRON_EINVAL:
      return "invalid argument";
    case SALTS_CRON_EPARSE:
      return "parse error";
    case SALTS_CRON_ENEXT:
      return "no next matching time";
    case SALTS_CRON_ESTATE:
      return "invalid runner state";
    case SALTS_CRON_ENOMEM:
      return "out of memory";
    default:
      return "unknown cron error";
  }
}
