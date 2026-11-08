#include "cyaml.h"
#include "tinytest.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


void test_cyaml_version(void)
{
    const char* ver = cyaml_version();
    check_not_null(ver);
    check_true(strlen(ver) > 0);
}

void test_cyaml_strerror(void)
{
    check_not_null(cyaml_strerror(CYAML_OK));
    check_not_null(cyaml_strerror(CYAML_ERR_NOMEM));
    check_not_null(cyaml_strerror(CYAML_ERR_SYNTAX));
    check_not_null(cyaml_strerror(CYAML_ERR_EOF));
    check_not_null(cyaml_strerror(CYAML_ERR_INDENT));
    check_not_null(cyaml_strerror(CYAML_ERR_ESCAPE));
    check_not_null(cyaml_strerror(CYAML_ERR_ANCHOR));
    check_not_null(cyaml_strerror(CYAML_ERR_ALIAS));
    check_not_null(cyaml_strerror(CYAML_ERR_TAG));
    check_not_null(cyaml_strerror(CYAML_ERR_DUP_KEY));
    check_not_null(cyaml_strerror(CYAML_ERR_IO));
}

void test_cyaml_parse_null_input(void)
{
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(NULL, 0, NULL, &err);
    check_null(doc);
}

void test_cyaml_parse_empty_string(void)
{
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse("", 0, NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_cyaml_parse_simple_scalar(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_not_null(cyaml_root(doc));
    check_true(cyaml_is_scalar(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_parse_simple_map(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_not_null(cyaml_root(doc));
    check_true(cyaml_is_map(cyaml_root(doc)));
    check_equal(cyaml_map_len(cyaml_root(doc)), 1);


    cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), 0);
    check_not_null(pair);
    check_not_null(pair->key);
    check_not_null(pair->val);
    check_true(cyaml_span_eq(doc, pair->key->span, "key"));
    check_true(cyaml_span_eq(doc, pair->val->span, "value"));
    cyaml_free(doc);
}

void test_cyaml_parse_simple_seq(void)
{
    const char* yaml = "- one\n- two\n- three";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_not_null(cyaml_root(doc));
    check_true(cyaml_is_seq(cyaml_root(doc)));
    check_equal(cyaml_seq_len(cyaml_root(doc)), 3);


    const char* expected[] = { "one", "two", "three" };
    for (uint32_t i = 0; i < cyaml_seq_len(cyaml_root(doc)); i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        check_not_null(item);
        check_true(cyaml_is_scalar(item));
        check_true(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_parse_with_options(void)
{
    const char* yaml = "nested:\n  deep:\n    value: 1";
    cyaml_opts_t opts = { .max_depth = 5 };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);
    cyaml_free(doc);
}

void test_cyaml_parse_rejects_duplicate_keys_by_default(void)
{
    const char* yaml = "name: first\nname: second\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
    check_equal(err.span.start_line, 2);
    check_true(err.msg[0] != '\0');
}

void test_cyaml_parse_rejects_duplicate_keys_when_disabled(void)
{
    const char* yaml = "name: first\nname: second\n";
    cyaml_opts_t opts = CYAML_OPTS_DEFAULT;
    cyaml_error_t err;
    opts.dup_keys = false;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
}

void test_cyaml_parse_rejects_duplicate_keys_without_error_output(void)
{
    const char* yaml = "name: first\nname: second\n";
    check_null(cyaml_parse(yaml, strlen(yaml), NULL, NULL));
}

void test_cyaml_parse_allows_duplicate_keys_when_enabled(void)
{
    const char* yaml = "name: first\nname: second\n";
    cyaml_opts_t opts = CYAML_OPTS_DEFAULT;
    cyaml_error_t err;
    opts.dup_keys = true;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);
    check_equal(cyaml_map_len(cyaml_root(doc)), 2);
    cyaml_free(doc);
}

void test_cyaml_parse_rejects_nested_duplicate_keys(void)
{
    const char* yaml = "outer:\n  name: first\n  name: second\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
    check_equal(err.span.start_line, 3);
}

void test_cyaml_parse_rejects_equivalent_quoted_key(void)
{
    const char* yaml = "name: first\n\"name\": second\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
    check_equal(err.span.start_line, 2);
}

void test_cyaml_parse_rejects_duplicate_complex_keys(void)
{
    const char* yaml = "? [one, two]\n: first\n? [one, two]\n: second\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
    check_equal(err.span.start_line, 3);
}

void test_cyaml_parse_rejects_duplicate_recursive_alias_keys(void)
{
    const char* yaml = "? &first [*first]\n: one\n? &second [*second]\n: two\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_null(doc);
    check_equal(err.code, CYAML_ERR_DUP_KEY);
    check_equal(err.span.start_line, 3);
}

void test_cyaml_parse_accepts_distinct_keys(void)
{
    const char* yaml = "first: one\nsecond: two\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_map_len(cyaml_root(doc)), 2);
    cyaml_free(doc);
}

void test_cyaml_parse_accepts_distinct_typed_keys(void)
{
    const char* yaml = "\"1\": text\n1: number\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_map_len(cyaml_root(doc)), 2);
    cyaml_free(doc);
}

void test_cyaml_parse_syntax_error(void)
{
    const char* yaml = ":\n  invalid";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_cyaml_free_null(void)
{
    cyaml_free(NULL);
}

void test_cyaml_parse_stream_single_doc(void)
{
    const char* yaml = "hello: world";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    check_not_null(stream);
    check_equal(cyaml_stream_count(stream), 1);
    check_not_null(cyaml_stream_doc(stream, 0));
    cyaml_stream_free(stream);
}

void test_cyaml_parse_stream_multi_doc(void)
{
    const char* yaml = "---\nfirst\n---\nsecond\n---\nthird";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    check_not_null(stream);
    check_equal(cyaml_stream_count(stream), 3);


    const char* expected[] = { "first", "second", "third" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_doc_t* doc = cyaml_stream_doc(stream, i);
        check_not_null(doc);
        cyaml_node_t* root = cyaml_root(doc);
        check_not_null(root);
        check_true(cyaml_is_scalar(root));
        check_true(cyaml_span_eq(doc, root->span, expected[i]));
    }
    cyaml_stream_free(stream);
}

void test_cyaml_stream_doc_out_of_bounds(void)
{
    const char* yaml = "doc";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(yaml, strlen(yaml), NULL, &err);
    check_not_null(stream);
    check_null(cyaml_stream_doc(stream, 100));
    cyaml_stream_free(stream);
}

void test_cyaml_stream_count_null(void)
{
    check_equal(cyaml_stream_count(NULL), 0);
}

void test_cyaml_stream_doc_null(void)
{
    check_null(cyaml_stream_doc(NULL, 0));
}

void test_cyaml_stream_free_null(void)
{
    cyaml_stream_free(NULL);
}

void test_cyaml_root_null(void)
{
    check_null(cyaml_root(NULL));
}

void test_cyaml_src(void)
{
    const char* yaml = "test";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_not_null(cyaml_src(doc));
    check_true(cyaml_src(doc) == yaml);
    cyaml_free(doc);
}

void test_cyaml_src_null(void)
{
    check_null(cyaml_src(NULL));
}

void test_cyaml_src_len(void)
{
    const char* yaml = "test";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_src_len(doc), strlen(yaml));
    cyaml_free(doc);
}

void test_cyaml_src_len_null(void)
{
    check_equal(cyaml_src_len(NULL), 0);
}

void test_cyaml_span_ptr(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    check_not_null(root);
    const char* ptr = cyaml_span_ptr(doc, root->span);
    check_not_null(ptr);
    cyaml_free(doc);
}

void test_cyaml_span_dup(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    char* dup = cyaml_span_dup(doc, root->span);
    check_not_null(dup);
    check_equal(dup, "hello");
    free(dup);
    cyaml_free(doc);
}

void test_cyaml_scalar_str_plain(void)
{
    const char* yaml = "hello world";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    char* str = cyaml_scalar_str(doc, cyaml_root(doc));
    check_not_null(str);
    check_equal(str, "hello world");
    free(str);
    cyaml_free(doc);
}

void test_cyaml_scalar_str_quoted(void)
{
    const char* yaml = "\"hello\\nworld\"";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    char* str = cyaml_scalar_str(doc, cyaml_root(doc));
    check_not_null(str);
    check_equal(str, "hello\nworld");
    free(str);
    cyaml_free(doc);
}

void test_cyaml_span_eq(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    check_true(cyaml_span_eq(doc, root->span, "hello"));
    check_false(cyaml_span_eq(doc, root->span, "world"));
    check_false(cyaml_span_eq(doc, root->span, "HELLO"));
    cyaml_free(doc);
}

void test_cyaml_span_ieq(void)
{
    const char* yaml = "Hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    check_true(cyaml_span_ieq(doc, root->span, "hello"));
    check_true(cyaml_span_ieq(doc, root->span, "HELLO"));
    check_true(cyaml_span_ieq(doc, root->span, "HeLLo"));
    check_false(cyaml_span_ieq(doc, root->span, "world"));
    cyaml_free(doc);
}

void test_cyaml_span_cmp(void)
{
    const char* yaml = "key: key";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    cyaml_pair_t* pair = cyaml_map_at(root, 0);
    check_not_null(pair);
    check_true(cyaml_span_cmp(doc, pair->key->span, pair->val->span));
    cyaml_free(doc);
}

void test_cyaml_is_null_with_null_ptr(void)
{
    check_true(cyaml_is_null(NULL));
}

void test_cyaml_is_null_with_null_node(void)
{
    const char* yaml = "~";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_null(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_scalar(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_scalar(cyaml_root(doc)));
    check_false(cyaml_is_seq(cyaml_root(doc)));
    check_false(cyaml_is_map(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_seq(void)
{
    const char* yaml = "- item";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_seq(cyaml_root(doc)));
    check_false(cyaml_is_scalar(cyaml_root(doc)));
    check_false(cyaml_is_map(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_map(void)
{
    const char* yaml = "key: val";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_map(cyaml_root(doc)));
    check_false(cyaml_is_scalar(cyaml_root(doc)));
    check_false(cyaml_is_seq(cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_alias(void)
{
    const char* yaml = "- &anchor value\n- *anchor";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* root = cyaml_root(doc);
    check_true(cyaml_is_seq(root));
    cyaml_node_t* first = cyaml_seq_get(root, 0);
    cyaml_node_t* second = cyaml_seq_get(root, 1);
    check_true(cyaml_is_scalar(first));
    check_true(cyaml_is_alias(second));
    cyaml_free(doc);
}

void test_cyaml_val(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_span_t span = cyaml_val(cyaml_root(doc));
    check_equal(span.len, 5);
    cyaml_free(doc);
}

void test_cyaml_str(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    const char* str = cyaml_str(doc, cyaml_root(doc));
    check_not_null(str);
    check_equal(str, "hello", 5);
    cyaml_free(doc);
}

void test_cyaml_len(void)
{
    const char* yaml = "hello";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_len(cyaml_root(doc)), 5);
    cyaml_free(doc);
}

void test_cyaml_as_int_positive(void)
{
    const char* yaml = "42";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    int64_t val;
    check_true(cyaml_as_int(doc, cyaml_root(doc), &val));
    check_equal(val, 42);
    cyaml_free(doc);
}

void test_cyaml_as_int_negative(void)
{
    const char* yaml = "-123";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    int64_t val;
    check_true(cyaml_as_int(doc, cyaml_root(doc), &val));
    check_equal(val, -123);
    cyaml_free(doc);
}

void test_cyaml_as_int_hex(void)
{
    const char* yaml = "0xff";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    int64_t val;
    check_true(cyaml_as_int(doc, cyaml_root(doc), &val));
    check_equal(val, 255);
    cyaml_free(doc);
}

void test_cyaml_as_int_octal(void)
{
    const char* yaml = "0o77";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    int64_t val;
    check_true(cyaml_as_int(doc, cyaml_root(doc), &val));
    check_equal(val, 63);
    cyaml_free(doc);
}

void test_cyaml_as_int_invalid(void)
{
    const char* yaml = "not_a_number";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    int64_t val;
    check_false(cyaml_as_int(doc, cyaml_root(doc), &val));
    cyaml_free(doc);
}

void test_cyaml_as_uint(void)
{
    const char* yaml = "12345678901234";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    uint64_t val;
    check_true(cyaml_as_uint(doc, cyaml_root(doc), &val));
    check_equal(val, 12345678901234ULL);
    cyaml_free(doc);
}

void test_cyaml_as_float_normal(void)
{
    const char* yaml = "3.14159";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    double val;
    check_true(cyaml_as_float(doc, cyaml_root(doc), &val));
    check_within(val, 3.14159, 0.00001);
    cyaml_free(doc);
}

void test_cyaml_as_float_inf(void)
{
    const char* yaml = ".inf";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    double val;
    check_true(cyaml_as_float(doc, cyaml_root(doc), &val));
    check_true(isinf(val) && val > 0);
    cyaml_free(doc);
}

void test_cyaml_as_float_neg_inf(void)
{
    const char* yaml = "-.inf";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    double val;
    check_true(cyaml_as_float(doc, cyaml_root(doc), &val));
    check_true(isinf(val) && val < 0);
    cyaml_free(doc);
}

void test_cyaml_as_float_nan(void)
{
    const char* yaml = ".nan";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    double val;
    check_true(cyaml_as_float(doc, cyaml_root(doc), &val));
    check_true(isnan(val));
    cyaml_free(doc);
}

void test_cyaml_as_bool_true(void)
{
    const char* yaml = "true";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    bool val;
    check_true(cyaml_as_bool(doc, cyaml_root(doc), &val));
    check_true(val);
    cyaml_free(doc);
}

void test_cyaml_as_bool_false(void)
{
    const char* yaml = "false";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    bool val;
    check_true(cyaml_as_bool(doc, cyaml_root(doc), &val));
    check_false(val);
    cyaml_free(doc);
}

void test_cyaml_as_bool_case_insensitive(void)
{
    const char* yaml = "TRUE";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    bool val;
    check_true(cyaml_as_bool(doc, cyaml_root(doc), &val));
    check_true(val);
    cyaml_free(doc);
}

void test_cyaml_is_null_val_tilde(void)
{
    const char* yaml = "~";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_null_val_null(void)
{
    const char* yaml = "null";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_is_null_val_NULL(void)
{
    const char* yaml = "NULL";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_null_val(doc, cyaml_root(doc)));
    cyaml_free(doc);
}

void test_cyaml_scalar_kind_null(void)
{
    const char* nulls[] = { "~", "null", "Null", "NULL" };
    for (size_t i = 0; i < sizeof(nulls) / sizeof(nulls[0]); i++) {
        cyaml_doc_t* doc = cyaml_parse(nulls[i], strlen(nulls[i]), NULL, NULL);
        check_not_null(doc);
        check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_NULL);
        cyaml_free(doc);
    }

    check_equal(cyaml_scalar_kind(NULL, NULL), CYAML_KIND_NULL);
}

void test_cyaml_scalar_kind_bool(void)
{
    const char* bools[] = { "true", "false", "True", "False", "TRUE", "FALSE" };
    for (size_t i = 0; i < sizeof(bools) / sizeof(bools[0]); i++) {
        cyaml_doc_t* doc = cyaml_parse(bools[i], strlen(bools[i]), NULL, NULL);
        check_not_null(doc);
        check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_BOOL);
        cyaml_free(doc);
    }
}

void test_cyaml_scalar_kind_int(void)
{
    const char* ints[] = { "0", "42", "-123", "+456", "0xff", "0xFF", "0o77", "0O10" };
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
        cyaml_doc_t* doc = cyaml_parse(ints[i], strlen(ints[i]), NULL, NULL);
        check_not_null(doc);
        check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_INT);
        cyaml_free(doc);
    }
}

void test_cyaml_scalar_kind_float(void)
{
    const char* floats[] = { "3.14", "-2.5", "+1.0", "1e10", "1.5e-3", ".inf", "-.inf", "+.inf", ".nan" };
    for (size_t i = 0; i < sizeof(floats) / sizeof(floats[0]); i++) {
        cyaml_doc_t* doc = cyaml_parse(floats[i], strlen(floats[i]), NULL, NULL);
        check_not_null(doc);
        check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_FLOAT);
        cyaml_free(doc);
    }
}

void test_cyaml_scalar_kind_string(void)
{
    const char* strings[] = { "hello", "foo bar", "yes", "no", "on", "off" };
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        cyaml_doc_t* doc = cyaml_parse(strings[i], strlen(strings[i]), NULL, NULL);
        check_not_null(doc);
        check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_STRING);
        cyaml_free(doc);
    }
}

void test_cyaml_scalar_kind_quoted_string(void)
{

    const char* yaml = "'123'";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);
    check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_STRING);
    cyaml_free(doc);

    yaml = "\"true\"";
    doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);
    check_equal(cyaml_scalar_kind(doc, cyaml_root(doc)), CYAML_KIND_STRING);
    cyaml_free(doc);
}

void test_cyaml_seq_len(void)
{
    const char* yaml = "- a\n- b\n- c";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_seq_len(cyaml_root(doc)), 3);


    const char* expected[] = { "a", "b", "c" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        check_not_null(item);
        check_true(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_seq_len_null(void)
{
    check_equal(cyaml_seq_len(NULL), 0);
}

void test_cyaml_seq_get(void)
{
    const char* yaml = "- first\n- second";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* first = cyaml_seq_get(cyaml_root(doc), 0);
    cyaml_node_t* second = cyaml_seq_get(cyaml_root(doc), 1);
    check_not_null(first);
    check_not_null(second);
    check_true(cyaml_span_eq(doc, first->span, "first"));
    check_true(cyaml_span_eq(doc, second->span, "second"));
    cyaml_free(doc);
}

void test_cyaml_seq_get_out_of_bounds(void)
{
    const char* yaml = "- item";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_null(cyaml_seq_get(cyaml_root(doc), 100));
    cyaml_free(doc);
}

void test_cyaml_seq_get_null(void)
{
    check_null(cyaml_seq_get(NULL, 0));
}

void test_cyaml_map_len(void)
{
    const char* yaml = "a: 1\nb: 2\nc: 3";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_equal(cyaml_map_len(cyaml_root(doc)), 3);


    const char* keys[] = { "a", "b", "c" };
    const char* vals[] = { "1", "2", "3" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), i);
        check_not_null(pair);
        check_not_null(pair->key);
        check_not_null(pair->val);
        check_true(cyaml_span_eq(doc, pair->key->span, keys[i]));
        check_true(cyaml_span_eq(doc, pair->val->span, vals[i]));
    }
    cyaml_free(doc);
}

void test_cyaml_map_len_null(void)
{
    check_equal(cyaml_map_len(NULL), 0);
}

void test_cyaml_map_at(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_pair_t* pair = cyaml_map_at(cyaml_root(doc), 0);
    check_not_null(pair);
    check_not_null(pair->key);
    check_not_null(pair->val);
    check_true(cyaml_span_eq(doc, pair->key->span, "key"));
    check_true(cyaml_span_eq(doc, pair->val->span, "value"));
    cyaml_free(doc);
}

void test_cyaml_map_at_out_of_bounds(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_null(cyaml_map_at(cyaml_root(doc), 100));
    cyaml_free(doc);
}

void test_cyaml_map_at_null(void)
{
    check_null(cyaml_map_at(NULL, 0));
}

void test_cyaml_get(void)
{
    const char* yaml = "name: John\nage: 30";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* name = cyaml_get(doc, cyaml_root(doc), "name");
    cyaml_node_t* age = cyaml_get(doc, cyaml_root(doc), "age");
    check_not_null(name);
    check_not_null(age);
    check_true(cyaml_span_eq(doc, name->span, "John"));
    int64_t age_val;
    check_true(cyaml_as_int(doc, age, &age_val));
    check_equal(age_val, 30);
    cyaml_free(doc);
}

void test_cyaml_get_not_found(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_null(cyaml_get(doc, cyaml_root(doc), "nonexistent"));
    cyaml_free(doc);
}

void test_cyaml_has(void)
{
    const char* yaml = "exists: yes";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_has(doc, cyaml_root(doc), "exists"));
    check_false(cyaml_has(doc, cyaml_root(doc), "missing"));
    cyaml_free(doc);
}

void test_cyaml_path_simple(void)
{
    const char* yaml = "user:\n  name: Alice";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* name = cyaml_path(doc, "/user/name");
    check_not_null(name);
    check_true(cyaml_span_eq(doc, name->span, "Alice"));
    cyaml_free(doc);
}

void test_cyaml_path_array_index(void)
{
    const char* yaml = "items:\n  - first\n  - second";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* item = cyaml_path(doc, "/items[1]");
    check_not_null(item);
    check_true(cyaml_span_eq(doc, item->span, "second"));
    cyaml_free(doc);
}

void test_cyaml_path_nested(void)
{
    const char* yaml = "a:\n  b:\n    c: deep";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* deep = cyaml_path(doc, "/a/b/c");
    check_not_null(deep);
    check_true(cyaml_span_eq(doc, deep->span, "deep"));
    cyaml_free(doc);
}

void test_cyaml_path_not_found(void)
{
    const char* yaml = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_null(cyaml_path(doc, "/missing/path"));
    cyaml_free(doc);
}

void test_cyaml_doc_new(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_free(doc);
}

void test_cyaml_new_null(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_null(doc);
    check_not_null(node);
    check_true(cyaml_is_null(node));
    cyaml_free(doc);
}

void test_cyaml_new_str(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_str(doc, "hello", 5);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    cyaml_free(doc);
}

void test_cyaml_new_cstr(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_cstr(doc, "hello world");
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    cyaml_free(doc);
}

void test_cyaml_new_int(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_int(doc, -42);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    int64_t val;
    check_true(cyaml_as_int(doc, node, &val));
    check_equal(val, -42);
    cyaml_free(doc);
}

void test_cyaml_new_uint(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_uint(doc, 18446744073709551615ULL);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    uint64_t val;
    check_true(cyaml_as_uint(doc, node, &val));
    check_equal(val, 18446744073709551615ULL);
    cyaml_free(doc);
}

void test_cyaml_new_float(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_float(doc, 3.14159);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    double val;
    check_true(cyaml_as_float(doc, node, &val));
    check_within(val, 3.14159, 0.00001);
    cyaml_free(doc);
}

void test_cyaml_new_bool_true(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_bool(doc, true);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    bool val;
    check_true(cyaml_as_bool(doc, node, &val));
    check_true(val);
    cyaml_free(doc);
}

void test_cyaml_new_bool_false(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* node = cyaml_new_bool(doc, false);
    check_not_null(node);
    check_true(cyaml_is_scalar(node));
    bool val;
    check_true(cyaml_as_bool(doc, node, &val));
    check_false(val);
    cyaml_free(doc);
}

void test_cyaml_new_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* seq = cyaml_new_seq(doc);
    check_not_null(seq);
    check_true(cyaml_is_seq(seq));
    check_equal(cyaml_seq_len(seq), 0);
    cyaml_free(doc);
}

