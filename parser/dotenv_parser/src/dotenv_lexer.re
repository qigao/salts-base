// re2c --lang c
#include "dotenv_lexer.h"
#include "simd_scan.h"
#include <string.h>

void dotenv_lexer_init(dotenv_lexer_t *lexer, const char *input, size_t length) {
  if (!lexer) return;
  lexer->input = input;
  lexer->cursor = input;
  lexer->limit = input + length;
  lexer->line = 1;
  lexer->state = DOTENV_LEX_STATE_NORMAL;
}

static int dotenv_lex(dotenv_lexer_t *lexer, dotenv_token_t *token) {
  const char *YYCURSOR = lexer->cursor;
  const char *YYMARKER;
  const char *YYLIMIT = lexer->limit;
  const char *token_start;

lex_start:
  const char *whitespace_end = salts_simd_skip_horizontal_whitespace(YYCURSOR, YYLIMIT);
  if (whitespace_end > YYCURSOR) {
    YYCURSOR = whitespace_end;
    goto lex_start;
  }
  token_start = YYCURSOR;

  if (YYCURSOR < YYLIMIT && *YYCURSOR == '#') {
    const char *comment_end = salts_simd_find_any4(YYCURSOR + 1, YYLIMIT,
                                                    '\r', '\n', '\0', '\0');
    token->type = DOTENV_TOKEN_COMMENT;
    token->value = token_start;
    token->length = (size_t)(comment_end - token_start);
    lexer->cursor = comment_end;
    return 1;
  }

  /*!re2c
      re2c:define:YYCTYPE = "char";
      re2c:yyfill:enable = 0;
      re2c:eof = 0;

      ws       = [ \t];
      newline  = "\r\n" | "\r" | "\n";
      comment  = ws* "#" [^\r\n\x00]*;
      key      = [a-zA-Z_][a-zA-Z0-9_.]*;

      // End of input
      $ {
          token->type = DOTENV_TOKEN_EOF;
          lexer->cursor = YYCURSOR;
          return 0;
      }

      // Skip whitespace
      ws+ {
          goto lex_start;
      }

      // Newline
      newline {
          token->type = DOTENV_TOKEN_NEWLINE;
          token->value = token_start;
          token->length = (size_t)(YYCURSOR - token_start);
          lexer->cursor = YYCURSOR;
          lexer->line++;
          return 1;
      }

      // Comment
      comment {
          token->type = DOTENV_TOKEN_COMMENT;
          token->value = token_start;
          token->length = (size_t)(YYCURSOR - token_start);
          lexer->cursor = YYCURSOR;
          return 1;
      }

      // Key = pattern
      key ws* "=" {
          token->type = DOTENV_TOKEN_KEY;
          // Trim trailing ws and '='
          const char *key_end = token_start;
          while (*key_end && *key_end != '=' && *key_end != ' ' && *key_end != '\t') {
              key_end++;
          }
          token->value = token_start;
          token->length = (size_t)(key_end - token_start);
          lexer->cursor = YYCURSOR;
          lexer->state = DOTENV_LEX_STATE_VALUE;
          return 1;
      }

      // Error
      * {
          token->type = DOTENV_TOKEN_ERROR;
          token->value = token_start;
          token->length = 1;
          lexer->cursor = YYCURSOR;
          return -1;
      }
  */
}

static int dotenv_lex_value(dotenv_lexer_t *lexer, dotenv_token_t *token) {
  const char *YYCURSOR = lexer->cursor;
  const char *YYMARKER;
  const char *YYLIMIT = lexer->limit;
  const char *token_start;

  // Skip leading spaces after '='
  YYCURSOR = salts_simd_skip_horizontal_whitespace(YYCURSOR, YYLIMIT);

  token_start = YYCURSOR;

  if (YYCURSOR < YYLIMIT && *YYCURSOR != '"' && *YYCURSOR != '\'' &&
      *YYCURSOR != '\r' && *YYCURSOR != '\n' && *YYCURSOR != '#') {
    const char *value_end = salts_simd_find_any4(YYCURSOR, YYLIMIT,
                                                  '\r', '\n', '#', '\0');
    const char *trimmed_end = value_end;
    while (trimmed_end > token_start &&
           (*(trimmed_end - 1) == ' ' || *(trimmed_end - 1) == '\t')) {
      --trimmed_end;
    }
    token->type = DOTENV_TOKEN_VALUE;
    token->value = token_start;
    token->length = (size_t)(trimmed_end - token_start);
    lexer->cursor = value_end;
    lexer->state = DOTENV_LEX_STATE_NORMAL;
    return 1;
  }

    /*!re2c
      re2c:define:YYCTYPE = "char";
      re2c:yyfill:enable = 0;
      re2c:eof = 0;

      // Quoted value
      dq_val = ["] ([^"\\] | [\\]["])* ["];
      sq_val = ['] ([^'\\] | [\\]['])* ['];
      // Unquoted value (up to newline or comment start)
      uval_char = [^\r\n#\x00];
      uval = uval_char+;

      $ {
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start;
          token->length = 0;
          lexer->cursor = YYCURSOR;
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }

      dq_val {
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start + 1; // skip "
          token->length = (size_t)(YYCURSOR - token_start - 2); // skip ""
          lexer->cursor = YYCURSOR;
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }

      sq_val {
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start + 1; // skip '
          token->length = (size_t)(YYCURSOR - token_start - 2); // skip ''
          lexer->cursor = YYCURSOR;
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }

      uval {
          const char *val_end = YYCURSOR;
          // trim trailing ws
          while (val_end > token_start && (*(val_end-1) == ' ' || *(val_end-1) == '\t')) {
              val_end--;
          }
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start;
          token->length = (size_t)(val_end - token_start);
          lexer->cursor = YYCURSOR;
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }

      // Empty value or immediate comment/newline
      [\r\n#] {
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start;
          token->length = 0;
          lexer->cursor = token_start; // push back
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }

      * {
          token->type = DOTENV_TOKEN_VALUE;
          token->value = token_start;
          token->length = 0;
          lexer->cursor = token_start;
          lexer->state = DOTENV_LEX_STATE_NORMAL;
          return 1;
      }
  */
}

int dotenv_lexer_next(dotenv_lexer_t *lexer, dotenv_token_t *token) {
  if (lexer->state == DOTENV_LEX_STATE_VALUE) {
    return dotenv_lex_value(lexer, token);
  }
  return dotenv_lex(lexer, token);
}
