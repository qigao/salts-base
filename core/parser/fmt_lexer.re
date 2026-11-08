// re2c -o fmt_lexer_gen.c fmt_lexer.re
/**
 * @file fmt_lexer.re
 * @brief Source for fmt.h style format string lexer
 */

#include "fmt_lexer.h"
#include <vstr.h>
#include <string.h>

fmt_token_t fmt_scan_v_n(const char **cursor, const char *end, vstr *token) {
    const char *start;

    if (!cursor || !*cursor || !token || !end || *cursor >= end) {
        if (token) {
            token->data = (cursor && *cursor) ? *cursor : NULL;
            token->len = 0;
        }
        return FMT_TOKEN_END;
    }

    start = *cursor;
    token->data = start;
    token->len = 0;

    if (*start != '{' && *start != '}') {
        const char *text_end = start;
        while (text_end < end && *text_end != '{' && *text_end != '}')
            ++text_end;
        token->data = start;
        token->len = (size_t)(text_end - start);
        *cursor = text_end;
        return FMT_TOKEN_TEXT;
    }

    if (*start == '{') {
        if (start + 1 < end && start[1] == '{') {
            *cursor = start + 2;
            token->data = start;
            token->len = 2;
            return FMT_TOKEN_LBRACE_ESC;
        }

        if (start + 1 < end && start[1] == '}') {
            *cursor = start + 2;
            token->data = start;
            token->len = 2;
            return FMT_TOKEN_PLACEHOLDER;
        }

        if (start + 1 < end && start[1] == ':') {
            const char *p = start + 2;
            while (p < end && *p != '}' && *p != '{')
                ++p;
            if (p < end && *p == '}') {
                *cursor = p + 1;
                token->data = start + 2;
                token->len = (size_t)(p - (start + 2));
                return FMT_TOKEN_SPECIFIER;
            }
            *cursor = start + 1;
            token->data = start;
            token->len = 1;
            return FMT_TOKEN_INVALID;
        }

        {
            const char *p = start + 1;
            while (p < end && *p != '}' && *p != '{')
                ++p;
            if (p < end && *p == '}') {
                *cursor = p + 1;
                token->data = start;
                token->len = (size_t)(p - start + 1);
                return FMT_TOKEN_INVALID;
            }
        }
        *cursor = start + 1;
        token->data = start;
        token->len = 1;
        return FMT_TOKEN_INVALID;
    }

    if (start + 1 < end && start[1] == '}') {
        *cursor = start + 2;
        token->data = start;
        token->len = 2;
        return FMT_TOKEN_RBRACE_ESC;
    }

    *cursor = start + 1;
    token->data = start;
    token->len = 1;
    return FMT_TOKEN_INVALID;
}

fmt_token_t fmt_scan_v(const char **cursor, vstr *token) {
    if (!cursor || !*cursor || !token) return FMT_TOKEN_END;
    return fmt_scan_v_n(cursor, *cursor + strlen(*cursor), token);
}

fmt_token_t fmt_scan(const char **cursor, const char **token_start, size_t *token_len) {
    vstr token = vstr_from_buf(NULL, 0);
    fmt_token_t type = fmt_scan_v(cursor, &token);
    if (token_start)
        *token_start = token.data;
    if (token_len)
        *token_len = token.len;
    return type;
}