void test_cyaml_new_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* map = cyaml_new_map(doc);
    check_not_null(map);
    check_true(cyaml_is_map(map));
    check_equal(cyaml_map_len(map), 0);
    cyaml_free(doc);
}

void test_cyaml_seq_push(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* seq = cyaml_new_seq(doc);
    check_true(cyaml_seq_push(seq, cyaml_new_cstr(doc, "one")));
    check_true(cyaml_seq_push(seq, cyaml_new_cstr(doc, "two")));
    check_equal(cyaml_seq_len(seq), 2);
    cyaml_free(doc);
}

void test_cyaml_map_set(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* map = cyaml_new_map(doc);
    check_true(cyaml_map_set(doc, map, "name", cyaml_new_cstr(doc, "value")));
    check_equal(cyaml_map_len(map), 1);
    check_true(cyaml_has(doc, map, "name"));
    cyaml_free(doc);
}

void test_cyaml_set_root(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* root = cyaml_new_cstr(doc, "root value");
    cyaml_set_root(doc, root);
    check_true(cyaml_root(doc) == root);
    cyaml_free(doc);
}

void test_cyaml_node_new(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    cyaml_node_t* scalar = cyaml_node_new(doc, CYAML_SCALAR);
    check_not_null(scalar);
    check_equal(scalar->type, CYAML_SCALAR);
    cyaml_node_t* seq = cyaml_node_new(doc, CYAML_SEQ);
    check_not_null(seq);
    check_equal(seq->type, CYAML_SEQ);
    cyaml_node_t* map = cyaml_node_new(doc, CYAML_MAP);
    check_not_null(map);
    check_equal(map->type, CYAML_MAP);
    cyaml_free(doc);
}

