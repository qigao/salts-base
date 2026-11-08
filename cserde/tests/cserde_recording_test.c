#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

static const unsigned char name[] = "alice";
static const cserde_token input[] = {
    { .kind = CSERDE_MAP_BEGIN },
    { .kind = CSERDE_STRING,
      .value.slice = { name, 5u, CSERDE_VIEW_STABLE } },
    { .kind = CSERDE_UINT, .value.uint = UINT64_C(7) },
    { .kind = CSERDE_MAP_END }
};

spec("CSerde recording providers") {
    it("reads a fixed token array then reports done") {
        cserde_recording_reader_context context = {
            .tokens = input,
            .count = sizeof(input) / sizeof(input[0])
        };
        cserde_reader reader = { 0 };
        cserde_token token = { 0 };
        size_t index;

        check_equal(cserde_reader_init(
                        &reader, &cserde_recording_reader_ops, &context),
                    CSERDE_OK);
        for (index = 0u; index < context.count; ++index) {
            check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
            check_true(token.kind == input[index].kind);
            if (token.kind == CSERDE_STRING) {
                check_true(token.value.slice.data == name);
                check_equal(token.value.slice.size, (size_t)5u);
                check_true(token.value.slice.lifetime == CSERDE_VIEW_STABLE);
            }
        }
        check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
        check_true(reader.state == CSERDE_READER_DONE);
        check_equal(context.index, context.count);
    }

    it("records a fixed token sequence and finishes") {
        cserde_token output[4] = { 0 };
        cserde_recording_writer_context context = {
            .tokens = output,
            .capacity = sizeof(output) / sizeof(output[0])
        };
        cserde_writer writer = { 0 };
        size_t index;

        check_equal(cserde_writer_init(
                        &writer, &cserde_recording_writer_ops, &context),
                    CSERDE_OK);
        for (index = 0u; index < sizeof(input) / sizeof(input[0]); ++index)
            check_equal(cserde_writer_write(&writer, &input[index]), CSERDE_OK);
        check_equal(cserde_writer_finish(&writer), CSERDE_OK);

        check_equal(context.count, (size_t)4u);
        check_true(context.finished);
        check_true(writer.state == CSERDE_WRITER_FINISHED);
        check_true(output[1].kind == CSERDE_STRING);
        check_true(output[1].value.slice.data == name);
        check_equal(output[1].value.slice.size, (size_t)5u);
        check_true(output[1].value.slice.lifetime == CSERDE_VIEW_STABLE);
        check_equal(output[2].value.uint, UINT64_C(7));
    }

    it("fails without writing beyond fixed capacity") {
        cserde_token output[3] = { 0 };
        cserde_recording_writer_context context = {
            .tokens = output,
            .capacity = sizeof(output) / sizeof(output[0])
        };
        cserde_writer writer = { 0 };

        check_equal(cserde_writer_init(
                        &writer, &cserde_recording_writer_ops, &context),
                    CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &input[0]), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &input[1]), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &input[2]), CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &input[3]),
                    CSERDE_LIMIT_EXCEEDED);

        check_equal(context.count, (size_t)3u);
        check_false(context.finished);
        check_true(writer.state == CSERDE_WRITER_FAILED);
        check_true(writer.status == CSERDE_LIMIT_EXCEEDED);
        check_true(output[1].value.slice.data == name);
        check_equal(cserde_writer_finish(&writer), CSERDE_LIMIT_EXCEEDED);
        check_false(context.finished);
    }

    it("feeds the generic nested value skipper") {
        cserde_recording_reader_context context = {
            .tokens = input,
            .count = sizeof(input) / sizeof(input[0])
        };
        cserde_reader reader = { 0 };
        cserde_token token = { 0 };

        check_equal(cserde_reader_init(
                        &reader, &cserde_recording_reader_ops, &context),
                    CSERDE_OK);
        check_equal(cserde_reader_skip_value(&reader, 1u), CSERDE_OK);
        check_equal(context.index, context.count);
        check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);
    }
}
