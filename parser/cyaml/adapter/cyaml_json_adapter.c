#include "cyaml_json_adapter.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CYAML_JSON_ADAPTER_MAX_DEPTH 256U
#define CYAML_JSON_ADAPTER_MAP_INIT_CAP 8U

/* Private CYAML helper used by this in-tree adapter for binary-safe scalars. */
char* cyaml_scalar_strn(const cyaml_doc_t* doc, const cyaml_node_t* node,
    size_t* len);

typedef enum {
    CYAML_JSON_TAG_NONE,
    CYAML_JSON_TAG_STRING,
    CYAML_JSON_TAG_NULL,
    CYAML_JSON_TAG_BOOL,
    CYAML_JSON_TAG_INT,
    CYAML_JSON_TAG_FLOAT,
    CYAML_JSON_TAG_SEQ,
    CYAML_JSON_TAG_MAP,
    CYAML_JSON_TAG_UNKNOWN
} cyaml_json_tag_t;

typedef struct {
    const cyaml_node_t* active[CYAML_JSON_ADAPTER_MAX_DEPTH + 1U];
    unsigned count;
} cyaml_json_visit_t;

static char cyaml_json_hex_digit(unsigned value)
{
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 0x0FU];
}

static cyaml_node_t* cyaml_json_new_string(cyaml_doc_t* doc,
    const char* string, size_t len)
{
    if (!doc || !string || len > (SIZE_MAX - 1U) / 4U)
        return NULL;

    char* encoded = malloc(len * 4U + 1U);
    if (!encoded)
        return NULL;

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)string[i];
        if (c == '"' || c == '\\') {
            encoded[out++] = '\\';
            encoded[out++] = (char)c;
        } else if (c == '\b') {
            encoded[out++] = '\\';
            encoded[out++] = 'b';
        } else if (c == '\f') {
            encoded[out++] = '\\';
            encoded[out++] = 'f';
        } else if (c == '\n') {
            encoded[out++] = '\\';
            encoded[out++] = 'n';
        } else if (c == '\r') {
            encoded[out++] = '\\';
            encoded[out++] = 'r';
        } else if (c == '\t') {
            encoded[out++] = '\\';
            encoded[out++] = 't';
        } else if (c < 0x20U || c == 0x7FU) {
            encoded[out++] = '\\';
            encoded[out++] = 'x';
            encoded[out++] = cyaml_json_hex_digit(c >> 4U);
            encoded[out++] = cyaml_json_hex_digit(c);
        } else {
            encoded[out++] = (char)c;
        }
    }

    cyaml_node_t* node = cyaml_new_str(doc, encoded, out);
    free(encoded);
    if (node)
        node->style = CYAML_DOUBLE;
    return node;
}

static cyaml_node_t* cyaml_json_new_number(cyaml_doc_t* doc,
    const json_value_t* value)
{
    double number = json_number(value);
    if (!doc || !isfinite(number))
        return NULL;

    size_t len = 0;
    char* serialized = json_serialize(value, &len);
    if (!serialized)
        return NULL;

    cyaml_node_t* node = cyaml_new_str(doc, serialized, len);
    json_serialize_free(serialized);
    return node;
}

static bool cyaml_json_map_append(cyaml_doc_t* doc, cyaml_node_t* map,
    const char* key, size_t key_len, cyaml_node_t* value)
{
    if (!doc || !map || map->type != CYAML_MAP || !key || !value || key_len > UINT32_MAX)
        return false;

    cyaml_node_t* key_node = cyaml_json_new_string(doc, key, key_len);
    if (!key_node)
        return false;

    if (map->map.count >= map->map.cap) {
        uint32_t new_cap = map->map.cap ? map->map.cap * 2U : CYAML_JSON_ADAPTER_MAP_INIT_CAP;
        if (new_cap < map->map.cap)
            return false;
        cyaml_pair_t* new_pairs = realloc(map->map.pairs,
            (size_t)new_cap * sizeof(*new_pairs));
        if (!new_pairs)
            return false;
        map->map.pairs = new_pairs;
        map->map.cap = new_cap;
    }

    map->map.pairs[map->map.count].key = key_node;
    map->map.pairs[map->map.count].val = value;
    map->map.count++;
    return true;
}