void test_cyaml_emit_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);
    cyaml_map_set(doc, root, "key", cyaml_new_cstr(doc, "value"));

    size_t len;
    char* yaml = cyaml_emit(doc, NULL, &len);
    check_not_null(yaml);
    check_true(len > 0);
    check_not_null(strstr(yaml, "key"));
    check_not_null(strstr(yaml, "value"));
    free(yaml);
    cyaml_free(doc);
}

void test_cyaml_emit_with_options(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_cstr(doc, "hello");
    cyaml_set_root(doc, root);

    cyaml_emit_opts_t opts = { .indent = 4, .doc_start = true };
    size_t len;
    char* yaml = cyaml_emit(doc, &opts, &len);
    check_not_null(yaml);
    check_not_null(strstr(yaml, "---"));
    free(yaml);
    cyaml_free(doc);
}

void test_cyaml_emit_empty_collections(void)
{
    enum { EMPTY_SEQUENCE, EMPTY_MAP, NESTED_SEQUENCE, NESTED_MAP, SHAPE_COUNT };
    static const cyaml_emit_opts_t options[] = {
        { .indent = 2, .coll = CYAML_BLOCK },
        { .indent = 2, .coll = CYAML_BLOCK, .preserve_style = true },
        { .indent = 2, .coll = CYAML_FLOW }
    };
    for (size_t shape = 0; shape < SHAPE_COUNT; ++shape) {
        cyaml_doc_t* doc = cyaml_doc_new();
        check_not_null(doc);
        cyaml_node_t* root = (shape == EMPTY_SEQUENCE || shape == NESTED_SEQUENCE)
            ? cyaml_new_seq(doc) : cyaml_new_map(doc);
        check_not_null(root);
        cyaml_set_root(doc, root);
        if (shape == NESTED_SEQUENCE) {
            check_true(cyaml_seq_push(root, cyaml_new_seq(doc)));
            check_true(cyaml_seq_push(root, cyaml_new_map(doc)));
        } else if (shape == NESTED_MAP) {
            check_true(cyaml_map_set(doc, root, "sequence", cyaml_new_seq(doc)));
            check_true(cyaml_map_set(doc, root, "mapping", cyaml_new_map(doc)));
        }
        char* expected = cyaml_json(doc, 0, NULL);
        check_not_null(expected);
        for (size_t mode = 0; mode < sizeof(options) / sizeof(options[0]); ++mode) {
            size_t len = 0;
            char* output = cyaml_emit(doc, &options[mode], &len);
            check_not_null(output);
            cyaml_doc_t* parsed = cyaml_parse(output, len, NULL, NULL);
            check_not_null(parsed);
            char* actual = cyaml_json(parsed, 0, NULL);
            check_not_null(actual);
            check_equal(actual, expected);
            free(actual);
            cyaml_free(parsed);
            free(output);
        }
        free(expected);
        cyaml_free(doc);
    }
}

