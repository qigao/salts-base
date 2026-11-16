/**
 * @file datetime_grammar.y
 * @brief Multi-format Date-Time Parser Grammar (Lemon)
 *
 * Supports 48 date/time formats including ISO-8601, RFC-822, NCSA Common Log,
 * and various regional formats. See format table in documentation.
 */

%name DatetimeParse
%token_prefix DATETIME_TOKEN_
%token_type {datetime_token_t}
%default_type {int}

%extra_argument {datetime_parse_ctx_t *ctx}

// Precedence for resolving shift/reduce conflicts:
// Definition order determines precedence (later = higher).
// We want: LOW_PREC < DIGITS < SEPARATORS
// This allows shift to win for time extension (HH:MM:SS) and date extension.
%left LOW_PREC.
%left DIGIT4 DIGIT12.
%left DOT HYPHEN SLASH COLON SPACE PLUS.

%include {
#include "datetime_types.h"
#include <stdio.h>
}

%syntax_error {
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Syntax error");
}

%parse_failure {
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Parse failure");
}

start ::= top.

top ::= date next_after_date.
top ::= time next_after_time.
// DIGIT4 is ambiguous at top level: could be YYYY (date start) or HHMM (compact time).
// Handle it explicitly: if followed by a separator, it's a date; otherwise compact time.
top ::= DIGIT4(V) SLASH DIGIT12(M) SLASH DIGIT12(D) next_after_date. {
    ctx->info.year = V.int_value;
    ctx->info.month = M.int_value;
    ctx->info.day = D.int_value;
}
top ::= DIGIT4(V) HYPHEN DIGIT12(M) HYPHEN DIGIT12(D) next_after_date. {
    ctx->info.year = V.int_value;
    ctx->info.month = M.int_value;
    ctx->info.day = D.int_value;
}
top ::= DIGIT4(HMS) next_after_compact_time. {
    ctx->info.hour = HMS.int_value / 100;
    ctx->info.minute = HMS.int_value % 100;
}

next_after_date ::= .
next_after_date ::= space_or_t time_colon next_after_time.
next_after_date ::= COLON time_colon next_after_time.
next_after_date ::= tz_offset.

next_after_time ::= .
next_after_time ::= tz_offset.
next_after_time ::= SPACE tz_offset.

// Compact time (HHMM) can only be followed by end-of-input or timezone
next_after_compact_time ::= .
next_after_compact_time ::= tz_offset.
next_after_compact_time ::= SPACE tz_offset.

// -----------------------------------------------------------------------------
// Date Formats (00-15)
// -----------------------------------------------------------------------------

date ::= date_full.
date ::= date_compact.
date ::= date_rfc822.
date ::= date_dotted.

// Format 00/01: YYYYMMDD / YYYYDDMM
date_compact ::= DIGIT8(D). {
    ctx->info.year = D.int_value / 10000;
    ctx->info.month = (D.int_value / 100) % 100;
    ctx->info.day = D.int_value % 100;
}

// Formats 04-05: DD/MM/YYYY, MM/DD/YYYY
date_full ::= DIGIT12(D) SLASH DIGIT12(M) SLASH DIGIT4(Y). {
    ctx->info.year = Y.int_value;
    ctx->info.month = M.int_value;
    ctx->info.day = D.int_value;
}

// Formats 08-09: DD-MM-YYYY, MM-DD-YYYY
date_full ::= DIGIT12(D) HYPHEN DIGIT12(M) HYPHEN DIGIT4(Y). {
    ctx->info.year = Y.int_value;
    ctx->info.month = M.int_value;
    ctx->info.day = D.int_value;
}

// Formats 10-11: DD.MM.YYYY, MM.DD.YYYY
date_dotted ::= DIGIT12(D) DOT DIGIT12(M) DOT DIGIT4(Y). {
    ctx->info.year = Y.int_value;
    ctx->info.month = M.int_value;
    ctx->info.day = D.int_value;
}

// Formats 12-15: DD-Mon-YY, DD-Mon-YYYY
date_rfc822 ::= DIGIT12(D) HYPHEN month(M) HYPHEN DIGIT4(Y). {
    ctx->info.year = Y.int_value;
    ctx->info.month = M;
    ctx->info.day = D.int_value;
}

date_rfc822 ::= DIGIT12(D) HYPHEN month(M) HYPHEN DIGIT12(Y). {
    ctx->info.year = (Y.int_value < 70) ? (2000 + Y.int_value) : (1900 + Y.int_value);
    ctx->info.month = M;
    ctx->info.day = D.int_value;
}

// Format 47: IMF-fixdate (Sun, 06 Nov 1994)
date_rfc822 ::= day_name(DW) COMMA SPACE DIGIT12(D) SPACE month(M) SPACE DIGIT4(Y). {
    ctx->info.day_of_week = DW;
    ctx->info.day = D.int_value;
    ctx->info.month = M;
    ctx->info.year = Y.int_value;
}

