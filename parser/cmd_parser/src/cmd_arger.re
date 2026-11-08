// re2c --lang c

#include "cmd_arger.h"
#include "cmd_arger_internal.h"

/**
 * @brief Lexer function implementation using re2c.
 * Decides if an argument is an option, a flag, or a positional value.
 */
cmd_token_kind_t lex_token(const char *s, const char **key_start, size_t *key_len,
                           const char **val_start) {
  const char *YYCURSOR = s;
  const char *YYMARKER;
  const char *yyt1, *yyt2, *yyt3;

  /*!re2c
      re2c:define:YYCTYPE = char;
      re2c:yyfill:enable = 0;
      re2c:flags:tags = 1;

      alpha = [a-zA-Z];
      alnum = [a-zA-Z0-9];
      word  = [a-zA-Z0-9-]+;

      * { return CMD_TOKEN_POSITIONAL; }

      "--" "\000" { return CMD_TOKEN_DASH_DASH; }

      "--help" "\000" { return CMD_TOKEN_HELP; }

      "--" @yyt1 word @yyt2 "\000" {
          *key_start = yyt1;
          *key_len = yyt2 - yyt1;
          return CMD_TOKEN_OPTION;
      }

      "--" @yyt1 word @yyt2 "=" @yyt3 [^\000]* "\000" {
          *key_start = yyt1;
          *key_len = yyt2 - yyt1;
          *val_start = yyt3;
          return CMD_TOKEN_OPTION_WITH_VALUE;
      }

      "-" @yyt1 alnum+ @yyt2 "\000" {
          *key_start = yyt1;
          *key_len = yyt2 - yyt1;
          return CMD_TOKEN_SHORT_OPTION;
      }
  */
}

/**
 * @brief Response file lexer. Splits a file buffer into tokens.
 */
size_t lex_next_arg(const char **YYCURSOR_PTR, const char **start) {
  const char *YYCURSOR = *YYCURSOR_PTR;
  const char *YYMARKER;

  // Skip whitespace and handle tokens
loop:
  *start = YYCURSOR;
  /*!re2c
      re2c:define:YYCTYPE = char;
      re2c:yyfill:enable = 0;

      ws = [ \t\r\n]+;

      "\000" { *YYCURSOR_PTR = YYCURSOR; return 0; }

      ws { goto loop; }

      // Simple quoted string (doesn't handle nested escapes for now)
      '"' [^"\000]* '"' {
          *start = *start + 1; // Skip opening quote
          *YYCURSOR_PTR = YYCURSOR;
          return (YYCURSOR - *start) - 1; // Length without closing quote
      }

      // Standard unquoted word
      [^ \t\r\n\000]+ {
          *YYCURSOR_PTR = YYCURSOR;
          return YYCURSOR - *start;
      }
  */
}

/**
 * @brief Implementation of the colorizer engine using re2c for fast tag matching.
 */
void mustache_colorize(char *out, const char *in, CmdArgerBool colors) {
  const char *YYCURSOR = in;
  const char *YYMARKER;
  char *p = out;

  for (;;) {
    /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;

        * { *p++ = YYCURSOR[-1]; if (YYCURSOR[-1] == '\0') return; continue; }

        "{{red}}"      { if (colors) { memcpy(p, "\x1b[91m", 5); p += 5; } continue; }
        "{{green}}"    { if (colors) { memcpy(p, "\x1b[92m", 5); p += 5; } continue; }
        "{{yellow}}"   { if (colors) { memcpy(p, "\x1b[93m", 5); p += 5; } continue; }
        "{{blue}}"     { if (colors) { memcpy(p, "\x1b[94m", 5); p += 5; } continue; }
        "{{magenta}}"  { if (colors) { memcpy(p, "\x1b[95m", 5); p += 5; } continue; }
        "{{cyan}}"     { if (colors) { memcpy(p, "\x1b[96m", 5); p += 5; } continue; }
        "{{white}}"    { if (colors) { memcpy(p, "\x1b[97m", 5); p += 5; } continue; }
        "{{grey}}"     { if (colors) { memcpy(p, "\x1b[90m", 5); p += 5; } continue; }
        "{{bold}}"     { if (colors) { memcpy(p, "\x1b[1m",  4); p += 4; } continue; }
        "{{italic}}"   { if (colors) { memcpy(p, "\x1b[3m",  4); p += 4; } continue; }
        "{{underline}}"{ if (colors) { memcpy(p, "\x1b[4m",  4); p += 4; } continue; }
        "{{reset}}"    { if (colors) { memcpy(p, "\x1b[0m",  4); p += 4; } continue; }

        // Premium semantic tags
        "{{error}}"    { if (colors) { memcpy(p, "\x1b[91m\x1b[1m", 9); p += 9; } continue; }
        "{{warning}}"  { if (colors) { memcpy(p, "\x1b[93m\x1b[1m", 9); p += 9; } continue; }

        "\000"         { *p = '\0'; return; }
    */
  }
}
