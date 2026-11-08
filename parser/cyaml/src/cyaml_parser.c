#include "cyaml_internal.h"
#include <math.h>

// #region Token Types

typedef enum {
    TOK_NONE = 0,
    TOK_STREAM_START,
    TOK_STREAM_END,
    TOK_DOC_START, // [9.1.2]
    TOK_DOC_END, // [9.1.3]
    TOK_BLOCK_SEQ_START, // [8.2.1]
    TOK_BLOCK_MAP_START, // [8.2.2]
    TOK_BLOCK_END,
    TOK_FLOW_SEQ_START, // [7.4.1]
    TOK_FLOW_SEQ_END,
    TOK_FLOW_MAP_START, // [7.4.2]
    TOK_FLOW_MAP_END,
    TOK_BLOCK_ENTRY, // [8.2.1]
    TOK_FLOW_ENTRY, // [7.4]
    TOK_KEY, // [8.2.2]
    TOK_VALUE, // [8.2.2]
    TOK_ALIAS, // [7.1]
    TOK_ANCHOR, // [7.1]
    TOK_TAG, // [7.2]
    TOK_SCALAR // [7.3, 8.1]
} scan_token_type_t;

typedef struct {
    scan_token_type_t type;
    cyaml_span_t span; //!< Span includes location info (start_line, start_col, etc.)
    cyaml_style_t style;
    cyaml_chomp_t chomp;
    uint8_t indent;
    uint8_t leading_breaks;
    uint8_t trailing_breaks;
    bool tab_sep; //!< Tab separator after doc indicator
} scan_token_t;

// #endregion

// #region Simple Key Tracking

//! [7.4.2] Implicit key detection
typedef struct {
    bool possible;
    bool required;
    size_t token_number;
    size_t mark_index;
    uint32_t mark_line;
    uint32_t mark_col;
} simple_key_t;

// #endregion

// #region Scanner State

typedef enum {
    SCAN_STREAM_START_PRODUCED = 1 << 0,
    SCAN_STREAM_END_PRODUCED = 1 << 1,
    SCAN_SIMPLE_KEY_ALLOWED = 1 << 2,
    SCAN_TOKEN_AVAILABLE = 1 << 3,
    SCAN_JSON_KEY_PENDING = 1 << 4,
    SCAN_DIRECTIVE_SEEN = 1 << 5,
    SCAN_YAML_DIRECTIVE_SEEN = 1 << 6,
    SCAN_CONTENT_SEEN = 1 << 7,
    SCAN_TABS_IN_LINE = 1 << 8,
} scan_flags_t;

#define TOKENS_INIT_CAP 16
#define INDENTS_INIT_CAP 16
#define SIMPLE_KEYS_INIT_CAP 8
#define STATES_INIT_CAP 16
#define STREAM_INIT_CAP 4

typedef enum {
    SYNERR_NOMEM,
    SYNERR_TAB_INDENT,
    SYNERR_COMMENT_WS,
    SYNERR_EXPECTED_COLON,
    SYNERR_FLOW_INDENT,
    SYNERR_DIRECTIVE_AFTER_CONTENT,
    SYNERR_DUP_YAML_DIRECTIVE,
    SYNERR_INVALID_YAML_VERSION,
    SYNERR_EXTRA_DIRECTIVE_CONTENT,
    SYNERR_INVALID_DOC_END,
    SYNERR_BLOCK_SEQ_NOT_ALLOWED,
    SYNERR_MAP_KEY_NOT_ALLOWED,
    SYNERR_MAP_VALUE_NOT_ALLOWED,
    SYNERR_UNTERMINATED_TAG,
    SYNERR_INVALID_BLOCK_HEADER,
    SYNERR_LEADING_SPACES,
    SYNERR_INVALID_ESCAPE,
    SYNERR_MULTILINE_INDENT,
    SYNERR_DOC_MARKER_IN_SCALAR,
    SYNERR_UNTERMINATED_SCALAR,
    SYNERR_INVALID_FLOW_CHAR,
    SYNERR_INVALID_CHAR,
    SYNERR_INVALID_UTF8,
    SYNERR_EXPECTED_STREAM_START,
    SYNERR_DIRECTIVE_WITHOUT_DOC,
    SYNERR_EXPECTED_DOC_START,
    SYNERR_MULTIPLE_ANCHORS,
    SYNERR_MULTIPLE_TAGS,
    SYNERR_UNDEFINED_TAG_HANDLE,
    SYNERR_EXPECTED_NODE,
    SYNERR_EXPECTED_BLOCK_ENTRY,
    SYNERR_EXPECTED_MAP_KEY,
    SYNERR_LEADING_COMMA,
    SYNERR_EMPTY_FLOW_ENTRY,
    SYNERR_EXPECTED_FLOW_SEQ_SEP,
    SYNERR_EXPECTED_FLOW_MAP_SEP,
} synerr_t;

static const char* synerr_msg(synerr_t e)
{
    switch (e) {
    case SYNERR_NOMEM:
        return "Out of memory";
    case SYNERR_TAB_INDENT:
        return "Tab used for indentation";
    case SYNERR_COMMENT_WS:
        return "Comment must be preceded by whitespace";
    case SYNERR_EXPECTED_COLON:
        return "Expected ':'";
    case SYNERR_FLOW_INDENT:
        return "Wrong indentation in flow context";
    case SYNERR_DIRECTIVE_AFTER_CONTENT:
        return "Directive after content requires '...'";
    case SYNERR_DUP_YAML_DIRECTIVE:
        return "Duplicate YAML directive";
    case SYNERR_INVALID_YAML_VERSION:
        return "Invalid YAML version";
    case SYNERR_EXTRA_DIRECTIVE_CONTENT:
        return "Extra content after directive";
    case SYNERR_INVALID_DOC_END:
        return "Invalid content after '...'";
    case SYNERR_BLOCK_SEQ_NOT_ALLOWED:
        return "Block sequence not allowed here";
    case SYNERR_MAP_KEY_NOT_ALLOWED:
        return "Mapping key not allowed here";
    case SYNERR_MAP_VALUE_NOT_ALLOWED:
        return "Mapping value not allowed here";
    case SYNERR_UNTERMINATED_TAG:
        return "Unterminated verbatim tag";
    case SYNERR_INVALID_BLOCK_HEADER:
        return "Invalid block scalar header";
    case SYNERR_LEADING_SPACES:
        return "Leading spaces exceed content indent";
    case SYNERR_INVALID_ESCAPE:
        return "Invalid escape sequence";
    case SYNERR_MULTILINE_INDENT:
        return "Wrong indentation in multiline scalar";
    case SYNERR_DOC_MARKER_IN_SCALAR:
        return "Document marker in quoted scalar";
    case SYNERR_UNTERMINATED_SCALAR:
        return "Unterminated quoted scalar";
    case SYNERR_INVALID_FLOW_CHAR:
        return "Invalid character in flow context";
    case SYNERR_INVALID_CHAR:
        return "Invalid character";
    case SYNERR_INVALID_UTF8:
        return "Invalid UTF-8 sequence";
    case SYNERR_EXPECTED_STREAM_START:
        return "Expected stream start";
    case SYNERR_DIRECTIVE_WITHOUT_DOC:
        return "Directive without document";
    case SYNERR_EXPECTED_DOC_START:
        return "Expected document start";
    case SYNERR_MULTIPLE_ANCHORS:
        return "Node cannot have multiple anchors";
    case SYNERR_MULTIPLE_TAGS:
        return "Node cannot have multiple tags";
    case SYNERR_UNDEFINED_TAG_HANDLE:
        return "Undefined tag handle";
    case SYNERR_EXPECTED_NODE:
        return "Expected node content";
    case SYNERR_EXPECTED_BLOCK_ENTRY:
        return "Expected block sequence entry";
    case SYNERR_EXPECTED_MAP_KEY:
        return "Expected mapping key";
    case SYNERR_LEADING_COMMA:
        return "Leading comma not allowed";
    case SYNERR_EMPTY_FLOW_ENTRY:
        return "Empty entry not allowed";
    case SYNERR_EXPECTED_FLOW_SEQ_SEP:
        return "Expected ',' or ']'";
    case SYNERR_EXPECTED_FLOW_MAP_SEP:
        return "Expected ',' or '}'";
    }
    return "Unknown error";
}

typedef struct {
    const char* src;
    size_t len;
    size_t pos;
    uint32_t line;
    uint32_t col;

    scan_token_t* tokens;
    size_t tok_cap;
    size_t tok_head;
    size_t tok_tail;
    size_t tokens_parsed;

    int* indents;
    size_t indents_cap;
    size_t indent_top;
    int indent;

    simple_key_t* simple_keys;
    size_t simple_keys_cap;
    size_t simple_key_top;

    int flow_level;
    uint32_t flags;
    size_t adjacent_value_allowed_at;

    cyaml_doc_t* doc;
    cyaml_error_t* err;
    cyaml_spec_t spec;
    bool final;
    bool need_more;
} scanner_t;

#define HAS_FLAG(s, f) (((s)->flags & (f)) != 0)
#define SET_FLAG(s, f) ((s)->flags |= (f))
#define CLEAR_FLAG(s, f) ((s)->flags &= (uint32_t)~(f))
#define PEEK(s) (((s)->pos < (s)->len) ? (s)->src[(s)->pos] : C_NUL)
#define PEEK_AT(s, off) (((s)->pos + (off) < (s)->len) ? (s)->src[(s)->pos + (off)] : C_NUL)
#define SKIP(s)                    \
    do {                           \
        if ((s)->pos < (s)->len) { \
            (s)->pos++;            \
            (s)->col++;            \
        }                          \
    } while (0)
#define FREE_SCANNER(s)        \
    do {                       \
        free((s).tokens);      \
        free((s).indents);     \
        free((s).simple_keys); \
    } while (0)
#define FREE_PARSER(p) free((p).states)

// #endregion

// #region Character Helpers

static inline void skip_line(scanner_t* s)
{
    if (PEEK(s) == C_CR) {
        s->pos++;
        if (PEEK(s) == C_LF)
            s->pos++;
    } else if (PEEK(s) == C_LF) {
        s->pos++;
    }
    s->line++;
    s->col = 1;
}

inline static void skip_comment(scanner_t* s)
{
    if (PEEK(s) != C_HASH)
        return;

    if (s->doc && s->doc->comments) {
        size_t start = s->pos;
        uint32_t line = s->line, col = s->col;
        while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s)))
            SKIP(s);

        if (s->doc->comments->count >= s->doc->comments->cap) {
            uint32_t new_cap = s->doc->comments->cap ? s->doc->comments->cap * 2 : 16;
            cyaml_span_t* new_items = realloc(s->doc->comments->items, new_cap * sizeof(cyaml_span_t));
            if (new_items) {
                s->doc->comments->items = new_items;
                s->doc->comments->cap = new_cap;
            }
        }

        if (s->doc->comments->count < s->doc->comments->cap) {
            cyaml_span_t* c = &s->doc->comments->items[s->doc->comments->count++];
            c->off = (uint32_t)start;
            c->len = (uint32_t)(s->pos - start);
            c->start_line = c->end_line = line;
            c->start_col = col;
            c->end_col = s->col;
        }
    } else {
        while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s)))
            SKIP(s);
    }
}

// #endregion

// #region Error Handling

static void set_synerr(scanner_t* s, synerr_t err)
{
    if (!s->final && s->pos >= s->len && err != SYNERR_NOMEM) {
        s->need_more = true;
        return;
    }
    if (s->err) {
        s->err->code = (err == SYNERR_NOMEM) ? CYAML_ERR_NOMEM : CYAML_ERR_SYNTAX;
        s->err->span.start_line = s->line;
        s->err->span.start_col = s->col;
        snprintf(s->err->msg, sizeof(s->err->msg), "%s", synerr_msg(err));
    }
}

//! Skip one UTF-8 codepoint, validating it
//! Returns false and sets error on invalid UTF-8
static inline bool skip_utf8(scanner_t* s)
{
    cyaml_cp_t cp;
    int width = cyaml_utf8_len((unsigned char)s->src[s->pos]);
    if (width == 0) {
        set_synerr(s, SYNERR_INVALID_UTF8);
        return false;
    }
    if (!s->final && (size_t)width > s->len - s->pos) {
        s->need_more = true;
        return false;
    }
    int consumed = cyaml_utf8_decode(s->src + s->pos, s->len - s->pos, &cp);
    if (consumed <= 0 || cp == CYAML_CP_INVALID) {
        set_synerr(s, SYNERR_INVALID_UTF8);
        return false;
    }
    s->pos += (size_t)consumed;
    s->col += (uint32_t)consumed; // Approximate - multibyte chars still count as 1 column visually
    return true;
}

// #endregion

// #region Token Queue

static inline size_t queue_count(const scanner_t* s)
{
    if (s->tok_tail >= s->tok_head)
        return s->tok_tail - s->tok_head;
    return s->tok_cap - s->tok_head + s->tok_tail;
}

static bool grow_tokens(scanner_t* s)
{
    size_t new_cap = s->tok_cap ? s->tok_cap * 2 : TOKENS_INIT_CAP;
    scan_token_t* new_tokens = malloc(new_cap * sizeof(scan_token_t));
    if (!new_tokens) {
        set_synerr(s, SYNERR_NOMEM);
        return false;
    }

    size_t count = queue_count(s);
    for (size_t i = 0; i < count; i++) {
        new_tokens[i] = s->tokens[(s->tok_head + i) % s->tok_cap];
    }

    free(s->tokens);
    s->tokens = new_tokens;
    s->tok_cap = new_cap;
    s->tok_head = 0;
    s->tok_tail = count;
    return true;
}

static bool enqueue_token(scanner_t* s, scan_token_t tok)
{
    size_t count = queue_count(s);
    if (s->tok_cap == 0 || count >= s->tok_cap - 1) {
        if (!grow_tokens(s))
            return false;
    }
    s->tokens[s->tok_tail] = tok;
    s->tok_tail = (s->tok_tail + 1) % s->tok_cap;
    return true;
}

static bool insert_token(scanner_t* s, size_t token_number, scan_token_t tok)
{
    size_t count = queue_count(s);
    if (s->tok_cap == 0 || count >= s->tok_cap - 1) {
        if (!grow_tokens(s))
            return false;
    }

    size_t insert_idx = token_number - s->tokens_parsed;
    if (insert_idx >= count) {
        return enqueue_token(s, tok);
    }

    s->tok_tail = (s->tok_tail + 1) % s->tok_cap;

    for (size_t i = count; i > insert_idx; i--) {
        size_t dst = (s->tok_head + i) % s->tok_cap;
        size_t src_idx = (s->tok_head + i - 1) % s->tok_cap;
        s->tokens[dst] = s->tokens[src_idx];
    }

    size_t pos = (s->tok_head + insert_idx) % s->tok_cap;
    s->tokens[pos] = tok;
    return true;
}

static scan_token_t* queue_head(scanner_t* s)
{
    if (queue_count(s) == 0)
        return NULL;
    return &s->tokens[s->tok_head];
}