// Format 46: NCSA (04/Mar/2006)
date_rfc822 ::= DIGIT12(D) SLASH month(M) SLASH DIGIT4(Y). {
    ctx->info.day = D.int_value;
    ctx->info.month = M;
    ctx->info.year = Y.int_value;
}

// -----------------------------------------------------------------------------
// Time Formats (16-24)
// -----------------------------------------------------------------------------

time ::= time_colon.
time ::= time_dot.
time ::= time_space.
time ::= time_compact.

// Formats 16-17: HH:MM:SS, HH:MM:SS.mss (colon-separated)
time_colon ::= DIGIT12(H) COLON DIGIT12(M) COLON DIGIT12(SEC). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
}

time_colon ::= DIGIT12(H) COLON DIGIT12(M) COLON DIGIT12(SEC) DOT DIGIT12(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

time_colon ::= DIGIT12(H) COLON DIGIT12(M) COLON DIGIT12(SEC) DOT DIGIT3(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

// Format 45: HH:MM (without seconds, for ISO8601 DateThh:mmTZD)
time_colon ::= DIGIT12(H) COLON DIGIT12(M). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
}

// Formats 20-21: HH.MM.SS, HH.MM.SS.mss (dot-separated)
time_dot ::= DIGIT12(H) DOT DIGIT12(M) DOT DIGIT12(SEC). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
}

time_dot ::= DIGIT12(H) DOT DIGIT12(M) DOT DIGIT12(SEC) DOT DIGIT12(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

time_dot ::= DIGIT12(H) DOT DIGIT12(M) DOT DIGIT12(SEC) DOT DIGIT3(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

// Formats 18-19: HH MM SS, HH MM SS mss (space-separated, standalone only)
time_space ::= DIGIT12(H) SPACE DIGIT12(M) SPACE DIGIT12(SEC). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
}

time_space ::= DIGIT12(H) SPACE DIGIT12(M) SPACE DIGIT12(SEC) SPACE DIGIT12(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

time_space ::= DIGIT12(H) SPACE DIGIT12(M) SPACE DIGIT12(SEC) SPACE DIGIT3(MS). {
    ctx->info.hour = H.int_value;
    ctx->info.minute = M.int_value;
    ctx->info.second = SEC.int_value;
    ctx->info.millisecond = MS.int_value;
}

// Format 22 (HHMM) is handled at the top level to avoid conflict with DIGIT4 dates.

// Format 23: HHMMSS
time_compact ::= DIGIT6(HMSS). {
    ctx->info.hour = HMSS.int_value / 10000;
    ctx->info.minute = (HMSS.int_value / 100) % 100;
    ctx->info.second = HMSS.int_value % 100;
}

// Format 24: HHMMSSmss
time_compact ::= DIGIT9(HMSSM). {
    ctx->info.hour = HMSSM.int_value / 10000000;
    ctx->info.minute = (HMSSM.int_value / 100000) % 100;
    ctx->info.second = (HMSSM.int_value / 1000) % 100;
    ctx->info.millisecond = HMSSM.int_value % 1000;
}

space_or_t ::= SPACE.
space_or_t ::= T.

// -----------------------------------------------------------------------------
// Timezone Offsets
// -----------------------------------------------------------------------------

tz_offset ::= TZ_CONST. { ctx->info.has_tz = 1; ctx->info.tz_offset = 0; }
tz_offset ::= offset_sign(S) DIGIT4(O). {
    ctx->info.has_tz = 1;
    int h = O.int_value / 100;
    int m = O.int_value % 100;
    ctx->info.tz_offset = S * (h * 60 + m);
}
tz_offset ::= offset_sign(S) DIGIT12(H) COLON DIGIT12(M). {
    ctx->info.has_tz = 1;
    ctx->info.tz_offset = S * (H.int_value * 60 + M.int_value);
}

offset_sign(A) ::= PLUS. { A = 1; }
offset_sign(A) ::= HYPHEN. [LOW_PREC] { A = -1; }

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

day_name(A) ::= SUN. { A = 0; }
day_name(A) ::= MON. { A = 1; }
day_name(A) ::= TUE. { A = 2; }
day_name(A) ::= WED. { A = 3; }
day_name(A) ::= THU. { A = 4; }
day_name(A) ::= FRI. { A = 5; }
day_name(A) ::= SAT. { A = 6; }

month(A) ::= JAN. { A = 1; }
month(A) ::= FEB. { A = 2; }
month(A) ::= MAR. { A = 3; }
month(A) ::= APR. { A = 4; }
month(A) ::= MAY. { A = 5; }
month(A) ::= JUN. { A = 6; }
month(A) ::= JUL. { A = 7; }
month(A) ::= AUG. { A = 8; }
month(A) ::= SEP. { A = 9; }
month(A) ::= OCT. { A = 10; }
month(A) ::= NOV. { A = 11; }
month(A) ::= DEC. { A = 12; }
