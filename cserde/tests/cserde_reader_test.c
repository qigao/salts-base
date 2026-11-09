#include <cserde/reader.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fake_reader_context {
    cserde_status next_status;
    cserde_token token;
    size_t calls;
} fake_reader_context;

typedef struct sequence_reader_context {
    const cserde_token *tokens;
    size_t count;
    size_t index;
    size_t calls;
} sequence_reader_context;

static cserde_status fake_reader_next(void *context, cserde_token *out) {
    fake_reader_context *state = (fake_reader_context *)context;

    ++state->calls;
    if (state->next_status == CSERDE_OK)
        *out = state->token;
    return state->next_status;
}

static cserde_status sequence_reader_next(void *context, cserde_token *out) {
    sequence_reader_context *state = (sequence_reader_context *)context;

    ++state->calls;
    if (state->index == state->count)
        return CSERDE_DONE;
    *out = state->tokens[state->index++];
    return CSERDE_OK;
}

static cserde_status null_context_reader_next(void *context,
                                               cserde_token *out) {
    if (context != NULL)
        return CSERDE_SOURCE_ERROR;
    *out = (cserde_token){ .kind = CSERDE_NULL };
    return CSERDE_OK;
}

#define READER_PREFIX \
    (offsetof(cserde_reader_ops, next) + \
     sizeof(((cserde_reader_ops *)0)->next))

static cserde_reader_ops reader_ops(cserde_reader_next_fn next) {
    return (cserde_reader_ops){
        .struct_size = READER_PREFIX,
        .abi_version = CSERDE_READER_OPS_ABI_VERSION,
        .next = next
    };
}

static cserde_token sentinel_token(void) {
    return (cserde_token){
        .kind = CSERDE_UINT,
        .value.uint = UINT64_C(0x1234)
    };
}

