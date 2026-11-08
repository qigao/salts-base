// re2c $INPUT -o $OUTPUT
#include "selector_internal.h"
#include "selector_v1_grammar_gen.h"

#include <stdio.h>
#include <string.h>

static int selector_lexer_fail(selector_lexer_t *lexer,
                               salts_selector_status_t status,
                               const char *cursor, const char *message) {
  lexer->status = status;
  lexer->error_offset = (size_t)(cursor - lexer->input);
  (void)snprintf(lexer->message, sizeof(lexer->message), "%s", message);
  return -1;
}

void selector_lexer_init(selector_lexer_t *lexer, const char *input,
                         size_t input_size) {
  if (!lexer) return;
  memset(lexer, 0, sizeof(*lexer));
  lexer->input = input;
  lexer->cursor = input;
  lexer->limit = input + input_size;
  lexer->marker = input;
  lexer->status = SALTS_SELECTOR_OK;
}

int selector_lexer_next(selector_lexer_t *lexer,
                        selector_semantic_t *token) {
  const char *YYCURSOR;
  const char *YYMARKER;
  const char *YYLIMIT;
  const char *token_start;

  if (!lexer || !token) return -1;
  YYCURSOR = lexer->cursor;
  YYMARKER = lexer->marker;
  YYLIMIT = lexer->limit;

again:
  memset(token, 0, sizeof(*token));
  token->node = SELECTOR_NO_NODE;
  token_start = YYCURSOR;

  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:yyfill:enable = 0;
    re2c:eof = 0;

    ws = [\x09\x0a\x0d\x20]+;
    ident = [A-Za-z_][A-Za-z0-9_-]*;
    field = ident ("." ident)*;
    escape = "\\" (["\\/bfnrt] | "u" [0-9A-Fa-f]{4});
    string = "\"" ([^"\\\x00-\x1f] | escape)* "\"";

    $ {
      lexer->cursor = YYCURSOR;
      lexer->marker = YYMARKER;
      return 0;
    }

    ws {
      lexer->cursor = YYCURSOR;
      lexer->marker = YYMARKER;
      goto again;
    }

    "&&" { token->node = SELECTOR_V1_TOKEN_AND; goto emit; }
    "||" { token->node = SELECTOR_V1_TOKEN_OR; goto emit; }
    "==" { token->node = SELECTOR_V1_TOKEN_EQ; goto emit; }
    "!=" { token->node = SELECTOR_V1_TOKEN_NE; goto emit; }
    "!"  { token->node = SELECTOR_V1_TOKEN_BANG; goto emit; }
    "("  {
      if (++lexer->parenthesis_depth > SALTS_SELECTOR_MAX_DEPTH_V1)
        return selector_lexer_fail(lexer, SALTS_SELECTOR_RESOURCE_LIMIT,
                                   token_start, "selector nesting limit exceeded");
      token->node = SELECTOR_V1_TOKEN_LPAREN;
      goto emit;
    }
    ")"  {
      if (lexer->parenthesis_depth > 0u) --lexer->parenthesis_depth;
      token->node = SELECTOR_V1_TOKEN_RPAREN;
      goto emit;
    }
    "["  { token->node = SELECTOR_V1_TOKEN_LBRACKET; goto emit; }
    "]"  { token->node = SELECTOR_V1_TOKEN_RBRACKET; goto emit; }
    ","  { token->node = SELECTOR_V1_TOKEN_COMMA; goto emit; }
    "in" { token->node = SELECTOR_V1_TOKEN_IN; goto emit; }
    "not" { token->node = SELECTOR_V1_TOKEN_NOT; goto emit; }
    "has" { token->node = SELECTOR_V1_TOKEN_HAS; goto emit; }
    "capability" { token->node = SELECTOR_V1_TOKEN_CAPABILITY; goto emit; }

    string { token->node = SELECTOR_V1_TOKEN_STRING; goto emit; }
    field { token->node = SELECTOR_V1_TOKEN_FIELD; goto emit; }

    * {
      return selector_lexer_fail(lexer, SALTS_SELECTOR_SYNTAX_ERROR,
                                 token_start, "invalid selector token");
    }
  */

emit:
  token->value = token_start;
  token->length = (size_t)(YYCURSOR - token_start);
  token->offset = (size_t)(token_start - lexer->input);
  lexer->cursor = YYCURSOR;
  lexer->marker = YYMARKER;
  return (int)token->node;
}