static cyaml_node_t* cyaml_json_convert_node(cyaml_doc_t* doc,
    const json_value_t* value, unsigned depth)
{
    if (!doc || !value || depth > CYAML_JSON_ADAPTER_MAX_DEPTH)
        return NULL;

    switch (json_type(value)) {
    case JSON_NULL:
        return cyaml_new_null(doc);
    case JSON_BOOL:
        return cyaml_new_bool(doc, json_bool(value));
    case JSON_NUMBER:
        return cyaml_json_new_number(doc, value);
    case JSON_STRING: {
        const char* string = json_string(value);
        return string
            ? cyaml_json_new_string(doc, string, json_string_len(value))
            : NULL;
    }
    case JSON_ARRAY: {
        cyaml_node_t* sequence = cyaml_new_seq(doc);
        if (!sequence)
            return NULL;

        size_t count = json_array_size(value);
        for (size_t i = 0; i < count; i++) {
            cyaml_node_t* item = cyaml_json_convert_node(doc,
                json_array_get(value, i), depth + 1U);
            if (!item || !cyaml_seq_push(sequence, item))
                return NULL;
        }
        return sequence;
    }
    case JSON_OBJECT: {
        cyaml_node_t* map = cyaml_new_map(doc);
        if (!map)
            return NULL;

        size_t count = json_object_size(value);
        for (size_t i = 0; i < count; i++) {
            const char* key = json_object_key(value, i);
            cyaml_node_t* item = cyaml_json_convert_node(doc,
                json_object_value(value, i), depth + 1U);
            if (!item || !cyaml_json_map_append(doc, map, key,
                    json_object_key_len(value, i), item))
                return NULL;
        }
        return map;
    }
    default:
        return NULL;
    }
}

cyaml_doc_t* cyaml_doc_from_json_value(const json_value_t* value)
{
    if (!value)
        return NULL;

    cyaml_doc_t* doc = cyaml_doc_new();
    if (!doc)
        return NULL;

    cyaml_node_t* root = cyaml_json_convert_node(doc, value, 0U);
    if (!root) {
        cyaml_free(doc);
        return NULL;
    }

    cyaml_set_root(doc, root);
    return doc;
}

cyaml_doc_t* cyaml_doc_from_json(const char* json, size_t len)
{
    json_value_t* value = json_parse(json, len);
    if (!value)
        return NULL;

    cyaml_doc_t* doc = cyaml_doc_from_json_value(value);
    json_free(value);
    return doc;
}

static bool cyaml_json_span_equals(const cyaml_doc_t* doc, cyaml_span_t span,
    const char* text, size_t len)
{
    const char* source = cyaml_src(doc);
    return source && span.len == len
        && memcmp(source + span.off, text, len) == 0;
}

static cyaml_json_tag_t cyaml_json_tag(const cyaml_doc_t* doc,
    const cyaml_node_t* node)
{
    if (!node || node->tag.len == 0)
        return CYAML_JSON_TAG_NONE;

#define CYAML_JSON_TAG_MATCH(text, result) \
    if (cyaml_json_span_equals(doc, node->tag, text, sizeof(text) - 1U)) \
        return result
    CYAML_JSON_TAG_MATCH("!", CYAML_JSON_TAG_STRING);
    CYAML_JSON_TAG_MATCH("!!str", CYAML_JSON_TAG_STRING);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:str>", CYAML_JSON_TAG_STRING);
    CYAML_JSON_TAG_MATCH("!!null", CYAML_JSON_TAG_NULL);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:null>", CYAML_JSON_TAG_NULL);
    CYAML_JSON_TAG_MATCH("!!bool", CYAML_JSON_TAG_BOOL);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:bool>", CYAML_JSON_TAG_BOOL);
    CYAML_JSON_TAG_MATCH("!!int", CYAML_JSON_TAG_INT);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:int>", CYAML_JSON_TAG_INT);
    CYAML_JSON_TAG_MATCH("!!float", CYAML_JSON_TAG_FLOAT);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:float>", CYAML_JSON_TAG_FLOAT);
    CYAML_JSON_TAG_MATCH("!!seq", CYAML_JSON_TAG_SEQ);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:seq>", CYAML_JSON_TAG_SEQ);
    CYAML_JSON_TAG_MATCH("!!map", CYAML_JSON_TAG_MAP);
    CYAML_JSON_TAG_MATCH("!<tag:yaml.org,2002:map>", CYAML_JSON_TAG_MAP);
#undef CYAML_JSON_TAG_MATCH
    return CYAML_JSON_TAG_UNKNOWN;
}

