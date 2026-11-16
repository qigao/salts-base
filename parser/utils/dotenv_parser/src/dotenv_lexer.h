#ifndef DOTENV_LEXER_H
#define DOTENV_LEXER_H

#include <stddef.h>

typedef enum {
  DOTENV_TOKEN_EOF = 0,
  DOTENV_TOKEN_KEY,
  DOTENV_TOKEN_VALUE,
  DOTENV_TOKEN_COMMENT,
  DOTENV_TOKEN_NEWLINE,
  DOTENV_TOKEN_ERROR
} dotenv_token_type_t;

typedef struct {
  dotenv_token_type_t type;
  const char *value;
  size_t length;
} dotenv_token_t;

typedef enum { DOTENV_LEX_STATE_NORMAL, DOTENV_LEX_STATE_VALUE } dotenv_lex_state_t;

typedef struct {
  const char *input;
  const char *cursor;
  const char *marker;
  const char *limit;
  int line;
  dotenv_lex_state_t state;
} dotenv_lexer_t;

void dotenv_lexer_init(dotenv_lexer_t *lexer, const char *input, size_t length);
int dotenv_lexer_next(dotenv_lexer_t *lexer, dotenv_token_t *token);

#endif // DOTENV_LEXER_H