static void skip_token(scanner_t* s)
{
    if (queue_count(s) > 0) {
        scan_token_t* tok = &s->tokens[s->tok_head];
        if (tok->type == TOK_STREAM_END) {
            SET_FLAG(s, SCAN_STREAM_END_PRODUCED);
        }
        s->tok_head = (s->tok_head + 1) % s->tok_cap;
        s->tokens_parsed++;
        CLEAR_FLAG(s, SCAN_TOKEN_AVAILABLE);
    }
}

// #endregion

// #region Simple Key Management [7.4.2, 8.2.2]

//! [7.4.2] Simple keys must be on single line in block context
static bool stale_simple_keys(scanner_t* s)
{
    for (size_t i = 0; i < s->simple_key_top; i++) {
        simple_key_t* sk = &s->simple_keys[i];
        if (sk->possible) {
            bool stale = (sk->mark_line < s->line);
            if (stale) {
                if (sk->required) {
                    set_synerr(s, SYNERR_EXPECTED_COLON);
                    return false;
                }
                sk->possible = false;
            }
        }
    }
    return true;
}

static bool grow_simple_keys(scanner_t* s)
{
    size_t new_cap = s->simple_keys_cap ? s->simple_keys_cap * 2 : SIMPLE_KEYS_INIT_CAP;
    simple_key_t* new_keys = realloc(s->simple_keys, new_cap * sizeof(simple_key_t));
    if (!new_keys) {
        set_synerr(s, SYNERR_NOMEM);
        return false;
    }
    s->simple_keys = new_keys;
    s->simple_keys_cap = new_cap;
    return true;
}

static bool save_simple_key(scanner_t* s)
{
    bool required = (!s->flow_level && s->indent == (int)(s->col - 1));

    if (HAS_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED)) {
        simple_key_t sk = {
            .possible = true,
            .required = required,
            .token_number = s->tokens_parsed + queue_count(s),
            .mark_index = s->pos,
            .mark_line = s->line,
            .mark_col = s->col
        };

        if (s->simple_key_top > 0) {
            simple_key_t* old = &s->simple_keys[s->simple_key_top - 1];
            if (old->possible && old->required) {
                set_synerr(s, SYNERR_EXPECTED_COLON);
                return false;
            }
            *old = sk;
        }
    }
    return true;
}

static bool remove_simple_key(scanner_t* s)
{
    if (s->simple_key_top > 0) {
        simple_key_t* sk = &s->simple_keys[s->simple_key_top - 1];
        if (sk->possible && sk->required) {
            set_synerr(s, SYNERR_EXPECTED_COLON);
            return false;
        }
        sk->possible = false;
    }
    return true;
}

// #endregion

// #region Flow Level Management [7.4]

static bool increase_flow_level(scanner_t* s)
{
    if (s->simple_key_top >= s->simple_keys_cap) {
        if (!grow_simple_keys(s))
            return false;
    }
    s->simple_keys[s->simple_key_top++] = (simple_key_t) { 0 };
    s->flow_level++;
    return true;
}

static void decrease_flow_level(scanner_t* s)
{
    if (s->flow_level > 0) {
        s->flow_level--;
        if (s->simple_key_top > 0)
            s->simple_key_top--;
    }
}

// #endregion

// #region Indentation Management [8.1.1]

static bool grow_indents(scanner_t* s)
{
    size_t new_cap = s->indents_cap ? s->indents_cap * 2 : INDENTS_INIT_CAP;
    int* new_indents = realloc(s->indents, new_cap * sizeof(int));
    if (!new_indents) {
        set_synerr(s, SYNERR_NOMEM);
        return false;
    }
    s->indents = new_indents;
    s->indents_cap = new_cap;
    return true;
}

static bool roll_indent(scanner_t* s, int column, size_t number,
    scan_token_type_t type, uint32_t line, uint32_t col)
{
    if (s->flow_level)
        return true;

    if (s->indent < column) {
        if (s->indent_top >= s->indents_cap) {
            if (!grow_indents(s))
                return false;
        }
        s->indents[s->indent_top++] = s->indent;
        s->indent = column;

        scan_token_t tok = { .type = type, .span = { .off = (uint32_t)s->pos, .len = 0, .start_line = line, .start_col = col, .end_line = line, .end_col = col } };
        if (number == (size_t)-1) {
            return enqueue_token(s, tok);
        } else {
            return insert_token(s, number, tok);
        }
    }
    return true;
}

static bool unroll_indent(scanner_t* s, int column)
{
    if (s->flow_level)
        return true;

    while (s->indent > column) {
        scan_token_t tok = { .type = TOK_BLOCK_END, .span = { .off = (uint32_t)s->pos, .len = 0, .start_line = s->line, .start_col = s->col, .end_line = s->line, .end_col = s->col } };
        if (!enqueue_token(s, tok))
            return false;
        if (s->indent_top > 0) {
            s->indent = s->indents[--s->indent_top];
        } else {
            s->indent = -1;
        }
    }
    return true;
}

// #endregion

// #region Skip Whitespace [6.3, 6.4, 6.5]

//! [6.2] Tabs are not allowed as indentation
static bool check_tab_indent(scanner_t* s)
{
    if (PEEK(s) == C_TAB && s->indent >= 0) {
        size_t j = 0;
        while (PEEK_AT(s, j) == C_TAB || PEEK_AT(s, j) == C_SP)
            j++;
        if (PEEK_AT(s, j) && !CYAML_IS_BREAK(PEEK_AT(s, j))) {
            set_synerr(s, SYNERR_TAB_INDENT);
            return false;
        }
    }
    return true;
}

static bool scan_to_next_token(scanner_t* s)
{
    while (true) {
        while (CYAML_IS_WHITE(PEEK(s))) {
            if (PEEK(s) == C_TAB)
                SET_FLAG(s, SCAN_TABS_IN_LINE);
            SKIP(s);
        }

        skip_comment(s);

        if (CYAML_IS_BREAK(PEEK(s))) {
            skip_line(s);
            if (!s->flow_level)
                SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
            CLEAR_FLAG(s, SCAN_TABS_IN_LINE);

            if (!check_tab_indent(s))
                return false;

            if (s->flow_level) {
                while (CYAML_IS_WHITE(PEEK(s)))
                    SKIP(s);
                if (PEEK(s) && !CYAML_IS_BREAK(PEEK(s)) && (int)(s->col - 1) <= s->indent) {
                    set_synerr(s, SYNERR_FLOW_INDENT);
                    return false;
                }
            }
        } else {
            break;
        }
    }
    return true;
}

// #endregion

// #region Document Markers [9.1]

//! [9.1.2] c-directives-end: '---'
static bool is_doc_start(scanner_t* s)
{
    return s->col == 1 && s->pos + 2 < s->len && s->src[s->pos] == '-' && s->src[s->pos + 1] == '-' && s->src[s->pos + 2] == '-' && CYAML_IS_BLANKZ(PEEK_AT(s, 3));
}

//! [9.1.3] c-document-end: '...'
static bool is_doc_end(scanner_t* s)
{
    return s->col == 1 && s->pos + 2 < s->len && s->src[s->pos] == '.' && s->src[s->pos + 1] == '.' && s->src[s->pos + 2] == '.' && CYAML_IS_BLANKZ(PEEK_AT(s, 3));
}

// #endregion

// #region Token Fetchers [5.2, 7, 8]

static bool fetch_stream_start(scanner_t* s)
{
    s->indent = -1;
    SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    if (s->simple_key_top >= s->simple_keys_cap) {
        if (!grow_simple_keys(s))
            return false;
    }
    s->simple_keys[s->simple_key_top++] = (simple_key_t) { 0 };
    SET_FLAG(s, SCAN_STREAM_START_PRODUCED);

    scan_token_t tok = { .type = TOK_STREAM_START, .span = { .off = (uint32_t)s->pos, .len = 0, .start_line = s->line, .start_col = s->col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_stream_end(scanner_t* s)
{
    if (s->col != 1) {
        s->col = 1;
        s->line++;
    }
    if (!unroll_indent(s, -1))
        return false;
    if (!remove_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);

    scan_token_t tok = { .type = TOK_STREAM_END, .span = { .off = (uint32_t)s->pos, .len = 0, .start_line = s->line, .start_col = s->col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

//! [6.8] Directives
static bool fetch_directive(scanner_t* s)
{
    if (HAS_FLAG(s, SCAN_CONTENT_SEEN)) {
        set_synerr(s, SYNERR_DIRECTIVE_AFTER_CONTENT);
        return false;
    }
    if (!unroll_indent(s, -1))
        return false;
    if (!remove_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_DIRECTIVE_SEEN);
    if (s->doc)
        s->doc->flags |= CYAML_HAS_DIRECTIVE;

    SKIP(s);

    size_t name_start = s->pos;
    while (PEEK(s) && !CYAML_IS_BLANKZ(PEEK(s)))
        SKIP(s);
    if (!s->final && PEEK(s) == C_NUL) {
        s->need_more = true;
        return false;
    }
    size_t name_len = s->pos - name_start;

    // [6.8.1] %YAML directive
    if (name_len == 4 && s->src[name_start] == 'Y' && s->src[name_start + 1] == 'A' && s->src[name_start + 2] == 'M' && s->src[name_start + 3] == 'L') {
        if (HAS_FLAG(s, SCAN_YAML_DIRECTIVE_SEEN)) {
            set_synerr(s, SYNERR_DUP_YAML_DIRECTIVE);
            return false;
        }
        SET_FLAG(s, SCAN_YAML_DIRECTIVE_SEEN);

        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);

        // Parse version (e.g., "1.2") - must be valid major.minor
        uint32_t major = 0, minor = 0;
        int major_digits = 0, minor_digits = 0;

        while (CYAML_IS_DIGIT(PEEK(s))) {
            major = major * 10 + (uint32_t)(PEEK(s) - '0');
            major_digits++;
            SKIP(s);
        }

        if (PEEK(s) != '.' || major_digits == 0) {
            set_synerr(s, SYNERR_INVALID_YAML_VERSION);
            return false;
        }
        SKIP(s); // skip '.'

        while (CYAML_IS_DIGIT(PEEK(s))) {
            minor = minor * 10 + (uint32_t)(PEEK(s) - '0');
            minor_digits++;
            SKIP(s);
        }

        if (minor_digits == 0 || major > 255 || minor > 255) {
            set_synerr(s, SYNERR_INVALID_YAML_VERSION);
            return false;
        }

        // Store version in document
        if (s->doc) {
            s->doc->version.major = (uint8_t)major;
            s->doc->version.minor = (uint8_t)minor;
        }

        if (PEEK(s) == C_HASH) {
            set_synerr(s, SYNERR_COMMENT_WS);
            return false;
        }

        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);
        skip_comment(s);
        if (PEEK(s) && !CYAML_IS_BREAK(PEEK(s))) {
            set_synerr(s, SYNERR_EXTRA_DIRECTIVE_CONTENT);
            return false;
        }
    }
    // [6.8.2] %TAG directive
    else if (name_len == 3 && s->src[name_start] == 'T' && s->src[name_start + 1] == 'A' && s->src[name_start + 2] == 'G') {

        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);

        size_t handle_start = s->pos;
        while (PEEK(s) && !CYAML_IS_BLANKZ(PEEK(s)))
            SKIP(s);
        size_t handle_len = s->pos - handle_start;

        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);

        size_t prefix_start = s->pos;
        while (PEEK(s) && !CYAML_IS_BLANKZ(PEEK(s)))
            SKIP(s);
        size_t prefix_len = s->pos - prefix_start;

        if (s->doc && s->doc->tag_count < CYAML_MAX_TAG_DIRECTIVES) {
            cyaml_tag_directive_t* tag = &s->doc->tags[s->doc->tag_count++];
            tag->handle = (cyaml_span_t) { .off = (uint32_t)handle_start, .len = (uint32_t)handle_len };
            tag->prefix = (cyaml_span_t) { .off = (uint32_t)prefix_start, .len = (uint32_t)prefix_len };
        }

        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);
        skip_comment(s);
        if (PEEK(s) && !CYAML_IS_BREAK(PEEK(s))) {
            set_synerr(s, SYNERR_EXTRA_DIRECTIVE_CONTENT);
            return false;
        }
    }

    while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s)))
        SKIP(s);
    if (!s->final && PEEK(s) == C_NUL) {
        s->need_more = true;
        return false;
    }
    if (CYAML_IS_BREAK(PEEK(s)))
        skip_line(s);
    return true;
}

static bool fetch_doc_indicator(scanner_t* s, scan_token_type_t type)
{
    if (!unroll_indent(s, -1))
        return false;
    if (!remove_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);

    uint32_t line = s->line, col = s->col;
    SKIP(s);
    SKIP(s);
    SKIP(s);

    // Check for tab separator after --- (needed for canonical dump)
    bool tab_sep = (type == TOK_DOC_START && PEEK(s) == C_TAB);

    if (type == TOK_DOC_END) {
        CLEAR_FLAG(s, SCAN_CONTENT_SEEN | SCAN_YAML_DIRECTIVE_SEEN);
        while (CYAML_IS_WHITE(PEEK(s)))
            SKIP(s);
        skip_comment(s);
        if (PEEK(s) && !CYAML_IS_BREAK(PEEK(s)) && PEEK(s) != C_NUL) {
            set_synerr(s, SYNERR_INVALID_DOC_END);
            return false;
        }
    } else {
        SET_FLAG(s, SCAN_CONTENT_SEEN);
    }

    scan_token_t tok = { .type = type, .span = { .off = (uint32_t)(s->pos - 3), .len = 3, // --- or ...
                                           .start_line = line,
                                           .start_col = col,
                                           .end_line = s->line,
                                           .end_col = s->col },
        .tab_sep = tab_sep };
    return enqueue_token(s, tok);
}

