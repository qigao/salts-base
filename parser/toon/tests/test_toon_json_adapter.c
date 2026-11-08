#include "json_parser.h"
#include "tinytest.h"
#include "toon_json_adapter.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static void toon_test_set_key(toonObject *node, const char *key)
{
    size_t len = strlen(key);
    node->key = TOONc_malloc(len + 1U);
    memcpy(node->key, key, len + 1U);
}

suite("toon json adapter") {
    it("validates arguments and clears output pointers on failure") {
        toonObject *toon = TOONc_newNullObj();
        toonObject *toon_out = (toonObject *)(uintptr_t)1;
        json_value_t *json = json_create_null();
        json_value_t *json_out = (json_value_t *)(uintptr_t)1;

        check_equal(toon_json_to_value(NULL, &json_out), SALTS_EINVAL);
        check_null(json_out);
        check_equal(toon_json_to_value(toon, NULL), SALTS_EINVAL);
        check_equal(toon_json_from_value(NULL, &toon_out), SALTS_EINVAL);
        check_null(toon_out);
        check_equal(toon_json_from_value(json, NULL), SALTS_EINVAL);

        json_free(json);
        TOONc_free(toon);
    }

    it("converts a TOON tree into an independently owned JSON DOM") {
        toonObject *root = TOONc_newObject(KV_OBJ);
        toonObject *name = TOONc_newStringObj("Ada", 3U);
        toonObject *items = TOONc_newListObj();
        json_value_t *value = NULL;

        toon_test_set_key(name, "name");
        toon_test_set_key(items, "items");
        TOONc_listPush(items, TOONc_newIntObj(7));
        TOONc_listPush(items, TOONc_newBoolObj(1));
        root->child = name;
        name->next = items;

        check_equal(toon_json_to_value(root, &value), SALTS_OK);
        check_not_null(value);
        if (value) {
            check_equal(json_type(value), JSON_OBJECT);
            check_equal(json_get_string(value, "name"), "Ada");
            json_value_t *array = json_object_get(value, "items");
            check_equal(json_array_size(array), 2U);
            check_within(json_number(json_array_get(array, 0)),
                7.0, 0.0);
            check_true(json_bool(json_array_get(array, 1)));
        }

        TOONc_free(root);
        if (value) {
            check_equal(json_get_string(value, "name"), "Ada");
            json_free(value);
        }
    }

    it("converts a JSON DOM into an independently owned TOON tree") {
        const char json[] =
            "{\"name\":\"Ada\",\"bytes\":\"x\\u0000y\","
            "\"items\":[null,2.5,{\"n\":3}]}";
        json_value_t *value = json_parse(json, sizeof(json) - 1U);
        toonObject *root = NULL;

        check_not_null(value);
        check_equal(toon_json_from_value(value, &root), SALTS_OK);
        check_not_null(root);
        json_free(value);

        if (root) {
            toonObject *bytes = TOONc_get(root, "bytes");
            toonObject *items = TOONc_get(root, "items");
            check_equal(TOON_GET_STRING(TOONc_get(root, "name")), "Ada");
            check_not_null(bytes);
            check_equal(bytes ? bytes->str.len : 0U, 3U);
            if (bytes) check_equal(bytes->str.ptr, "x\0y", 3U);
            check_equal(TOONc_getArrayLength(items), 3U);
            check_within(
                TOON_GET_DOUBLE(TOONc_getArrayItem(items, 1)), 2.5, 0.0);
            check_equal(TOON_GET_INT(TOONc_get(
                TOONc_getArrayItem(items, 2), "n")), 3);
            TOONc_free(root);
        }
    }

    it("uses the JSON serializer for escaping and embedded NUL values") {
        const char source[] = {'q', '"', '\\', '\n', '\x01', '\0', 'z'};
        toonObject *root = TOONc_newObject(KV_OBJ);
        toonObject *text = TOONc_newStringObj((char *)source, sizeof(source));
        size_t serialized_len = 0;
        char *serialized;
        json_value_t *parsed;

        toon_test_set_key(text, "text");
        root->child = text;
        serialized = TOONc_toJSONString(root, &serialized_len);
        check_not_null(serialized);
        parsed = serialized ? json_parse(serialized, serialized_len) : NULL;
        check_not_null(parsed);
        if (parsed) {
            json_value_t *roundtrip = json_object_get(parsed, "text");
            check_equal(json_string_len(roundtrip), sizeof(source));
            check_equal(json_string(roundtrip), source, sizeof(source));
            json_free(parsed);
        }
        TOONc_serializeFree(serialized);
        TOONc_free(root);
    }

    it("rejects JSON object keys that TOON cannot represent") {
        const char json[] = "{\"a\\u0000b\":1}";
        json_value_t *value = json_parse(json, sizeof(json) - 1U);
        toonObject *root = (toonObject *)(uintptr_t)1;

        check_not_null(value);
        check_equal(toon_json_from_value(value, &root), SALTS_ENOTSUP);
        check_null(root);
        json_free(value);
    }

    it("rejects integer tokens outside the exact TOON double range") {
        json_value_t *value = json_create_int64(INT64_C(9007199254740993));
        json_value_t *unsigned_value = json_create_uint64(UINT64_MAX);
        toonObject *root = (toonObject *)(uintptr_t)1;

        check_not_null(value);
        check_equal(toon_json_from_value(value, &root), SALTS_ERANGE);
        check_null(root);
        check_not_null(unsigned_value);
        check_equal(toon_json_from_value(unsigned_value, &root),
            SALTS_ERANGE);
        check_null(root);
        json_free(value);
        json_free(unsigned_value);
    }

    it("preserves the largest exactly representable integer boundary") {
        json_value_t *value = json_create_int64(INT64_C(9007199254740992));
        toonObject *root = NULL;

        check_not_null(value);
        check_equal(toon_json_from_value(value, &root), SALTS_OK);
        check_not_null(root);
        if (root) {
            check_equal(root->kvtype, KV_DOUBLE);
            check_within(root->d, 9007199254740992.0, 0.0);
        }

        TOONc_free(root);
        json_free(value);
    }

    it("preserves the sign of JSON negative zero") {
        json_value_t *value = json_parse("-0.0", 4U);
        toonObject *root = NULL;
        json_value_t *roundtrip = NULL;

        check_not_null(value);
        check_equal(toon_json_from_value(value, &root), SALTS_OK);
        check_not_null(root);
        if (root) {
            check_equal(root->kvtype, KV_DOUBLE);
            check_true(signbit(root->d));
            check_equal(toon_json_to_value(root, &roundtrip), SALTS_OK);
            check_not_null(roundtrip);
            if (roundtrip) check_true(signbit(json_number(roundtrip)));
        }

        json_free(roundtrip);
        TOONc_free(root);
        json_free(value);
    }

    it("rejects duplicate keys and cyclic TOON graphs") {
        toonObject *root = TOONc_newObject(KV_OBJ);
        toonObject *first = TOONc_newIntObj(1);
        toonObject *second = TOONc_newIntObj(2);
        json_value_t *value = (json_value_t *)(uintptr_t)1;

        toon_test_set_key(first, "same");
        toon_test_set_key(second, "same");
        root->child = first;
        first->next = second;
        check_equal(toon_json_to_value(root, &value), SALTS_EPROTO);
        check_null(value);

        memcpy(second->key, "diff", 5U);
        toon_test_set_key(root, "root");
        second->next = root;
        check_equal(toon_json_to_value(root, &value), SALTS_EPROTO);
        check_null(value);
        second->next = NULL;
        TOONc_free(root);
    }

    it("rejects invalid text, non-finite numbers, and unsupported nodes") {
        const char invalid_utf8[] = {'\xC0', '\xAF'};
        toonObject *text = TOONc_newStringObj((char *)invalid_utf8,
            sizeof(invalid_utf8));
        toonObject *number = TOONc_newDoubleObj(NAN);
        toonObject *unsupported = TOONc_newObject(KV_LOBJ);
        json_value_t *invalid_json = json_create_string_n(invalid_utf8,
            sizeof(invalid_utf8));
        toonObject *toon = NULL;
        json_value_t *value = NULL;

        check_equal(toon_json_to_value(text, &value), SALTS_ECHARSET);
        check_null(value);
        check_equal(toon_json_to_value(number, &value), SALTS_ERANGE);
        check_null(value);
        check_equal(toon_json_to_value(unsupported, &value), SALTS_ENOTSUP);
        check_null(value);
        check_not_null(invalid_json);
        check_equal(toon_json_from_value(invalid_json, &toon),
            SALTS_ECHARSET);
        check_null(toon);

        json_free(invalid_json);
        TOONc_free(text);
        TOONc_free(number);
        TOONc_free(unsupported);
    }

    it("rejects a keyed TOON root instead of dropping its key") {
        toonObject *root = TOONc_newIntObj(1);
        json_value_t *value = NULL;

        toon_test_set_key(root, "value");
        check_equal(toon_json_to_value(root, &value), SALTS_EPROTO);
        check_null(value);
        TOONc_free(root);
    }

    it("rejects shared nodes in TOON arrays") {
        toonObject *root = TOONc_newListObj();
        toonObject *item = TOONc_newIntObj(1);
        json_value_t *value = NULL;

        TOONc_listPush(root, item);
        TOONc_listPush(root, item);
        check_equal(toon_json_to_value(root, &value), SALTS_EPROTO);
        check_null(value);

        root->array.len = 1U;
        TOONc_free(root);
    }

    it("enforces the adapter depth limit") {
        json_value_t *root = json_create_array();
        json_value_t *cursor = root;
        toonObject *toon = (toonObject *)(uintptr_t)1;

        for (unsigned i = 0; i <= TOON_JSON_ADAPTER_MAX_DEPTH; ++i) {
            json_value_t *child = json_create_array();
            check_not_null(child);
            check_true(json_array_add_checked(cursor, child));
            cursor = child;
        }

        check_equal(toon_json_from_value(root, &toon), SALTS_ERANGE);
        check_null(toon);
        json_free(root);
    }

    it("enforces the depth limit when converting TOON to JSON") {
        toonObject *root = TOONc_newListObj();
        toonObject *cursor = root;
        json_value_t *json = NULL;

        for (unsigned i = 0; i <= TOON_JSON_ADAPTER_MAX_DEPTH; ++i) {
            toonObject *child = TOONc_newListObj();
            check_not_null(child);
            TOONc_listPush(cursor, child);
            cursor = child;
        }

        check_equal(toon_json_to_value(root, &json), SALTS_ERANGE);
        check_null(json);
        TOONc_free(root);
    }
}
