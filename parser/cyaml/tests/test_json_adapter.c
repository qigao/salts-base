#include "cyaml_json_adapter.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

suite("cyaml json adapter") {
    it("converts nested JSON values into an owned CYAML tree") {
        const char* json = "{\"name\":\"Ada\",\"active\":true,\"items\":[null,2.5,{\"n\":3}]}";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));

        check_not_null(doc);
        check_true(cyaml_is_map(cyaml_root(doc)));
        char* name = cyaml_scalar_str(doc, cyaml_path(doc, "/name"));
        check_equal(name, "Ada");
        free(name);

        bool active = false;
        check_true(cyaml_as_bool(doc, cyaml_path(doc, "/active"), &active));
        check_true(active);
        check_true(cyaml_is_null(cyaml_path(doc, "/items[0]")));

        double number = 0.0;
        check_true(cyaml_as_float(doc, cyaml_path(doc, "/items[1]"), &number));
        check_within(number, 2.5, 0.0);

        int64_t integer = 0;
        check_true(cyaml_as_int(doc, cyaml_path(doc, "/items[2]/n"), &integer));
        check_equal(integer, 3);
        cyaml_free(doc);
    }

    it("keeps JSON strings distinct from YAML typed scalars") {
        const char* json = "[\"true\",\"null\",\"42\",true,null,42]";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        size_t json_len = 0;
        char* roundtrip = cyaml_json(doc, 0, &json_len);

        check_not_null(doc);
        check_not_null(roundtrip);
        check_equal(roundtrip, json);
        check_equal(json_len, strlen(json));
        free(roundtrip);
        cyaml_free(doc);
    }

    it("preserves empty strings and empty containers") {
        const char* json = "{\"s\":\"\",\"a\":[],\"o\":{}}";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        size_t json_len = 0;
        char* roundtrip = cyaml_json(doc, 0, &json_len);

        check_not_null(doc);
        check_not_null(roundtrip);
        check_equal(roundtrip, json);
        check_equal(json_len, strlen(json));
        free(roundtrip);
        cyaml_free(doc);
    }

    it("preserves escaped NUL bytes in string values and object keys") {
        const char* json = "{\"a\\u0000b\":\"x\\u0000y\"}";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        cyaml_node_t* root = cyaml_root(doc);
        cyaml_pair_t* pair = cyaml_map_at(root, 0);

        check_not_null(doc);
        check_not_null(pair);
        check_equal(cyaml_len(pair->key), 6);
        check_equal(cyaml_str(doc, pair->key), "a\\x00b", 6);
        check_equal(cyaml_len(pair->val), 6);
        check_equal(cyaml_str(doc, pair->val), "x\\x00y", 6);

        char* decoded_key = cyaml_scalar_str(doc, pair->key);
        char* decoded_value = cyaml_scalar_str(doc, pair->val);
        check_not_null(decoded_key);
        check_not_null(decoded_value);
        check_equal(decoded_key, "a\0b", 3);
        check_equal(decoded_value, "x\0y", 3);
        free(decoded_key);
        free(decoded_value);

        size_t json_len = 0;
        char* roundtrip = cyaml_json(doc, 0, &json_len);
        check_not_null(roundtrip);
        check_equal(roundtrip, json);
        check_equal(json_len, strlen(json));
        free(roundtrip);
        cyaml_free(doc);
    }

    it("preserves decoded strings that contain YAML escape characters") {
        const char* json = "[\"a\\\\b\",\"line\\nnext\",\"\\\"quoted\\\"\"]";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        size_t json_len = 0;
        char* roundtrip = cyaml_json(doc, 0, &json_len);

        check_not_null(doc);
        check_not_null(roundtrip);
        check_equal(roundtrip, json);
        check_equal(json_len, strlen(json));
        free(roundtrip);
        cyaml_free(doc);
    }

    it("copies data independently from the source JSON DOM") {
        json_value_t* value = json_parse("{\"name\":\"Ada\"}", 14);
        cyaml_doc_t* doc = cyaml_doc_from_json_value(value);
        json_free(value);

        char* name = cyaml_scalar_str(doc, cyaml_path(doc, "/name"));
        check_not_null(doc);
        check_equal(name, "Ada");
        free(name);
        cyaml_free(doc);
    }

    it("retains the full precision present in a JSON double") {
        const char* json = "1.2345678901234567";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        double value = 0.0;

        check_not_null(doc);
        check_true(cyaml_as_float(doc, cyaml_root(doc), &value));
        check_within(value, 1.2345678901234567, 0.0);
        cyaml_free(doc);
    }

    it("fails fast for invalid input and null DOM values") {
        check_null(cyaml_doc_from_json("{", 1));
        check_null(cyaml_doc_from_json(NULL, 0));
        check_null(cyaml_doc_from_json_value(NULL));
    }

    it("converts a nested CYAML tree into a JSON DOM") {
        const char* yaml =
            "name: Ada\n"
            "active: true\n"
            "items: [null, 2.5, \"42\"]\n";
        cyaml_error_t error = { 0 };
        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &error);
        json_value_t* value = json_value_from_cyaml(doc);

        check_not_null(doc);
        check_not_null(value);
        check_equal(json_type(value), JSON_OBJECT);
        check_equal(json_get_string(value, "name"), "Ada");
        check_true(json_get_bool(value, "active", false));

        json_value_t* items = json_object_get(value, "items");
        check_equal(json_array_size(items), 3);
        check_true(json_is_null(json_array_get(items, 0)));
        check_within(json_number(json_array_get(items, 1)), 2.5, 0.0);
        check_equal(json_type(json_array_get(items, 2)), JSON_STRING);
        check_equal(json_string(json_array_get(items, 2)), "42");
        json_free(value);
        cyaml_free(doc);
    }

    it("round-trips JSON through CYAML and back into a JSON DOM") {
        const char* json = "{\"s\":\"true\",\"empty\":\"\",\"a\":[1,false,null]}";
        cyaml_doc_t* doc = cyaml_doc_from_json(json, strlen(json));
        json_value_t* value = json_value_from_cyaml(doc);
        size_t len = 0;
        char* roundtrip = json_serialize(value, &len);

        check_not_null(doc);
        check_not_null(value);
        check_not_null(roundtrip);
        check_equal(roundtrip, json);
        check_equal(len, strlen(json));
        json_serialize_free(roundtrip);
        json_free(value);
        cyaml_free(doc);
    }

    it("preserves YAML string escapes and explicit string tags") {
        const char* yaml =
            "\"a\\0b\": \"x\\0y\"\n"
            "typed: !!str true\n";
        cyaml_error_t error = { 0 };
        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &error);
        json_value_t* value = json_value_from_cyaml(doc);

        check_not_null(doc);
        check_not_null(value);
        check_equal(json_object_key_len(value, 0), 3);
        check_equal(json_object_key(value, 0), "a\0b", 3);
        check_equal(json_string_len(json_object_value(value, 0)), 3);
        check_equal(json_string(json_object_value(value, 0)), "x\0y", 3);
        check_equal(json_type(json_object_get(value, "typed")), JSON_STRING);
        check_equal(json_string(json_object_get(value, "typed")), "true");
        json_free(value);
        cyaml_free(doc);
    }

    it("expands non-cyclic YAML aliases") {
        const char* yaml = "value: &item [1, 2]\ncopy: *item\n";
        cyaml_error_t error = { 0 };
        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &error);
        json_value_t* value = json_value_from_cyaml(doc);

        check_not_null(doc);
        check_not_null(value);
        check_equal(json_array_size(json_object_get(value, "value")), 2);
        check_equal(json_array_size(json_object_get(value, "copy")), 2);
        check_true(json_object_get(value, "value") != json_object_get(value, "copy"));
        json_free(value);
        cyaml_free(doc);
    }

    it("rejects YAML values outside the JSON data model") {
        const char* invalid[] = {
            ".inf",
            ".nan",
            "[key]: value",
            "!custom value",
            "!!int nope"
        };

        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            cyaml_error_t error = { 0 };
            cyaml_doc_t* doc = cyaml_parse(invalid[i], strlen(invalid[i]), NULL, &error);
            capture(i, "%zu");
            check_not_null(doc);
            check_null(json_value_from_cyaml(doc));
            cyaml_free(doc);
        }
        check_null(json_value_from_cyaml(NULL));
    }

    it("rejects duplicate YAML keys when parsing permits them") {
        const char* yaml = "key: 1\nkey: 2\n";
        cyaml_opts_t opts = CYAML_OPTS_DEFAULT;
        cyaml_error_t error = { 0 };
        opts.dup_keys = true;

        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &error);

        check_not_null(doc);
        check_null(json_value_from_cyaml(doc));
        cyaml_free(doc);
    }

    it("preserves exact signed and unsigned 64-bit YAML integers") {
        const char* yaml =
            "[9223372036854775807, -9223372036854775808, 18446744073709551615]";
        cyaml_error_t error = { 0 };
        cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &error);
        json_value_t* value = json_value_from_cyaml(doc);
        size_t len = 0;

        check_not_null(doc);
        check_not_null(value);
        check_equal(json_number_text(json_array_get(value, 0), &len),
            "9223372036854775807");
        check_equal(len, 19);
        check_equal(json_number_text(json_array_get(value, 1), &len),
            "-9223372036854775808");
        check_equal(len, 20);
        check_equal(json_number_text(json_array_get(value, 2), &len),
            "18446744073709551615");
        check_equal(len, 20);
        json_free(value);
        cyaml_free(doc);
    }

    it("rejects cyclic aliases") {
        cyaml_doc_t* doc = cyaml_doc_new();
        cyaml_node_t* alias = cyaml_node_new(doc, CYAML_ALIAS);
        alias->alias.target = alias;
        cyaml_set_root(doc, alias);

        check_null(json_value_from_cyaml(doc));
        cyaml_free(doc);
    }
}
