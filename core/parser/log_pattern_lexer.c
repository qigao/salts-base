/* Native C11 realization of the original
 * utils/parser/log_pattern_lexer.re token grammar. No dynamic CMeta,
 * generated compiler output or unchecked NUL reads at SDK build time. */
#include "log_pattern_lexer.h"
#include <string.h>

typedef struct log_token_literal {
  const char *name;
  size_t size;
  log_token_t type;
} log_token_literal;

static const log_token_literal log_placeholder_tokens[] = {
  {"{time_ms}", sizeof("{time_ms}")-1u, LOG_TOKEN_TIME_MS},
  {"{time}", sizeof("{time}")-1u, LOG_TOKEN_TIME},
  {"{level}", sizeof("{level}")-1u, LOG_TOKEN_LEVEL},
  {"{component}", sizeof("{component}")-1u, LOG_TOKEN_COMPONENT},
  {"{file}", sizeof("{file}")-1u, LOG_TOKEN_FILE},
  {"{line}", sizeof("{line}")-1u, LOG_TOKEN_LINE},
  {"{thread}", sizeof("{thread}")-1u, LOG_TOKEN_THREAD},
  {"{message}", sizeof("{message}")-1u, LOG_TOKEN_MESSAGE}
};

log_token_t log_pattern_scan_n(const char **cursor, const char *end,
                               const char **token_start, size_t *token_len) {
  if (!cursor || !*cursor || !end || !token_start || !token_len)
    return LOG_TOKEN_END;
  const char *begin = *cursor;
  *token_start = begin;
  *token_len = 0u;
  if (begin >= end) return LOG_TOKEN_END;

  if (*begin != '{') {
    const char *p = begin;
    while (p < end && *p != '{') ++p;
    *cursor = p;
    *token_len = (size_t)(p - begin);
    return LOG_TOKEN_TEXT;
  }

  const size_t remaining = (size_t)(end - begin);
  for (size_t i=0u;i<sizeof(log_placeholder_tokens)/sizeof(log_placeholder_tokens[0]);++i) {
    const log_token_literal *item = &log_placeholder_tokens[i];
    if (remaining >= item->size && memcmp(begin,item->name,item->size)==0) {
      *cursor = begin + item->size;
      return item->type;
    }
  }

  /* Preserve original re2c {anything} token semantics. A nested '{' is
   * ordinary content; only '}' terminates the unknown placeholder. */
  const char *p = begin + 1;
  while (p < end && *p != '}') ++p;
  if (p < end) {
    *cursor = p+1;
    *token_len = (size_t)(*cursor - begin);
    return LOG_TOKEN_UNKNOWN;
  }
  *cursor = begin + 1;
  *token_len = 1u;
  return LOG_TOKEN_TEXT;
}

log_token_t log_pattern_scan(const char **cursor,
                             const char **token_start, size_t *token_len) {
  if (!cursor || !*cursor || !token_start || !token_len) return LOG_TOKEN_END;
  return log_pattern_scan_n(cursor, *cursor + strlen(*cursor), token_start, token_len);
}
