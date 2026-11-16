#include <xml_parser/xml_parser.h>

#include "core/cxdefs.h"
#include "core/cxlist.h"
#include "core/cxmset.h"
#include "core/cxstr.h"
#include "core/cxtable.h"
#include "query/cxqapi.h"
#include "xml/cxparser.h"
#include "xml/cxprinter.h"
#include "xpath/cxxpeval.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SALTS_XML_DEFAULT_MAX_INPUT_BYTES (16u * 1024u * 1024u)
#define SALTS_XML_DEFAULT_MAX_NODES 1048576u
#define SALTS_XML_DEFAULT_MAX_ATTRIBUTES 1048576u
#define SALTS_XML_DEFAULT_MAX_DEPTH 256u
#define SALTS_XML_DEFAULT_MAX_RETAINED_STRING_BYTES (32u * 1024u * 1024u)

typedef struct xml_document_impl {
    char *input;
    size_t input_size;
    cxml_root_node *root;
} xml_document_impl;

typedef struct xml_counts {
    size_t nodes;
    size_t attributes;
    size_t retained_strings;
} xml_counts;

static xml_string_view empty_view(void) {
    const xml_string_view view = {NULL, 0u};
    return view;
}

static xml_string_view literal_view(const char *value) {
    const xml_string_view view = {
        value, value != NULL ? strlen(value) : 0u};
    return view;
}

static xml_location empty_location(void) {
    const xml_location location = {0u, 0u, 0u};
    return location;
}

static xml_location convert_location(cxml_source_location source) {
    const xml_location location = {
        source.byte_offset, (uint32_t)source.line, (uint32_t)source.column};
    return location;
}

static void clear_diagnostic(xml_diagnostic *diagnostic) {
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
}

static xml_status fail(xml_diagnostic *diagnostic,
                             xml_status status,
                             xml_location location,
                             const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        if (message != NULL) {
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
        }
    }
    return status;
}

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static xml_location location_at(const char *input, const char *position) {
    xml_location location = {0u, 1u, 1u};
    const char *cursor;

    location.byte_offset = (size_t)(position - input);
    for (cursor = input; cursor < position; ++cursor) {
        if (*cursor == '\n') {
            ++location.line;
            location.column = 1u;
        } else {
            ++location.column;
        }
    }
    return location;
}

static const char *find_sequence(const char *cursor, const char *end,
                                 const char *sequence, size_t sequence_size) {
    while ((size_t)(end - cursor) >= sequence_size) {
        if (memcmp(cursor, sequence, sequence_size) == 0) return cursor;
        ++cursor;
    }
    return NULL;
}

static xml_status preflight_depth(const char *input, size_t input_size,
                                        size_t max_depth,
                                        xml_diagnostic *diagnostic) {
    const char *cursor = input;
    const char *const end = input + input_size;
    size_t depth = 0u;

    while (cursor < end) {
        const char *tag_end;
        bool closing = false;
        bool self_closing = false;
        char quote = '\0';

        if (*cursor != '<') {
            ++cursor;
            continue;
        }
        if ((size_t)(end - cursor) >= 4u &&
            memcmp(cursor, "<!--", 4u) == 0) {
            tag_end = find_sequence(cursor + 4, end, "-->", 3u);
            if (tag_end == NULL) return SALTS_XML_OK;
            cursor = tag_end + 3;
            continue;
        }
        if ((size_t)(end - cursor) >= 9u &&
            memcmp(cursor, "<![CDATA[", 9u) == 0) {
            tag_end = find_sequence(cursor + 9, end, "]]>", 3u);
            if (tag_end == NULL) return SALTS_XML_OK;
            cursor = tag_end + 3;
            continue;
        }
        if ((size_t)(end - cursor) >= 9u &&
            memcmp(cursor, "<!DOCTYPE", 9u) == 0) {
            return fail(diagnostic, SALTS_XML_UNSUPPORTED,
                        location_at(input, cursor),
                        "DTD declarations are not supported");
        }
        if ((size_t)(end - cursor) >= 2u && cursor[1] == '?') {
            tag_end = find_sequence(cursor + 2, end, "?>", 2u);
            if (tag_end == NULL) return SALTS_XML_OK;
            cursor = tag_end + 2;
            continue;
        }
        closing = (size_t)(end - cursor) >= 2u && cursor[1] == '/';
        tag_end = cursor + (closing ? 2 : 1);
        while (tag_end < end) {
            if (quote != '\0') {
                if (*tag_end == quote) quote = '\0';
            } else if (*tag_end == '\'' || *tag_end == '"') {
                quote = *tag_end;
            } else if (*tag_end == '>') {
                break;
            }
            ++tag_end;
        }
        if (tag_end == end) return SALTS_XML_OK;
        if (closing) {
            if (depth > 0u) --depth;
        } else {
            const char *before_end = tag_end;
            while (before_end > cursor &&
                   isspace((unsigned char)before_end[-1])) {
                --before_end;
            }
            self_closing = before_end > cursor && before_end[-1] == '/';
            if (!self_closing) {
                ++depth;
                if (depth > max_depth) {
                    return fail(diagnostic, SALTS_XML_LIMIT_EXCEEDED,
                                location_at(input, cursor),
                                "XML nesting depth exceeds max_depth");
                }
            }
        }
        cursor = tag_end + 1;
    }
    return SALTS_XML_OK;
}