static bool cyaml_json_visit_push(cyaml_json_visit_t* visit,
    const cyaml_node_t* node)
{
    if (!visit || !node || visit->count >= CYAML_JSON_ADAPTER_MAX_DEPTH)
        return false;
    for (unsigned i = 0; i < visit->count; i++) {
        if (visit->active[i] == node)
            return false;
    }
    visit->active[visit->count++] = node;
    return true;
}

static json_value_t* cyaml_json_from_node(const cyaml_doc_t* doc,
    const cyaml_node_t* node, cyaml_json_visit_t* visit);

static json_value_t* cyaml_json_from_string(const cyaml_doc_t* doc,
    const cyaml_node_t* node)
{
    size_t len = 0;
    char* string = cyaml_scalar_strn(doc, node, &len);
    if (!string)
        return NULL;
    json_value_t* value = json_create_string_n(string, len);
    free(string);
    return value;
}

static json_value_t* cyaml_json_from_integer(const cyaml_doc_t* doc,
    const cyaml_node_t* node)
{
    int64_t signed_value = 0;
    if (cyaml_as_int(doc, node, &signed_value))
        return json_create_int64(signed_value);

    uint64_t unsigned_value = 0;
    return cyaml_as_uint(doc, node, &unsigned_value)
        ? json_create_uint64(unsigned_value)
        : NULL;
}

static json_value_t* cyaml_json_from_scalar(const cyaml_doc_t* doc,
    const cyaml_node_t* node, cyaml_json_tag_t tag)
{
    cyaml_scalar_kind_t kind;
    if (tag == CYAML_JSON_TAG_STRING)
        return cyaml_json_from_string(doc, node);
    if (tag == CYAML_JSON_TAG_UNKNOWN || tag == CYAML_JSON_TAG_SEQ
        || tag == CYAML_JSON_TAG_MAP)
        return NULL;

    if (tag == CYAML_JSON_TAG_NULL)
        kind = CYAML_KIND_NULL;
    else if (tag == CYAML_JSON_TAG_BOOL)
        kind = CYAML_KIND_BOOL;
    else if (tag == CYAML_JSON_TAG_INT)
        kind = CYAML_KIND_INT;
    else if (tag == CYAML_JSON_TAG_FLOAT)
        kind = CYAML_KIND_FLOAT;
    else
        kind = cyaml_scalar_kind(doc, node);

    switch (kind) {
    case CYAML_KIND_NULL:
        return json_create_null();
    case CYAML_KIND_BOOL: {
        bool value = false;
        return cyaml_as_bool(doc, node, &value)
            ? json_create_bool(value)
            : NULL;
    }
    case CYAML_KIND_INT:
        return cyaml_json_from_integer(doc, node);
    case CYAML_KIND_FLOAT: {
        double value = 0.0;
        return cyaml_as_float(doc, node, &value) && isfinite(value)
            ? json_create_number(value)
            : NULL;
    }
    case CYAML_KIND_STRING:
        return cyaml_json_from_string(doc, node);
    default:
        return NULL;
    }
}

