#ifndef SALTS_CRON_LEXER_H
#define SALTS_CRON_LEXER_H

#include <stddef.h>

typedef enum cron_token_kind_e {
  CRON_TOKEN_EOF = 0,
  CRON_TOKEN_STAR,
  CRON_TOKEN_COMMA,
  CRON_TOKEN_DASH,
  CRON_TOKEN_SLASH,
  CRON_TOKEN_NUMBER,
  CRON_TOKEN_NAME,
  CRON_TOKEN_ERROR
} cron_token_kind_t;

typedef struct cron_token_s {
  cron_token_kind_t kind;
  const char *text;
  size_t length;
  int number;
} cron_token_t;

typedef struct cron_lexer_s {
  const char *input;
  const char *cursor;
  const char *limit;
} cron_lexer_t;

void cron_lexer_init(cron_lexer_t *lexer, const char *input, size_t length);
int cron_lexer_next(cron_lexer_t *lexer, cron_token_t *token);

#endif