static bool is_whitespace_text(const void *node) {
    const cxml_text_node *text;
    const char *value;
    unsigned int index;
    unsigned int size;

    if (node == NULL || _cxml_node_type(node) != CXML_TEXT_NODE) return false;
    text = (const cxml_text_node *)node;
    value = cxml_string_as_raw((cxml_string *)&text->value);
    size = cxml_string_len((cxml_string *)&text->value);
    for (index = 0u; index < size; ++index) {
        if (!isspace((unsigned char)value[index])) return false;
    }
    return true;
}

static bool count_string(size_t *total, const cxml_string *string) {
    return checked_add(*total,
                       (size_t)cxml_string_len((cxml_string *)string), total);
}

static bool collect_counts(const cxml_elem_node *element, size_t depth,
                           const xml_limits *limits,
                           xml_counts *counts,
                           xml_location *limit_location) {
    struct _cxml_list__node *cursor;

    if (element == NULL || depth > limits->max_depth ||
        !checked_add(counts->nodes, 1u, &counts->nodes) ||
        counts->nodes > limits->max_nodes ||
        !count_string(&counts->retained_strings, &element->name.qname)) {
        if (element != NULL) *limit_location = convert_location(element->source);
        return false;
    }

    if (element->namespaces != NULL) {
        for (cursor = element->namespaces->head; cursor != NULL;
             cursor = cursor->next) {
            const cxml_ns_node *ns = (const cxml_ns_node *)cursor->item;
            if (!checked_add(counts->attributes, 1u, &counts->attributes) ||
                !count_string(&counts->retained_strings, &ns->prefix) ||
                !count_string(&counts->retained_strings, &ns->uri)) {
                *limit_location = convert_location(ns->source);
                return false;
            }
        }
    }
    if (element->attributes != NULL) {
        for (cursor = element->attributes->keys.head; cursor != NULL;
             cursor = cursor->next) {
            const char *key = (const char *)cursor->item;
            const cxml_attr_node *attribute =
                (const cxml_attr_node *)cxml_table_get(element->attributes, key);
            if (!checked_add(counts->attributes, 1u, &counts->attributes) ||
                !count_string(&counts->retained_strings,
                              &attribute->name.qname) ||
                !count_string(&counts->retained_strings, &attribute->value)) {
                *limit_location = convert_location(attribute->source);
                return false;
            }
        }
    }
    if (counts->attributes > limits->max_attributes ||
        counts->retained_strings > limits->max_retained_string_bytes) {
        *limit_location = convert_location(element->source);
        return false;
    }

    for (cursor = element->children.head; cursor != NULL;
         cursor = cursor->next) {
        const void *child = cursor->item;
        const _cxml_node_t type = _cxml_node_type(child);
        if (type == CXML_ELEM_NODE) {
            if (!collect_counts((const cxml_elem_node *)child, depth + 1u,
                                limits, counts, limit_location)) {
                return false;
            }
        } else if (type == CXML_TEXT_NODE) {
            const cxml_text_node *text = (const cxml_text_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &text->value)) {
                *limit_location = convert_location(text->source);
                return false;
            }
        } else if (type == CXML_COMM_NODE) {
            const cxml_comm_node *comment = (const cxml_comm_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &comment->value)) {
                *limit_location = convert_location(comment->source);
                return false;
            }
        } else if (type == CXML_PI_NODE) {
            const cxml_pi_node *pi = (const cxml_pi_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &pi->target) ||
                !count_string(&counts->retained_strings, &pi->value)) {
                *limit_location = convert_location(pi->source);
                return false;
            }
        }
        if (counts->nodes > limits->max_nodes ||
            counts->retained_strings > limits->max_retained_string_bytes) {
            *limit_location = convert_location(element->source);
            return false;
        }
    }
    return true;
}