static json_value_t* cyaml_json_from_sequence(const cyaml_doc_t* doc,
    const cyaml_node_t* node, cyaml_json_visit_t* visit)
{
    json_value_t* array = json_create_array();
    if (!array)
        return NULL;

    for (uint32_t i = 0; i < cyaml_seq_len(node); i++) {
        json_value_t* item = cyaml_json_from_node(doc,
            cyaml_seq_get(node, i), visit);
        if (!item || !json_array_add_checked(array, item)) {
            json_free(item);
            json_free(array);
            return NULL;
        }
    }
    return array;
}

static bool cyaml_json_object_has_key(const json_value_t* object,
    const char* key, size_t key_len)
{
    size_t count = json_object_size(object);
    for (size_t i = 0; i < count; i++) {
        if (json_object_key_len(object, i) == key_len
            && memcmp(json_object_key(object, i), key, key_len) == 0)
            return true;
    }
    return false;
}

static json_value_t* cyaml_json_from_mapping(const cyaml_doc_t* doc,
    const cyaml_node_t* node, cyaml_json_visit_t* visit)
{
    json_value_t* object = json_create_object();
    if (!object)
        return NULL;

    for (uint32_t i = 0; i < cyaml_map_len(node); i++) {
        cyaml_pair_t* pair = cyaml_map_at(node, i);
        const cyaml_node_t* key_node = pair ? pair->key : NULL;
        if (key_node && key_node->type == CYAML_ALIAS)
            key_node = key_node->alias.target;
        cyaml_json_tag_t key_tag = cyaml_json_tag(doc, key_node);
        if (!key_node || key_node->type != CYAML_SCALAR
            || (key_tag != CYAML_JSON_TAG_STRING
                && (key_tag != CYAML_JSON_TAG_NONE
                    || cyaml_scalar_kind(doc, key_node) != CYAML_KIND_STRING))) {
            json_free(object);
            return NULL;
        }

        size_t key_len = 0;
        char* key = cyaml_scalar_strn(doc, key_node, &key_len);
        if (!key || cyaml_json_object_has_key(object, key, key_len)) {
            free(key);
            json_free(object);
            return NULL;
        }

        json_value_t* value = cyaml_json_from_node(doc, pair->val, visit);
        if (!value || !json_object_add_n(object, key, key_len, value)) {
            free(key);
            json_free(value);
            json_free(object);
            return NULL;
        }
        free(key);
    }
    return object;
}

static json_value_t* cyaml_json_from_node(const cyaml_doc_t* doc,
    const cyaml_node_t* node, cyaml_json_visit_t* visit)
{
    if (!node || node->type == CYAML_NONE || node->type == CYAML_NULL)
        return json_create_null();
    if (!cyaml_json_visit_push(visit, node))
        return NULL;

    cyaml_json_tag_t tag = cyaml_json_tag(doc, node);
    json_value_t* result = NULL;
    switch (node->type) {
    case CYAML_SCALAR:
        result = cyaml_json_from_scalar(doc, node, tag);
        break;
    case CYAML_SEQ:
        if (tag == CYAML_JSON_TAG_NONE || tag == CYAML_JSON_TAG_SEQ)
            result = cyaml_json_from_sequence(doc, node, visit);
        break;
    case CYAML_MAP:
        if (tag == CYAML_JSON_TAG_NONE || tag == CYAML_JSON_TAG_MAP)
            result = cyaml_json_from_mapping(doc, node, visit);
        break;
    case CYAML_ALIAS:
        if (tag == CYAML_JSON_TAG_NONE && node->alias.target)
            result = cyaml_json_from_node(doc, node->alias.target, visit);
        break;
    default:
        break;
    }

    visit->count--;
    return result;
}

json_value_t* json_value_from_cyaml(const cyaml_doc_t* doc)
{
    if (!doc)
        return NULL;
    if (!cyaml_root(doc))
        return json_create_null();

    cyaml_json_visit_t visit = { 0 };
    return cyaml_json_from_node(doc, cyaml_root(doc), &visit);
}

json_value_t* json_value_from_cyaml_node(const cyaml_doc_t* doc,
    const cyaml_node_t* node)
{
    if (!doc || !node)
        return NULL;

    cyaml_json_visit_t visit = { 0 };
    return cyaml_json_from_node(doc, node, &visit);
}
