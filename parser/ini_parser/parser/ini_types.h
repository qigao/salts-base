/**
 * @file ini_types.h
 * @brief INI Parser Internal Types (shared between grammar and implementation)
 */

#ifndef INI_TYPES_H
#define INI_TYPES_H

#include <stddef.h>
#include "tstr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ini_entry_s {
    tstr key;
    tstr value;
    struct ini_entry_s *next;
} ini_entry_t;

typedef struct ini_section_s {
    tstr name;
    ini_entry_t *entries;
    ini_entry_t *entries_tail;
    struct ini_section_s *next;
} ini_section_t;

typedef struct {
    ini_section_t *sections;
    ini_section_t *sections_tail;
    ini_section_t *current;
    int error;
} ini_parse_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* INI_TYPES_H */