static xml_status check_mutation_budget(xml_node node,
                                              size_t added_nodes,
                                              size_t added_string_bytes,
                                              bool adds_element) {
    xml_limits limits = xml_default_limits();
    xml_counts counts = {0u, 0u, 0u};
    xml_location location = empty_location();
    void *cursor = (void *)node.impl;
    cxml_root_node *root;
    size_t depth = 0u;

    while (cursor != NULL && _cxml_node_type(cursor) != CXML_ROOT_NODE) {
        if (_cxml_node_type(cursor) == CXML_ELEM_NODE) ++depth;
        cursor = cxml_parent(cursor);
    }
    if (cursor == NULL) return SALTS_XML_INVALID_ARGUMENT;
    root = (cxml_root_node *)cursor;
    if (root->root_element == NULL ||
        !collect_counts(root->root_element, 1u, &limits, &counts, &location))
        return SALTS_XML_LIMIT_EXCEEDED;
    if (adds_element && depth >= limits.max_depth)
        return SALTS_XML_LIMIT_EXCEEDED;
    if (!checked_add(counts.nodes, added_nodes, &counts.nodes) ||
        counts.nodes > limits.max_nodes ||
        !checked_add(counts.retained_strings, added_string_bytes,
                     &counts.retained_strings) ||
        counts.retained_strings > limits.max_retained_string_bytes)
        return SALTS_XML_LIMIT_EXCEEDED;
    return SALTS_XML_OK;
}

xml_limits xml_default_limits(void) {
    const xml_limits limits = {
        SALTS_XML_DEFAULT_MAX_INPUT_BYTES,
        SALTS_XML_DEFAULT_MAX_NODES,
        SALTS_XML_DEFAULT_MAX_ATTRIBUTES,
        SALTS_XML_DEFAULT_MAX_DEPTH,
        SALTS_XML_DEFAULT_MAX_RETAINED_STRING_BYTES};
    return limits;
}