static bool fetch_flow_collection_start(scanner_t* s, scan_token_type_t type)
{
    if (!save_simple_key(s))
        return false;
    if (!increase_flow_level(s))
        return false;
    SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED | SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    scan_token_t tok = { .type = type, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_flow_collection_end(scanner_t* s, scan_token_type_t type)
{
    if (!remove_simple_key(s))
        return false;
    decrease_flow_level(s);
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    if (PEEK(s) == C_HASH) {
        set_synerr(s, SYNERR_COMMENT_WS);
        return false;
    }

    s->adjacent_value_allowed_at = s->pos;
    if (s->flow_level)
        SET_FLAG(s, SCAN_JSON_KEY_PENDING);

    scan_token_t tok = { .type = type, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_flow_entry(scanner_t* s)
{
    if (!remove_simple_key(s))
        return false;
    SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    CLEAR_FLAG(s, SCAN_JSON_KEY_PENDING);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    // # without preceding whitespace is invalid (not a comment)
    if (PEEK(s) == C_HASH) {
        set_synerr(s, SYNERR_COMMENT_WS);
        return false;
    }

    scan_token_t tok = { .type = TOK_FLOW_ENTRY, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

//! [6.2] Check for invalid tab before nested block structure
static inline bool check_tabs_before_block(scanner_t* s, int col)
{
    if (HAS_FLAG(s, SCAN_TABS_IN_LINE) && col > s->indent) {
        set_synerr(s, SYNERR_TAB_INDENT);
        return false;
    }
    return true;
}

static bool fetch_block_entry(scanner_t* s)
{
    if (!s->flow_level) {
        if (!HAS_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED)) {
            set_synerr(s, SYNERR_BLOCK_SEQ_NOT_ALLOWED);
            return false;
        }
        if (!check_tabs_before_block(s, (int)(s->col - 1)))
            return false;
        if (!roll_indent(s, (int)(s->col - 1), (size_t)-1,
                TOK_BLOCK_SEQ_START, s->line, s->col))
            return false;
    }
    if (!remove_simple_key(s))
        return false;
    SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED | SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    scan_token_t tok = { .type = TOK_BLOCK_ENTRY, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_key(scanner_t* s)
{
    if (!s->flow_level) {
        if (!HAS_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED)) {
            set_synerr(s, SYNERR_MAP_KEY_NOT_ALLOWED);
            return false;
        }
        if (!check_tabs_before_block(s, (int)(s->col - 1)))
            return false;
        if (!roll_indent(s, (int)(s->col - 1), (size_t)-1,
                TOK_BLOCK_MAP_START, s->line, s->col))
            return false;
    }
    if (!remove_simple_key(s))
        return false;
    if (s->flow_level)
        CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    else
        SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    scan_token_t tok = { .type = TOK_KEY, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_value(scanner_t* s)
{
    CLEAR_FLAG(s, SCAN_JSON_KEY_PENDING);

    if (s->simple_key_top > 0) {
        simple_key_t* sk = &s->simple_keys[s->simple_key_top - 1];
        if (sk->possible) {
            scan_token_t key_tok = { .type = TOK_KEY, .span = { .off = (uint32_t)sk->mark_index, .len = 0, .start_line = sk->mark_line, .start_col = sk->mark_col, .end_line = sk->mark_line, .end_col = sk->mark_col } };
            if (!insert_token(s, sk->token_number, key_tok))
                return false;

            if (!s->flow_level) {
                if (!check_tabs_before_block(s, (int)(sk->mark_col - 1)))
                    return false;
                if (!roll_indent(s, (int)(sk->mark_col - 1), sk->token_number,
                        TOK_BLOCK_MAP_START, sk->mark_line, sk->mark_col))
                    return false;
            }

            sk->possible = false;
            CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);

            uint32_t line = s->line, col = s->col;
            size_t start = s->pos;
            SKIP(s);

            scan_token_t tok = { .type = TOK_VALUE, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
            return enqueue_token(s, tok);
        }
    }

    bool need_implicit_key = false;
    if (!s->flow_level) {
        if (!HAS_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED)) {
            set_synerr(s, SYNERR_MAP_VALUE_NOT_ALLOWED);
            return false;
        }
        int target_col = (int)(s->col - 1);
        if (!check_tabs_before_block(s, target_col))
            return false;
        need_implicit_key = (s->indent < target_col);
        if (!roll_indent(s, target_col, (size_t)-1,
                TOK_BLOCK_MAP_START, s->line, s->col))
            return false;
    } else if (HAS_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED)) {
        need_implicit_key = true;
    }

    if (s->flow_level)
        CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    else
        SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    if (need_implicit_key) {
        scan_token_t key_tok = { .type = TOK_KEY, .span = { .off = (uint32_t)start, .len = 0, .start_line = line, .start_col = col, .end_line = line, .end_col = col } };
        if (!enqueue_token(s, key_tok))
            return false;
    }

    scan_token_t tok = { .type = TOK_VALUE, .span = { .off = (uint32_t)start, .len = 1, .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col } };
    return enqueue_token(s, tok);
}

static bool fetch_anchor(scanner_t* s, scan_token_type_t type)
{
    if (!save_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos + 1;
    SKIP(s);

    while (PEEK(s) && !CYAML_IS_BLANKZ(PEEK(s)) && !CYAML_IS_FLOW(PEEK(s))) {
        SKIP(s);
    }
    if (!s->final && PEEK(s) == C_NUL) {
        s->need_more = true;
        return false;
    }

    scan_token_t tok = {
        .type = type,
        .span = { .off = (uint32_t)start, .len = (uint32_t)(s->pos - start), .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col }
    };
    return enqueue_token(s, tok);
}

static bool fetch_tag(scanner_t* s)
{
    if (!save_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    if (PEEK(s) == '<') {
        SKIP(s);
        while (PEEK(s) && PEEK(s) != '>') {
            SKIP(s);
        }
        if (PEEK(s) == '>') {
            SKIP(s);
        } else {
            set_synerr(s, SYNERR_UNTERMINATED_TAG);
            return false;
        }
    } else {
        while (PEEK(s) && !CYAML_IS_BLANKZ(PEEK(s)) && !CYAML_IS_FLOW(PEEK(s))) {
            SKIP(s);
        }
        if (!s->final && PEEK(s) == C_NUL) {
            s->need_more = true;
            return false;
        }
    }

    scan_token_t tok = {
        .type = TOK_TAG,
        .span = { .off = (uint32_t)start, .len = (uint32_t)(s->pos - start), .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col }
    };
    return enqueue_token(s, tok);
}

//! [8.1] Block scalars
static bool fetch_block_scalar(scanner_t* s, bool literal)
{
    if (!remove_simple_key(s))
        return false;
    SET_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED | SCAN_CONTENT_SEEN);

    uint32_t start_line = s->line, start_col = s->col;
    SKIP(s); // Skip '|' or '>'

    cyaml_chomp_t chomp = CYAML_CLIP;
    int explicit_indent = 0;

    //< Parse header: [-+]?[1-9]? or [1-9]?[-+]?
    bool seen_blank = false;
    while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s))) {
        char c = PEEK(s);
        if (c == '-') {
            chomp = CYAML_STRIP;
            SKIP(s);
        } else if (c == '+') {
            chomp = CYAML_KEEP;
            SKIP(s);
        } else if (c >= '1' && c <= '9') {
            explicit_indent = c - '0';
            SKIP(s);
        } else if (CYAML_IS_WHITE(c)) {
            seen_blank = true;
            SKIP(s);
        } else if (c == C_HASH) {
            if (!seen_blank) {
                set_synerr(s, SYNERR_COMMENT_WS);
                return false;
            }
            skip_comment(s);
            break;
        } else
            break;
    }

    if (!CYAML_IS_BREAK(PEEK(s)) && PEEK(s) != C_NUL) {
        set_synerr(s, SYNERR_INVALID_BLOCK_HEADER);
        return false;
    }
    if (CYAML_IS_BREAK(PEEK(s)))
        skip_line(s);

    int block_indent = s->indent + 1;
    int max_empty_spaces = 0; // Track max spaces on empty lines for validation
    bool found_content = false; // Whether we found a content line to set indent
    if (explicit_indent > 0) {
        block_indent = s->indent + explicit_indent;
        found_content = true; // Explicit indent counts as having a defined indent
    } else {
        // Auto-detect indent from first non-empty line
        size_t save_pos = s->pos;
        uint32_t save_line = s->line, save_col = s->col;

        while (PEEK(s)) {
            int spaces = 0;
            while (PEEK(s) == C_SP) {
                SKIP(s);
                spaces++;
            }
            if (CYAML_IS_BREAK(PEEK(s))) {
                // Track max spaces on empty lines
                if (spaces > max_empty_spaces)
                    max_empty_spaces = spaces;
                skip_line(s);
                continue;
            }
            if (PEEK(s) == C_NUL)
                break;
            if (spaces > s->indent) {
                block_indent = spaces;
                found_content = true;
            }
            break;
        }

        s->pos = save_pos;
        s->line = save_line;
        s->col = save_col;

        // Error if empty lines had more spaces than detected indent
        // Only check if we found content - otherwise there's no defined indent to exceed
        if (found_content && max_empty_spaces > block_indent) {
            set_synerr(s, SYNERR_LEADING_SPACES);
            return false;
        }
    }

    // Scan content lines, tracking trailing breaks
    // Content span includes all content lines with their newlines
    // trailing_breaks only counts empty lines AFTER all content lines
    // leading_breaks counts empty lines BEFORE first content (output separately)
    size_t content_start = 0;
    size_t content_end = 0;
    bool first_line = true;
    int leading_breaks = 0; // Count of empty lines before first content
    int trailing_breaks = 0; // Count of empty lines after last content

    while (PEEK(s)) {
        // Skip only the indent spaces (up to block_indent)
        int indent_spaces = 0;
        while (PEEK(s) == C_SP && indent_spaces < block_indent) {
            SKIP(s);
            indent_spaces++;
        }

        // Check what we have after the indent
        if (indent_spaces < block_indent) {
            // Didn't get enough indent spaces
            if (CYAML_IS_BREAK(PEEK(s)) || PEEK(s) == C_NUL) {
                // True empty line (less than block_indent spaces before break/EOF)
                // This is still part of block scalar
                if (first_line) {
                    leading_breaks++;
                } else {
                    trailing_breaks++;
                }
                if (CYAML_IS_BREAK(PEEK(s)))
                    skip_line(s);
                continue;
            }
            // Tab in place of required indent is invalid
            if (PEEK(s) == C_TAB) {
                set_synerr(s, SYNERR_TAB_INDENT);
                return false;
            }
            // Check for document markers at column 0 - these end the block scalar
            if (indent_spaces == 0 && (is_doc_start(s) || is_doc_end(s))) {
                break;
            }
            // Non-empty line with less indent - end of block scalar
            s->pos -= (size_t)indent_spaces;
            s->col -= (uint32_t)indent_spaces;
            break;
        }

        // Check for document markers at column 0 - these always end the block scalar
        if (s->col == 1 && (is_doc_start(s) || is_doc_end(s))) {
            // Back up to start of line
            s->pos -= (size_t)indent_spaces;
            s->col -= (uint32_t)indent_spaces;
            break;
        }

        // We've skipped exactly block_indent spaces
        // Now check if there's content (could be more spaces, or regular chars, or break)
        // A line with only whitespace after indent is still an "empty" line per YAML spec
        if (CYAML_IS_BREAK(PEEK(s))) {
            // Empty line (exactly block_indent indent, no content)
            if (first_line) {
                leading_breaks++;
            } else {
                trailing_breaks++;
            }
            skip_line(s);
            continue;
        }

        if (PEEK(s) == C_NUL)
            break;

        // Scan rest of line, checking for non-space content (tabs ARE content)
        size_t line_start = s->pos;
        bool has_content = false;
        while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s))) {
            char c = PEEK(s);
            if (c != C_SP)
                has_content = true;
            // ASCII characters can use fast SKIP, multibyte needs UTF-8 validation
            if (CYAML_IS_ASCII(c)) {
                SKIP(s);
            } else {
                if (!skip_utf8(s))
                    return false;
            }
        }

        // Space-only lines before first content are empty (leading breaks)
        // But lines with tabs or other chars after indent are content
        if (!has_content && first_line) {
            leading_breaks++;
            if (CYAML_IS_BREAK(PEEK(s)))
                skip_line(s);
            continue;
        }

        // Content line found
        // Any previously counted "trailing" breaks were actually internal
        trailing_breaks = 0;

        if (first_line) {
            // content_start includes the block_indent spaces (for raw span storage)
            content_start = line_start - (size_t)block_indent;
            first_line = false;
        }
        content_end = s->pos;

        if (CYAML_IS_BREAK(PEEK(s))) {
            trailing_breaks = 1;
            skip_line(s);
        } else if (PEEK(s) == C_NUL) {
            // EOF after content - treat as trailing break for clip/keep chomping
            trailing_breaks = 1;
        }
    }

    if (!s->final && PEEK(s) == C_NUL) {
        s->need_more = true;
        return false;
    }

    // For empty scalars (no content found), blank lines are trailing, not leading
    // This affects chomping: strip/clip remove them, keep preserves them
    if (first_line) {
        trailing_breaks = leading_breaks;
        leading_breaks = 0;
    }

    // Content span now includes all content with internal newlines
    // trailing_breaks counts only empty lines AFTER the last content line

    scan_token_t tok = {
        .type = TOK_SCALAR,
        .span = { .off = (uint32_t)content_start, .len = (uint32_t)(content_end - content_start), .start_line = start_line, .start_col = start_col, .end_line = s->line, .end_col = s->col },
        .style = literal ? CYAML_LITERAL : CYAML_FOLDED,
        .chomp = chomp,
        .indent = (uint8_t)block_indent,
        .leading_breaks = (uint8_t)leading_breaks,
        .trailing_breaks = (uint8_t)trailing_breaks
    };
    return enqueue_token(s, tok);
}

//! [7.3] Flow scalars
static bool fetch_flow_scalar(scanner_t* s, bool single)
{
    if (!save_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_CONTENT_SEEN);

    char quote = single ? '\'' : '"';
    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    SKIP(s);

    while (PEEK(s)) {
        char c = PEEK(s);
        if (c == quote) {
            // Check for escaped quote in single-quoted strings
            if (single && PEEK_AT(s, 1) == '\'') {
                SKIP(s);
                SKIP(s); //! Skip both quotes
                continue;
            }
            break; //! End of quoted scalar
        } else if (c == C_BSLASH && !single) {
            SKIP(s);
            char esc = PEEK(s);
            // Validate escape sequence per YAML spec
            if (esc == '0' || esc == 'a' || esc == 'b' || esc == 't' || esc == 'n' || esc == 'v' || esc == 'f' || esc == 'r' || esc == 'e' || esc == C_SP || esc == '"' || esc == '/' || esc == C_BSLASH || esc == 'N' || esc == '_' || esc == 'L' || esc == 'P' || esc == C_TAB) {
                SKIP(s);
            } else if (esc == 'x') {
                SKIP(s); //! \xNN
                if (PEEK(s))
                    SKIP(s);
                if (PEEK(s))
                    SKIP(s);
            } else if (esc == 'u') {
                SKIP(s); //! \uNNNN
                for (int i = 0; i < 4 && PEEK(s); i++)
                    SKIP(s);
            } else if (esc == 'U') {
                SKIP(s); //! \UNNNNNNNN
                for (int i = 0; i < 8 && PEEK(s); i++)
                    SKIP(s);
            } else if (CYAML_IS_BREAK(esc)) {
                skip_line(s); //! Line continuation
            } else {
                set_synerr(s, SYNERR_INVALID_ESCAPE);
                return false;
            }
        } else if (CYAML_IS_BREAK(c)) {
            skip_line(s);
            // Check indentation - count spaces first
            size_t i = 0;
            while (PEEK_AT(s, i) == C_SP)
                i++; //! Only spaces for indentation
            int next_col = (int)(s->col - 1 + i);
            char next = PEEK_AT(s, i);
            // If not enough indentation...
            if (next_col <= s->indent && next && !CYAML_IS_BREAK(next) && next != quote) {
                // Tab in place of required indentation spaces is invalid
                if (next == C_TAB) {
                    set_synerr(s, SYNERR_TAB_INDENT);
                    return false;
                }
                set_synerr(s, SYNERR_MULTILINE_INDENT);
                return false;
            }
            // Check for document marker at column 0 inside quoted string
            if (s->col == 1 && (is_doc_start(s) || is_doc_end(s))) {
                set_synerr(s, SYNERR_DOC_MARKER_IN_SCALAR);
                return false;
            }
        } else {
            if (!skip_utf8(s))
                return false;
        }
    }

    if (PEEK(s) != quote) {
        set_synerr(s, SYNERR_UNTERMINATED_SCALAR);
        return false;
    }
    SKIP(s);

    // # without preceding whitespace is invalid (not a comment)
    if (PEEK(s) == C_HASH) {
        set_synerr(s, SYNERR_COMMENT_WS);
        return false;
    }

    s->adjacent_value_allowed_at = s->pos;
    if (s->flow_level)
        SET_FLAG(s, SCAN_JSON_KEY_PENDING);

    scan_token_t tok = {
        .type = TOK_SCALAR,
        .span = { .off = (uint32_t)(start + 1), .len = (uint32_t)(s->pos - start - 2), .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col },
        .style = single ? CYAML_SINGLE : CYAML_DOUBLE
    };
    return enqueue_token(s, tok);
}

//! [7.3.3] Plain scalars
static bool fetch_plain_scalar(scanner_t* s)
{
    if (!save_simple_key(s))
        return false;
    CLEAR_FLAG(s, SCAN_SIMPLE_KEY_ALLOWED);
    SET_FLAG(s, SCAN_CONTENT_SEEN);

    uint32_t line = s->line, col = s->col;
    size_t start = s->pos;
    size_t end = s->pos;
    // Use block indent level, not scalar start position
    // Continuation lines must be indented more than the current block level
    int base_indent = s->indent;

    while (true) {
        while (PEEK(s) && !CYAML_IS_BREAK(PEEK(s))) {
            char c = PEEK(s);

            // Comment
            if (c == C_HASH && s->pos > start && CYAML_IS_WHITE(s->src[s->pos - 1]))
                break;

            // Value indicator
            if (c == ':' && CYAML_IS_BLANKZ(PEEK_AT(s, 1)))
                break;
            if (s->flow_level && c == ':' && CYAML_IS_FLOW(PEEK_AT(s, 1)))
                break;

            // Flow indicators
            if (s->flow_level && CYAML_IS_FLOW(c))
                break;

            // ASCII characters can use fast SKIP, multibyte needs UTF-8 validation
            if (CYAML_IS_ASCII(c)) {
                SKIP(s);
            } else {
                if (!skip_utf8(s))
                    return false;
            }
            if (!CYAML_IS_WHITE(c))
                end = s->pos;
        }

        if (!CYAML_IS_BREAK(PEEK(s)))
            break;

        // Handle multi-line plain scalars
        size_t save_pos = s->pos;
        uint32_t save_line = s->line, save_col = s->col;
        skip_line(s);

        // In flow context, skip whitespace and check continuation
        if (s->flow_level) {
            // Skip empty lines and whitespace
            bool saw_comment = false;
            while (PEEK(s)) {
                while (PEEK(s) == C_SP || PEEK(s) == C_TAB)
                    SKIP(s);
                if (PEEK(s) == C_HASH) {
                    skip_comment(s);
                    saw_comment = true;
                }
                if (CYAML_IS_BREAK(PEEK(s))) {
                    skip_line(s);
                    continue;
                }
                break;
            }
            // After comment, the scalar is done - next content is separate
            if (saw_comment) {
                s->pos = save_pos;
                s->line = save_line;
                s->col = save_col;
                break;
            }
            // In flow context, continuation line must not start with flow indicator
            char nc = PEEK(s);
            if (nc == C_NUL && !s->final) {
                s->need_more = true;
                return false;
            }
            if (nc == C_NUL || CYAML_IS_FLOW(nc) || (nc == ':' && CYAML_IS_BLANKZ(PEEK_AT(s, 1)))) {
                s->pos = save_pos;
                s->line = save_line;
                s->col = save_col;
                break;
            }
            // Valid continuation - continue scanning
            continue;
        }

        // In block context, skip only empty lines (not indentation)
        while (CYAML_IS_BREAK(PEEK(s))) {
            skip_line(s);
        }

        // Check indent for non-empty continuation line
        int spaces = 0;
        while (PEEK(s) == C_SP) {
            SKIP(s);
            spaces++;
        }

        if (PEEK(s) == C_NUL && !s->final) {
            s->need_more = true;
            return false;
        }
        if (spaces <= base_indent || PEEK(s) == C_NUL || is_doc_start(s) || is_doc_end(s)) {
            s->pos = save_pos;
            s->line = save_line;
            s->col = save_col;
            break;
        }
    }

    if (!s->final && PEEK(s) == C_NUL) {
        s->need_more = true;
        return false;
    }

    while (end > start && CYAML_IS_WHITE(s->src[end - 1]))
        end--;

    scan_token_t tok = {
        .type = TOK_SCALAR,
        .span = { .off = (uint32_t)start, .len = (uint32_t)(end - start), .start_line = line, .start_col = col, .end_line = s->line, .end_col = s->col },
        .style = CYAML_PLAIN
    };
    return enqueue_token(s, tok);
}

// #endregion

// #region Main Token Fetcher

static bool fetch_next_token(scanner_t* s)
{
    if (!HAS_FLAG(s, SCAN_STREAM_START_PRODUCED))
        return fetch_stream_start(s);

    if (!scan_to_next_token(s))
        return false;
    if (!stale_simple_keys(s))
        return false;
    if (!unroll_indent(s, (int)(s->col - 1)))
        return false;

    char c = PEEK(s);

    if (c == C_NUL) {
        if (!s->final) {
            s->need_more = true;
            return false;
        }
        return fetch_stream_end(s);
    }
    if (!s->final && s->col == 1) {
        size_t remaining = s->len - s->pos;
        if ((c == '-' && remaining <= 3 && memcmp(s->src + s->pos, "---", remaining) == 0)
            || (c == '.' && remaining <= 3 && memcmp(s->src + s->pos, "...", remaining) == 0)) {
            s->need_more = true;
            return false;
        }
    }
    if (!s->final && s->pos + 1 == s->len && (c == '-' || c == '?' || c == ':')) {
        s->need_more = true;
        return false;
    }
    if (s->col == 1 && c == '%' && !s->flow_level)
        return fetch_directive(s);
    if (is_doc_start(s))
        return fetch_doc_indicator(s, TOK_DOC_START);
    if (is_doc_end(s))
        return fetch_doc_indicator(s, TOK_DOC_END);

    if (c == '[')
        return fetch_flow_collection_start(s, TOK_FLOW_SEQ_START);
    if (c == '{')
        return fetch_flow_collection_start(s, TOK_FLOW_MAP_START);
    if (c == ']')
        return fetch_flow_collection_end(s, TOK_FLOW_SEQ_END);
    if (c == '}')
        return fetch_flow_collection_end(s, TOK_FLOW_MAP_END);
    if (c == ',')
        return fetch_flow_entry(s);

    if (c == '-' && CYAML_IS_BLANKZ(PEEK_AT(s, 1)))
        return fetch_block_entry(s);
    if (c == '?' && CYAML_IS_BLANKZ(PEEK_AT(s, 1)))
        return fetch_key(s);
    if (c == ':' && CYAML_IS_BLANKZ(PEEK_AT(s, 1)))
        return fetch_value(s);
    if (s->flow_level && c == ':' && CYAML_IS_FLOW(PEEK_AT(s, 1)))
        return fetch_value(s);
    if (s->flow_level && c == ':' && s->pos == s->adjacent_value_allowed_at)
        return fetch_value(s);
    if (s->flow_level && c == ':' && HAS_FLAG(s, SCAN_JSON_KEY_PENDING))
        return fetch_value(s);

    if (c == '*')
        return fetch_anchor(s, TOK_ALIAS);
    if (c == '&')
        return fetch_anchor(s, TOK_ANCHOR);
    if (c == '!')
        return fetch_tag(s);

    if (c == '|' && !s->flow_level)
        return fetch_block_scalar(s, true);
    if (c == '>' && !s->flow_level)
        return fetch_block_scalar(s, false);

    if (c == '\'')
        return fetch_flow_scalar(s, true);
    if (c == '"')
        return fetch_flow_scalar(s, false);

    bool can_start_plain = !CYAML_IS_BLANKZ(c) && c != '-' && c != '?' && c != ':' && c != ',' && c != '[' && c != ']' && c != '{' && c != '}' && c != C_HASH && c != '&' && c != '*' && c != '!' && c != '|' && c != '>' && c != '\'' && c != '"' && c != '%' && c != '@' && c != '`';

    if (can_start_plain)
        return fetch_plain_scalar(s);

    if ((c == '-' || c == '?' || c == ':') && !CYAML_IS_BLANKZ(PEEK_AT(s, 1))) {
        if (s->flow_level && CYAML_IS_FLOW(PEEK_AT(s, 1))) {
            set_synerr(s, SYNERR_INVALID_FLOW_CHAR);
            return false;
        }
        return fetch_plain_scalar(s);
    }

    set_synerr(s, SYNERR_INVALID_CHAR);
    return false;
}

static bool fetch_more_tokens(scanner_t* s)
{
    while (true) {
        bool need_more = (queue_count(s) == 0);

        if (!need_more) {
            if (!stale_simple_keys(s))
                return false;
            for (size_t i = 0; i < s->simple_key_top; i++) {
                if (s->simple_keys[i].possible && s->simple_keys[i].token_number == s->tokens_parsed) {
                    need_more = true;
                    break;
                }
            }
        }

        if (!need_more)
            break;
        if (!fetch_next_token(s))
            return false;
    }

    SET_FLAG(s, SCAN_TOKEN_AVAILABLE);
    return true;
}

static scan_token_t* peek_token(scanner_t* s)
{
    if (!HAS_FLAG(s, SCAN_TOKEN_AVAILABLE) && !fetch_more_tokens(s))
        return NULL;
    return queue_head(s);
}

// #region Parser State Machine [9]

typedef enum {
    PARSE_STREAM_START,
    PARSE_IMPLICIT_DOC_START,
    PARSE_DOC_START,
    PARSE_DOC_CONTENT,
    PARSE_DOC_END,
    PARSE_BLOCK_NODE,
    PARSE_BLOCK_NODE_OR_INDENTLESS_SEQ,
    PARSE_FLOW_NODE,
    PARSE_BLOCK_SEQ_FIRST_ENTRY,
    PARSE_BLOCK_SEQ_ENTRY,
    PARSE_INDENTLESS_SEQ_ENTRY,
    PARSE_BLOCK_MAP_FIRST_KEY,
    PARSE_BLOCK_MAP_KEY,
    PARSE_BLOCK_MAP_VALUE,
    PARSE_FLOW_SEQ_FIRST_ENTRY,
    PARSE_FLOW_SEQ_ENTRY,
    PARSE_FLOW_SEQ_ENTRY_MAP_KEY,
    PARSE_FLOW_SEQ_ENTRY_MAP_VALUE,
    PARSE_FLOW_SEQ_ENTRY_MAP_END,
    PARSE_FLOW_MAP_FIRST_KEY,
    PARSE_FLOW_MAP_KEY,
    PARSE_FLOW_MAP_VALUE,
    PARSE_FLOW_MAP_EMPTY_VALUE,
    PARSE_END
} parse_state_t;

typedef enum {
    EVT_NONE,
    EVT_STREAM_START,
    EVT_STREAM_END,
    EVT_DOC_START,
    EVT_DOC_END,
    EVT_ALIAS,
    EVT_SCALAR,
    EVT_SEQ_START,
    EVT_SEQ_END,
    EVT_MAP_START,
    EVT_MAP_END
} event_type_t;

typedef struct {
    event_type_t type;
    cyaml_span_t anchor, tag, value;
    cyaml_style_t style;
    cyaml_chomp_t chomp;
    uint8_t indent;
    uint8_t leading_breaks;
    uint8_t trailing_breaks;
    uint8_t scalar_flags;
    bool implicit, block;
    bool tab_sep; //!< Tab separator after doc start
} event_t;

typedef struct {
    scanner_t* scanner;
    parse_state_t state;
    parse_state_t* states;
    size_t states_cap;
    size_t state_top;
} yaml_parser_t;

static bool grow_states(yaml_parser_t* p)
{
    size_t new_cap = p->states_cap ? p->states_cap * 2 : STATES_INIT_CAP;
    parse_state_t* new_states = realloc(p->states, new_cap * sizeof(*new_states));
    if (!new_states) {
        set_synerr(p->scanner, SYNERR_NOMEM);
        return false;
    }
    p->states = new_states;
    p->states_cap = new_cap;
    return true;
}

static bool push_state(yaml_parser_t* p, parse_state_t state)
{
    if (p->state_top >= p->states_cap) {
        if (!grow_states(p))
            return false;
    }
    p->states[p->state_top++] = state;
    return true;
}

static parse_state_t pop_state(yaml_parser_t* p)
{
    return (p->state_top > 0) ? p->states[--p->state_top] : PARSE_END;
}

static bool parse_event(yaml_parser_t* p, event_t* event);
static bool parse_node(yaml_parser_t* p, event_t* event, bool block, bool indentless_seq);

static bool parse_stream_start(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok || tok->type != TOK_STREAM_START) {
        set_synerr(p->scanner, SYNERR_EXPECTED_STREAM_START);
        return false;
    }
    skip_token(p->scanner);
    p->state = PARSE_IMPLICIT_DOC_START;
    event->type = EVT_STREAM_START;
    return true;
}

