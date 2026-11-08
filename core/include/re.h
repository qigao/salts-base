/* Retained original regex public API from utils/include/re.h blob 2b99ec662352677b21aceddfc13dea6d624df15f.
 * Sole native Salts::Core C11 implementation; no CMeta or CFlow runtime.
 */
/*
 * Bounded byte-oriented regular expression engine, part of Salts.
 *
 * Supported syntax: . ^ $ * + ? {m} {m,} {m,n} with lazy variants *? +? ??,
 * character classes, inverted classes, ranges, branches, capturing-style
 * groups ( ) and non-capturing groups (?: ), word boundaries \b \B,
 * positive lookahead (?= ... ) and negative lookahead (?! ... ), and the
 * \s \S \w \W \d \D escapes. Captures are not retained: groups exist for
 * grouping and alternation only.
 *
 * The engine performs no I/O and allocates only through an explicit bounded
 * workspace, so it is safe for native and freestanding (Wasm) guests.
 * Backtracking is bounded by the step/depth/workspace budgets in re_limits_t.
 */

#ifndef SALTS_RE_H
#define SALTS_RE_H

#include "cmeta_api.h"

#include <stddef.h>
#include <stdint.h>

#ifndef RE_DOT_MATCHES_NEWLINE
  #define RE_DOT_MATCHES_NEWLINE 1
#endif

#define RE_DEFAULT_MAX_PATTERN_BYTES 255u
#define RE_DEFAULT_MAX_TEXT_BYTES (1024u * 1024u)
#define RE_DEFAULT_MAX_DEPTH 256u
#define RE_DEFAULT_MAX_WORKSPACE_BYTES (1024u * 1024u)
#define RE_DEFAULT_MAX_STEPS 1000000ull

#ifdef __cplusplus
extern "C" {
#endif

typedef struct regex_t *re_t;

typedef enum re_status {
  RE_STATUS_OK = 0,
  RE_STATUS_NO_MATCH = 1,
  RE_STATUS_INVALID_ARGUMENT = 2,
  RE_STATUS_INVALID_PATTERN = 3,
  RE_STATUS_PATTERN_LIMIT = 4,
  RE_STATUS_TEXT_LIMIT = 5,
  RE_STATUS_DEPTH_LIMIT = 6,
  RE_STATUS_STEP_LIMIT = 7,
  RE_STATUS_WORKSPACE_LIMIT = 8,
  RE_STATUS_NO_MEMORY = 9
} re_status_t;

typedef struct re_limits {
  uint32_t struct_size;
  uint32_t max_pattern_bytes;
  uint32_t max_text_bytes;
  uint32_t max_depth;
  uint32_t max_workspace_bytes;
  uint64_t max_steps;
} re_limits_t;

#define RE_LIMITS_INIT                                                                             \
  {sizeof(re_limits_t),  RE_DEFAULT_MAX_PATTERN_BYTES,   RE_DEFAULT_MAX_TEXT_BYTES,                \
   RE_DEFAULT_MAX_DEPTH, RE_DEFAULT_MAX_WORKSPACE_BYTES, RE_DEFAULT_MAX_STEPS}

typedef struct re_match_result {
  size_t index;
  size_t length;
} re_match_result_t;

#define RE_NPOS ((size_t)-1)

/* Returns the default bounded configuration by value. */
SALTS_API re_limits_t re_limits_default(void);

/* Validates exactly pattern_len bytes. NULL limits selects the defaults. */
SALTS_API re_status_t re_validate_n(const char *pattern, size_t pattern_len,
                                 const re_limits_t *limits);

/*
 * Compiles an owned, immutable pattern. The returned handle is independent of
 * every other handle and may be matched concurrently. The caller owns it.
 */
SALTS_API re_status_t re_compile_n(const char *pattern, size_t pattern_len,
                                const re_limits_t *limits, re_t *out_pattern);
SALTS_API void re_destroy(re_t pattern);

/* Searches exactly text_len bytes using a compiled pattern. */
SALTS_API re_status_t re_matchn(re_t pattern, const char *text, size_t text_len,
                             const re_limits_t *limits,
                             re_match_result_t *out_match);

/* Validates and searches an uncompiled pattern without retaining allocations. */
SALTS_API re_status_t re_match_n(const char *pattern, size_t pattern_len,
                              const char *text, size_t text_len,
                              const re_limits_t *limits,
                              re_match_result_t *out_match);

SALTS_API const char *re_status_string(re_status_t status);

/*
 * NUL-terminated compatibility API. re_compile() returns an owned handle that
 * must be released with re_destroy(). Failure and no-match both map to -1.
 */
SALTS_API re_t re_compile(const char *pattern);
SALTS_API int re_matchp(re_t pattern, const char *text, int *matchlength);
SALTS_API int re_match(const char *pattern, const char *text, int *matchlength);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_RE_H */