xml_status xml_parse(xml_document *out,
                                 const char *input,
                                 size_t input_size,
                                 const xml_limits *limits_or_null,
                                 xml_diagnostic *diagnostic) {
    xml_limits limits;
    xml_document_impl *impl = NULL;
    xml_counts counts = {0u, 0u, 0u};
    xml_location limit_location = {0u, 1u, 1u};
    cxml_parse_error parse_error;
    cxml_parse_limits parse_limits;
    xml_status status;
    const char *nul;

    clear_diagnostic(diagnostic);
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u) {
        return fail(diagnostic, SALTS_XML_INVALID_ARGUMENT, empty_location(),
                    "out must be empty and input must be nonempty");
    }
    limits = limits_or_null != NULL ? *limits_or_null
                                    : xml_default_limits();
    if (limits.max_input_bytes == 0u || limits.max_nodes == 0u ||
        limits.max_attributes == 0u || limits.max_depth == 0u ||
        limits.max_retained_string_bytes == 0u) {
        return fail(diagnostic, SALTS_XML_INVALID_ARGUMENT, empty_location(),
                    "all XML limits must be positive");
    }
    if (input_size > limits.max_input_bytes) {
        return fail(diagnostic, SALTS_XML_LIMIT_EXCEEDED,
                    location_at(input, input + limits.max_input_bytes),
                    "XML input exceeds max_input_bytes");
    }
    nul = (const char *)memchr(input, '\0', input_size);
    if (nul != NULL) {
        return fail(diagnostic, SALTS_XML_EMBEDDED_NUL,
                    location_at(input, nul),
                    "embedded NUL is not valid XML input");
    }
    status = preflight_depth(input, input_size, limits.max_depth, diagnostic);
    if (status != SALTS_XML_OK) return status;

    impl = (xml_document_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        return fail(diagnostic, SALTS_XML_ALLOCATION_FAILED, empty_location(),
                    "unable to allocate XML document");
    }
    if (input_size == SIZE_MAX) {
        free(impl);
        return fail(diagnostic, SALTS_XML_LIMIT_EXCEEDED, empty_location(),
                    "XML input size cannot be terminated safely");
    }
    impl->input = (char *)malloc(input_size + 1u);
    if (impl->input == NULL) {
        free(impl);
        return fail(diagnostic, SALTS_XML_ALLOCATION_FAILED, empty_location(),
                    "unable to copy XML input");
    }
    memcpy(impl->input, input, input_size);
    impl->input[input_size] = '\0';
    impl->input_size = input_size;
    memset(&parse_error, 0, sizeof(parse_error));
    parse_limits.max_nodes = limits.max_nodes;
    parse_limits.max_attributes = limits.max_attributes;
    parse_limits.max_retained_string_bytes =
        limits.max_retained_string_bytes;
    impl->root = cxml_parse_xml_limited(impl->input, &parse_limits,
                                        &parse_error);
    if (impl->root == NULL) {
        xml_location location = convert_location(parse_error.source);
        const char *message = parse_error.message[0] != '\0'
                                  ? parse_error.message
                                  : "malformed XML";
        const xml_status parse_status =
            parse_error.kind == CXML_PARSE_ERROR_ALLOCATION
                ? SALTS_XML_ALLOCATION_FAILED
                : parse_error.kind == CXML_PARSE_ERROR_LIMIT
                      ? SALTS_XML_LIMIT_EXCEEDED
                      : SALTS_XML_MALFORMED;
        free(impl->input);
        free(impl);
        return fail(diagnostic, parse_status, location, message);
    }
    if (!impl->root->is_well_formed || impl->root->root_element == NULL) {
        cxml_root_node_free(impl->root);
        free(impl->input);
        free(impl);
        return fail(diagnostic, SALTS_XML_MALFORMED, limit_location,
                    "XML document is not well formed");
    }
    if (!collect_counts(impl->root->root_element, 1u, &limits, &counts,
                        &limit_location)) {
        cxml_root_node_free(impl->root);
        free(impl->input);
        free(impl);
        return fail(diagnostic, SALTS_XML_LIMIT_EXCEEDED, limit_location,
                    "XML document exceeds a configured structural limit");
    }
    out->impl = impl;
    return SALTS_XML_OK;
}

void xml_document_destroy(xml_document *document) {
    xml_document_impl *impl;
    if (document == NULL || document->impl == NULL) return;
    impl = (xml_document_impl *)document->impl;
    cxml_root_node_free(impl->root);
    free(impl->input);
    free(impl);
    document->impl = NULL;
}

xml_node xml_document_root(const xml_document *document) {
    xml_node node = {NULL};
    if (document != NULL && document->impl != NULL) {
        const xml_document_impl *impl =
            (const xml_document_impl *)document->impl;
        node.impl = impl->root->root_element;
    }
    return node;
}

xml_status xml_serialize_children(
    xml_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size) {
    const cxml_elem_node *element;
    char *serialized;
    size_t serialized_size;
    size_t required_capacity;

    if (xml_node_type(node) != SALTS_XML_ELEMENT || out_size == NULL ||
        max_bytes == 0u || (output == NULL) != (output_capacity == 0u)) {
        return SALTS_XML_INVALID_ARGUMENT;
    }
    element = (const cxml_elem_node *)node.impl;
    if (element->children.head == NULL) {
        *out_size = 0u;
        if (output != NULL) output[0] = '\0';
        return SALTS_XML_OK;
    }

    serialized = cxml_element_children_to_xml_rstring(
        (cxml_element_node *)element);
    if (serialized == NULL) return SALTS_XML_ALLOCATION_FAILED;
    serialized_size = strlen(serialized);
    *out_size = serialized_size;
    if (serialized_size > max_bytes ||
        !checked_add(serialized_size, 1u, &required_capacity) ||
        (output != NULL && output_capacity < required_capacity)) {
        free(serialized);
        return SALTS_XML_LIMIT_EXCEEDED;
    }
    if (output != NULL) memcpy(output, serialized, required_capacity);
    free(serialized);
    return SALTS_XML_OK;
}