static bool parse_doc_start(yaml_parser_t* p, event_t* event, bool implicit)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    // Skip any TOK_DOC_END markers - after these, implicit doc start is allowed
    bool saw_doc_end = false;
    while (tok->type == TOK_DOC_END) {
        saw_doc_end = true;
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;
    }

    if ((implicit || saw_doc_end) && tok->type != TOK_DOC_START && tok->type != TOK_STREAM_END) {
        CLEAR_FLAG(p->scanner, SCAN_DIRECTIVE_SEEN);
        if (!push_state(p, PARSE_DOC_END))
            return false;
        p->state = PARSE_BLOCK_NODE;
        event->type = EVT_DOC_START;
        event->implicit = true;
        return true;
    }

    if (tok->type == TOK_STREAM_END) {
        if (HAS_FLAG(p->scanner, SCAN_DIRECTIVE_SEEN)) {
            set_synerr(p->scanner, SYNERR_DIRECTIVE_WITHOUT_DOC);
            return false;
        }
        p->state = PARSE_END;
        skip_token(p->scanner);
        event->type = EVT_STREAM_END;
        return true;
    }

    if (tok->type == TOK_DOC_START) {
        bool tab_sep = tok->tab_sep; // Capture before skip
        skip_token(p->scanner);
        CLEAR_FLAG(p->scanner, SCAN_DIRECTIVE_SEEN);
        if (!push_state(p, PARSE_DOC_END))
            return false;
        p->state = PARSE_DOC_CONTENT;
        event->type = EVT_DOC_START;
        event->implicit = false;
        event->tab_sep = tab_sep;
        return true;
    }

    set_synerr(p->scanner, SYNERR_EXPECTED_DOC_START);
    return false;
}

