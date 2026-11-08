/**
 * @file ini_parser.c
 * @brief INI Parser Implementation using re2c/lemon
 */

#include "ini_parser.h"
#include "ini_lexer.h"
#include "ini_types.h"
#include "ini_grammar_gen.h"
#include "tstr.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

struct ini_t {
    ini_section_t *sections;
    size_t section_count;
};

/* Forward declarations for lemon parser */
void *IniParseAlloc(void *(*)(size_t));
void IniParseFree(void *, void (*)(void *));
void IniParse(void *, int, ini_token_t, ini_parse_ctx_t *);

static ini_section_t *ini_find_section(ini_t *ini, const char *name) {
    if (!name) name = "";
    vstr name_v = vstr_from_cstr(name);
    for (ini_section_t *s = ini->sections; s; s = s->next) {
        if (tstr_eq_v(s->name, name_v)) {
            return s;
        }
    }
    return NULL;
}

static ini_section_t *ini_find_section_v(ini_t *ini, vstr name) {
    if (!name.data) {
        name = vstr_from_cstr("");
    }
    for (ini_section_t *s = ini->sections; s; s = s->next) {
        if (tstr_eq_v(s->name, name)) {
            return s;
        }
    }
    return NULL;
}

ini_t *ini_parse(const char *content, size_t len) {
    if (!content) return NULL;

    ini_lexer_t lexer;
    ini_token_t token;
    ini_parse_ctx_t ctx = {0};

    ini_lexer_init(&lexer, content, len);

    void *parser = IniParseAlloc(malloc);
    if (!parser) return NULL;

    int result;
    while ((result = ini_lexer_next(&lexer, &token)) != 0) {
        if (result < 0) {
            ctx.error = 1;
            break;
        }
        IniParse(parser, token.type, token, &ctx);
    }

    /* Send EOF (token type 0) */
    token.type = 0;
    IniParse(parser, 0, token, &ctx);

    IniParseFree(parser, free);

    if (ctx.error) {
        /* Free parsed data on error */
        ini_section_t *s = ctx.sections;
        while (s) {
            ini_section_t *next_s = s->next;
            ini_entry_t *e = s->entries;
            while (e) {
                ini_entry_t *next_e = e->next;
                tstr_free(e->key);
                tstr_free(e->value);
                free(e);
                e = next_e;
            }
            tstr_free(s->name);
            free(s);
            s = next_s;
        }
        return NULL;
    }

    ini_t *ini = calloc(1, sizeof(ini_t));
    if (!ini) return NULL;

    ini->sections = ctx.sections;

    /* Count sections */
    for (ini_section_t *s = ini->sections; s; s = s->next) {
        ini->section_count++;
    }

    return ini;
}

ini_t *ini_parse_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, (size_t)size, f);
    fclose(f);

    content[read_size] = '\0';
    ini_t *ini = ini_parse(content, read_size);
    free(content);
    return ini;
}

void ini_free(ini_t *ini) {
    if (!ini) return;

    ini_section_t *s = ini->sections;
    while (s) {
        ini_section_t *next_s = s->next;
        ini_entry_t *e = s->entries;
        while (e) {
            ini_entry_t *next_e = e->next;
            tstr_free(e->key);
            tstr_free(e->value);
            free(e);
            e = next_e;
        }
        tstr_free(s->name);
        free(s);
        s = next_s;
    }
    free(ini);
}

const char *ini_get(ini_t *ini, const char *section, const char *key) {
    if (!ini || !key) return NULL;

    ini_section_t *s = ini_find_section(ini, section);
    if (!s) return NULL;

    vstr key_v = vstr_from_cstr(key);
    for (ini_entry_t *e = s->entries; e; e = e->next) {
        if (tstr_eq_v(e->key, key_v)) {
            return e->value;
        }
    }
    return NULL;
}

vstr ini_get_v(ini_t *ini, const char *section, const char *key) {
    if (!ini || !key) return vstr_from_buf(NULL, 0);

    ini_section_t *s = ini_find_section(ini, section);
    if (!s) return vstr_from_buf(NULL, 0);

    vstr key_v = vstr_from_cstr(key);
    for (ini_entry_t *e = s->entries; e; e = e->next) {
        if (tstr_eq_v(e->key, key_v)) {
            return tstr_to_v(e->value);
        }
    }
    return vstr_from_buf(NULL, 0);
}

vstr ini_get_vv(ini_t *ini, vstr section, vstr key) {
    if (!ini || !key.data) return vstr_from_buf(NULL, 0);

    ini_section_t *s = ini_find_section_v(ini, section);
    if (!s) return vstr_from_buf(NULL, 0);

    for (ini_entry_t *e = s->entries; e; e = e->next) {
        if (tstr_eq_v(e->key, key)) {
            return tstr_to_v(e->value);
        }
    }
    return vstr_from_buf(NULL, 0);
}