xml_node_kind xml_node_type(xml_node node) {
    if (node.impl == NULL) return SALTS_XML_INVALID_NODE;
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE: return SALTS_XML_ELEMENT;
        case CXML_TEXT_NODE: return SALTS_XML_TEXT;
        case CXML_COMM_NODE: return SALTS_XML_COMMENT;
        case CXML_PI_NODE: return SALTS_XML_PROCESSING_INSTRUCTION;
        case CXML_ATTR_NODE: return SALTS_XML_ATTRIBUTE;
        case CXML_ROOT_NODE: return SALTS_XML_DOCUMENT;
        case CXML_NS_NODE: return SALTS_XML_NAMESPACE;
        case CXML_XHDR_NODE: return SALTS_XML_XML_HEADER;
        case CXML_DTD_NODE: return SALTS_XML_DTD;
        default: return SALTS_XML_INVALID_NODE;
    }
}

xml_location xml_node_location(xml_node node) {
    if (node.impl == NULL) return empty_location();
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE:
            return convert_location(((const cxml_elem_node *)node.impl)->source);
        case CXML_TEXT_NODE:
            return convert_location(((const cxml_text_node *)node.impl)->source);
        case CXML_COMM_NODE:
            return convert_location(((const cxml_comm_node *)node.impl)->source);
        case CXML_PI_NODE:
            return convert_location(((const cxml_pi_node *)node.impl)->source);
        default: return empty_location();
    }
}

static xml_string_view string_view(const cxml_string *string) {
    xml_string_view view;
    view.data = cxml_string_as_raw((cxml_string *)string);
    view.size = (size_t)cxml_string_len((cxml_string *)string);
    return view;
}

xml_string_view xml_node_qualified_name(xml_node node) {
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return empty_view();
    return string_view(&((const cxml_elem_node *)node.impl)->name.qname);
}

xml_string_view xml_node_local_name(xml_node node) {
    xml_string_view view = empty_view();
    const cxml_elem_node *element;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return view;
    element = (const cxml_elem_node *)node.impl;
    view.data = element->name.lname;
    view.size = (size_t)element->name.lname_len;
    return view;
}

xml_string_view xml_node_namespace_uri(xml_node node) {
    const cxml_elem_node *element;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return empty_view();
    element = (const cxml_elem_node *)node.impl;
    return element->namespace != NULL ? string_view(&element->namespace->uri)
                                      : empty_view();
}

xml_string_view xml_node_value(xml_node node) {
    if (node.impl == NULL) return empty_view();
    switch (_cxml_node_type(node.impl)) {
        case CXML_TEXT_NODE:
            return string_view(&((const cxml_text_node *)node.impl)->value);
        case CXML_COMM_NODE:
            return string_view(&((const cxml_comm_node *)node.impl)->value);
        case CXML_PI_NODE:
            return string_view(&((const cxml_pi_node *)node.impl)->value);
        default: return empty_view();
    }
}

static bool visible_child(const void *child) {
    if (child == NULL || is_whitespace_text(child)) return false;
    switch (_cxml_node_type(child)) {
        case CXML_ELEM_NODE:
        case CXML_TEXT_NODE:
        case CXML_COMM_NODE:
        case CXML_PI_NODE: return true;
        default: return false;
    }
}

size_t xml_node_child_count(xml_node node) {
    const cxml_elem_node *element;
    const struct _cxml_list__node *cursor;
    size_t count = 0u;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return 0u;
    element = (const cxml_elem_node *)node.impl;
    for (cursor = element->children.head; cursor != NULL; cursor = cursor->next) {
        if (visible_child(cursor->item)) ++count;
    }
    return count;
}

xml_node xml_node_child_at(xml_node node, size_t index) {
    xml_node result = {NULL};
    const cxml_elem_node *element;
    const struct _cxml_list__node *cursor;
    size_t current = 0u;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return result;
    element = (const cxml_elem_node *)node.impl;
    for (cursor = element->children.head; cursor != NULL; cursor = cursor->next) {
        if (!visible_child(cursor->item)) continue;
        if (current == index) {
            result.impl = cursor->item;
            return result;
        }
        ++current;
    }
    return result;
}

size_t xml_node_attribute_count(xml_node node) {
    const cxml_elem_node *element;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return 0u;
    element = (const cxml_elem_node *)node.impl;
    return element->attributes != NULL
               ? (size_t)cxml_table_size(element->attributes)
               : 0u;
}

