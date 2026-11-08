#include <cserde/writer.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fake_writer_context {
    cserde_status write_status;
    cserde_status finish_status;
    size_t write_calls;
    size_t finish_calls;
    cserde_token observed;
} fake_writer_context;

static cserde_status fake_writer_write(void *context,
                                        const cserde_token *token) {
    fake_writer_context *state = (fake_writer_context *)context;

    ++state->write_calls;
    if (state->write_status == CSERDE_OK)
        state->observed = *token;
    return state->write_status;
}

static cserde_status fake_writer_finish(void *context) {
    fake_writer_context *state = (fake_writer_context *)context;

    ++state->finish_calls;
    return state->finish_status;
}

static cserde_status null_context_writer_write(void *context,
                                                const cserde_token *token) {
    (void)token;
    return context == NULL ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static cserde_status null_context_writer_finish(void *context) {
    return context == NULL ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static cserde_status fake_byte_sink(void *context,
                                     const void *data,
                                     size_t size) {
    (void)context;
    (void)data;
    (void)size;
    return CSERDE_OK;
}

#define WRITER_PREFIX \
    (offsetof(cserde_writer_ops, finish) + \
     sizeof(((cserde_writer_ops *)0)->finish))

static cserde_writer_ops writer_ops(cserde_writer_write_token_fn write,
                                    cserde_writer_finish_fn finish) {
    return (cserde_writer_ops){
        .struct_size = WRITER_PREFIX,
        .abi_version = CSERDE_WRITER_OPS_ABI_VERSION,
        .write = write,
        .finish = finish
    };
}

spec("CSerde push writer") {
    it("accepts exact v1 ops prefix") {
        fake_writer_context context = { 0 };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_true(writer.ops == &ops);
        check_true(writer.context == &context);
        check_true(writer.state == CSERDE_WRITER_READY);
        check_true(writer.status == CSERDE_OK);
    }

    it("rejects malformed ops without mutating zero writer") {
        fake_writer_context context = { 0 };
        cserde_writer_ops short_ops = writer_ops(fake_writer_write,
                                                  fake_writer_finish);
        cserde_writer_ops bad_abi = writer_ops(fake_writer_write,
                                                fake_writer_finish);
        cserde_writer_ops no_write = writer_ops(NULL, fake_writer_finish);
        cserde_writer_ops no_finish = writer_ops(fake_writer_write, NULL);
        cserde_writer writer = { 0 };

        short_ops.struct_size = WRITER_PREFIX - 1u;
        check_equal(cserde_writer_init(&writer, &short_ops, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(writer.ops == NULL);
        check_true(writer.context == NULL);
        check_true(writer.state == CSERDE_WRITER_ZERO);
        check_true(writer.status == CSERDE_OK);

        bad_abi.abi_version = CSERDE_WRITER_OPS_ABI_VERSION + 1u;
        check_equal(cserde_writer_init(&writer, &bad_abi, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(writer.ops == NULL);
        check_true(writer.state == CSERDE_WRITER_ZERO);

        check_equal(cserde_writer_init(&writer, &no_write, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(writer.ops == NULL);
        check_true(writer.state == CSERDE_WRITER_ZERO);

        check_equal(cserde_writer_init(&writer, &no_finish, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_true(writer.ops == NULL);
        check_true(writer.state == CSERDE_WRITER_ZERO);
    }

    it("accepts a null provider context") {
        cserde_writer_ops ops = writer_ops(null_context_writer_write,
                                            null_context_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = { .kind = CSERDE_NULL };

        check_equal(cserde_writer_init(&writer, &ops, NULL), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_OK);
        check_equal(cserde_writer_finish(&writer), CSERDE_OK);
        check_true(writer.state == CSERDE_WRITER_FINISHED);
    }

    it("forwards one valid token") {
        fake_writer_context context = { 0 };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = {
            .kind = CSERDE_UINT,
            .value.uint = UINT64_C(19)
        };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_OK);
        check_equal(context.write_calls, (size_t)1u);
        check_true(context.observed.kind == CSERDE_UINT);
        check_equal(context.observed.value.uint, UINT64_C(19));
        check_true(writer.state == CSERDE_WRITER_READY);
        check_true(writer.status == CSERDE_OK);
    }

    it("rejects an invalid token without calling or poisoning provider") {
        fake_writer_context context = { 0 };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = {
            .kind = CSERDE_STRING,
            .value.slice = { NULL, 1u, CSERDE_VIEW_STABLE }
        };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_INVALID_TOKEN);
        check_equal(context.write_calls, (size_t)0u);
        check_true(writer.state == CSERDE_WRITER_READY);
        check_true(writer.status == CSERDE_OK);
    }

    it("does not poison ready writer on caller precondition errors") {
        fake_writer_context context = { 0 };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = { .kind = CSERDE_NULL };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_equal(cserde_writer_write(NULL, &token), CSERDE_INVALID_ARGUMENT);
        check_equal(cserde_writer_write(&writer, NULL), CSERDE_INVALID_ARGUMENT);
        check_equal(cserde_writer_finish(NULL), CSERDE_INVALID_ARGUMENT);
        check_equal(context.write_calls, (size_t)0u);
        check_equal(context.finish_calls, (size_t)0u);
        check_true(writer.state == CSERDE_WRITER_READY);
        check_true(writer.status == CSERDE_OK);
    }

    it("keeps allowed write callback errors sticky") {
        const cserde_status errors[] = {
            CSERDE_LIMIT_EXCEEDED,
            CSERDE_UNSUPPORTED,
            CSERDE_SINK_ERROR
        };
        size_t i;

        for (i = 0u; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            fake_writer_context context = { .write_status = errors[i] };
            cserde_writer_ops ops = writer_ops(fake_writer_write,
                                                fake_writer_finish);
            cserde_writer writer = { 0 };
            cserde_token token = { .kind = CSERDE_NULL };

            check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
            check_equal(cserde_writer_write(&writer, &token), errors[i]);
            check_true(writer.state == CSERDE_WRITER_FAILED);
            check_true(writer.status == errors[i]);
            check_equal(context.write_calls, (size_t)1u);
            check_equal(cserde_writer_write(&writer, &token), errors[i]);
            check_equal(cserde_writer_finish(&writer), errors[i]);
            check_equal(context.write_calls, (size_t)1u);
            check_equal(context.finish_calls, (size_t)0u);
        }
    }

    it("normalizes disallowed write callback statuses") {
        const cserde_status statuses[] = {
            CSERDE_VALUE_OUT_OF_RANGE,
            CSERDE_SOURCE_ERROR,
            CSERDE_DONE,
            (cserde_status)999
        };
        size_t i;

        for (i = 0u; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
            fake_writer_context context = { .write_status = statuses[i] };
            cserde_writer_ops ops = writer_ops(fake_writer_write,
                                                fake_writer_finish);
            cserde_writer writer = { 0 };
            cserde_token token = { .kind = CSERDE_NULL };

            check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
            check_equal(cserde_writer_write(&writer, &token),
                        CSERDE_CALLBACK_ERROR);
            check_true(writer.state == CSERDE_WRITER_FAILED);
            check_true(writer.status == CSERDE_CALLBACK_ERROR);
            check_equal(context.write_calls, (size_t)1u);
        }
    }

    it("finishes once and makes finished state terminal") {
        fake_writer_context context = { 0 };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = { .kind = CSERDE_NULL };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_equal(cserde_writer_finish(&writer), CSERDE_OK);
        check_true(writer.state == CSERDE_WRITER_FINISHED);
        check_true(writer.status == CSERDE_OK);
        check_equal(context.finish_calls, (size_t)1u);
        check_equal(cserde_writer_finish(&writer), CSERDE_INVALID_STATE);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_INVALID_STATE);
        check_equal(context.finish_calls, (size_t)1u);
        check_equal(context.write_calls, (size_t)0u);
    }

    it("keeps an allowed finish error sticky") {
        fake_writer_context context = { .finish_status = CSERDE_SINK_ERROR };
        cserde_writer_ops ops = writer_ops(fake_writer_write,
                                            fake_writer_finish);
        cserde_writer writer = { 0 };
        cserde_token token = { .kind = CSERDE_NULL };

        check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
        check_equal(cserde_writer_finish(&writer), CSERDE_SINK_ERROR);
        check_true(writer.state == CSERDE_WRITER_FAILED);
        check_true(writer.status == CSERDE_SINK_ERROR);
        check_equal(context.finish_calls, (size_t)1u);
        check_equal(cserde_writer_finish(&writer), CSERDE_SINK_ERROR);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_SINK_ERROR);
        check_equal(context.finish_calls, (size_t)1u);
        check_equal(context.write_calls, (size_t)0u);
    }

    it("normalizes disallowed finish callback statuses") {
        const cserde_status statuses[] = {
            CSERDE_VALUE_OUT_OF_RANGE,
            CSERDE_SOURCE_ERROR
        };
        size_t i;

        for (i = 0u; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
            fake_writer_context context = { .finish_status = statuses[i] };
            cserde_writer_ops ops = writer_ops(fake_writer_write,
                                                fake_writer_finish);
            cserde_writer writer = { 0 };

            check_equal(cserde_writer_init(&writer, &ops, &context), CSERDE_OK);
            check_equal(cserde_writer_finish(&writer), CSERDE_CALLBACK_ERROR);
            check_true(writer.state == CSERDE_WRITER_FAILED);
            check_true(writer.status == CSERDE_CALLBACK_ERROR);
            check_equal(context.finish_calls, (size_t)1u);
        }
    }

    it("exposes the byte sink callback without casts") {
        cserde_byte_sink_fn sink = fake_byte_sink;

        check_true(sink != NULL);
        check_equal(sink(NULL, NULL, 0u), CSERDE_OK);
    }
}
