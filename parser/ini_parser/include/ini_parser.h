#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <vstr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ini_t ini_t;

ini_t*      ini_parse(const char* content, size_t len);
ini_t*      ini_parse_file(const char* filename);
void        ini_free(ini_t* ini);

const char* ini_get(ini_t* ini, const char* section, const char* key);
vstr      ini_get_v(ini_t* ini, const char* section, const char* key);
vstr      ini_get_vv(ini_t* ini, vstr section, vstr key);

int         ini_get_int(ini_t* ini, const char* section, const char* key, int default_val);
bool        ini_get_bool(ini_t* ini, const char* section, const char* key, bool default_val);
double      ini_get_double(ini_t* ini, const char* section, const char* key, double default_val);

int         ini_get_int_v(ini_t* ini, vstr section, vstr key, int default_val);
bool        ini_get_bool_v(ini_t* ini, vstr section, vstr key, bool default_val);
double      ini_get_double_v(ini_t* ini, vstr section, vstr key, double default_val);

size_t      ini_section_count(ini_t* ini);
const char* ini_section_name(ini_t* ini, size_t index);
vstr      ini_section_name_v(ini_t* ini, size_t index);

size_t      ini_key_count(ini_t* ini, const char* section);
size_t      ini_key_count_v(ini_t* ini, vstr section);
const char* ini_key_name(ini_t* ini, const char* section, size_t index);
vstr      ini_key_name_v(ini_t* ini, vstr section, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* INI_PARSER_H */