xml_attribute xml_node_attribute_at(xml_node node,
                                                 size_t index) {
    xml_attribute result = {NULL};
    const cxml_elem_node *element;
    const char *key;
    if (xml_node_type(node) != SALTS_XML_ELEMENT) return result;
    element = (const cxml_elem_node *)node.impl;
    if (element->attributes == NULL || index >= (size_t)element->attributes->count)
        return result;
    key = (const char *)cxml_list_get(&element->attributes->keys, (int)index);
    result.impl = cxml_table_get(element->attributes, key);
    return result;
}

xml_location xml_attribute_location(xml_attribute attribute) {
    return attribute.impl != NULL
               ? convert_location(((const cxml_attr_node *)attribute.impl)->source)
               : empty_location();
}

xml_string_view xml_attribute_qualified_name(
    xml_attribute attribute) {
    return attribute.impl != NULL
               ? string_view(&((const cxml_attr_node *)attribute.impl)->name.qname)
               : empty_view();
}

xml_string_view xml_attribute_local_name(
    xml_attribute attribute) {
    xml_string_view view = empty_view();
    const cxml_attr_node *node;
    if (attribute.impl == NULL) return view;
    node = (const cxml_attr_node *)attribute.impl;
    view.data = node->name.lname;
    view.size = (size_t)node->name.lname_len;
    return view;
}

xml_string_view xml_attribute_namespace_uri(
    xml_attribute attribute) {
    const cxml_attr_node *node;
    if (attribute.impl == NULL) return empty_view();
    node = (const cxml_attr_node *)attribute.impl;
    return node->namespace != NULL ? string_view(&node->namespace->uri)
                                   : empty_view();
}

xml_string_view xml_attribute_value(xml_attribute attribute) {
    return attribute.impl != NULL
               ? string_view(&((const cxml_attr_node *)attribute.impl)->value)
               : empty_view();
}

