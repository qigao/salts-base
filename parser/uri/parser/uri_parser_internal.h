#ifndef SALTS_URI_PARSER_INTERNAL_H
#define SALTS_URI_PARSER_INTERNAL_H

#include "uri_parser.h"

#include <stddef.h>

int uri_copy_substring_checked(uri_t *uri, const char *src, size_t start, size_t len, char *dest,
                               size_t dest_size);

#endif /* SALTS_URI_PARSER_INTERNAL_H */