void test_cyaml_emit_null_doc(void)
{
    size_t len;
    char* yaml = cyaml_emit(NULL, NULL, &len);
    check_null(yaml);
}

void test_cyaml_stream_emit(void)
{
    const char* input = "---\nfirst\n---\nsecond";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    check_not_null(stream);

    size_t len;
    char* yaml = cyaml_stream_emit(stream, &len);
    check_not_null(yaml);
    check_true(len > 0);
    free(yaml);
    cyaml_stream_free(stream);
}

void test_cyaml_dump(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    check_not_null(doc);

    size_t len;
    char* dump = cyaml_dump(doc, &len);
    check_not_null(dump);
    check_true(len > 0);
    free(dump);
    cyaml_free(doc);
}

void test_cyaml_stream_dump(void)
{
    const char* input = "---\ndoc1\n---\ndoc2";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    check_not_null(stream);

    size_t len;
    char* dump = cyaml_stream_dump(stream, &len);
    check_not_null(dump);
    check_true(len > 0);
    free(dump);
    cyaml_stream_free(stream);
}

void test_cyaml_events(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    check_not_null(doc);

    size_t len;
    char* events = cyaml_events(doc, false, &len);
    check_not_null(events);
    check_not_null(strstr(events, "+DOC"));
    check_not_null(strstr(events, "-DOC"));
    free(events);
    cyaml_free(doc);
}

void test_cyaml_stream_events(void)
{
    const char* input = "hello";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    check_not_null(stream);

    size_t len;
    char* events = cyaml_stream_events(stream, false, &len);
    check_not_null(events);
    check_not_null(strstr(events, "+STR"));
    check_not_null(strstr(events, "-STR"));
    free(events);
    cyaml_stream_free(stream);
}

void test_cyaml_json_simple(void)
{
    const char* input = "key: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    check_not_null(doc);

    size_t len;
    char* json = cyaml_json(doc, 0, &len);
    check_not_null(json);
    check_not_null(strstr(json, "\"key\""));
    check_not_null(strstr(json, "\"value\""));
    free(json);
    cyaml_free(doc);
}

void test_cyaml_json_with_indent(void)
{
    const char* input = "a: 1\nb: 2";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    check_not_null(doc);

    size_t len;
    char* json = cyaml_json(doc, 2, &len);
    check_not_null(json);
    check_not_null(strstr(json, "\n"));
    free(json);
    cyaml_free(doc);
}

void test_cyaml_stream_json_single(void)
{
    const char* input = "value";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    check_not_null(stream);

    size_t len;
    char* json = cyaml_stream_json(stream, 0, &len);
    check_not_null(json);
    check_not_null(strstr(json, "\"value\""));
    free(json);
    cyaml_stream_free(stream);
}

void test_cyaml_stream_json_multi(void)
{
    const char* input = "---\none\n---\ntwo";
    cyaml_error_t err;
    cyaml_stream_t* stream = cyaml_parse_stream(input, strlen(input), NULL, &err);
    check_not_null(stream);
    check_equal(cyaml_stream_count(stream), 2);

    size_t len;
    char* json = cyaml_stream_json(stream, 0, &len);
    check_not_null(json);
    check_equal(json[0], '[');
    check_not_null(strstr(json, "\"one\""));
    check_not_null(strstr(json, "\"two\""));
    free(json);
    cyaml_stream_free(stream);
}

void test_nested_structures(void)
{
    const char* yaml = "users:\n"
                       "  - name: Alice\n"
                       "    age: 30\n"
                       "  - name: Bob\n"
                       "    age: 25\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);

    cyaml_node_t* alice_name = cyaml_path(doc, "/users[0]/name");
    check_not_null(alice_name);
    check_true(cyaml_span_eq(doc, alice_name->span, "Alice"));

    cyaml_node_t* bob_age = cyaml_path(doc, "/users[1]/age");
    check_not_null(bob_age);
    int64_t age;
    check_true(cyaml_as_int(doc, bob_age, &age));
    check_equal(age, 25);

    cyaml_free(doc);
}

void test_build_and_emit(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* users = cyaml_new_seq(doc);
    cyaml_map_set(doc, root, "users", users);

    cyaml_node_t* user1 = cyaml_new_map(doc);
    cyaml_map_set(doc, user1, "name", cyaml_new_cstr(doc, "Alice"));
    cyaml_map_set(doc, user1, "active", cyaml_new_bool(doc, true));
    cyaml_seq_push(users, user1);

    size_t len;
    char* yaml = cyaml_emit(doc, NULL, &len);
    check_not_null(yaml);
    check_not_null(strstr(yaml, "users"));
    check_not_null(strstr(yaml, "Alice"));

    free(yaml);
    cyaml_free(doc);
}

void test_round_trip(void)
{
    const char* input = "config:\n  port: 8080\n  host: localhost\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(input, strlen(input), NULL, &err);
    check_not_null(doc);

    size_t len;
    char* output = cyaml_emit(doc, NULL, &len);
    check_not_null(output);

    cyaml_doc_t* doc2 = cyaml_parse(output, len, NULL, &err);
    check_not_null(doc2);

    cyaml_node_t* port = cyaml_path(doc2, "/config/port");
    check_not_null(port);
    int64_t port_val;
    check_true(cyaml_as_int(doc2, port, &port_val));
    check_equal(port_val, 8080);

    free(output);
    cyaml_free(doc);
    cyaml_free(doc2);
}

void test_iteration_macros(void)
{
    const char* yaml = "- a\n- b\n- c";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);

    cyaml_node_t* root = cyaml_root(doc);
    cyaml_node_t* item;
    uint32_t count = 0;
    const char* expected[] = { "a", "b", "c" };

    CYAML_EACH_SEQ(root, item, i)
    {
        check_not_null(item);
        check_true(cyaml_is_scalar(item));
        check_true(cyaml_span_eq(doc, item->span, expected[i]));
        count++;
    }
    check_equal(count, 3);

    cyaml_free(doc);


    const char* yaml_map = "x: 1\ny: 2\nz: 3";
    doc = cyaml_parse(yaml_map, strlen(yaml_map), NULL, &err);
    check_not_null(doc);

    root = cyaml_root(doc);
    cyaml_pair_t* pair;
    count = 0;
    const char* exp_keys[] = { "x", "y", "z" };
    const char* exp_vals[] = { "1", "2", "3" };

    CYAML_EACH_MAP(root, pair, j)
    {
        check_not_null(pair);
        check_not_null(pair->key);
        check_not_null(pair->val);
        check_true(cyaml_is_scalar(pair->key));
        check_true(cyaml_is_scalar(pair->val));
        check_true(cyaml_span_eq(doc, pair->key->span, exp_keys[j]));
        check_true(cyaml_span_eq(doc, pair->val->span, exp_vals[j]));
        count++;
    }
    check_equal(count, 3);

    cyaml_free(doc);
}

