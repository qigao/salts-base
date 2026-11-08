#include "cyaml.h"
#include "tinytest.h"

#include <string.h>

#define SAX_MAX_VALUES 32
#define SAX_VALUE_SIZE 128

typedef struct {
    int document_starts;
    int document_ends;
    int mapping_starts;
    int mapping_ends;
    int sequence_starts;
    int sequence_ends;
    int nulls;
    int aliases;
    int callback_count;
    int fail_at;
    bool last_mapping_start_is_key;
    bool last_sequence_start_is_key;
    bool last_sequence_end_is_key;
    char values[SAX_MAX_VALUES][SAX_VALUE_SIZE];
    bool value_is_key[SAX_MAX_VALUES];
    cyaml_scalar_kind_t value_kind[SAX_MAX_VALUES];
    size_t value_count;
} sax_state_t;

static int sax_step(sax_state_t* state)
{
    state->callback_count++;
    return state->fail_at > 0 && state->callback_count >= state->fail_at ? -1 : 0;
}

static int on_document_start(void* ctx)
{
    sax_state_t* state = ctx;
    state->document_starts++;
    return sax_step(state);
}

static int on_document_end(void* ctx)
{
    sax_state_t* state = ctx;
    state->document_ends++;
    return sax_step(state);
}

static int on_null(void* ctx, bool is_key)
{
    sax_state_t* state = ctx;
    (void)is_key;
    state->nulls++;
    return sax_step(state);
}

static int on_scalar(void* ctx, cyaml_scalar_kind_t kind, const char* value,
    size_t value_len, bool is_key)
{
    sax_state_t* state = ctx;
    if (state->value_count < SAX_MAX_VALUES) {
        size_t copy_len = value_len < SAX_VALUE_SIZE - 1 ? value_len : SAX_VALUE_SIZE - 1;
        memcpy(state->values[state->value_count], value, copy_len);
        state->values[state->value_count][copy_len] = '\0';
        state->value_is_key[state->value_count] = is_key;
        state->value_kind[state->value_count] = kind;
        state->value_count++;
    }
    return sax_step(state);
}

static int on_sequence_start(void* ctx, bool is_key)
{
    sax_state_t* state = ctx;
    state->sequence_starts++;
    state->last_sequence_start_is_key = is_key;
    return sax_step(state);
}

static int on_sequence_end(void* ctx, bool is_key)
{
    sax_state_t* state = ctx;
    state->sequence_ends++;
    state->last_sequence_end_is_key = is_key;
    return sax_step(state);
}

static int on_mapping_start(void* ctx, bool is_key)
{
    sax_state_t* state = ctx;
    state->mapping_starts++;
    state->last_mapping_start_is_key = is_key;
    return sax_step(state);
}

static int on_mapping_end(void* ctx, bool is_key)
{
    sax_state_t* state = ctx;
    (void)is_key;
    state->mapping_ends++;
    return sax_step(state);
}

static int on_alias(void* ctx, const char* value, size_t value_len, bool is_key)
{
    sax_state_t* state = ctx;
    (void)value;
    (void)value_len;
    (void)is_key;
    state->aliases++;
    return sax_step(state);
}

static const cyaml_sax_handler_t sax_handler = {
    .on_document_start = on_document_start,
    .on_document_end = on_document_end,
    .on_null = on_null,
    .on_scalar = on_scalar,
    .on_sequence_start = on_sequence_start,
    .on_sequence_end = on_sequence_end,
    .on_mapping_start = on_mapping_start,
    .on_mapping_end = on_mapping_end,
    .on_alias = on_alias,
};

static cyaml_sax_parser_t* make_parser(sax_state_t* state)
{
    return cyaml_sax_parser_create(&sax_handler, state, NULL);
}

