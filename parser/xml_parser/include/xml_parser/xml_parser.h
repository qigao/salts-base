#ifndef SALTS_XML_PARSER_H
#define SALTS_XML_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <query_vm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_XML_DIAGNOSTIC_CAPACITY 256u

typedef enum salts_xml_status {
    SALTS_XML_OK = 0,
    SALTS_XML_INVALID_ARGUMENT,
    SALTS_XML_LIMIT_EXCEEDED,
    SALTS_XML_ALLOCATION_FAILED,
    SALTS_XML_EMBEDDED_NUL,
    SALTS_XML_UNSUPPORTED,
    SALTS_XML_MALFORMED
} salts_xml_status;

typedef enum salts_xml_node_kind {
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
} salts_xml_node_kind;

typedef struct salts_xml_location {
    /** Zero-based byte offset in the original UTF-8 input. */
    size_t byte_offset;
    /** One-based source line. */
    uint32_t line;
    /** One-based byte column. */
    uint32_t column;
} salts_xml_location;

typedef struct salts_xml_string_view {
    const char *data;
    size_t size;
} salts_xml_string_view;

typedef struct salts_xml_limits {
    size_t max_input_bytes;
    /** All retained parser nodes, including declarations and DTD nodes. */
    size_t max_nodes;
    /** Element, namespace, and XML declaration attributes. */
    size_t max_attributes;
    size_t max_depth;
    /** Bytes retained by names and values in the private parser DOM. */
    size_t max_retained_string_bytes;
} salts_xml_limits;

typedef struct salts_xml_diagnostic {
    salts_xml_status status;
    salts_xml_location location;
    char message[SALTS_XML_DIAGNOSTIC_CAPACITY];
} salts_xml_diagnostic;

typedef struct salts_xml_document {
    void *impl;
} salts_xml_document;

/**
 * Borrowed immutable node handle. It and every returned string view are valid
 * only while the owning document remains alive.
 */
typedef struct salts_xml_node {
    const void *impl;
} salts_xml_node;

/** Borrowed immutable attribute handle with the same document lifetime. */
typedef struct salts_xml_attribute {
    const void *impl;
} salts_xml_attribute;

/** Owned array of borrowed nodes. Destroy the list before its document. */
typedef struct salts_xml_node_list {
    void *items;
    size_t size;
} salts_xml_node_list;

/** Return the bounded defaults used when parse receives a NULL limits pointer. */
salts_xml_limits salts_xml_default_limits(void);

/**
 * Parse exactly input_size bytes and atomically publish an owning document.
 * `out` must be zero-initialized. Input bytes are copied. Failure leaves an
 * empty output and writes the first caller-owned diagnostic when provided.
 */
salts_xml_status salts_xml_parse(salts_xml_document *out,
                                 const char *input,
                                 size_t input_size,
                                 const salts_xml_limits *limits,
                                 salts_xml_diagnostic *diagnostic);

/** Destroy a document after all borrowed nodes/views are quiescent. */
void salts_xml_document_destroy(salts_xml_document *document);

/**
 * Create an owning mutable document with one root element.
 * `out` must be zero-initialized. The document is single-owner and must not be
 * copied. Mutation and destruction require all readers to be quiescent.
 */
salts_xml_status salts_xml_document_create(salts_xml_document *out,
                                           const char *root_name);

/** Append a new element. Existing handles remain valid; borrowed views may not. */
salts_xml_status salts_xml_node_add_element(salts_xml_node parent,
                                            const char *name,
                                            salts_xml_node *out);

/** Append escaped text content. Existing borrowed string views may be invalidated. */
salts_xml_status salts_xml_node_set_text(salts_xml_node node,
                                         const char *text);

/** Return a malloc-owned complete document string. Free with salts_xml_owned_string_free(). */
char *salts_xml_document_serialize(const salts_xml_document *document,
                                   size_t *out_size);
void salts_xml_owned_string_free(char *string);

salts_xml_node salts_xml_document_root(const salts_xml_document *document);
salts_xml_node_kind salts_xml_node_type(salts_xml_node node);
salts_xml_location salts_xml_node_location(salts_xml_node node);
salts_xml_string_view salts_xml_node_qualified_name(salts_xml_node node);
salts_xml_string_view salts_xml_node_local_name(salts_xml_node node);
salts_xml_string_view salts_xml_node_namespace_uri(salts_xml_node node);
salts_xml_string_view salts_xml_node_value(salts_xml_node node);
size_t salts_xml_node_child_count(salts_xml_node node);
salts_xml_node salts_xml_node_child_at(salts_xml_node node, size_t index);
size_t salts_xml_node_attribute_count(salts_xml_node node);
salts_xml_attribute salts_xml_node_attribute_at(salts_xml_node node,
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
salts_xml_status salts_xml_serialize_children(
    salts_xml_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size);

salts_xml_location salts_xml_attribute_location(salts_xml_attribute attribute);
salts_xml_string_view salts_xml_attribute_qualified_name(
    salts_xml_attribute attribute);
salts_xml_string_view salts_xml_attribute_local_name(
    salts_xml_attribute attribute);
salts_xml_string_view salts_xml_attribute_namespace_uri(
    salts_xml_attribute attribute);
salts_xml_string_view salts_xml_attribute_value(salts_xml_attribute attribute);

/** Return the first borrowed node matching the legacy element-query syntax. */
salts_xml_node salts_xml_node_find(salts_xml_node root, const char *query);

/**
 * Populate a zero-initialized owned list with borrowed matching nodes.
 * Destroy the list before destroying or mutating its document.
 */
salts_xml_status salts_xml_node_find_all(salts_xml_node root, const char *query,
                                         salts_xml_node_list *out);
/** Return malloc-owned aggregate node text. Free with salts_xml_owned_string_free(). */
char *salts_xml_node_text_dup(salts_xml_node node);

/**
 * Execute XPath and populate a zero-initialized owned list of borrowed nodes.
 * XPath evaluation currently requires process-wide external serialization;
 * concurrent XPath calls are unsupported by the private engine.
 */
qvm_status_t salts_xml_document_xpath_query(const salts_xml_document *document,
                                            const char *xpath,
                                            salts_xml_node_list *out,
                                            const qvm_limits_t *limits,
                                            qvm_diagnostic_t *diagnostic);
size_t salts_xml_node_list_size(const salts_xml_node_list *list);
salts_xml_node salts_xml_node_list_at(const salts_xml_node_list *list,
                                      size_t index);
void salts_xml_node_list_destroy(salts_xml_node_list *list);

salts_xml_string_view salts_xml_node_display_name(salts_xml_node node);
salts_xml_string_view salts_xml_node_text_view(salts_xml_node node);
/**
 * Return non-zero when the text selected by salts_xml_node_text_view() came
 * from ordinary XML text containing an entity/character reference.
 *
 * CDATA returns zero even when its literal bytes contain '&...;'. This lets
 * adapters decode logical XML text without guessing private parser node flags.
 */
int salts_xml_node_text_has_entity_reference(salts_xml_node node);
char *salts_xml_node_serialize(salts_xml_node node, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_XML_PARSER_H */
