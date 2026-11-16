/*
 * Copyright © 2021 Jeremiah Ikosin
 * Distributed under the terms of the MIT license.
 */

#ifndef CXML_CXPARSER_H
#define CXML_CXPARSER_H

#include <setjmp.h>
#include "xml/cxlexer.h"
#include "core/cxstack.h"
#include "xml/cxscope.h"
#include "core/cxdefs.h"
#include "core/cxlrucache.h"

#define CXML_PARSE_ERROR_CAPACITY 256

typedef enum cxml_parse_error_kind {
    CXML_PARSE_ERROR_SYNTAX = 0,
    CXML_PARSE_ERROR_ALLOCATION,
    CXML_PARSE_ERROR_LIMIT
} cxml_parse_error_kind;

typedef struct cxml_parse_error {
    cxml_parse_error_kind kind;
    cxml_source_location source;
    char message[CXML_PARSE_ERROR_CAPACITY];
} cxml_parse_error;

typedef struct cxml_parse_limits {
    size_t max_nodes;
    size_t max_attributes;
    size_t max_retained_string_bytes;
} cxml_parse_limits;


typedef struct _cxml_parser{
    _cxml_token current_tok;
    _cxml_token prev_tok;
    cxml_xhdr_node *xml_header;
    // no special treatment for dtd, since non-validating
    cxml_dtd_node *xml_doctype;
    cxml_elem_node *root_element;
    cxml_root_node *root_node;
    bool has_header;
    bool has_dtd;
    // flag for when the root element is wrapped
    bool is_root_wrapped;
    // node position counter
    unsigned int pos_c;
    cxml_list errors;
    cxml_table attr_checker;
    // temporarily store attributes after they're being parsed for post-processing.
    cxml_list attr_list;
    _cxml_stack _cx_stack;
    _cxml_lexer cxlexer;
    char *err_msg;
    // config
    cxml_config cfg;
    // namespace scope lookup - for namespace scoping and resolution
    struct _cxml_scope_table *current_scope;
    // error recovery: longjmp target for parse errors (instead of exit)
    jmp_buf error_jmp;
    bool has_error;
    const char *source;
    size_t source_size;
    cxml_parse_limits limits;
    size_t node_count;
    size_t attribute_count;
    size_t retained_string_bytes;
    /* Node allocated by the current production but not yet owned by the DOM. */
    void *pending_node;
    /* Expanded attribute name not yet owned by attr_checker. */
    cxml_string pending_expanded_name;
    cxml_parse_error error;
}_cxml_parser;


void _cxml_parser_init(
        _cxml_parser *parser,
        const char *src,
        const char* file_name,
        bool stream);

cxml_root_node* create_root_node();

cxml_root_node* cxml_parse_xml(const char *src);

cxml_root_node* cxml_parse_xml_ex(const char *src, cxml_parse_error *error);

cxml_root_node* cxml_parse_xml_limited(const char *src,
                                       const cxml_parse_limits *limits,
                                       cxml_parse_error *error);

cxml_root_node* cxml_parse_xml_lazy(const char *file_name);

void _cxml_parser_free(_cxml_parser *cxparser);


#endif //CXML_CXPARSER_H