spec("cyaml incremental SAX")
{
    group("chunk boundaries")
    {
        it("delivers committed events during feed without truncating a plain scalar")
        {
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            check_not_null(parser);

            check_equal(cyaml_sax_parser_feed(parser, "name: sal", 9), 0);
            check_equal(state.document_starts, 1);
            check_equal(state.mapping_starts, 1);
            check_equal(state.value_count, 1);
            check_equal(state.values[0], "name");

            check_equal(cyaml_sax_parser_feed(parser, "ts\nitems:\n  - 1",
                             sizeof("ts\nitems:\n  - 1") - 1),
                0);
            check_equal(state.sequence_starts, 1);
            check_equal(state.value_count, 3);
            check_equal(state.values[1], "salts");
            check_equal(state.values[2], "items");

            check_equal(cyaml_sax_parser_feed(parser, "\n  - 2\n", 7), 0);
            check_equal(state.value_count, 4);
            check_equal(state.values[3], "1");
            check_equal(cyaml_sax_parser_finish(parser), 0);
            check_equal(state.value_count, 5);
            check_equal(state.values[4], "2");
            check_equal(state.sequence_ends, 1);
            check_equal(state.mapping_ends, 1);
            check_equal(state.document_ends, 1);
            cyaml_sax_parser_destroy(parser);
        }

        it("waits for a split quoted scalar")
        {
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            check_not_null(parser);
            check_equal(cyaml_sax_parser_feed(parser, "key: \"hel", 9), 0);
            check_equal(state.value_count, 1);
            check_equal(cyaml_sax_parser_feed(parser, "lo\"\n", 4), 0);
            check_equal(state.value_count, 2);
            check_equal(state.values[1], "hello");
            check_equal(cyaml_sax_parser_finish(parser), 0);
            cyaml_sax_parser_destroy(parser);
        }

        it("waits for block scalar termination by dedent")
        {
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            check_not_null(parser);
            check_equal(cyaml_sax_parser_feed(parser, "text: |\n  line one\n",
                             sizeof("text: |\n  line one\n") - 1),
                0);
            check_equal(state.value_count, 1);
            check_equal(cyaml_sax_parser_feed(parser, "  line two\nnext: value\n",
                             sizeof("  line two\nnext: value\n") - 1),
                0);
            check_equal(state.value_count, 3);
            check_equal(state.values[1], "line one\nline two\n");
            check_equal(state.values[2], "next");
            check_equal(cyaml_sax_parser_finish(parser), 0);
            check_equal(state.values[3], "value");
            cyaml_sax_parser_destroy(parser);
        }

        it("accepts UTF-8 split at every byte boundary")
        {
            const char input[] = "name: \xE4\xB8\xAD\xF0\x9F\x98\x80\nitems: [1, 2]\n";
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            size_t i;
            check_not_null(parser);
            for (i = 0; i < sizeof(input) - 1; ++i)
                check_equal(cyaml_sax_parser_feed(parser, input + i, 1), 0);
            check_equal(cyaml_sax_parser_finish(parser), 0);
            check_equal(state.value_count, 5);
            check_equal(state.values[1], "\xE4\xB8\xAD\xF0\x9F\x98\x80", 7);
            check_equal(state.sequence_starts, 1);
            check_equal(state.sequence_ends, 1);
            cyaml_sax_parser_destroy(parser);
        }
    }

    group("fail fast")
    {
        it("rejects an invalid UTF-8 start byte during feed")
        {
            const char input[] = "key: \x80";
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            check_not_null(parser);
            check_equal(cyaml_sax_parser_feed(parser, input, sizeof(input)), -1);
            check_not_null(cyaml_sax_parser_error(parser));
            check_equal(cyaml_sax_parser_error(parser)->code, CYAML_ERR_SYNTAX);
            cyaml_sax_parser_destroy(parser);
        }

        it("enforces max size across compacted chunks")
        {
            const cyaml_opts_t opts = { .max_depth = 1000, .max_size = 8 };
            sax_state_t state = { 0 };
            cyaml_sax_parser_t* parser
                = cyaml_sax_parser_create(&sax_handler, &state, &opts);
            check_not_null(parser);
            check_equal(cyaml_sax_parser_feed(parser, "a: 1\n", 5), 0);
            check_equal(cyaml_sax_parser_feed(parser, "b: 2\n", 5), -1);
            check_not_null(cyaml_sax_parser_error(parser));
            cyaml_sax_parser_destroy(parser);
        }

        it("rejects incomplete quoted and flow input at finish")
        {
            const char* inputs[] = { "key: \"unterminated", "items: [1, 2" };
            size_t i;
            for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
                sax_state_t state = { 0 };
                cyaml_sax_parser_t* parser = make_parser(&state);
                check_not_null(parser);
                check_equal(cyaml_sax_parser_feed(parser, inputs[i], strlen(inputs[i])), 0);
                check_equal(cyaml_sax_parser_finish(parser), -1);
                check_not_null(cyaml_sax_parser_error(parser));
                cyaml_sax_parser_destroy(parser);
            }
        }

        it("stops permanently when a callback fails")
        {
            sax_state_t state = { .fail_at = 2 };
            cyaml_sax_parser_t* parser = make_parser(&state);
            check_not_null(parser);
            check_equal(cyaml_sax_parser_feed(parser, "key: value\n", 11), -1);
            check_not_null(cyaml_sax_parser_error(parser));
            check_equal(cyaml_sax_parser_feed(parser, "next: value\n", 12), -1);
            check_equal(cyaml_sax_parser_finish(parser), -1);
            cyaml_sax_parser_destroy(parser);
        }
    }
}
