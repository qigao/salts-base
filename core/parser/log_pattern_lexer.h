/**
 * @file log_pattern_lexer.h
 * @brief Log format pattern lexer API
 */

#ifndef LOG_PATTERN_LEXER_H
#define LOG_PATTERN_LEXER_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// Placeholder token types
typedef enum {
  LOG_TOKEN_TEXT = 0,      // Literal text
  LOG_TOKEN_TIME,          // {time}
  LOG_TOKEN_TIME_MS,       // {time_ms}
  LOG_TOKEN_LEVEL,         // {level}
  LOG_TOKEN_COMPONENT,     // {component}
  LOG_TOKEN_FILE,          // {file}
  LOG_TOKEN_LINE,          // {line}
  LOG_TOKEN_THREAD,        // {thread}
  LOG_TOKEN_MESSAGE,       // {message}
  LOG_TOKEN_END,           // End of pattern
  LOG_TOKEN_UNKNOWN        // Unknown placeholder
} log_token_t;

/**
 * @brief Scan next token from pattern
 * @param cursor Pointer to current position (updated on return)
 * @param token_start Set to start of token
 * @param token_len Set to length of token (for TEXT tokens)
 * @return Token type
 */
log_token_t log_pattern_scan(const char **cursor, const char **token_start, size_t *token_len);

/** Length-bounded scanner used while compiling a pattern. */
log_token_t log_pattern_scan_n(const char **cursor, const char *end,
                               const char **token_start, size_t *token_len);

#ifdef __cplusplus
}
#endif
#endif // LOG_PATTERN_LEXER_H