void test_flow_style(void)
{
    const char* yaml = "{key: value, num: 123}";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_map(cyaml_root(doc)));
    check_equal(cyaml_map_len(cyaml_root(doc)), 2);


    cyaml_node_t* key_val = cyaml_get(doc, cyaml_root(doc), "key");
    cyaml_node_t* num_val = cyaml_get(doc, cyaml_root(doc), "num");
    check_not_null(key_val);
    check_not_null(num_val);
    check_true(cyaml_span_eq(doc, key_val->span, "value"));
    int64_t num;
    check_true(cyaml_as_int(doc, num_val, &num));
    check_equal(num, 123);
    cyaml_free(doc);
}

void test_flow_sequence(void)
{
    const char* yaml = "[one, two, three]";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    check_true(cyaml_is_seq(cyaml_root(doc)));
    check_equal(cyaml_seq_len(cyaml_root(doc)), 3);


    const char* expected[] = { "one", "two", "three" };
    for (uint32_t i = 0; i < 3; i++) {
        cyaml_node_t* item = cyaml_seq_get(cyaml_root(doc), i);
        check_not_null(item);
        check_true(cyaml_span_eq(doc, item->span, expected[i]));
    }
    cyaml_free(doc);
}

void test_multiline_string(void)
{
    const char* yaml = "text: |\n  line 1\n  line 2\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* text = cyaml_get(doc, cyaml_root(doc), "text");
    check_not_null(text);
    char* str = cyaml_scalar_str(doc, text);
    check_not_null(str);
    check_not_null(strstr(str, "line 1"));
    check_not_null(strstr(str, "line 2"));
    free(str);
    cyaml_free(doc);
}

void test_folded_string(void)
{
    const char* yaml = "text: >\n  line 1\n  line 2\n";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_node_t* text = cyaml_get(doc, cyaml_root(doc), "text");
    check_not_null(text);
    char* str = cyaml_scalar_str(doc, text);
    check_not_null(str);
    free(str);
    cyaml_free(doc);
}



void test_cyaml_set_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* node = cyaml_new_cstr(doc, "value");
    check_true(cyaml_set_anchor(doc, node, "myanchor"));
    check_equal(cyaml_anchor_len(node), 8);
    const char* anchor = cyaml_anchor(doc, node);
    check_not_null(anchor);
    check_equal(anchor, "myanchor", 8);
    cyaml_free(doc);
}

void test_cyaml_set_anchor_clear(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* node = cyaml_new_cstr(doc, "value");
    check_true(cyaml_set_anchor(doc, node, "anchor"));
    check_true(cyaml_anchor_len(node) > 0);
    check_true(cyaml_set_anchor(doc, node, NULL));
    check_equal(cyaml_anchor_len(node), 0);
    cyaml_free(doc);
}

void test_cyaml_new_alias(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* target = cyaml_new_cstr(doc, "value");
    cyaml_set_anchor(doc, target, "anchor");
    cyaml_node_t* alias = cyaml_new_alias(doc, target);
    check_not_null(alias);
    check_true(cyaml_is_alias(alias));
    check_true(alias->alias.target == target);
    cyaml_free(doc);
}

void test_cyaml_new_alias_no_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* target = cyaml_new_cstr(doc, "value");

    cyaml_node_t* alias = cyaml_new_alias(doc, target);
    check_null(alias);
    cyaml_free(doc);
}

void test_cyaml_find_anchor(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* val = cyaml_new_cstr(doc, "anchored");
    cyaml_set_anchor(doc, val, "myref");
    cyaml_map_set(doc, root, "key", val);

    cyaml_node_t* found = cyaml_find_anchor(doc, "myref");
    check_not_null(found);
    check_true(found == val);

    check_null(cyaml_find_anchor(doc, "nonexistent"));
    cyaml_free(doc);
}

void test_cyaml_find_anchor_nested(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_seq(doc);
    cyaml_set_root(doc, root);

    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_seq_push(root, map);

    cyaml_node_t* deep = cyaml_new_cstr(doc, "deep value");
    cyaml_set_anchor(doc, deep, "deepanchor");
    cyaml_map_set(doc, map, "nested", deep);

    cyaml_node_t* found = cyaml_find_anchor(doc, "deepanchor");
    check_not_null(found);
    check_true(found == deep);
    cyaml_free(doc);
}



void test_cyaml_node_copy_scalar(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* orig = cyaml_new_cstr(doc, "hello");
    cyaml_node_t* copy = cyaml_node_copy(doc, doc, orig);
    check_not_null(copy);
    check_true(cyaml_is_scalar(copy));
    check((copy) != (orig));
    check_true(cyaml_span_eq(doc, copy->span, "hello"));
    cyaml_free(doc);
}

void test_cyaml_node_copy_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* seq = cyaml_new_seq(doc);
    cyaml_seq_push(seq, cyaml_new_cstr(doc, "a"));
    cyaml_seq_push(seq, cyaml_new_cstr(doc, "b"));

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, seq);
    check_not_null(copy);
    check_true(cyaml_is_seq(copy));
    check_equal(cyaml_seq_len(copy), 2);
    check((copy) != (seq));
    cyaml_free(doc);
}

void test_cyaml_node_copy_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "key1", cyaml_new_cstr(doc, "val1"));
    cyaml_map_set(doc, map, "key2", cyaml_new_int(doc, 42));

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, map);
    check_not_null(copy);
    check_true(cyaml_is_map(copy));
    check_equal(cyaml_map_len(copy), 2);
    check((copy) != (map));

    cyaml_node_t* val1 = cyaml_get(doc, copy, "key1");
    check_not_null(val1);
    check_true(cyaml_span_eq(doc, val1->span, "val1"));
    cyaml_free(doc);
}

void test_cyaml_node_copy_deep(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_node_t* nested = cyaml_new_map(doc);
    cyaml_map_set(doc, nested, "inner", cyaml_new_cstr(doc, "value"));
    cyaml_map_set(doc, root, "outer", nested);

    cyaml_node_t* copy = cyaml_node_copy(doc, doc, root);
    check_not_null(copy);

    cyaml_node_t* copy_nested = cyaml_get(doc, copy, "outer");
    check_not_null(copy_nested);
    check((copy_nested) != (nested));

    cyaml_node_t* copy_inner = cyaml_get(doc, copy_nested, "inner");
    check_not_null(copy_inner);
    check_true(cyaml_span_eq(doc, copy_inner->span, "value"));
    cyaml_free(doc);
}



void test_cyaml_map_merge_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_map_set(doc, dst, "a", cyaml_new_int(doc, 1));

    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_map_set(doc, src, "b", cyaml_new_int(doc, 2));

    check_true(cyaml_map_merge(doc, dst, src));
    check_equal(cyaml_map_len(dst), 2);
    check_true(cyaml_has(doc, dst, "a"));
    check_true(cyaml_has(doc, dst, "b"));
    cyaml_free(doc);
}

void test_cyaml_map_merge_overwrite(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_map_set(doc, dst, "key", cyaml_new_int(doc, 1));

    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_map_set(doc, src, "key", cyaml_new_int(doc, 99));

    check_true(cyaml_map_merge(doc, dst, src));
    check_equal(cyaml_map_len(dst), 1);

    int64_t val;
    check_true(cyaml_as_int(doc, cyaml_get(doc, dst, "key"), &val));
    check_equal(val, 99);
    cyaml_free(doc);
}

void test_cyaml_map_merge_deep(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();


    cyaml_node_t* dst = cyaml_new_map(doc);
    cyaml_node_t* dst_config = cyaml_new_map(doc);
    cyaml_map_set(doc, dst_config, "port", cyaml_new_int(doc, 80));
    cyaml_map_set(doc, dst_config, "host", cyaml_new_cstr(doc, "localhost"));
    cyaml_map_set(doc, dst, "config", dst_config);


    cyaml_node_t* src = cyaml_new_map(doc);
    cyaml_node_t* src_config = cyaml_new_map(doc);
    cyaml_map_set(doc, src_config, "port", cyaml_new_int(doc, 8080));
    cyaml_map_set(doc, src_config, "debug", cyaml_new_bool(doc, true));
    cyaml_map_set(doc, src, "config", src_config);

    check_true(cyaml_map_merge(doc, dst, src));


    cyaml_node_t* merged_config = cyaml_get(doc, dst, "config");
    check_not_null(merged_config);
    check_equal(cyaml_map_len(merged_config), 3);

    int64_t port;
    check_true(cyaml_as_int(doc, cyaml_get(doc, merged_config, "port"), &port));
    check_equal(port, 8080);

    check_true(cyaml_has(doc, merged_config, "host"));
    check_true(cyaml_has(doc, merged_config, "debug"));

    cyaml_free(doc);
}