static bool parse_doc_content(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_DOC_START || tok->type == TOK_DOC_END || tok->type == TOK_STREAM_END) {
        p->state = pop_state(p);
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    return parse_node(p, event, true, false);
}

static bool parse_doc_end(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    event->implicit = true;
    if (tok->type == TOK_DOC_END) {
        skip_token(p->scanner);
        event->implicit = false;
        p->state = PARSE_IMPLICIT_DOC_START; //! After explicit ..., allow implicit doc
    } else {
        p->state = PARSE_DOC_START; //! After implicit doc end, require explicit ---
    }
    event->type = EVT_DOC_END;
    return true;
}

static bool parse_node(yaml_parser_t* p, event_t* event, bool block, bool indentless_seq)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_ALIAS) {
        p->state = pop_state(p);
        event->type = EVT_ALIAS;
        event->anchor = tok->span;
        skip_token(p->scanner);
        return true;
    }

    cyaml_span_t anchor = { 0 }, tag = { 0 };
    while (tok && (tok->type == TOK_ANCHOR || tok->type == TOK_TAG)) {
        if (tok->type == TOK_ANCHOR) {
            if (anchor.len > 0) {
                set_synerr(p->scanner, SYNERR_MULTIPLE_ANCHORS);
                return false;
            }
            anchor = tok->span;
        } else {
            if (tag.len > 0) {
                set_synerr(p->scanner, SYNERR_MULTIPLE_TAGS);
                return false;
            }
            tag = tok->span;
            // Validate tag handle is defined
            const char* t = p->scanner->src + tag.off;
            if (tag.len > 1 && t[0] == '!' && t[tag.len - 1] != '>' && t[1] != '<') {
                // Find handle end (second ! in named handle like !prefix!)
                size_t handle_len = 1; //! Default: primary handle !
                if (tag.len > 1 && t[1] == '!') {
                    handle_len = 2; //! Secondary handle !!
                } else {
                    for (size_t i = 1; i < tag.len; i++) {
                        if (t[i] == '!') {
                            handle_len = i + 1; //! Named handle !name!
                            break;
                        }
                    }
                }
                // Check if named handle (ends with ! and is > 1 char) is defined
                if (handle_len > 2 && t[handle_len - 1] == '!') {
                    bool found = false;
                    cyaml_doc_t* doc = p->scanner->doc;
                    if (doc) {
                        for (uint8_t i = 0; i < doc->tag_count; i++) {
                            if (doc->tags[i].handle.len == handle_len && memcmp(p->scanner->src + doc->tags[i].handle.off, t, handle_len) == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        set_synerr(p->scanner, SYNERR_UNDEFINED_TAG_HANDLE);
                        return false;
                    }
                }
            }
        }
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
    }
    if (!tok)
        return false;

    if (indentless_seq && tok->type == TOK_BLOCK_ENTRY) {
        p->state = PARSE_INDENTLESS_SEQ_ENTRY;
        event->type = EVT_SEQ_START;
        event->anchor = anchor;
        event->tag = tag;
        event->block = true;
        return true;
    }

    if (block && tok->type == TOK_BLOCK_SEQ_START) {
        if (!push_state(p, pop_state(p)))
            return false;
        p->state = PARSE_BLOCK_SEQ_FIRST_ENTRY;
        event->type = EVT_SEQ_START;
        event->anchor = anchor;
        event->tag = tag;
        event->block = true;
        skip_token(p->scanner);
        return true;
    }

    if (block && tok->type == TOK_BLOCK_MAP_START) {
        if (!push_state(p, pop_state(p)))
            return false;
        p->state = PARSE_BLOCK_MAP_FIRST_KEY;
        event->type = EVT_MAP_START;
        event->anchor = anchor;
        event->tag = tag;
        event->block = true;
        skip_token(p->scanner);
        return true;
    }

    if (tok->type == TOK_FLOW_SEQ_START) {
        if (!push_state(p, pop_state(p)))
            return false;
        p->state = PARSE_FLOW_SEQ_FIRST_ENTRY;
        event->type = EVT_SEQ_START;
        event->anchor = anchor;
        event->tag = tag;
        event->value.start_line = tok->span.start_line;
        event->value.start_col = tok->span.start_col;
        event->block = false;
        skip_token(p->scanner);
        return true;
    }

    if (tok->type == TOK_FLOW_MAP_START) {
        if (!push_state(p, pop_state(p)))
            return false;
        p->state = PARSE_FLOW_MAP_FIRST_KEY;
        event->type = EVT_MAP_START;
        event->anchor = anchor;
        event->tag = tag;
        event->value.start_line = tok->span.start_line;
        event->value.start_col = tok->span.start_col;
        event->block = false;
        skip_token(p->scanner);
        return true;
    }

    if (tok->type == TOK_SCALAR) {
        p->state = pop_state(p);
        event->type = EVT_SCALAR;
        event->anchor = anchor;
        event->tag = tag;
        event->value = tok->span;
        event->style = tok->style;
        event->chomp = tok->chomp;
        event->indent = tok->indent;
        event->leading_breaks = tok->leading_breaks;
        event->trailing_breaks = tok->trailing_breaks;
        skip_token(p->scanner);
        return true;
    }

    // Empty node
    if (tok->type == TOK_BLOCK_END || tok->type == TOK_BLOCK_ENTRY || tok->type == TOK_FLOW_SEQ_END || tok->type == TOK_FLOW_MAP_END || tok->type == TOK_FLOW_ENTRY || tok->type == TOK_DOC_END || tok->type == TOK_DOC_START || tok->type == TOK_STREAM_END || tok->type == TOK_KEY || tok->type == TOK_VALUE) {
        p->state = pop_state(p);
        event->type = EVT_SCALAR;
        event->anchor = anchor;
        event->tag = tag;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    set_synerr(p->scanner, SYNERR_EXPECTED_NODE);
    return false;
}

static bool parse_block_seq_entry(yaml_parser_t* p, event_t* event, bool first)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_BLOCK_ENTRY) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_BLOCK_ENTRY && tok->type != TOK_BLOCK_END) {
            if (!push_state(p, PARSE_BLOCK_SEQ_ENTRY))
                return false;
            return parse_node(p, event, true, false);
        }

        p->state = PARSE_BLOCK_SEQ_ENTRY;
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    if (tok->type == TOK_BLOCK_END) {
        p->state = pop_state(p);
        event->type = EVT_SEQ_END;
        skip_token(p->scanner);
        return true;
    }

    (void)first;
    set_synerr(p->scanner, SYNERR_EXPECTED_BLOCK_ENTRY);
    return false;
}

static bool parse_indentless_seq_entry(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_BLOCK_ENTRY) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_BLOCK_ENTRY && tok->type != TOK_KEY && tok->type != TOK_VALUE && tok->type != TOK_BLOCK_END) {
            if (!push_state(p, PARSE_INDENTLESS_SEQ_ENTRY))
                return false;
            return parse_node(p, event, true, false);
        }

        p->state = PARSE_INDENTLESS_SEQ_ENTRY;
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    p->state = pop_state(p);
    event->type = EVT_SEQ_END;
    return true;
}

static bool parse_block_map_key(yaml_parser_t* p, event_t* event, bool first)
{
    (void)first;
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_KEY) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_KEY && tok->type != TOK_VALUE && tok->type != TOK_BLOCK_END) {
            if (!push_state(p, PARSE_BLOCK_MAP_VALUE))
                return false;
            return parse_node(p, event, true, true);
        }

        p->state = PARSE_BLOCK_MAP_VALUE;
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    // TOK_VALUE without TOK_KEY means empty key
    if (tok->type == TOK_VALUE) {
        p->state = PARSE_BLOCK_MAP_VALUE;
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    if (tok->type == TOK_BLOCK_END) {
        p->state = pop_state(p);
        event->type = EVT_MAP_END;
        skip_token(p->scanner);
        return true;
    }

    set_synerr(p->scanner, SYNERR_EXPECTED_MAP_KEY);
    return false;
}

static bool parse_block_map_value(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_VALUE) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_KEY && tok->type != TOK_VALUE && tok->type != TOK_BLOCK_END) {
            if (!push_state(p, PARSE_BLOCK_MAP_KEY))
                return false;
            return parse_node(p, event, true, true);
        }
    }

    p->state = PARSE_BLOCK_MAP_KEY;
    event->type = EVT_SCALAR;
    event->value = (cyaml_span_t) { 0 };
    event->style = CYAML_PLAIN;
    return true;
}

static bool parse_flow_seq_entry(yaml_parser_t* p, event_t* event, bool first)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    // Leading comma at start of flow sequence is invalid
    if (first && tok->type == TOK_FLOW_ENTRY) {
        set_synerr(p->scanner, SYNERR_LEADING_COMMA);
        return false;
    }

    if (!first) {
        if (tok->type == TOK_FLOW_ENTRY) {
            skip_token(p->scanner);
            tok = peek_token(p->scanner);
            if (!tok)
                return false;
            // Double comma (empty entry) is invalid in YAML flow sequences
            if (tok->type == TOK_FLOW_ENTRY) {
                set_synerr(p->scanner, SYNERR_EMPTY_FLOW_ENTRY);
                return false;
            }
        } else if (tok->type != TOK_FLOW_SEQ_END) {
            set_synerr(p->scanner, SYNERR_EXPECTED_FLOW_SEQ_SEP);
            return false;
        }
    }

    if (tok->type == TOK_KEY) {
        p->state = PARSE_FLOW_SEQ_ENTRY_MAP_KEY;
        skip_token(p->scanner);
        event->type = EVT_MAP_START;
        event->block = false;
        return true;
    }

    if (tok->type != TOK_FLOW_SEQ_END) {
        if (!push_state(p, PARSE_FLOW_SEQ_ENTRY))
            return false;
        return parse_node(p, event, false, false);
    }

    p->state = pop_state(p);
    event->type = EVT_SEQ_END;
    event->value.end_line = tok->span.end_line;
    event->value.end_col = tok->span.end_col;
    skip_token(p->scanner);
    return true;
}

static bool parse_flow_seq_entry_map_key(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type != TOK_VALUE && tok->type != TOK_FLOW_ENTRY && tok->type != TOK_FLOW_SEQ_END) {
        if (!push_state(p, PARSE_FLOW_SEQ_ENTRY_MAP_VALUE))
            return false;
        return parse_node(p, event, false, false);
    }

    p->state = PARSE_FLOW_SEQ_ENTRY_MAP_VALUE;
    event->type = EVT_SCALAR;
    event->value = (cyaml_span_t) { 0 };
    event->style = CYAML_PLAIN;
    return true;
}

static bool parse_flow_seq_entry_map_value(yaml_parser_t* p, event_t* event)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (tok->type == TOK_VALUE) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_FLOW_ENTRY && tok->type != TOK_FLOW_SEQ_END) {
            if (!push_state(p, PARSE_FLOW_SEQ_ENTRY_MAP_END))
                return false;
            return parse_node(p, event, false, false);
        }
    }

    p->state = PARSE_FLOW_SEQ_ENTRY_MAP_END;
    event->type = EVT_SCALAR;
    event->value = (cyaml_span_t) { 0 };
    event->style = CYAML_PLAIN;
    return true;
}

static bool parse_flow_seq_entry_map_end(yaml_parser_t* p, event_t* event)
{
    p->state = PARSE_FLOW_SEQ_ENTRY;
    event->type = EVT_MAP_END;
    return true;
}

static bool parse_flow_map_key(yaml_parser_t* p, event_t* event, bool first)
{
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    if (!first) {
        if (tok->type == TOK_FLOW_ENTRY) {
            skip_token(p->scanner);
            tok = peek_token(p->scanner);
            if (!tok)
                return false;
        } else if (tok->type != TOK_FLOW_MAP_END) {
            set_synerr(p->scanner, SYNERR_EXPECTED_FLOW_MAP_SEP);
            return false;
        }
    }

    if (tok->type == TOK_KEY) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_VALUE && tok->type != TOK_FLOW_ENTRY && tok->type != TOK_FLOW_MAP_END) {
            if (!push_state(p, PARSE_FLOW_MAP_VALUE))
                return false;
            return parse_node(p, event, false, false);
        }

        p->state = PARSE_FLOW_MAP_VALUE;
        event->type = EVT_SCALAR;
        event->value = (cyaml_span_t) { 0 };
        event->style = CYAML_PLAIN;
        return true;
    }

    if (tok->type != TOK_FLOW_MAP_END) {
        if (!push_state(p, PARSE_FLOW_MAP_EMPTY_VALUE))
            return false;
        return parse_node(p, event, false, false);
    }

    p->state = pop_state(p);
    event->type = EVT_MAP_END;
    event->value.end_line = tok->span.end_line;
    event->value.end_col = tok->span.end_col;
    skip_token(p->scanner);
    return true;
}

static bool parse_flow_map_value(yaml_parser_t* p, event_t* event, bool empty)
{
    (void)empty; //! No longer used - always check for TOK_VALUE
    scan_token_t* tok = peek_token(p->scanner);
    if (!tok)
        return false;

    // Check for value indicator (even when 'empty' - the key was implicit but value may exist)
    if (tok->type == TOK_VALUE) {
        skip_token(p->scanner);
        tok = peek_token(p->scanner);
        if (!tok)
            return false;

        if (tok->type != TOK_FLOW_ENTRY && tok->type != TOK_FLOW_MAP_END) {
            if (!push_state(p, PARSE_FLOW_MAP_KEY))
                return false;
            return parse_node(p, event, false, false);
        }
    }

    p->state = PARSE_FLOW_MAP_KEY;
    event->type = EVT_SCALAR;
    event->style = CYAML_PLAIN;
    event->value = (cyaml_span_t) { 0 };
    // If followed by "}" (no comma), mark as explicit empty (not null)
    // This applies whether or not there was a colon
    if (tok->type == TOK_FLOW_MAP_END) {
        event->scalar_flags = CYAML_SCALAR_EXPLICIT_EMPTY;
    }
    return true;
}

