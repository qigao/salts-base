#ifndef CYAML_JSON_ADAPTER_H
#define CYAML_JSON_ADAPTER_H

#include "cyaml.h"
#include "json_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a json_parser DOM into an independently owned CYAML document.
 * JSON numbers retain the source DOM's double precision, not their input text.
 */
cyaml_doc_t* cyaml_doc_from_json_value(const json_value_t* value);

/* Parse JSON with json_parser, then convert it into an owned CYAML document. */
cyaml_doc_t* cyaml_doc_from_json(const char* json, size_t len);

/*
 * Convert a CYAML document into an independently owned json_parser DOM.
 * Returns NULL when YAML semantics cannot be represented without loss, such
 * as non-string keys, duplicate keys, cyclic aliases, non-finite numbers,
 * unknown tags, or integers outside the exact JSON DOM double range.
 */
json_value_t* json_value_from_cyaml(const cyaml_doc_t* doc);

/* Convert one node from a CYAML document into an owned json_parser DOM. */
json_value_t* json_value_from_cyaml_node(const cyaml_doc_t* doc,
    const cyaml_node_t* node);

#ifdef __cplusplus
}
#endif

#endif