void test_cyaml_resolve_aliases(void)
{
    const char* yaml = "- &anchor value\n- *anchor\n- *anchor";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);

    cyaml_node_t* root = cyaml_root(doc);
    check_true(cyaml_is_alias(cyaml_seq_get(root, 1)));
    check_true(cyaml_is_alias(cyaml_seq_get(root, 2)));

    check_true(cyaml_resolve_aliases(doc));


    check_true(cyaml_is_scalar(cyaml_seq_get(root, 1)));
    check_true(cyaml_is_scalar(cyaml_seq_get(root, 2)));

    cyaml_free(doc);
}



void test_cyaml_comment_count_null(void)
{
    check_equal(cyaml_comment_count(NULL), 0);
}

void test_cyaml_comment_count_no_comments_option(void)
{
    const char* yaml = "key: value  # comment";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);

    check_equal(cyaml_comment_count(doc), 0);
    cyaml_free(doc);
}

void test_cyaml_comment_count_with_comments(void)
{
    const char* yaml = "# header comment\nkey: value  # inline comment";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);

    check_true(cyaml_comment_count(doc) >= 1);
    cyaml_free(doc);
}

void test_cyaml_comment_at_null(void)
{
    cyaml_span_t span = cyaml_comment_at(NULL, 0);
    check_equal(span.len, 0);
    check_equal(span.off, 0);
}

void test_cyaml_comment_at_out_of_bounds(void)
{
    const char* yaml = "# comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);

    cyaml_span_t span = cyaml_comment_at(doc, 1000);
    check_equal(span.len, 0);
    cyaml_free(doc);
}

void test_cyaml_comment_at_valid(void)
{
    const char* yaml = "# this is a comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);
    if (cyaml_comment_count(doc) > 0) {
        cyaml_span_t span = cyaml_comment_at(doc, 0);
        check_true(span.len > 0);

        const char* ptr = cyaml_span_ptr(doc, span);
        check_not_null(ptr);
    }
    cyaml_free(doc);
}

void test_cyaml_comment_multiple(void)
{
    const char* yaml = "# first comment\n# second comment\nkey: value";
    cyaml_opts_t opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &opts, &err);
    check_not_null(doc);
    uint32_t count = cyaml_comment_count(doc);
    if (count >= 2) {
        cyaml_span_t span0 = cyaml_comment_at(doc, 0);
        cyaml_span_t span1 = cyaml_comment_at(doc, 1);
        check_true(span0.len > 0);
        check_true(span1.len > 0);

        check((span1.off) != (span0.off));
    }
    cyaml_free(doc);
}