static bool parse_event(yaml_parser_t* p, event_t* event)
{
    memset(event, 0, sizeof(*event));

    switch (p->state) {
    case PARSE_STREAM_START:
        return parse_stream_start(p, event);
    case PARSE_IMPLICIT_DOC_START:
        return parse_doc_start(p, event, true);
    case PARSE_DOC_START:
        return parse_doc_start(p, event, false);
    case PARSE_DOC_CONTENT:
        return parse_doc_content(p, event);
    case PARSE_DOC_END:
        return parse_doc_end(p, event);
    case PARSE_BLOCK_NODE:
        return parse_node(p, event, true, false);
    case PARSE_BLOCK_NODE_OR_INDENTLESS_SEQ:
        return parse_node(p, event, true, true);
    case PARSE_FLOW_NODE:
        return parse_node(p, event, false, false);
    case PARSE_BLOCK_SEQ_FIRST_ENTRY:
        return parse_block_seq_entry(p, event, true);
    case PARSE_BLOCK_SEQ_ENTRY:
        return parse_block_seq_entry(p, event, false);
    case PARSE_INDENTLESS_SEQ_ENTRY:
        return parse_indentless_seq_entry(p, event);
    case PARSE_BLOCK_MAP_FIRST_KEY:
        return parse_block_map_key(p, event, true);
    case PARSE_BLOCK_MAP_KEY:
        return parse_block_map_key(p, event, false);
    case PARSE_BLOCK_MAP_VALUE:
        return parse_block_map_value(p, event);
    case PARSE_FLOW_SEQ_FIRST_ENTRY:
        return parse_flow_seq_entry(p, event, true);
    case PARSE_FLOW_SEQ_ENTRY:
        return parse_flow_seq_entry(p, event, false);
    case PARSE_FLOW_SEQ_ENTRY_MAP_KEY:
        return parse_flow_seq_entry_map_key(p, event);
    case PARSE_FLOW_SEQ_ENTRY_MAP_VALUE:
        return parse_flow_seq_entry_map_value(p, event);
    case PARSE_FLOW_SEQ_ENTRY_MAP_END:
        return parse_flow_seq_entry_map_end(p, event);
    case PARSE_FLOW_MAP_FIRST_KEY:
        return parse_flow_map_key(p, event, true);
    case PARSE_FLOW_MAP_KEY:
        return parse_flow_map_key(p, event, false);
    case PARSE_FLOW_MAP_VALUE:
        return parse_flow_map_value(p, event, false);
    case PARSE_FLOW_MAP_EMPTY_VALUE:
        return parse_flow_map_value(p, event, true);
    case PARSE_END:
        event->type = EVT_NONE;
        return true;
    }
    return false;
}

// #region Composer [3.2]

typedef struct {
    yaml_parser_t* parser;
    cyaml_doc_t* doc;
    bool allow_duplicate_keys;
    bool failed;
    struct {
        cyaml_span_t name;
        cyaml_node_t* node;
    } anchors[256];
    size_t anchor_count;
} composer_t;

typedef enum {
    FRAME_SEQ,
    FRAME_MAP_KEY,
    FRAME_MAP_VAL
} frame_state_t;

typedef struct {
    cyaml_node_t* node;
    cyaml_node_t* pending_key;
    frame_state_t state;
} compose_frame_t;

#define COMPOSE_STACK_INIT_CAP 32

static cyaml_node_t* compose_node(composer_t* c);

static void register_anchor(composer_t* c, cyaml_span_t name, cyaml_node_t* node)
{
    if (c->anchor_count < 256 && name.len > 0) {
        c->anchors[c->anchor_count].name = name;
        c->anchors[c->anchor_count].node = node;
        c->anchor_count++;
    }
}

static cyaml_node_t* composer_resolve_alias(composer_t* c, cyaml_span_t name)
{
    const char* src = cyaml_src(c->doc);
    // YAML 1.2 [7.1]: alias refers to most recent preceding node with same anchor
    // Iterate backwards to find most recent definition
    for (size_t i = c->anchor_count; i > 0; i--) {
        if (c->anchors[i - 1].name.len == name.len && memcmp(src + c->anchors[i - 1].name.off, src + name.off, name.len) == 0) {
            return c->anchors[i - 1].node;
        }
    }
    return NULL;
}

static cyaml_node_t* compose_scalar(composer_t* c, event_t* evt)
{
    cyaml_node_t* node = cyaml_pool_alloc(c->doc);
    if (!node)
        return NULL;

    node->type = CYAML_SCALAR;
    node->style = evt->style;
    node->span = evt->value;
    node->anchor = evt->anchor;
    node->tag = evt->tag;
    node->chomp = evt->chomp;
    node->indent = (uint8_t)(evt->indent & 0x3F);
    node->leading_breaks = evt->leading_breaks;
    node->trailing_breaks = evt->trailing_breaks;
    node->scalar.flags = evt->scalar_flags;

    // Convert plain scalars to null unless marked as explicit empty
    if (evt->style == CYAML_PLAIN && !(evt->scalar_flags & CYAML_SCALAR_EXPLICIT_EMPTY)) {
        const char* src = cyaml_src(c->doc);
        uint32_t len = evt->value.len;
        if (len == 0 || (len == L_TILDE && src[evt->value.off] == C_TILDE) || (len == L_NULL && cyaml_memicmp(src + evt->value.off, S_NULL, L_NULL) == 0)) {
            node->type = CYAML_NULL;
        }
    }

    if (evt->anchor.len > 0)
        register_anchor(c, evt->anchor, node);
    return node;
}

static bool seq_append(cyaml_node_t* seq, cyaml_node_t* item)
{
    if (seq->seq.count >= seq->seq.cap) {
        uint32_t new_cap = seq->seq.cap ? seq->seq.cap * 2 : SEQ_INIT_CAP;
        cyaml_node_t** items = realloc(seq->seq.items, (size_t)new_cap * sizeof(*items));
        if (!items)
            return false;
        seq->seq.items = items;
        seq->seq.cap = new_cap;
    }
    seq->seq.items[seq->seq.count++] = item;
    return true;
}

typedef enum {
    NODE_COMPARE_ERROR = -1,
    NODE_COMPARE_DIFFERENT = 0,
    NODE_COMPARE_EQUAL = 1
} node_compare_t;

typedef struct {
    const cyaml_node_t* lhs;
    const cyaml_node_t* rhs;
} compared_node_pair_t;

typedef struct {
    const cyaml_doc_t* doc;
    compared_node_pair_t* pairs;
    size_t count;
    size_t cap;
} node_compare_ctx_t;

static node_compare_t node_equal_impl(node_compare_ctx_t* ctx,
    const cyaml_node_t* lhs, const cyaml_node_t* rhs);

static node_compare_t scalar_equal(const cyaml_doc_t* doc,
    const cyaml_node_t* lhs, const cyaml_node_t* rhs)
{
    cyaml_scalar_kind_t lhs_kind = cyaml_scalar_kind(doc, lhs);
    cyaml_scalar_kind_t rhs_kind = cyaml_scalar_kind(doc, rhs);
    if (lhs_kind != rhs_kind)
        return NODE_COMPARE_DIFFERENT;

    const char* src = cyaml_src(doc);
    if (lhs->span.len == rhs->span.len
        && memcmp(src + lhs->span.off, src + rhs->span.off, lhs->span.len) == 0)
        return NODE_COMPARE_EQUAL;

    switch (lhs_kind) {
    case CYAML_KIND_BOOL: {
        bool lhs_value;
        bool rhs_value;
        return cyaml_as_bool(doc, lhs, &lhs_value)
                && cyaml_as_bool(doc, rhs, &rhs_value)
            ? (lhs_value == rhs_value ? NODE_COMPARE_EQUAL : NODE_COMPARE_DIFFERENT)
            : NODE_COMPARE_DIFFERENT;
    }
    case CYAML_KIND_INT: {
        int64_t lhs_value;
        int64_t rhs_value;
        if (cyaml_as_int(doc, lhs, &lhs_value) && cyaml_as_int(doc, rhs, &rhs_value))
            return lhs_value == rhs_value ? NODE_COMPARE_EQUAL : NODE_COMPARE_DIFFERENT;
        uint64_t lhs_unsigned;
        uint64_t rhs_unsigned;
        if (cyaml_as_uint(doc, lhs, &lhs_unsigned) && cyaml_as_uint(doc, rhs, &rhs_unsigned))
            return lhs_unsigned == rhs_unsigned ? NODE_COMPARE_EQUAL : NODE_COMPARE_DIFFERENT;
        break;
    }
    case CYAML_KIND_FLOAT: {
        double lhs_value;
        double rhs_value;
        if (cyaml_as_float(doc, lhs, &lhs_value) && cyaml_as_float(doc, rhs, &rhs_value)) {
            if (isnan(lhs_value) && isnan(rhs_value))
                return NODE_COMPARE_EQUAL;
            return lhs_value == rhs_value ? NODE_COMPARE_EQUAL : NODE_COMPARE_DIFFERENT;
        }
        break;
    }
    default:
        break;
    }

    size_t lhs_len;
    size_t rhs_len;
    char* lhs_value = cyaml_scalar_strn(doc, lhs, &lhs_len);
    if (!lhs_value)
        return NODE_COMPARE_ERROR;
    char* rhs_value = cyaml_scalar_strn(doc, rhs, &rhs_len);
    if (!rhs_value) {
        free(lhs_value);
        return NODE_COMPARE_ERROR;
    }
    node_compare_t result = lhs_len == rhs_len
            && memcmp(lhs_value, rhs_value, lhs_len) == 0
        ? NODE_COMPARE_EQUAL
        : NODE_COMPARE_DIFFERENT;
    free(rhs_value);
    free(lhs_value);
    return result;
}

static node_compare_t remember_node_pair(node_compare_ctx_t* ctx,
    const cyaml_node_t* lhs, const cyaml_node_t* rhs)
{
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->pairs[i].lhs == lhs && ctx->pairs[i].rhs == rhs)
            return NODE_COMPARE_EQUAL;
    }
    if (ctx->count >= ctx->cap) {
        size_t new_cap = ctx->cap ? ctx->cap * 2 : COMPOSE_STACK_INIT_CAP;
        compared_node_pair_t* pairs = realloc(ctx->pairs, new_cap * sizeof(*pairs));
        if (!pairs)
            return NODE_COMPARE_ERROR;
        ctx->pairs = pairs;
        ctx->cap = new_cap;
    }
    ctx->pairs[ctx->count++] = (compared_node_pair_t) { lhs, rhs };
    return NODE_COMPARE_DIFFERENT;
}

static node_compare_t node_equal_impl(node_compare_ctx_t* ctx,
    const cyaml_node_t* lhs, const cyaml_node_t* rhs)
{
    if (lhs == rhs)
        return NODE_COMPARE_EQUAL;
    if (!lhs || !rhs)
        return NODE_COMPARE_DIFFERENT;
    if (lhs->type == CYAML_ALIAS && lhs->alias.target)
        return node_equal_impl(ctx, lhs->alias.target, rhs);
    if (rhs->type == CYAML_ALIAS && rhs->alias.target)
        return node_equal_impl(ctx, lhs, rhs->alias.target);
    if (lhs->type != rhs->type)
        return NODE_COMPARE_DIFFERENT;

    if (lhs->tag.len != rhs->tag.len
        || (lhs->tag.len > 0
            && memcmp(cyaml_src(ctx->doc) + lhs->tag.off,
                   cyaml_src(ctx->doc) + rhs->tag.off, lhs->tag.len)
                != 0))
        return NODE_COMPARE_DIFFERENT;

    switch (lhs->type) {
    case CYAML_NULL:
        return NODE_COMPARE_EQUAL;
    case CYAML_SCALAR:
        return scalar_equal(ctx->doc, lhs, rhs);
    case CYAML_ALIAS:
        return lhs->anchor.len == rhs->anchor.len
                && memcmp(cyaml_src(ctx->doc) + lhs->anchor.off,
                       cyaml_src(ctx->doc) + rhs->anchor.off, lhs->anchor.len)
                    == 0
            ? NODE_COMPARE_EQUAL
            : NODE_COMPARE_DIFFERENT;
    case CYAML_SEQ:
        if (lhs->seq.count != rhs->seq.count)
            return NODE_COMPARE_DIFFERENT;
        size_t seq_checkpoint = ctx->count;
        {
            node_compare_t remembered = remember_node_pair(ctx, lhs, rhs);
            if (remembered != NODE_COMPARE_DIFFERENT)
                return remembered;
        }
        for (uint32_t i = 0; i < lhs->seq.count; i++) {
            node_compare_t result = node_equal_impl(ctx, lhs->seq.items[i], rhs->seq.items[i]);
            if (result != NODE_COMPARE_EQUAL) {
                ctx->count = seq_checkpoint;
                return result;
            }
        }
        ctx->count = seq_checkpoint;
        return NODE_COMPARE_EQUAL;
    case CYAML_MAP:
        if (lhs->map.count != rhs->map.count)
            return NODE_COMPARE_DIFFERENT;
        size_t map_checkpoint = ctx->count;
        {
            node_compare_t remembered = remember_node_pair(ctx, lhs, rhs);
            if (remembered != NODE_COMPARE_DIFFERENT)
                return remembered;
        }
        for (uint32_t i = 0; i < lhs->map.count; i++) {
            bool found = false;
            for (uint32_t j = 0; j < rhs->map.count; j++) {
                node_compare_t keys = node_equal_impl(ctx,
                    lhs->map.pairs[i].key, rhs->map.pairs[j].key);
                if (keys == NODE_COMPARE_ERROR) {
                    ctx->count = map_checkpoint;
                    return NODE_COMPARE_ERROR;
                }
                if (keys != NODE_COMPARE_EQUAL)
                    continue;
                node_compare_t values = node_equal_impl(ctx,
                    lhs->map.pairs[i].val, rhs->map.pairs[j].val);
                if (values == NODE_COMPARE_ERROR) {
                    ctx->count = map_checkpoint;
                    return NODE_COMPARE_ERROR;
                }
                if (values == NODE_COMPARE_EQUAL) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ctx->count = map_checkpoint;
                return NODE_COMPARE_DIFFERENT;
            }
        }
        ctx->count = map_checkpoint;
        return NODE_COMPARE_EQUAL;
    }
    return NODE_COMPARE_DIFFERENT;
}

static node_compare_t node_equal(const cyaml_doc_t* doc,
    const cyaml_node_t* lhs, const cyaml_node_t* rhs)
{
    node_compare_ctx_t ctx = { .doc = doc };
    node_compare_t result = node_equal_impl(&ctx, lhs, rhs);
    free(ctx.pairs);
    return result;
}

static bool map_append(cyaml_node_t* map, cyaml_node_t* key, cyaml_node_t* val)
{
    if (map->map.count >= map->map.cap) {
        uint32_t new_cap = map->map.cap ? map->map.cap * 2 : MAP_INIT_CAP;
        cyaml_pair_t* pairs = realloc(map->map.pairs, (size_t)new_cap * sizeof(*pairs));
        if (!pairs)
            return false;
        map->map.pairs = pairs;
        map->map.cap = new_cap;
    }
    map->map.pairs[map->map.count].key = key;
    map->map.pairs[map->map.count].val = val;
    map->map.count++;
    return true;
}

static bool map_commit(composer_t* c, cyaml_node_t* map,
    cyaml_node_t* key, cyaml_node_t* val)
{
    if (!c->allow_duplicate_keys) {
        for (uint32_t i = 0; i < map->map.count; i++) {
            node_compare_t result = node_equal(c->doc, map->map.pairs[i].key, key);
            if (result == NODE_COMPARE_ERROR) {
                c->failed = true;
                set_synerr(c->parser->scanner, SYNERR_NOMEM);
                return false;
            }
            if (result == NODE_COMPARE_EQUAL) {
                c->failed = true;
                cyaml_error_t* err = c->parser->scanner->err;
                if (err) {
                    err->code = CYAML_ERR_DUP_KEY;
                    err->span = key->span;
                    snprintf(err->msg, sizeof(err->msg), "Duplicate mapping key");
                }
                return false;
            }
        }
    }
    if (map_append(map, key, val))
        return true;
    c->failed = true;
    set_synerr(c->parser->scanner, SYNERR_NOMEM);
    return false;
}