spec("CSerde pull reader") {
    it("accepts exact v1 ops prefix") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_true(reader.ops == &ops);
        check_true(reader.context == &context);
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
    }

    it("rejects short prefix without mutating zero reader") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };

        ops.struct_size = READER_PREFIX - 1u;
        check_equal(cserde_reader_init(&reader, &ops, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(reader.ops == NULL);
        check_true(reader.context == NULL);
        check_true(reader.state == CSERDE_READER_ZERO);
        check_true(reader.status == CSERDE_OK);
    }

    it("rejects wrong ABI without mutation") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };

        ops.abi_version = CSERDE_READER_OPS_ABI_VERSION + 1u;
        check_equal(cserde_reader_init(&reader, &ops, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(reader.ops == NULL);
        check_true(reader.context == NULL);
        check_true(reader.state == CSERDE_READER_ZERO);
        check_true(reader.status == CSERDE_OK);
    }

    it("rejects null next callback without mutation") {
        cserde_reader_ops ops = reader_ops(NULL);
        cserde_reader reader = { 0 };

        check_equal(cserde_reader_init(&reader, &ops, NULL),
                    CSERDE_INVALID_ARGUMENT);
        check_true(reader.ops == NULL);
        check_true(reader.context == NULL);
        check_true(reader.state == CSERDE_READER_ZERO);
        check_true(reader.status == CSERDE_OK);
    }

    it("accepts null provider context") {
        cserde_reader_ops ops = reader_ops(null_context_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, NULL), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_NULL);
    }

    it("commits only a valid callback token") {
        fake_reader_context context = {
            .next_status = CSERDE_OK,
            .token = { .kind = CSERDE_SINT, .value.sint = INT64_C(-7) }
        };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_SINT);
        check_equal(out.value.sint, INT64_C(-7));
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
        check_equal(context.calls, (size_t)1u);
    }

    it("rejects invalid callback token and preserves output") {
        fake_reader_context context = {
            .next_status = CSERDE_OK,
            .token = {
                .kind = CSERDE_STRING,
                .value.slice = { NULL, 1u, CSERDE_VIEW_STABLE }
            }
        };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(0x1234));
        check_true(reader.state == CSERDE_READER_FAILED);
        check_true(reader.status == CSERDE_INVALID_TOKEN);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_equal(context.calls, (size_t)1u);
    }

    it("makes done terminal without reinvoking provider") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_DONE);
        check_true(reader.state == CSERDE_READER_DONE);
        check_true(reader.status == CSERDE_DONE);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_DONE);
        check_equal(context.calls, (size_t)1u);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(0x1234));
    }

    it("keeps allowed provider errors sticky without touching output") {
        const cserde_status errors[] = {
            CSERDE_VALUE_OUT_OF_RANGE,
            CSERDE_LIMIT_EXCEEDED,
            CSERDE_UNSUPPORTED,
            CSERDE_SOURCE_ERROR
        };
        size_t i;

        for (i = 0u; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            fake_reader_context context = { .next_status = errors[i] };
            cserde_reader_ops ops = reader_ops(fake_reader_next);
            cserde_reader reader = { 0 };
            cserde_token out = sentinel_token();

            check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
            check_equal(cserde_reader_next(&reader, &out), errors[i]);
            check_true(reader.state == CSERDE_READER_FAILED);
            check_true(reader.status == errors[i]);
            check_true(out.kind == CSERDE_UINT);
            check_equal(out.value.uint, UINT64_C(0x1234));
            check_equal(cserde_reader_next(&reader, &out), errors[i]);
            check_equal(context.calls, (size_t)1u);
        }
    }

    it("normalizes disallowed provider status to callback error") {
        const cserde_status statuses[] = {
            CSERDE_SINK_ERROR,
            (cserde_status)999
        };
        size_t i;

        for (i = 0u; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
            fake_reader_context context = { .next_status = statuses[i] };
            cserde_reader_ops ops = reader_ops(fake_reader_next);
            cserde_reader reader = { 0 };
            cserde_token out = sentinel_token();

            check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
            check_equal(cserde_reader_next(&reader, &out), CSERDE_CALLBACK_ERROR);
            check_true(reader.state == CSERDE_READER_FAILED);
            check_true(reader.status == CSERDE_CALLBACK_ERROR);
            check_equal(context.calls, (size_t)1u);
        }
    }

    it("does not poison ready reader on caller precondition errors") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(NULL, &out), CSERDE_INVALID_ARGUMENT);
        check_equal(cserde_reader_next(&reader, NULL), CSERDE_INVALID_ARGUMENT);
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
        check_equal(context.calls, (size_t)0u);
    }

    it("rejects next on zero reader without callback") {
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_STATE);
        check_true(reader.state == CSERDE_READER_ZERO);
        check_true(reader.status == CSERDE_OK);
    }

    it("skips exactly one scalar value") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_SINT, .value.sint = INT64_C(7) },
            { .kind = CSERDE_UINT, .value.uint = UINT64_C(9) }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 0u), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(9));
    }

    it("skips nested arrays within depth budget") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_SINT, .value.sint = INT64_C(1) },
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_BOOL, .value.boolean = true },
            { .kind = CSERDE_ARRAY_END },
            { .kind = CSERDE_ARRAY_END },
            { .kind = CSERDE_UINT, .value.uint = UINT64_C(9) }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 2u), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(9));
    }

    it("allows a container value as a canonical map key") {
        static const unsigned char value_text[] = "value";
        const cserde_token tokens[] = {
            { .kind = CSERDE_MAP_BEGIN },
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_SINT, .value.sint = INT64_C(1) },
            { .kind = CSERDE_ARRAY_END },
            { .kind = CSERDE_STRING,
              .value.slice = { value_text, 5u, CSERDE_VIEW_STABLE } },
            { .kind = CSERDE_MAP_END },
            { .kind = CSERDE_NULL }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 2u), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_NULL);
    }

    it("rejects a mismatched end and keeps the error sticky") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_SINT, .value.sint = INT64_C(1) },
            { .kind = CSERDE_MAP_END }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_INVALID_TOKEN);
        check_true(reader.state == CSERDE_READER_FAILED);
        check_true(reader.status == CSERDE_INVALID_TOKEN);
        calls = context.calls;
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_equal(context.calls, calls);
    }

    it("rejects a map key without a value") {
        static const unsigned char key_text[] = "key";
        const cserde_token tokens[] = {
            { .kind = CSERDE_MAP_BEGIN },
            { .kind = CSERDE_STRING,
              .value.slice = { key_text, 3u, CSERDE_VIEW_STABLE } },
            { .kind = CSERDE_MAP_END }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_INVALID_TOKEN);
        calls = context.calls;
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_equal(context.calls, calls);
    }

    it("upgrades initial end of stream to unexpected end") {
        sequence_reader_context context = { 0 };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_UNEXPECTED_END);
        check_true(reader.state == CSERDE_READER_FAILED);
        check_true(reader.status == CSERDE_UNEXPECTED_END);
        calls = context.calls;
        check_equal(cserde_reader_next(&reader, &out), CSERDE_UNEXPECTED_END);
        check_equal(context.calls, calls);
    }

    it("upgrades an unclosed container end of stream to unexpected end") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_SINT, .value.sint = INT64_C(1) }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_UNEXPECTED_END);
        calls = context.calls;
        check_equal(cserde_reader_next(&reader, &out), CSERDE_UNEXPECTED_END);
        check_equal(context.calls, calls);
    }

    it("enforces container nesting depth") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_NULL },
            { .kind = CSERDE_ARRAY_END },
            { .kind = CSERDE_ARRAY_END }
        };
        sequence_reader_context shallow = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        sequence_reader_context exact = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader shallow_reader = { 0 };
        cserde_reader exact_reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&shallow_reader, &ops, &shallow),
                    CSERDE_OK);
        check_equal(cserde_reader_skip_value(&shallow_reader, 1u),
                    CSERDE_LIMIT_EXCEEDED);
        calls = shallow.calls;
        check_equal(cserde_reader_next(&shallow_reader, &out),
                    CSERDE_LIMIT_EXCEEDED);
        check_equal(shallow.calls, calls);

        check_equal(cserde_reader_init(&exact_reader, &ops, &exact), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&exact_reader, 2u), CSERDE_OK);
        check_equal(exact.index, exact.count);
    }

    it("does not charge scalar values against depth budget") {
        const cserde_token scalar_tokens[] = {
            { .kind = CSERDE_SINT, .value.sint = INT64_C(1) }
        };
        const cserde_token array_tokens[] = {
            { .kind = CSERDE_ARRAY_BEGIN },
            { .kind = CSERDE_ARRAY_END }
        };
        sequence_reader_context scalar = {
            .tokens = scalar_tokens,
            .count = sizeof(scalar_tokens) / sizeof(scalar_tokens[0])
        };
        sequence_reader_context array = {
            .tokens = array_tokens,
            .count = sizeof(array_tokens) / sizeof(array_tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader scalar_reader = { 0 };
        cserde_reader array_reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&scalar_reader, &ops, &scalar), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&scalar_reader, 0u), CSERDE_OK);

        check_equal(cserde_reader_init(&array_reader, &ops, &array), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&array_reader, 0u),
                    CSERDE_LIMIT_EXCEEDED);
        calls = array.calls;
        check_equal(cserde_reader_next(&array_reader, &out),
                    CSERDE_LIMIT_EXCEEDED);
        check_equal(array.calls, calls);
    }

    it("rejects a top level end marker") {
        const cserde_token tokens[] = {
            { .kind = CSERDE_ARRAY_END }
        };
        sequence_reader_context context = {
            .tokens = tokens,
            .count = sizeof(tokens) / sizeof(tokens[0])
        };
        cserde_reader_ops ops = reader_ops(sequence_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();
        size_t calls;

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_INVALID_TOKEN);
        calls = context.calls;
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_equal(context.calls, calls);
    }
}
