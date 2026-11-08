/**
 * @file ini_grammar.y
 * @brief INI File Parser Grammar (Lemon)
 *
 * Build: lemon -Tlempar.c ini_grammar.y
 */

%name IniParse
%token_prefix INI_TOKEN_
%token_type {ini_token_t}
%default_type {ini_token_t}

%extra_argument {ini_parse_ctx_t *ctx}

%include {
#include "ini_lexer.h"
#include "ini_types.h"
#include <stdlib.h>
#include <string.h>
#include "tstr.h"

static tstr ini_strndup(const char *s, size_t n) {
    return tstr_dup_len(s, n);
}

static ini_section_t *ini_add_section(ini_parse_ctx_t *ctx, const char *name, size_t len) {
    for (ini_section_t *s = ctx->sections; s; s = s->next) {
        if (tstr_len(s->name) == len && strncmp(s->name, name, len) == 0) {
            ctx->current = s;
            return s;
        }
    }

    ini_section_t *sec = (ini_section_t *)calloc(1, sizeof(ini_section_t));
    if (!sec) return NULL;

    sec->name = ini_strndup(name, len);
    if (!sec->name) {
        free(sec);
        return NULL;
    }

    if (ctx->sections_tail) {
        ctx->sections_tail->next = sec;
    } else {
        ctx->sections = sec;
    }
    ctx->sections_tail = sec;
    ctx->current = sec;
    return sec;
}

static int ini_add_entry(ini_parse_ctx_t *ctx, const char *key, size_t key_len,
                         const char *value, size_t value_len) {
    if (!ctx->current) {
        ini_add_section(ctx, "", 0);
    }

    for (ini_entry_t *e = ctx->current->entries; e; e = e->next) {
        if (tstr_len(e->key) == key_len && strncmp(e->key, key, key_len) == 0) {
            tstr new_val = ini_strndup(value, value_len);
            if (!new_val) return -1;
            tstr_free(e->value);
            e->value = new_val;
            return 0;
        }
    }

    ini_entry_t *entry = (ini_entry_t *)calloc(1, sizeof(ini_entry_t));
    if (!entry) return -1;

    entry->key = ini_strndup(key, key_len);
    entry->value = ini_strndup(value, value_len);

    if (!entry->key || !entry->value) {
        tstr_free(entry->key);
        tstr_free(entry->value);
        free(entry);
        return -1;
    }

    if (ctx->current->entries_tail) {
        ctx->current->entries_tail->next = entry;
    } else {
        ctx->current->entries = entry;
    }
    ctx->current->entries_tail = entry;
    return 0;
}
}

// Token declarations
%token SECTION KEY VALUE NEWLINE COMMENT.

// Grammar rules
start ::= ini_file.

ini_file ::= statements.

statements ::= statements statement.
statements ::= .

statement ::= section.
statement ::= key_value.
statement ::= NEWLINE.
statement ::= COMMENT.

section ::= SECTION(S). {
    ini_add_section(ctx, S.value, S.length);
}

key_value ::= KEY(K) VALUE(V). {
    ini_add_entry(ctx, K.value, K.length, V.value, V.length);
}

%syntax_error {
    ctx->error = 1;
}

%parse_failure {
    ctx->error = 1;
}
