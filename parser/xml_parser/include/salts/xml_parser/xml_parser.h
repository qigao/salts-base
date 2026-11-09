#ifndef SALTS_XML_PARSER_H
#define SALTS_XML_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <salts/query_vm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_XML_DIAGNOSTIC_CAPACITY 256u

typedef enum xml_status {
    SALTS_XML_OK = 0,
    SALTS_XML_INVALID_ARGUMENT,
    SALTS_XML_LIMIT_EXCEEDED,
    SALTS_XML_ALLOCATION_FAILED,
    SALTS_XML_EMBEDDED_NUL,
    SALTS_XML_UNSUPPORTED,
    SALTS_XML_MALFORMED
} xml_status;

typedef enum xml_node_kind {
    SALTS_XML_ELEMENT = 0,
    SALTS_XML_TEXT,
    SALTS_XML_COMMENT,
    SALTS_XML_PROCESSING_INSTRUCTION,
    SALTS_XML_ATTRIBUTE,
    SALTS_XML_DOCUMENT,
    SALTS_XML_NAMESPACE,
    SALTS_XML_XML_HEADER,
    SALTS_XML_DTD,
    SALTS_XML_INVALID_NODE
} xml_node_kind;

typedef struct xml_location {
    /** Zero-based byte offset in the original UTF-8 input. */
    size_t byte_offset;
    /** One-based source line. */
    uint32_t line;
    /** One-based byte column. */
    uint32_t column;
} xml_location;

typedef struct xml_string_view {
    const char *data;
    size_t size;
} xml_string_view;

typedef struct xml_limits {
    size_t max_input_bytes;
    /** All retained parser nodes, including declarations and DTD nodes. */
    size_t max_nodes;
    /** Element, namespace, and XML declaration attributes. */
    size_t max_attributes;
    size_t max_depth;
    /** Bytes retained by names and values in the private parser DOM. */
    size_t max_retained_string_bytes;
} xml_limits;

typedef struct xml_diagnostic {
    xml_status status;
    xml_location location;
    char message[SALTS_XML_DIAGNOSTIC_CAPACITY];
} xml_diagnostic;

typedef struct xml_document {
    void *impl;
} xml_document;

/**
 * Borrowed immutable node handle. It and every returned string view are valid
 * only while the owning document remains alive.
 */
typedef struct xml_node {
    const void *impl;
} xml_node;

/** Borrowed immutable attribute handle with the same document lifetime. */
typedef struct xml_attribute {
    const void *impl;
} xml_attribute;

/** Owned array of borrowed nodes. Destroy the list before its document. */
typedef struct xml_node_list {
    void *items;
    size_t size;
} xml_node_list;

/** Return the bounded defaults used when parse receives a NULL limits pointer. */
xml_limits xml_default_limits(void);

/**
 * Parse exactly input_size bytes and atomically publish an owning document.
 * `out` must be zero-initialized. Input bytes are copied. Failure leaves an
 * empty output and writes the first caller-owned diagnostic when provided.
 */
xml_status xml_parse(xml_document *out,
                                 const char *input,
                                 size_t input_size,
                                 const xml_limits *limits,
                                 xml_diagnostic *diagnostic);

/** Destroy a document after all borrowed nodes/views are quiescent. */
void xml_document_destroy(xml_document *document);

/**
 * Create an owning mutable document with one root element.
 * `out` must be zero-initialized. The document is single-owner and must not be
 * copied. Mutation and destruction require all readers to be quiescent.
 */
xml_status xml_document_create(xml_document *out,
                                           const char *root_name);

/** Append a new element. Existing handles remain valid; borrowed views may not. */
xml_status xml_node_add_element(xml_node parent,
                                            const char *name,
                                            xml_node *out);

/** Append escaped text content. Existing borrowed string views may be invalidated. */
xml_status xml_node_set_text(xml_node node,
                                         const char *text);

/** Return a malloc-owned complete document string. Free with xml_owned_string_free(). */
char *xml_document_serialize(const xml_document *document,
                                   size_t *out_size);
void xml_owned_string_free(char *string);

xml_node xml_document_root(const xml_document *document);
xml_node_kind xml_node_type(xml_node node);
xml_location xml_node_location(xml_node node);
xml_string_view xml_node_qualified_name(xml_node node);
xml_string_view xml_node_local_name(xml_node node);
xml_string_view xml_node_namespace_uri(xml_node node);
xml_string_view xml_node_value(xml_node node);
size_t xml_node_child_count(xml_node node);
xml_node xml_node_child_at(xml_node node, size_t index);
size_t xml_node_attribute_count(xml_node node);
xml_attribute xml_node_attribute_at(xml_node node,
                                                 size_t index);

/**
 * Serialize every child of one element as a compact UTF-8 XML fragment.
 * Text, element, comment, and processing-instruction order is preserved,
 * including whitespace-only text hidden by the semantic child iterator.
 *
 * Pass NULL/0 for output/output_capacity to measure. On success out_size is
 * the byte count excluding NUL. A provided output requires out_size + 1 bytes.
 * max_bytes is a mandatory hard bound. The facade owns all temporary storage.
 */
xml_status xml_serialize_children(
    xml_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size);

xml_location xml_attribute_location(xml_attribute attribute);
xml_string_view xml_attribute_qualified_name(
    xml_attribute attribute);
xml_string_view xml_attribute_local_name(
    xml_attribute attribute);
xml_string_view xml_attribute_namespace_uri(
    xml_attribute attribute);
xml_string_view xml_attribute_value(xml_attribute attribute);

/** Return the first borrowed node matching the legacy element-query syntax. */
xml_node xml_node_find(xml_node root, const char *query);

/**
 * Populate a zero-initialized owned list with borrowed matching nodes.
 * Destroy the list before destroying or mutating its document.
 */
xml_status xml_node_find_all(xml_node root, const char *query,
                                         xml_node_list *out);
/** Return malloc-owned aggregate node text. Free with xml_owned_string_free(). */
char *xml_node_text_dup(xml_node node);

/**
 * Execute XPath and populate a zero-initialized owned list of borrowed nodes.
 * XPath evaluation currently requires process-wide external serialization;
 * concurrent XPath calls are unsupported by the private engine.
 */
qvm_status_t xml_document_xpath_query(const xml_document *document,
                                            const char *xpath,
                                            xml_node_list *out,
                                            const qvm_limits_t *limits,
                                            qvm_diagnostic_t *diagnostic);
size_t xml_node_list_size(const xml_node_list *list);
xml_node xml_node_list_at(const xml_node_list *list,
                                      size_t index);
void xml_node_list_destroy(xml_node_list *list);

xml_string_view xml_node_display_name(xml_node node);
xml_string_view xml_node_text_view(xml_node node);
/**
 * Return non-zero when the text selected by xml_node_text_view() came
 * from ordinary XML text containing an entity/character reference.
 *
 * CDATA returns zero even when its literal bytes contain '&...;'. This lets
 * adapters decode logical XML text without guessing private parser node flags.
 */
int xml_node_text_has_entity_reference(xml_node node);
char *xml_node_serialize(xml_node node, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_XML_PARSER_H */
