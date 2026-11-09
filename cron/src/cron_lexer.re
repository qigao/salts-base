// re2c --lang c
#include "cron_lexer.h"

static int cron_parse_number(const char *start, const char *end) {
  int value = 0;
  while (start < end) {
    value = (value * 10) + (*start - '0');
    start++;
  }
  return value;
}

void cron_lexer_init(cron_lexer_t *lexer, const char *input, size_t length) {
  if (!lexer) {
    return;
  }
  lexer->input = input;
  lexer->cursor = input;
  lexer->limit = input + length;
}

int cron_lexer_next(cron_lexer_t *lexer, cron_token_t *token) {
  const char *YYCURSOR = lexer->cursor;
  const char *YYMARKER;
  const char *YYLIMIT = lexer->limit;
  const char *token_start = YYCURSOR;

  /*!re2c
    re2c:define:YYCTYPE = "char";
    re2c:yyfill:enable = 0;
    re2c:eof = 0;

    number = [0-9]+;
    name = [A-Za-z][A-Za-z0-9]*;

    $ {
      token->kind = CRON_TOKEN_EOF;
      token->text = YYCURSOR;
      token->length = 0;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 0;
    }

    "*" {
      token->kind = CRON_TOKEN_STAR;
      token->text = token_start;
      token->length = 1;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 1;
    }

    "," {
      token->kind = CRON_TOKEN_COMMA;
      token->text = token_start;
      token->length = 1;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 1;
    }

    "-" {
      token->kind = CRON_TOKEN_DASH;
      token->text = token_start;
      token->length = 1;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 1;
    }

    "/" {
      token->kind = CRON_TOKEN_SLASH;
      token->text = token_start;
      token->length = 1;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 1;
    }

    number {
      token->kind = CRON_TOKEN_NUMBER;
      token->text = token_start;
      token->length = (size_t)(YYCURSOR - token_start);
      token->number = cron_parse_number(token_start, YYCURSOR);
      lexer->cursor = YYCURSOR;
      return 1;
    }

    name {
      token->kind = CRON_TOKEN_NAME;
      token->text = token_start;
      token->length = (size_t)(YYCURSOR - token_start);
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return 1;
    }

    * {
      token->kind = CRON_TOKEN_ERROR;
      token->text = token_start;
      token->length = 1;
      token->number = 0;
      lexer->cursor = YYCURSOR;
      return -1;
    }
  */
}