static cyaml_node_t* compose_alias(composer_t* c, event_t* evt)
{
    cyaml_node_t* node = cyaml_pool_alloc(c->doc);
    if (node) {
        node->type = CYAML_ALIAS;
        node->anchor = evt->anchor;
        node->alias.target = composer_resolve_alias(c, evt->anchor);
    }
    return node;
}

static cyaml_node_t* init_sequence(composer_t* c, event_t* evt)
{
    cyaml_node_t* node = cyaml_pool_alloc(c->doc);
    if (!node)
        return NULL;
    node->type = CYAML_SEQ;
    node->style = (cyaml_style_t)(evt->block ? CYAML_BLOCK : CYAML_FLOW);
    node->anchor = evt->anchor;
    node->tag = evt->tag;
    node->span.start_line = evt->value.start_line;
    node->span.start_col = evt->value.start_col;
    if (evt->anchor.len > 0)
        register_anchor(c, evt->anchor, node);
    return node;
}

static cyaml_node_t* init_mapping(composer_t* c, event_t* evt)
{
    cyaml_node_t* node = cyaml_pool_alloc(c->doc);
    if (!node)
        return NULL;
    node->type = CYAML_MAP;
    node->style = (cyaml_style_t)(evt->block ? CYAML_BLOCK : CYAML_FLOW);
    node->anchor = evt->anchor;
    node->tag = evt->tag;
    node->span.start_line = evt->value.start_line;
    node->span.start_col = evt->value.start_col;
    if (evt->anchor.len > 0)
        register_anchor(c, evt->anchor, node);
    return node;
}

static cyaml_node_t* compose_node(composer_t* c)
{
    event_t evt;
    if (!parse_event(c->parser, &evt))
        return NULL;

    if (evt.type == EVT_SCALAR)
        return compose_scalar(c, &evt);
    if (evt.type == EVT_ALIAS)
        return compose_alias(c, &evt);
    if (evt.type != EVT_SEQ_START && evt.type != EVT_MAP_START)
        return NULL;

    compose_frame_t* stack = NULL;
    size_t stack_count = 0;
    size_t stack_cap = 0;
    cyaml_node_t* result = NULL;

    cyaml_node_t* root = (evt.type == EVT_SEQ_START)
        ? init_sequence(c, &evt)
        : init_mapping(c, &evt);
    if (!root)
        goto cleanup;

    stack = malloc(COMPOSE_STACK_INIT_CAP * sizeof(*stack));
    if (!stack)
        goto cleanup;
    stack_cap = COMPOSE_STACK_INIT_CAP;
    stack[0].node = root;
    stack[0].pending_key = NULL;
    stack[0].state = (evt.type == EVT_SEQ_START) ? FRAME_SEQ : FRAME_MAP_KEY;
    stack_count = 1;

    while (stack_count > 0) {
        compose_frame_t* frame = &stack[stack_count - 1];

        if (!parse_event(c->parser, &evt))
            goto cleanup;

        if (evt.type == EVT_SEQ_END || evt.type == EVT_MAP_END) {
            frame->node->span.end_line = evt.value.end_line;
            frame->node->span.end_col = evt.value.end_col;
            cyaml_node_t* completed = frame->node;
            stack_count--;

            if (stack_count == 0) {
                result = completed;
                goto cleanup;
            }

            compose_frame_t* parent = &stack[stack_count - 1];
            if (parent->state == FRAME_SEQ) {
                if (!seq_append(parent->node, completed))
                    goto cleanup;
            } else if (parent->state == FRAME_MAP_KEY) {
                parent->pending_key = completed;
                parent->state = FRAME_MAP_VAL;
            } else {
                if (!map_commit(c, parent->node, parent->pending_key, completed))
                    goto cleanup;
                parent->pending_key = NULL;
                parent->state = FRAME_MAP_KEY;
            }
            continue;
        }

        cyaml_node_t* node = NULL;
        bool is_container = false;

        switch (evt.type) {
        case EVT_SCALAR:
            node = compose_scalar(c, &evt);
            break;
        case EVT_ALIAS:
            node = compose_alias(c, &evt);
            break;
        case EVT_SEQ_START:
            node = init_sequence(c, &evt);
            is_container = true;
            break;
        case EVT_MAP_START:
            node = init_mapping(c, &evt);
            is_container = true;
            break;
        default:
            goto cleanup;
        }

        if (!node)
            goto cleanup;

        if (is_container) {
            if (stack_count >= stack_cap) {
                size_t new_cap = stack_cap * 2;
                compose_frame_t* new_stack = realloc(stack, new_cap * sizeof(*stack));
                if (!new_stack)
                    goto cleanup;
                stack = new_stack;
                stack_cap = new_cap;
            }
            stack[stack_count].node = node;
            stack[stack_count].pending_key = NULL;
            stack[stack_count].state = (evt.type == EVT_SEQ_START) ? FRAME_SEQ : FRAME_MAP_KEY;
            stack_count++;
        } else {
            if (frame->state == FRAME_SEQ) {
                if (!seq_append(frame->node, node))
                    goto cleanup;
            } else if (frame->state == FRAME_MAP_KEY) {
                frame->pending_key = node;
                frame->state = FRAME_MAP_VAL;
            } else {
                if (!map_commit(c, frame->node, frame->pending_key, node))
                    goto cleanup;
                frame->pending_key = NULL;
                frame->state = FRAME_MAP_KEY;
            }
        }
    }

cleanup:
    free(stack);
    return result;
}

// #region Document Parsing

static cyaml_doc_t* parse_document_internal(scanner_t* scanner, yaml_parser_t* parser,
    const cyaml_opts_t* opts, bool* failed)
{
    *failed = false;
    cyaml_doc_t* doc = calloc(1, sizeof(cyaml_doc_t));
    if (!doc) {
        *failed = true;
        set_synerr(scanner, SYNERR_NOMEM);
        return NULL;
    }

    doc->mode = CYAML_PARSING;
    doc->src.borrow.ptr = scanner->src;
    doc->src.borrow.len = (uint32_t)scanner->len;

    if (opts && opts->preserve_comments) {
        doc->comments = calloc(1, sizeof(*doc->comments));
    }

    // Set default version based on spec option (may be overridden by %YAML directive)
    doc->version.major = 1;
    switch (scanner->spec) {
    case CYAML_SPEC_1_1:
        doc->version.minor = 1;
        break;
    case CYAML_SPEC_1_3:
        doc->version.minor = 3;
        break;
    default:
        doc->version.minor = 2;
        break; // AUTO and 1.2
    }

    scanner->doc = doc;

    event_t evt;
    while (true) {
        if (!parse_event(parser, &evt)) {
            *failed = true;
            cyaml_free(doc);
            return NULL;
        }
        if (evt.type == EVT_STREAM_START)
            continue;
        if (evt.type == EVT_DOC_START) {
            if (!evt.implicit)
                doc->flags |= CYAML_DOC_START;
            if (evt.tab_sep)
                doc->flags |= CYAML_DOC_TAB_SEP;
            break;
        }
        if (evt.type == EVT_STREAM_END) {
            // End of stream, no more documents
            cyaml_free(doc);
            return NULL;
        }
    }

    composer_t composer = {
        .parser = parser,
        .doc = doc,
        .allow_duplicate_keys = opts && opts->dup_keys,
        .failed = false,
        .anchor_count = 0
    };
    doc->root = compose_node(&composer);
    if (!doc->root || composer.failed) {
        *failed = true;
        cyaml_free(doc);
        return NULL;
    }

    if (!parse_event(parser, &evt)) {
        *failed = true;
        cyaml_free(doc);
        return NULL;
    }
    if (evt.type == EVT_DOC_END && !evt.implicit)
        doc->flags |= CYAML_DOC_END;

    return doc;
}

// #region Public API

CYAML_API cyaml_doc_t* cyaml_parse(const char* src, size_t len,
    const cyaml_opts_t* opts, cyaml_error_t* err)
{
    if (!src) {
        if (err) {
            err->code = CYAML_ERR_SYNTAX;
            snprintf(err->msg, sizeof(err->msg), "NULL source");
        }
        return NULL;
    }

    scanner_t scanner = {
        .src = src, .len = len, .pos = 0, .line = 1, 1, .tokens = NULL, .tok_cap = 0, .tok_head = 0, .tok_tail = 0, .tokens_parsed = 0, .indents = NULL, .indents_cap = 0, .indent_top = 0, .indent = -1, .simple_keys = NULL, .simple_keys_cap = 0, .simple_key_top = 0, .flow_level = 0, .flags = SCAN_SIMPLE_KEY_ALLOWED, .adjacent_value_allowed_at = (size_t)-1, .doc = NULL, .err = err, .spec = opts ? opts->spec : CYAML_SPEC_AUTO, .final = true
    };

    yaml_parser_t parser = { .scanner = &scanner, .state = PARSE_STREAM_START, .states = NULL, .states_cap = 0, .state_top = 0 };

    if (err) {
        err->code = CYAML_OK;
        err->msg[0] = C_NUL;
    }

    bool failed;
    cyaml_doc_t* doc = parse_document_internal(&scanner, &parser, opts, &failed);

    FREE_SCANNER(scanner);
    FREE_PARSER(parser);

    if (failed || (err && err->code != CYAML_OK)) {
        cyaml_free(doc);
        return NULL;
    }

    return doc;
}

// #region Stream Parsing

CYAML_API cyaml_stream_t* cyaml_parse_stream(const char* src, size_t len,
    const cyaml_opts_t* opts, cyaml_error_t* err)
{
    if (!src) {
        if (err) {
            err->code = CYAML_ERR_SYNTAX;
            snprintf(err->msg, sizeof(err->msg), "NULL source");
        }
        return NULL;
    }

    cyaml_stream_t* stream = calloc(1, sizeof(cyaml_stream_t));
    if (!stream) {
        if (err) {
            err->code = CYAML_ERR_NOMEM;
            snprintf(err->msg, sizeof(err->msg), "ERR_NOMEM");
        }
        return NULL;
    }

    stream->src = src;
    stream->src_len = (uint32_t)len;

    scanner_t scanner = {
        .src = src, .len = len, .pos = 0, .line = 1, 1, .tokens = NULL, .tok_cap = 0, .tok_head = 0, .tok_tail = 0, .tokens_parsed = 0, .indents = NULL, .indents_cap = 0, .indent_top = 0, .indent = -1, .simple_keys = NULL, .simple_keys_cap = 0, .simple_key_top = 0, .flow_level = 0, .flags = SCAN_SIMPLE_KEY_ALLOWED, .adjacent_value_allowed_at = (size_t)-1, .doc = NULL, .err = err, .spec = opts ? opts->spec : CYAML_SPEC_AUTO, .final = true
    };

    yaml_parser_t parser = { .scanner = &scanner, .state = PARSE_STREAM_START, .states = NULL, .states_cap = 0, .state_top = 0 };

    if (err) {
        err->code = CYAML_OK;
        err->msg[0] = C_NUL;
    }

    while (true) {
        bool failed;
        cyaml_doc_t* doc = parse_document_internal(&scanner, &parser, opts, &failed);
        if (!doc) {
            if (failed || (err && err->code != CYAML_OK)) {
                FREE_SCANNER(scanner);
                FREE_PARSER(parser);
                cyaml_stream_free(stream);
                return NULL;
            }
            break;
        }

        if (stream->count >= stream->cap) {
            uint32_t new_cap = stream->cap ? stream->cap * 2 : STREAM_INIT_CAP;
            cyaml_doc_t** docs = realloc(stream->docs, (size_t)new_cap * sizeof(*docs));
            if (!docs) {
                cyaml_free(doc);
                FREE_SCANNER(scanner);
                FREE_PARSER(parser);
                cyaml_stream_free(stream);
                if (err) {
                    err->code = CYAML_ERR_NOMEM;
                    snprintf(err->msg, sizeof(err->msg), "ERR_NOMEM");
                }
                return NULL;
            }
            stream->docs = docs;
            stream->cap = new_cap;
        }
        stream->docs[stream->count++] = doc;

        if (parser.state == PARSE_END || HAS_FLAG(&scanner, SCAN_STREAM_END_PRODUCED))
            break;
    }

    FREE_SCANNER(scanner);
    FREE_PARSER(parser);
    return stream;
}

// #region Incremental SAX Parsing

typedef struct {
    bool is_map;
    bool is_key;
    bool expect_key;
} sax_frame_t;

struct cyaml_sax_parser {
    cyaml_sax_handler_t handler;
    void* ctx;
    cyaml_opts_t opts;
    cyaml_error_t error;
    char* input;
    size_t input_len;
    size_t input_cap;
    size_t total_input_len;
    scanner_t scanner;
    yaml_parser_t parser;
    cyaml_doc_t scratch_doc;
    sax_frame_t* frames;
    size_t depth;
    size_t frame_cap;
    bool finished;
    bool failed;
};

typedef struct {
    scanner_t scanner;
    yaml_parser_t parser;
    cyaml_doc_t scratch_doc;
} sax_checkpoint_t;

static void sax_checkpoint_discard(sax_checkpoint_t* checkpoint)
{
    free(checkpoint->scanner.tokens);
    free(checkpoint->scanner.indents);
    free(checkpoint->scanner.simple_keys);
    free(checkpoint->parser.states);
    memset(checkpoint, 0, sizeof(*checkpoint));
}

static bool sax_copy_array(void** out, const void* src, size_t count, size_t item_size)
{
    if (count == 0) {
        *out = NULL;
        return true;
    }
    *out = malloc(count * item_size);
    if (!*out)
        return false;
    memcpy(*out, src, count * item_size);
    return true;
}

static bool sax_checkpoint_take(cyaml_sax_parser_t* parser, sax_checkpoint_t* checkpoint)
{
    memset(checkpoint, 0, sizeof(*checkpoint));
    checkpoint->scanner = parser->scanner;
    checkpoint->parser = parser->parser;
    checkpoint->scratch_doc = parser->scratch_doc;
    checkpoint->scanner.tokens = NULL;
    checkpoint->scanner.indents = NULL;
    checkpoint->scanner.simple_keys = NULL;
    checkpoint->parser.states = NULL;

    if (!sax_copy_array((void**)&checkpoint->scanner.tokens, parser->scanner.tokens,
            parser->scanner.tok_cap, sizeof(*parser->scanner.tokens))
        || !sax_copy_array((void**)&checkpoint->scanner.indents, parser->scanner.indents,
            parser->scanner.indents_cap, sizeof(*parser->scanner.indents))
        || !sax_copy_array((void**)&checkpoint->scanner.simple_keys, parser->scanner.simple_keys,
            parser->scanner.simple_keys_cap, sizeof(*parser->scanner.simple_keys))
        || !sax_copy_array((void**)&checkpoint->parser.states, parser->parser.states,
            parser->parser.states_cap, sizeof(*parser->parser.states))) {
        sax_checkpoint_discard(checkpoint);
        parser->error.code = CYAML_ERR_NOMEM;
        snprintf(parser->error.msg, sizeof(parser->error.msg), "Out of memory checkpointing YAML parser");
        parser->failed = true;
        return false;
    }
    return true;
}