xml_status xml_document_create(xml_document *out,
                                           const char *root_name) {
    xml_document_impl *impl;
    cxml_elem_node *root;
    const xml_limits limits = xml_default_limits();

    if (out == NULL || out->impl != NULL || root_name == NULL || root_name[0] == '\0')
        return SALTS_XML_INVALID_ARGUMENT;
    if (strlen(root_name) > limits.max_retained_string_bytes)
        return SALTS_XML_LIMIT_EXCEEDED;
    impl = (xml_document_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_XML_ALLOCATION_FAILED;
    impl->root = (cxml_root_node *)cxml_create_node(CXML_ROOT_NODE);
    root = (cxml_elem_node *)cxml_create_node(CXML_ELEM_NODE);
    if (impl->root == NULL || root == NULL ||
        !cxml_set_name(root, NULL, root_name) || !cxml_add_child(impl->root, root)) {
        if (root != NULL && (impl->root == NULL || root->parent != impl->root))
            cxml_free_element_node(root);
        if (impl->root != NULL) cxml_free_root_node(impl->root);
        free(impl);
        return SALTS_XML_ALLOCATION_FAILED;
    }
    out->impl = impl;
    return SALTS_XML_OK;
}

xml_status xml_node_add_element(xml_node parent,
                                            const char *name,
                                            xml_node *out) {
    cxml_elem_node *element;
    xml_status status;
    if (parent.impl == NULL || name == NULL || name[0] == '\0' || out == NULL ||
        out->impl != NULL)
        return SALTS_XML_INVALID_ARGUMENT;
    if (_cxml_node_type(parent.impl) != CXML_ELEM_NODE)
        return SALTS_XML_INVALID_ARGUMENT;
    status = check_mutation_budget(parent, 1u, strlen(name), true);
    if (status != SALTS_XML_OK) return status;
    element = (cxml_elem_node *)cxml_create_node(CXML_ELEM_NODE);
    if (element == NULL || !cxml_set_name(element, NULL, name) ||
        !cxml_add_child((void *)parent.impl, element)) {
        if (element != NULL) cxml_free_element_node(element);
        return SALTS_XML_ALLOCATION_FAILED;
    }
    out->impl = element;
    return SALTS_XML_OK;
}

xml_status xml_node_set_text(xml_node node,
                                         const char *text) {
    cxml_text_node *child;
    xml_status status;
    if (node.impl == NULL || text == NULL ||
        _cxml_node_type(node.impl) != CXML_ELEM_NODE)
        return SALTS_XML_INVALID_ARGUMENT;
    status = check_mutation_budget(node, 1u, strlen(text), false);
    if (status != SALTS_XML_OK) return status;
    child = (cxml_text_node *)cxml_create_node(CXML_TEXT_NODE);
    if (child == NULL || !cxml_set_text_value(child, text, false)) {
        if (child != NULL) cxml_free_text_node(child);
        return SALTS_XML_ALLOCATION_FAILED;
    }
    child->has_entity = true;
    if (!cxml_add_child((void *)node.impl, child)) {
        cxml_free_text_node(child);
        return SALTS_XML_ALLOCATION_FAILED;
    }
    return SALTS_XML_OK;
}

char *xml_document_serialize(const xml_document *document,
                                   size_t *out_size) {
    char *serialized;
    const xml_document_impl *impl;
    if (out_size != NULL) *out_size = 0u;
    if (document == NULL || document->impl == NULL) return NULL;
    impl = (const xml_document_impl *)document->impl;
    serialized = cxml_document_to_xml_rstring(impl->root);
    if (serialized != NULL && out_size != NULL) *out_size = strlen(serialized);
    return serialized;
}

void xml_owned_string_free(char *string) { free(string); }

static xml_status node_list_allocate(xml_node_list *out,
                                           size_t size) {
    if (out == NULL || out->items != NULL || out->size != 0u)
        return SALTS_XML_INVALID_ARGUMENT;
    if (size == 0u) return SALTS_XML_OK;
    if (size > SIZE_MAX / sizeof(xml_node)) return SALTS_XML_LIMIT_EXCEEDED;
    out->items = calloc(size, sizeof(xml_node));
    if (out->items == NULL) return SALTS_XML_ALLOCATION_FAILED;
    out->size = size;
    return SALTS_XML_OK;
}

xml_node xml_node_find(xml_node root, const char *query) {
    xml_node result = {NULL};
    if (root.impl != NULL && query != NULL)
        result.impl = cxml_find((void *)root.impl, query);
    return result;
}

xml_status xml_node_find_all(xml_node root, const char *query,
                                         xml_node_list *out) {
    cxml_list matches = new_cxml_list();
    xml_status status;
    size_t index = 0u;

    if (root.impl == NULL || query == NULL || out == NULL || out->items != NULL || out->size != 0u)
        return SALTS_XML_INVALID_ARGUMENT;
    cxml_find_all((void *)root.impl, query, &matches);
    status = node_list_allocate(out, (size_t)cxml_list_size(&matches));
    if (status == SALTS_XML_OK) {
        cxml_for_each(item, &matches) {
            ((xml_node *)out->items)[index++].impl = item;
        }
    }
    cxml_list_free(&matches);
    return status;
}

char *xml_node_text_dup(xml_node node) {
    if (node.impl == NULL) return NULL;
    if (_cxml_node_type(node.impl) == CXML_ROOT_NODE ||
        _cxml_node_type(node.impl) == CXML_ELEM_NODE)
        return cxml_text((void *)node.impl, NULL);
    {
        const xml_string_view value = xml_node_text_view(node);
        if (value.size == SIZE_MAX) return NULL;
        char *copy = (char *)malloc(value.size + 1u);
        if (copy == NULL) return NULL;
        if (value.size > 0u) memcpy(copy, value.data, value.size);
        copy[value.size] = '\0';
        return copy;
    }
}

static qvm_status_t xpath_fail(qvm_diagnostic_t *diagnostic,
                               qvm_status_t status, const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->instruction = QVM_NO_INSTRUCTION;
        diagnostic->opcode = QVM_NO_OPCODE;
        diagnostic->operand = QVM_NO_OPERAND;
        diagnostic->message = message;
    }
    return status;
}