void test_cyaml_emit_with_comments_disabled(void)
{
    const char* yaml = "# header comment\nkey: value";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = false };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_null(strstr(output, "# header"));
    check_not_null(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_with_comments_enabled(void)
{
    const char* yaml = "# header comment\nkey: value";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# header comment"));
    check_not_null(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_inline_comment(void)
{
    const char* yaml = "key: value  # inline comment";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# inline comment"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_seq_with_comments(void)
{
    const char* yaml = "# list header\n- one  # first item\n- two\n# trailing";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# list header"));
    check_not_null(strstr(output, "one"));
    check_not_null(strstr(output, "two"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_map_with_comments(void)
{
    const char* yaml = "# config section\nhost: localhost\n# port setting\nport: 8080";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# config section"));
    check_not_null(strstr(output, "host"));
    check_not_null(strstr(output, "port"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_no_comments_parsed(void)
{
    const char* yaml = "# comment\nkey: value";
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_null(strstr(output, "# comment"));
    check_not_null(strstr(output, "key"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_map_inline_comments(void)
{
    const char* yaml = "variables:\n"
                       "  MY_VARIABLE: 'true'    # Comment 1\n"
                       "  OTHER_VARIABLE: 'true' # Comment 2\n"
                       "  BEST_VARIABLE: 'true'  # Comment 3\n";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# Comment 1"));
    check_not_null(strstr(output, "# Comment 2"));
    check_not_null(strstr(output, "# Comment 3"));
    check_not_null(strstr(output, "MY_VARIABLE"));
    free(output);
    cyaml_free(doc);
}

void test_cyaml_emit_seq_complex_comments(void)
{
    const char* yaml = "# comment before a sequence\n"
                       "- first item\n"
                       "- # comment before a scalar\n"
                       "  second item # trailing comment\n"
                       "              # continuation comment\n"
                       "# comment describing last item\n"
                       "- last item\n";
    cyaml_opts_t parse_opts = { .preserve_comments = true };
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), &parse_opts, &err);
    check_not_null(doc);
    check_true(cyaml_comment_count(doc) >= 4);
    cyaml_emit_opts_t emit_opts = { .indent = 2, .preserve_comments = true };
    size_t len;
    char* output = cyaml_emit(doc, &emit_opts, &len);
    check_not_null(output);
    check_not_null(strstr(output, "# comment before a sequence"));
    check_not_null(strstr(output, "first item"));
    check_not_null(strstr(output, "second item"));
    check_not_null(strstr(output, "last item"));
    free(output);
    cyaml_free(doc);
}



void test_cyaml_map_sort_alphabetical(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "zebra", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, map, "apple", cyaml_new_int(doc, 2));
    cyaml_map_set(doc, map, "mango", cyaml_new_int(doc, 3));

    check_true(cyaml_map_sort(doc, map, NULL));


    cyaml_pair_t* p0 = cyaml_map_at(map, 0);
    cyaml_pair_t* p1 = cyaml_map_at(map, 1);
    cyaml_pair_t* p2 = cyaml_map_at(map, 2);

    check_true(cyaml_span_eq(doc, p0->key->span, "apple"));
    check_true(cyaml_span_eq(doc, p1->key->span, "mango"));
    check_true(cyaml_span_eq(doc, p2->key->span, "zebra"));

    cyaml_free(doc);
}

static int reverse_cmp(const cyaml_doc_t* doc, const cyaml_node_t* a, const cyaml_node_t* b)
{
    const char* src = cyaml_src(doc);
    if (!src || !a || !b)
        return 0;

    size_t la = a->span.len, lb = b->span.len;
    size_t min_len = la < lb ? la : lb;
    int cmp = memcmp(src + a->span.off, src + b->span.off, min_len);
    if (cmp != 0)
        return -cmp;
    return (lb > la) - (lb < la);
}

void test_cyaml_map_sort_custom(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* map = cyaml_new_map(doc);
    cyaml_map_set(doc, map, "apple", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, map, "zebra", cyaml_new_int(doc, 2));
    cyaml_map_set(doc, map, "mango", cyaml_new_int(doc, 3));

    check_true(cyaml_map_sort(doc, map, reverse_cmp));


    cyaml_pair_t* p0 = cyaml_map_at(map, 0);
    cyaml_pair_t* p1 = cyaml_map_at(map, 1);
    cyaml_pair_t* p2 = cyaml_map_at(map, 2);

    check_true(cyaml_span_eq(doc, p0->key->span, "zebra"));
    check_true(cyaml_span_eq(doc, p1->key->span, "mango"));
    check_true(cyaml_span_eq(doc, p2->key->span, "apple"));

    cyaml_free(doc);
}

void test_cyaml_map_sort_recursive(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    cyaml_node_t* root = cyaml_new_map(doc);

    cyaml_node_t* nested = cyaml_new_map(doc);
    cyaml_map_set(doc, nested, "z", cyaml_new_int(doc, 1));
    cyaml_map_set(doc, nested, "a", cyaml_new_int(doc, 2));

    cyaml_map_set(doc, root, "z", cyaml_new_int(doc, 3));
    cyaml_map_set(doc, root, "a", nested);

    check_true(cyaml_map_sort_recursive(doc, root, NULL));


    check_true(cyaml_span_eq(doc, cyaml_map_at(root, 0)->key->span, "a"));
    check_true(cyaml_span_eq(doc, cyaml_map_at(root, 1)->key->span, "z"));


    cyaml_node_t* sorted_nested = cyaml_get(doc, root, "a");
    check_true(cyaml_span_eq(doc, cyaml_map_at(sorted_nested, 0)->key->span, "a"));
    check_true(cyaml_span_eq(doc, cyaml_map_at(sorted_nested, 1)->key->span, "z"));

    cyaml_free(doc);
}


void test_cyaml_scanf_basic(void)
{
    const char* yaml = "server:\n  host: localhost\n  port: 8080\n  ssl: true";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    char host[256] = { 0 };
    unsigned int port = 0;
    bool ssl = false;

    int count = cyaml_scanf(doc, "/server/host %255s /server/port %u /server/ssl %b", host, &port, &ssl);
    check_equal(count, 3);
    check_equal(host, "localhost");
    check_equal(port, 8080);
    check_true(ssl);

    cyaml_free(doc);
}

void test_cyaml_scanf_integers(void)
{
    const char* yaml = "a: 42\nb: -17\nc: 0x1F\nd: 0o77";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    int a = 0;
    int64_t b = 0;
    uint64_t c = 0;
    int64_t d = 0;

    int count = cyaml_scanf(doc, "/a %d /b %lld /c %llu /d %lld", &a, &b, &c, &d);
    check_equal(count, 4);
    check_equal(a, 42);
    check_equal(b, -17);
    check_equal(c, 31);
    check_equal(d, 63);

    cyaml_free(doc);
}

void test_cyaml_scanf_floats(void)
{
    const char* yaml = "pi: 3.14159\ntemp: -273.15";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    float pi = 0;
    double temp = 0;

    int count = cyaml_scanf(doc, "/pi %f /temp %lf", &pi, &temp);
    check_equal(count, 2);
    check_within(pi, 3.14159f, 0.0001f);
    check_within(temp, -273.15, 0.0001);

    cyaml_free(doc);
}

void test_cyaml_scanf_node_ptr(void)
{
    const char* yaml = "items:\n  - one\n  - two";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    cyaml_node_t* items = NULL;
    int count = cyaml_scanf(doc, "/items %n", &items);
    check_equal(count, 1);
    check_not_null(items);
    check_true(cyaml_is_seq(items));
    check_equal(cyaml_seq_len(items), 2);

    cyaml_free(doc);
}

void test_cyaml_node_scanf_relative(void)
{
    const char* yaml = "users:\n  - name: alice\n    age: 30\n  - name: bob\n    age: 25";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    cyaml_node_t* user = cyaml_path(doc, "/users[0]");
    check_not_null(user);

    char name[64] = { 0 };
    int age = 0;
    int count = cyaml_node_scanf(doc, user, "name %63s age %d", name, &age);
    check_equal(count, 2);
    check_equal(name, "alice");
    check_equal(age, 30);

    cyaml_free(doc);
}


void test_cyaml_buildf_scalar(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "%d", 42);
    check_not_null(n);
    check_true(cyaml_is_scalar(n));

    int64_t v;
    check_true(cyaml_as_int(doc, n, &v));
    check_equal(v, 42);

    cyaml_free(doc);
}

void test_cyaml_buildf_string(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "%s", "hello world");
    check_not_null(n);
    check_true(cyaml_is_scalar(n));

    cyaml_free(doc);
}

void test_cyaml_buildf_map(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "name: %s\nage: %d", "alice", 30);
    check_not_null(n);
    check_true(cyaml_is_map(n));
    check_equal(cyaml_map_len(n), 2);

    cyaml_free(doc);
}

void test_cyaml_buildf_seq(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "- %s\n- %s\n- %d", "one", "two", 3);
    check_not_null(n);
    check_true(cyaml_is_seq(n));
    check_equal(cyaml_seq_len(n), 3);

    cyaml_free(doc);
}

void test_cyaml_buildf_bool(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    cyaml_node_t* n = cyaml_buildf(doc, "enabled: %b\ndisabled: %b", 1, 0);
    check_not_null(n);
    check_true(cyaml_is_map(n));

    bool enabled, disabled;
    check_true(cyaml_as_bool(doc, cyaml_get(doc, n, "enabled"), &enabled));
    check_true(cyaml_as_bool(doc, cyaml_get(doc, n, "disabled"), &disabled));
    check_true(enabled);
    check_false(disabled);

    cyaml_free(doc);
}


void test_cyaml_insert_at_simple(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);
    doc->root = cyaml_new_map(doc);

    check_true(cyaml_insert_at(doc, "/name", cyaml_new_cstr(doc, "test")));
    check_not_null(cyaml_get(doc, doc->root, "name"));

    cyaml_free(doc);
}

void test_cyaml_insert_at_nested(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    check_true(cyaml_insert_at(doc, "/server/host", cyaml_new_cstr(doc, "localhost")));
    check_true(cyaml_insert_at(doc, "/server/port", cyaml_new_int(doc, 8080)));

    cyaml_node_t* server = cyaml_path(doc, "/server");
    check_not_null(server);
    check_true(cyaml_is_map(server));
    check_equal(cyaml_map_len(server), 2);

    cyaml_free(doc);
}

void test_cyaml_insertf(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);

    check_true(cyaml_insertf(doc, "/config", "timeout: %d\nretries: %d", 30, 3));

    cyaml_node_t* config = cyaml_path(doc, "/config");
    check_not_null(config);
    check_true(cyaml_is_map(config));

    int64_t timeout;
    check_true(cyaml_as_int(doc, cyaml_get(doc, config, "timeout"), &timeout));
    check_equal(timeout, 30);

    cyaml_free(doc);
}

void test_cyaml_delete_at(void)
{
    const char* yaml = "a: 1\nb: 2\nc: 3";
    cyaml_doc_t* doc = cyaml_parse(yaml, strlen(yaml), NULL, NULL);
    check_not_null(doc);

    check_equal(cyaml_map_len(doc->root), 3);
    check_true(cyaml_delete_at(doc, "/b"));
    check_equal(cyaml_map_len(doc->root), 2);
    check_null(cyaml_get(doc, doc->root, "b"));

    cyaml_free(doc);
}

void test_cyaml_append_at(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);


    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_node_t* items = cyaml_new_seq(doc);
    cyaml_seq_push(items, cyaml_new_cstr(doc, "one"));
    cyaml_seq_push(items, cyaml_new_cstr(doc, "two"));
    cyaml_map_set(doc, root, "items", items);
    doc->root = root;

    check_equal(cyaml_seq_len(items), 2);

    check_true(cyaml_append_at(doc, "/items", cyaml_new_cstr(doc, "three")));
    check_equal(cyaml_seq_len(items), 3);

    cyaml_free(doc);
}

void test_cyaml_appendf(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    check_not_null(doc);


    cyaml_node_t* root = cyaml_new_map(doc);
    cyaml_map_set(doc, root, "users", cyaml_new_seq(doc));
    doc->root = root;

    check_true(cyaml_appendf(doc, "/users", "name: %s\nage: %d", "alice", 30));

    cyaml_node_t* users = cyaml_path(doc, "/users");
    check_equal(cyaml_seq_len(users), 1);

    cyaml_node_t* user = cyaml_seq_get(users, 0);
    check_true(cyaml_is_map(user));

    cyaml_free(doc);
}

void test_parse_tag_offbyone(void)
{
    char s[3] = {':', ' ', '!'};
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(s, sizeof(s), NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_parse_deep_nesting(void)
{
    char nested[2048];
    memset(nested, '{', sizeof(nested));
    cyaml_error_t err;
    cyaml_doc_t* doc = cyaml_parse(nested, sizeof(nested), NULL, &err);
    if (doc)
        cyaml_free(doc);
}

void test_path_large_exponent(void)
{
    cyaml_doc_t* doc = cyaml_parse("x: 0", 4, NULL, NULL);
    check_not_null(doc);
    cyaml_path(doc, "1e4000000000");
    cyaml_free(doc);
}

#define CYAML_CASE(name) it(#name) { test_##name(); }

suite("cyaml API") {
    group("parser regressions") {
        CYAML_CASE(parse_tag_offbyone);
        CYAML_CASE(parse_deep_nesting);
        CYAML_CASE(path_large_exponent);
    }

    group("parsing and document access") {
        CYAML_CASE(cyaml_version);
        CYAML_CASE(cyaml_strerror);
        CYAML_CASE(cyaml_parse_null_input);
        CYAML_CASE(cyaml_parse_empty_string);
        CYAML_CASE(cyaml_parse_simple_scalar);
        CYAML_CASE(cyaml_parse_simple_map);
        CYAML_CASE(cyaml_parse_simple_seq);
        CYAML_CASE(cyaml_parse_with_options);
        CYAML_CASE(cyaml_parse_rejects_duplicate_keys_by_default);
        CYAML_CASE(cyaml_parse_rejects_duplicate_keys_when_disabled);
        CYAML_CASE(cyaml_parse_rejects_duplicate_keys_without_error_output);
        CYAML_CASE(cyaml_parse_allows_duplicate_keys_when_enabled);
        CYAML_CASE(cyaml_parse_rejects_nested_duplicate_keys);
        CYAML_CASE(cyaml_parse_rejects_equivalent_quoted_key);
        CYAML_CASE(cyaml_parse_rejects_duplicate_complex_keys);
        CYAML_CASE(cyaml_parse_rejects_duplicate_recursive_alias_keys);
        CYAML_CASE(cyaml_parse_accepts_distinct_keys);
        CYAML_CASE(cyaml_parse_accepts_distinct_typed_keys);
        CYAML_CASE(cyaml_parse_syntax_error);
        CYAML_CASE(cyaml_free_null);
        CYAML_CASE(cyaml_parse_stream_single_doc);
        CYAML_CASE(cyaml_parse_stream_multi_doc);
        CYAML_CASE(cyaml_stream_doc_out_of_bounds);
        CYAML_CASE(cyaml_stream_count_null);
        CYAML_CASE(cyaml_stream_doc_null);
        CYAML_CASE(cyaml_stream_free_null);
        CYAML_CASE(cyaml_root_null);
        CYAML_CASE(cyaml_src);
        CYAML_CASE(cyaml_src_null);
        CYAML_CASE(cyaml_src_len);
        CYAML_CASE(cyaml_src_len_null);
        CYAML_CASE(cyaml_span_ptr);
        CYAML_CASE(cyaml_span_dup);
        CYAML_CASE(cyaml_scalar_str_plain);
        CYAML_CASE(cyaml_scalar_str_quoted);
        CYAML_CASE(cyaml_span_eq);
        CYAML_CASE(cyaml_span_ieq);
        CYAML_CASE(cyaml_span_cmp);
    }

    group("node values and collections") {
        CYAML_CASE(cyaml_is_null_with_null_ptr);
        CYAML_CASE(cyaml_is_null_with_null_node);
        CYAML_CASE(cyaml_is_scalar);
        CYAML_CASE(cyaml_is_seq);
        CYAML_CASE(cyaml_is_map);
        CYAML_CASE(cyaml_is_alias);
        CYAML_CASE(cyaml_val);
        CYAML_CASE(cyaml_str);
        CYAML_CASE(cyaml_len);
        CYAML_CASE(cyaml_as_int_positive);
        CYAML_CASE(cyaml_as_int_negative);
        CYAML_CASE(cyaml_as_int_hex);
        CYAML_CASE(cyaml_as_int_octal);
        CYAML_CASE(cyaml_as_int_invalid);
        CYAML_CASE(cyaml_as_uint);
        CYAML_CASE(cyaml_as_float_normal);
        CYAML_CASE(cyaml_as_float_inf);
        CYAML_CASE(cyaml_as_float_neg_inf);
        CYAML_CASE(cyaml_as_float_nan);
        CYAML_CASE(cyaml_as_bool_true);
        CYAML_CASE(cyaml_as_bool_false);
        CYAML_CASE(cyaml_as_bool_case_insensitive);
        CYAML_CASE(cyaml_is_null_val_tilde);
        CYAML_CASE(cyaml_is_null_val_null);
        CYAML_CASE(cyaml_is_null_val_NULL);
        CYAML_CASE(cyaml_scalar_kind_null);
        CYAML_CASE(cyaml_scalar_kind_bool);
        CYAML_CASE(cyaml_scalar_kind_int);
        CYAML_CASE(cyaml_scalar_kind_float);
        CYAML_CASE(cyaml_scalar_kind_string);
        CYAML_CASE(cyaml_scalar_kind_quoted_string);
        CYAML_CASE(cyaml_seq_len);
        CYAML_CASE(cyaml_seq_len_null);
        CYAML_CASE(cyaml_seq_get);
        CYAML_CASE(cyaml_seq_get_out_of_bounds);
        CYAML_CASE(cyaml_seq_get_null);
        CYAML_CASE(cyaml_map_len);
        CYAML_CASE(cyaml_map_len_null);
        CYAML_CASE(cyaml_map_at);
        CYAML_CASE(cyaml_map_at_out_of_bounds);
        CYAML_CASE(cyaml_map_at_null);
        CYAML_CASE(cyaml_get);
        CYAML_CASE(cyaml_get_not_found);
        CYAML_CASE(cyaml_has);
        CYAML_CASE(cyaml_path_simple);
        CYAML_CASE(cyaml_path_array_index);
        CYAML_CASE(cyaml_path_nested);
        CYAML_CASE(cyaml_path_not_found);
    }

    group("builder API") {
        CYAML_CASE(cyaml_doc_new);
        CYAML_CASE(cyaml_new_null);
        CYAML_CASE(cyaml_new_str);
        CYAML_CASE(cyaml_new_cstr);
        CYAML_CASE(cyaml_new_int);
        CYAML_CASE(cyaml_new_uint);
        CYAML_CASE(cyaml_new_float);
        CYAML_CASE(cyaml_new_bool_true);
        CYAML_CASE(cyaml_new_bool_false);
        CYAML_CASE(cyaml_new_seq);
        CYAML_CASE(cyaml_new_map);
        CYAML_CASE(cyaml_seq_push);
        CYAML_CASE(cyaml_map_set);
        CYAML_CASE(cyaml_set_root);
        CYAML_CASE(cyaml_node_new);
        CYAML_CASE(build_and_emit);
        CYAML_CASE(iteration_macros);
    }

    group("serialization") {
        CYAML_CASE(cyaml_emit_simple);
        CYAML_CASE(cyaml_emit_with_options);
        CYAML_CASE(cyaml_emit_empty_collections);
        CYAML_CASE(cyaml_emit_null_doc);
        CYAML_CASE(cyaml_stream_emit);
        CYAML_CASE(cyaml_dump);
        CYAML_CASE(cyaml_stream_dump);
        CYAML_CASE(cyaml_events);
        CYAML_CASE(cyaml_stream_events);
        CYAML_CASE(cyaml_json_simple);
        CYAML_CASE(cyaml_json_with_indent);
        CYAML_CASE(cyaml_stream_json_single);
        CYAML_CASE(cyaml_stream_json_multi);
        CYAML_CASE(nested_structures);
        CYAML_CASE(round_trip);
        CYAML_CASE(flow_style);
        CYAML_CASE(flow_sequence);
        CYAML_CASE(multiline_string);
        CYAML_CASE(folded_string);
    }

    group("anchors aliases and copies") {
        CYAML_CASE(cyaml_set_anchor);
        CYAML_CASE(cyaml_set_anchor_clear);
        CYAML_CASE(cyaml_new_alias);
        CYAML_CASE(cyaml_new_alias_no_anchor);
        CYAML_CASE(cyaml_find_anchor);
        CYAML_CASE(cyaml_find_anchor_nested);
        CYAML_CASE(cyaml_node_copy_scalar);
        CYAML_CASE(cyaml_node_copy_seq);
        CYAML_CASE(cyaml_node_copy_map);
        CYAML_CASE(cyaml_node_copy_deep);
        CYAML_CASE(cyaml_map_merge_simple);
        CYAML_CASE(cyaml_map_merge_overwrite);
        CYAML_CASE(cyaml_map_merge_deep);
        CYAML_CASE(cyaml_resolve_aliases);
    }

    group("comments and sorting") {
        CYAML_CASE(cyaml_comment_count_null);
        CYAML_CASE(cyaml_comment_count_no_comments_option);
        CYAML_CASE(cyaml_comment_count_with_comments);
        CYAML_CASE(cyaml_comment_at_null);
        CYAML_CASE(cyaml_comment_at_out_of_bounds);
        CYAML_CASE(cyaml_comment_at_valid);
        CYAML_CASE(cyaml_comment_multiple);
        CYAML_CASE(cyaml_emit_with_comments_disabled);
        CYAML_CASE(cyaml_emit_with_comments_enabled);
        CYAML_CASE(cyaml_emit_inline_comment);
        CYAML_CASE(cyaml_emit_seq_with_comments);
        CYAML_CASE(cyaml_emit_map_with_comments);
        CYAML_CASE(cyaml_emit_no_comments_parsed);
        CYAML_CASE(cyaml_emit_map_inline_comments);
        CYAML_CASE(cyaml_emit_seq_complex_comments);
        CYAML_CASE(cyaml_map_sort_alphabetical);
        CYAML_CASE(cyaml_map_sort_custom);
        CYAML_CASE(cyaml_map_sort_recursive);
    }

    group("formatted access and modification") {
        CYAML_CASE(cyaml_scanf_basic);
        CYAML_CASE(cyaml_scanf_integers);
        CYAML_CASE(cyaml_scanf_floats);
        CYAML_CASE(cyaml_scanf_node_ptr);
        CYAML_CASE(cyaml_node_scanf_relative);
        CYAML_CASE(cyaml_buildf_scalar);
        CYAML_CASE(cyaml_buildf_string);
        CYAML_CASE(cyaml_buildf_map);
        CYAML_CASE(cyaml_buildf_seq);
        CYAML_CASE(cyaml_buildf_bool);
        CYAML_CASE(cyaml_insert_at_simple);
        CYAML_CASE(cyaml_insert_at_nested);
        CYAML_CASE(cyaml_insertf);
        CYAML_CASE(cyaml_delete_at);
        CYAML_CASE(cyaml_append_at);
        CYAML_CASE(cyaml_appendf);
    }
}

#undef CYAML_CASE
