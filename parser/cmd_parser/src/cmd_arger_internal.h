#ifndef __CMD_ARGER_INTERNAL_H__
#define __CMD_ARGER_INTERNAL_H__

#include <stddef.h>
#include <stdarg.h>
#include "cmd_arger.h"

/**
 * @brief Token types for the re2c lexer.
 */
typedef enum {
    CMD_TOKEN_POSITIONAL,        /**< A positional argument */
    CMD_TOKEN_HELP,              /**< The --help flag */
    CMD_TOKEN_OPTION,            /**< A long option (e.g., --name) */
    CMD_TOKEN_OPTION_WITH_VALUE, /**< A long option with inline value (e.g., --name=val) */
    CMD_TOKEN_SHORT_OPTION,      /**< A short option or bundle (e.g., -v or -abc) */
    CMD_TOKEN_DASH_DASH          /**< The -- separator to stop option parsing */
} cmd_token_kind_t;

/**
 * @brief Lexes a single argument string.
 *
 * @param s The null-terminated string to lex.
 * @param key_start Output: start of the key (option name).
 * @param key_len Output: length of the key.
 * @param val_start Output: start of the value (for --key=val).
 * @return The token type found.
 */
cmd_token_kind_t lex_token(const char* s, const char** key_start, size_t* key_len, const char** val_start);

/**
 * @brief Extracts the next token from a string buffer, skipping whitespace.
 *
 * @param YYCURSOR Pointer to a cursor within the buffer. Updated to next position.
 * @param start Output: start of the found token.
 * @return Length of the token, or 0 if end of buffer.
 */
size_t lex_next_arg(const char** YYCURSOR, const char** start);

/**
 * @brief Colorizes a string by replacing mustache tags with ANSI escape sequences.
 *
 * @param out Buffer to store the colorized output.
 * @param in Null-terminated input string with mustache tags (e.g., {{red}}).
 * @param colors Whether to actually apply colors or just strip tags.
 */
void mustache_colorize(char* out, const char* in, CmdArgerBool colors);

#endif // __CMD_ARGER_INTERNAL_H__