qvm_status_t xml_document_xpath_query(
    const xml_document *document, const char *xpath,
    xml_node_list *out, const qvm_limits_t *limits,
    qvm_diagnostic_t *diagnostic) {
    const xml_document_impl *impl;
    cxml_set *matches = NULL;
    qvm_status_t status;
    int count;
    int index;

    if (document == NULL || document->impl == NULL || xpath == NULL || out == NULL ||
        out->items != NULL || out->size != 0u) {
        return xpath_fail(diagnostic, QVM_STATUS_INVALID_ARGUMENT,
                          "invalid XML XPath arguments");
    }
    impl = (const xml_document_impl *)document->impl;
    status = (qvm_status_t)cxml_xpath_ex(impl->root, xpath, &matches, limits, diagnostic);
    if (status != QVM_STATUS_OK) return status;
    count = matches != NULL ? cxml_set_size(matches) : 0;
    if (node_list_allocate(out, (size_t)count) != SALTS_XML_OK) {
        if (matches != NULL) {
            cxml_set_free(matches);
            free(matches);
        }
        return xpath_fail(diagnostic, QVM_STATUS_NO_MEMORY,
                          "unable to allocate XML XPath result handles");
    }
    for (index = 0; index < count; ++index)
        ((xml_node *)out->items)[index].impl = cxml_set_get(matches, index);
    if (matches != NULL) {
        cxml_set_free(matches);
        free(matches);
    }
    return QVM_STATUS_OK;
}

size_t xml_node_list_size(const xml_node_list *list) {
    return list != NULL ? list->size : 0u;
}

xml_node xml_node_list_at(const xml_node_list *list,
                                      size_t index) {
    xml_node result = {NULL};
    if (list != NULL && list->items != NULL && index < list->size)
        result = ((const xml_node *)list->items)[index];
    return result;
}

void xml_node_list_destroy(xml_node_list *list) {
    if (list == NULL) return;
    free(list->items);
    list->items = NULL;
    list->size = 0u;
}

xml_string_view xml_node_display_name(xml_node node) {
    if (node.impl == NULL) return empty_view();
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE: return string_view(&((const cxml_elem_node *)node.impl)->name.qname);
        case CXML_ATTR_NODE: return string_view(&((const cxml_attr_node *)node.impl)->name.qname);
        case CXML_ROOT_NODE: return string_view(&((const cxml_root_node *)node.impl)->name);
        case CXML_PI_NODE: return string_view(&((const cxml_pi_node *)node.impl)->target);
        case CXML_NS_NODE: {
            const cxml_ns_node *ns = (const cxml_ns_node *)node.impl;
            return ns->is_default ? literal_view("xmlns") : string_view(&ns->prefix);
        }
        default: return empty_view();
    }
}

xml_string_view xml_node_text_view(xml_node node) {
    if (node.impl == NULL) return empty_view();
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE: {
            const cxml_elem_node *element = (const cxml_elem_node *)node.impl;
            if (element->has_text && !cxml_list_is_empty((cxml_list *)&element->children)) {
                const cxml_text_node *text =
                    (const cxml_text_node *)cxml_list_get((cxml_list *)&element->children, 0);
                if (text != NULL && text->_type == CXML_TEXT_NODE) return string_view(&text->value);
            }
            return empty_view();
        }
        case CXML_TEXT_NODE: return string_view(&((const cxml_text_node *)node.impl)->value);
        case CXML_ATTR_NODE: return string_view(&((const cxml_attr_node *)node.impl)->value);
        case CXML_COMM_NODE: return string_view(&((const cxml_comm_node *)node.impl)->value);
        case CXML_PI_NODE: return string_view(&((const cxml_pi_node *)node.impl)->value);
        case CXML_NS_NODE: return string_view(&((const cxml_ns_node *)node.impl)->uri);
        case CXML_DTD_NODE: return string_view(&((const cxml_dtd_node *)node.impl)->value);
        default: return empty_view();
    }
}

int xml_node_text_has_entity_reference(xml_node node) {
    const cxml_text_node *text = NULL;
    if (node.impl == NULL) return 0;
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE: {
            const cxml_elem_node *element = (const cxml_elem_node *)node.impl;
            if (element->has_text &&
                !cxml_list_is_empty((cxml_list *)&element->children)) {
                const void *first =
                    cxml_list_get((cxml_list *)&element->children, 0);
                if (first != NULL && _cxml_node_type(first) == CXML_TEXT_NODE)
                    text = (const cxml_text_node *)first;
            }
            break;
        }
        case CXML_TEXT_NODE:
            text = (const cxml_text_node *)node.impl;
            break;
        default:
            break;
    }
    return text != NULL && text->has_entity && !text->is_cdata;
}

char *xml_node_serialize(xml_node node, size_t *out_size) {
    char *serialized;
    if (out_size != NULL) *out_size = 0u;
    if (node.impl == NULL) return NULL;
    serialized = cxml_node_to_rstring((void *)node.impl);
    if (serialized != NULL && out_size != NULL) *out_size = strlen(serialized);
    return serialized;
}