static void sax_checkpoint_restore(cyaml_sax_parser_t* parser, sax_checkpoint_t* checkpoint)
{
    free(parser->scanner.tokens);
    free(parser->scanner.indents);
    free(parser->scanner.simple_keys);
    free(parser->parser.states);
    parser->scanner = checkpoint->scanner;
    parser->parser = checkpoint->parser;
    parser->scratch_doc = checkpoint->scratch_doc;
    parser->scanner.doc = &parser->scratch_doc;
    parser->scanner.err = &parser->error;
    parser->parser.scanner = &parser->scanner;
    memset(checkpoint, 0, sizeof(*checkpoint));
}

static bool sax_grow_frames(cyaml_sax_parser_t* parser)
{
    size_t new_cap = parser->frame_cap ? parser->frame_cap * 2 : 32;
    sax_frame_t* frames;
    if (parser->opts.max_depth && new_cap > parser->opts.max_depth)
        new_cap = parser->opts.max_depth;
    if (new_cap <= parser->frame_cap) {
        parser->error.code = CYAML_ERR_SYNTAX;
        snprintf(parser->error.msg, sizeof(parser->error.msg), "Maximum YAML nesting depth exceeded");
        return false;
    }
    frames = realloc(parser->frames, new_cap * sizeof(*frames));
    if (!frames) {
        parser->error.code = CYAML_ERR_NOMEM;
        snprintf(parser->error.msg, sizeof(parser->error.msg), "Out of memory growing YAML event stack");
        return false;
    }
    parser->frames = frames;
    parser->frame_cap = new_cap;
    return true;
}

static bool sax_event_is_key(const cyaml_sax_parser_t* parser)
{
    return parser->depth > 0 && parser->frames[parser->depth - 1].is_map
        && parser->frames[parser->depth - 1].expect_key;
}

static void sax_complete_value(cyaml_sax_parser_t* parser)
{
    if (parser->depth > 0 && parser->frames[parser->depth - 1].is_map)
        parser->frames[parser->depth - 1].expect_key
            = !parser->frames[parser->depth - 1].expect_key;
}

static int sax_callback_failed(cyaml_sax_parser_t* parser)
{
    parser->error.code = CYAML_ERR_IO;
    snprintf(parser->error.msg, sizeof(parser->error.msg), "YAML event callback failed");
    parser->failed = true;
    return -1;
}

static char* sax_scalar_text(cyaml_sax_parser_t* parser, const event_t* event,
    cyaml_scalar_kind_t* kind, size_t* len)
{
    cyaml_node_t node = { 0 };
    node.type = CYAML_SCALAR;
    node.style = event->style;
    node.chomp = event->chomp;
    node.indent = event->indent;
    node.leading_breaks = event->leading_breaks;
    node.trailing_breaks = event->trailing_breaks;
    node.span = event->value;
    node.scalar.flags = event->scalar_flags;
    if (event->style == CYAML_PLAIN && !(event->scalar_flags & CYAML_SCALAR_EXPLICIT_EMPTY)) {
        const char* src = parser->scanner.src;
        if (event->value.len == 0
            || (event->value.len == L_TILDE && src[event->value.off] == C_TILDE)
            || (event->value.len == L_NULL
                && cyaml_memicmp(src + event->value.off, S_NULL, L_NULL) == 0))
            node.type = CYAML_NULL;
    }
    *kind = cyaml_scalar_kind(&parser->scratch_doc, &node);
    if (*kind == CYAML_KIND_NULL) {
        *len = 0;
        return NULL;
    }
    return cyaml_scalar_strn(&parser->scratch_doc, &node, len);
}

static int sax_dispatch_event(cyaml_sax_parser_t* parser, const event_t* event)
{
    bool is_key = sax_event_is_key(parser);
    int rc = 0;
    switch (event->type) {
    case EVT_STREAM_START:
    case EVT_STREAM_END:
    case EVT_NONE:
        return 0;
    case EVT_DOC_START:
        return !parser->handler.on_document_start
                || parser->handler.on_document_start(parser->ctx) == 0
            ? 0
            : sax_callback_failed(parser);
    case EVT_DOC_END:
        rc = !parser->handler.on_document_end
                || parser->handler.on_document_end(parser->ctx) == 0
            ? 0
            : sax_callback_failed(parser);
        parser->scratch_doc.flags = 0;
        parser->scratch_doc.tag_count = 0;
        parser->scratch_doc.version.major = 1;
        parser->scratch_doc.version.minor = 2;
        return rc;
    case EVT_SCALAR: {
        cyaml_scalar_kind_t kind;
        size_t len = 0;
        char* text = sax_scalar_text(parser, event, &kind, &len);
        if (kind == CYAML_KIND_NULL) {
            rc = !parser->handler.on_null || parser->handler.on_null(parser->ctx, is_key) == 0
                ? 0
                : sax_callback_failed(parser);
        } else if (!text) {
            parser->error.code = CYAML_ERR_NOMEM;
            snprintf(parser->error.msg, sizeof(parser->error.msg), "Out of memory decoding YAML scalar");
            parser->failed = true;
            return -1;
        } else {
            rc = !parser->handler.on_scalar
                    || parser->handler.on_scalar(parser->ctx, kind, text, len, is_key) == 0
                ? 0
                : sax_callback_failed(parser);
        }
        free(text);
        if (rc == 0)
            sax_complete_value(parser);
        return rc;
    }
    case EVT_ALIAS: {
        const char* value = parser->scanner.src + event->anchor.off;
        rc = !parser->handler.on_alias
                || parser->handler.on_alias(parser->ctx, value, event->anchor.len, is_key) == 0
            ? 0
            : sax_callback_failed(parser);
        if (rc == 0)
            sax_complete_value(parser);
        return rc;
    }
    case EVT_SEQ_START:
    case EVT_MAP_START:
        if (parser->depth >= parser->frame_cap && !sax_grow_frames(parser)) {
            parser->failed = true;
            return -1;
        }
        rc = event->type == EVT_SEQ_START
            ? (!parser->handler.on_sequence_start
                          || parser->handler.on_sequence_start(parser->ctx, is_key) == 0
                      ? 0
                      : sax_callback_failed(parser))
            : (!parser->handler.on_mapping_start
                          || parser->handler.on_mapping_start(parser->ctx, is_key) == 0
                      ? 0
                      : sax_callback_failed(parser));
        if (rc != 0)
            return rc;
        parser->frames[parser->depth++] = (sax_frame_t) {
            .is_map = event->type == EVT_MAP_START,
            .is_key = is_key,
            .expect_key = event->type == EVT_MAP_START
        };
        return 0;
    case EVT_SEQ_END:
    case EVT_MAP_END: {
        sax_frame_t frame;
        if (parser->depth == 0) {
            parser->error.code = CYAML_ERR_SYNTAX;
            snprintf(parser->error.msg, sizeof(parser->error.msg), "Unbalanced YAML collection event");
            parser->failed = true;
            return -1;
        }
        frame = parser->frames[--parser->depth];
        rc = event->type == EVT_SEQ_END
            ? (!parser->handler.on_sequence_end
                          || parser->handler.on_sequence_end(parser->ctx, frame.is_key) == 0
                      ? 0
                      : sax_callback_failed(parser))
            : (!parser->handler.on_mapping_end
                          || parser->handler.on_mapping_end(parser->ctx, frame.is_key) == 0
                      ? 0
                      : sax_callback_failed(parser));
        if (rc == 0)
            sax_complete_value(parser);
        return rc;
    }
    }
    return 0;
}

static int sax_drive(cyaml_sax_parser_t* parser)
{
    while (!parser->failed) {
        event_t event;
        sax_checkpoint_t checkpoint;
        parser->scanner.need_more = false;
        if (!sax_checkpoint_take(parser, &checkpoint))
            return -1;
        if (!parse_event(&parser->parser, &event)) {
            if (parser->scanner.need_more && !parser->scanner.final) {
                sax_checkpoint_restore(parser, &checkpoint);
                parser->error.code = CYAML_OK;
                parser->error.msg[0] = C_NUL;
                return 0;
            }
            sax_checkpoint_discard(&checkpoint);
            parser->failed = true;
            if (parser->error.code == CYAML_OK) {
                parser->error.code = CYAML_ERR_SYNTAX;
                snprintf(parser->error.msg, sizeof(parser->error.msg), "Invalid YAML stream");
            }
            return -1;
        }
        sax_checkpoint_discard(&checkpoint);
        if (sax_dispatch_event(parser, &event) != 0)
            return -1;
        if (event.type == EVT_STREAM_END || event.type == EVT_NONE)
            return 0;
    }
    return -1;
}

static size_t sax_retained_offset(const cyaml_sax_parser_t* parser)
{
    const scanner_t* scanner = &parser->scanner;
    size_t retained = scanner->pos;
    size_t count = queue_count(scanner);
    size_t i;

    for (i = 0; i < count; ++i) {
        const scan_token_t* token
            = &scanner->tokens[(scanner->tok_head + i) % scanner->tok_cap];
        if (token->span.off < retained)
            retained = token->span.off;
    }
    for (i = 0; i < scanner->simple_key_top; ++i) {
        const simple_key_t* key = &scanner->simple_keys[i];
        if (key->possible && key->mark_index < retained)
            retained = key->mark_index;
    }
    for (i = 0; i < parser->scratch_doc.tag_count; ++i) {
        const cyaml_tag_directive_t* tag = &parser->scratch_doc.tags[i];
        if (tag->handle.off < retained)
            retained = tag->handle.off;
        if (tag->prefix.off < retained)
            retained = tag->prefix.off;
    }
    return retained;
}

static void sax_compact_input(cyaml_sax_parser_t* parser)
{
    scanner_t* scanner = &parser->scanner;
    size_t trim = sax_retained_offset(parser);
    size_t count;
    size_t i;

    if (trim == 0)
        return;

    memmove(parser->input, parser->input + trim, parser->input_len - trim);
    parser->input_len -= trim;
    parser->input[parser->input_len] = C_NUL;
    scanner->pos -= trim;
    scanner->len = parser->input_len;

    count = queue_count(scanner);
    for (i = 0; i < count; ++i) {
        scan_token_t* token = &scanner->tokens[(scanner->tok_head + i) % scanner->tok_cap];
        token->span.off -= (uint32_t)trim;
    }
    for (i = 0; i < scanner->simple_key_top; ++i) {
        simple_key_t* key = &scanner->simple_keys[i];
        if (key->possible)
            key->mark_index -= trim;
    }
    if (scanner->adjacent_value_allowed_at != (size_t)-1) {
        scanner->adjacent_value_allowed_at = scanner->adjacent_value_allowed_at >= trim
            ? scanner->adjacent_value_allowed_at - trim
            : (size_t)-1;
    }
    for (i = 0; i < parser->scratch_doc.tag_count; ++i) {
        parser->scratch_doc.tags[i].handle.off -= (uint32_t)trim;
        parser->scratch_doc.tags[i].prefix.off -= (uint32_t)trim;
    }
    parser->scratch_doc.src.borrow.ptr = parser->input;
    parser->scratch_doc.src.borrow.len = (uint32_t)parser->input_len;
}

CYAML_API cyaml_sax_parser_t* cyaml_sax_parser_create(
    const cyaml_sax_handler_t* handler, void* ctx, const cyaml_opts_t* opts)
{
    cyaml_sax_parser_t* parser;
    if (!handler)
        return NULL;
    parser = calloc(1, sizeof(*parser));
    if (!parser)
        return NULL;
    parser->handler = *handler;
    parser->ctx = ctx;
    parser->opts = opts ? *opts : CYAML_OPTS_DEFAULT;
    parser->scratch_doc.mode = CYAML_PARSING;
    parser->scratch_doc.version.major = 1;
    parser->scratch_doc.version.minor = 2;
    parser->scanner.line = 1;
    parser->scanner.col = 1;
    parser->scanner.indent = -1;
    parser->scanner.adjacent_value_allowed_at = (size_t)-1;
    parser->scanner.doc = &parser->scratch_doc;
    parser->scanner.err = &parser->error;
    parser->scanner.spec = parser->opts.spec;
    parser->parser.scanner = &parser->scanner;
    parser->parser.state = PARSE_STREAM_START;
    return parser;
}

CYAML_API int cyaml_sax_parser_feed(cyaml_sax_parser_t* parser,
    const char* data, size_t len)
{
    char* input;
    size_t required;
    if (!parser || parser->finished || parser->failed || (!data && len != 0))
        return -1;
    if (len == 0)
        return 0;
    if (len > UINT32_MAX - parser->total_input_len
        || (parser->opts.max_size && len > parser->opts.max_size - parser->total_input_len)) {
        parser->error.code = CYAML_ERR_NOMEM;
        snprintf(parser->error.msg, sizeof(parser->error.msg), "YAML input exceeds configured size");
        parser->failed = true;
        return -1;
    }
    required = parser->input_len + len + 1;
    if (required > parser->input_cap) {
        size_t new_cap = parser->input_cap ? parser->input_cap : 256;
        while (new_cap < required)
            new_cap *= 2;
        input = realloc(parser->input, new_cap);
        if (!input) {
            parser->error.code = CYAML_ERR_NOMEM;
            snprintf(parser->error.msg, sizeof(parser->error.msg), "Out of memory buffering YAML input");
            parser->failed = true;
            return -1;
        }
        parser->input = input;
        parser->input_cap = new_cap;
    }
    memcpy(parser->input + parser->input_len, data, len);
    parser->input_len += len;
    parser->total_input_len += len;
    parser->input[parser->input_len] = C_NUL;
    parser->scanner.src = parser->input;
    parser->scanner.len = parser->input_len;
    parser->scratch_doc.src.borrow.ptr = parser->input;
    parser->scratch_doc.src.borrow.len = (uint32_t)parser->input_len;
    if (sax_drive(parser) != 0)
        return -1;
    sax_compact_input(parser);
    return 0;
}

CYAML_API int cyaml_sax_parser_finish(cyaml_sax_parser_t* parser)
{
    if (!parser || parser->finished || parser->failed)
        return -1;
    parser->finished = true;
    parser->scanner.final = true;
    return sax_drive(parser);
}

CYAML_API const cyaml_error_t* cyaml_sax_parser_error(const cyaml_sax_parser_t* parser)
{
    return parser && parser->error.code != CYAML_OK ? &parser->error : NULL;
}

CYAML_API void cyaml_sax_parser_destroy(cyaml_sax_parser_t* parser)
{
    if (!parser)
        return;
    FREE_SCANNER(parser->scanner);
    FREE_PARSER(parser->parser);
    free(parser->frames);
    free(parser->input);
    free(parser);
}

// #endregion