int ini_get_int(ini_t *ini, const char *section, const char *key, int default_val) {
    const char *val = ini_get(ini, section, key);
    if (!val) return default_val;

    char *end;
    long result = strtol(val, &end, 0);
    return (end != val) ? (int)result : default_val;
}

bool ini_get_bool(ini_t *ini, const char *section, const char *key, bool default_val) {
    const char *val = ini_get(ini, section, key);
    if (!val) return default_val;

    if (strcmp(val, "1") == 0 || tstr_casecmp(val, "true") == 0 ||
        tstr_casecmp(val, "yes") == 0 || tstr_casecmp(val, "on") == 0) {
        return true;
    }
    if (strcmp(val, "0") == 0 || tstr_casecmp(val, "false") == 0 ||
        tstr_casecmp(val, "no") == 0 || tstr_casecmp(val, "off") == 0) {
        return false;
    }
    return default_val;
}

double ini_get_double(ini_t *ini, const char *section, const char *key, double default_val) {
    const char *val = ini_get(ini, section, key);
    if (!val) return default_val;

    char *end;
    double result = strtod(val, &end);
    return (end != val) ? result : default_val;
}

int ini_get_int_v(ini_t *ini, vstr section, vstr key, int default_val) {
    vstr val = ini_get_vv(ini, section, key);
    if (!val.data) return default_val;

    char *end;
    char *cstr = vstr_to_cstr(val);
    if (!cstr) return default_val;
    long result = strtol(cstr, &end, 0);
    int ret = (end != cstr) ? (int)result : default_val;
    free(cstr);
    return ret;
}

bool ini_get_bool_v(ini_t *ini, vstr section, vstr key, bool default_val) {
    vstr val = ini_get_vv(ini, section, key);
    if (!val.data) return default_val;

    if (val.len == 1 && val.data[0] == '1') return true;
    if (val.len == 1 && val.data[0] == '0') return false;
    if (vstr_ieq(val, vstr_from_cstr("true"))) return true;
    if (vstr_ieq(val, vstr_from_cstr("false"))) return false;
    if (vstr_ieq(val, vstr_from_cstr("yes"))) return true;
    if (vstr_ieq(val, vstr_from_cstr("no"))) return false;
    if (vstr_ieq(val, vstr_from_cstr("on"))) return true;
    if (vstr_ieq(val, vstr_from_cstr("off"))) return false;
    return default_val;
}

double ini_get_double_v(ini_t *ini, vstr section, vstr key, double default_val) {
    vstr val = ini_get_vv(ini, section, key);
    if (!val.data) return default_val;

    char *end;
    char *cstr = vstr_to_cstr(val);
    if (!cstr) return default_val;
    double result = strtod(cstr, &end);
    double ret = (end != cstr) ? result : default_val;
    free(cstr);
    return ret;
}

size_t ini_section_count(ini_t *ini) {
    return ini ? ini->section_count : 0;
}

const char *ini_section_name(ini_t *ini, size_t index) {
    if (!ini) return NULL;

    size_t i = 0;
    for (ini_section_t *s = ini->sections; s; s = s->next, i++) {
        if (i == index) return s->name;
    }
    return NULL;
}

vstr ini_section_name_v(ini_t *ini, size_t index) {
    if (!ini) return vstr_from_buf(NULL, 0);

    size_t i = 0;
    for (ini_section_t *s = ini->sections; s; s = s->next, i++) {
        if (i == index) return tstr_to_v(s->name);
    }
    return vstr_from_buf(NULL, 0);
}

size_t ini_key_count(ini_t *ini, const char *section) {
    if (!ini) return 0;

    ini_section_t *s = ini_find_section(ini, section);
    if (!s) return 0;

    size_t count = 0;
    for (ini_entry_t *e = s->entries; e; e = e->next) {
        count++;
    }
    return count;
}

size_t ini_key_count_v(ini_t *ini, vstr section) {
    if (!ini) return 0;

    ini_section_t *s = ini_find_section_v(ini, section);
    if (!s) return 0;

    size_t count = 0;
    for (ini_entry_t *e = s->entries; e; e = e->next) {
        count++;
    }
    return count;
}

const char *ini_key_name(ini_t *ini, const char *section, size_t index) {
    if (!ini) return NULL;

    ini_section_t *s = ini_find_section(ini, section);
    if (!s) return NULL;

    size_t i = 0;
    for (ini_entry_t *e = s->entries; e; e = e->next, i++) {
        if (i == index) return e->key;
    }
    return NULL;
}

vstr ini_key_name_v(ini_t *ini, vstr section, size_t index) {
    if (!ini) return vstr_from_buf(NULL, 0);

    ini_section_t *s = ini_find_section_v(ini, section);
    if (!s) return vstr_from_buf(NULL, 0);

    size_t i = 0;
    for (ini_entry_t *e = s->entries; e; e = e->next, i++) {
        if (i == index) return tstr_to_v(e->key);
    }
    return vstr_from_buf(NULL, 0);
}
