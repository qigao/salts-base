#include <cserde/cserde.h>
#include "tinytest.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cserde_slice>::value, "slice ABI");
static_assert(std::is_standard_layout<cserde_token>::value, "token ABI");
static_assert(std::is_standard_layout<cserde_reader_ops>::value,
              "reader ops ABI");
static_assert(std::is_standard_layout<cserde_reader>::value,
              "reader facade ABI");
static_assert(std::is_standard_layout<cserde_writer_ops>::value,
              "writer ops ABI");
static_assert(std::is_standard_layout<cserde_writer>::value,
              "writer facade ABI");
static_assert(CSERDE_READER_OPS_ABI_VERSION == 1u, "reader ABI version");
static_assert(CSERDE_WRITER_OPS_ABI_VERSION == 1u, "writer ABI version");

struct cpp_reader_context {
    bool emitted;
};

struct cpp_writer_context {
    std::size_t writes;
    std::size_t finishes;
};

static cserde_status cpp_reader_next(void *context, cserde_token *out) {
    auto *state = static_cast<cpp_reader_context *>(context);

    if (state->emitted)
        return CSERDE_DONE;
    out->kind = CSERDE_NULL;
    state->emitted = true;
    return CSERDE_OK;
}

static cserde_status cpp_writer_write(void *context,
                                      const cserde_token *token) {
    auto *state = static_cast<cpp_writer_context *>(context);

    if (token == nullptr)
        return CSERDE_SINK_ERROR;
    ++state->writes;
    return CSERDE_OK;
}

static cserde_status cpp_writer_finish(void *context) {
    auto *state = static_cast<cpp_writer_context *>(context);

    ++state->finishes;
    return CSERDE_OK;
}

spec("CSerde C++17 public linkage") {
    it("calls the C token reader and writer facades") {
        cpp_reader_context reader_context{};
        cpp_writer_context writer_context{};
        cserde_reader_ops reader_ops = {
            offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
            static_cast<std::uint32_t>(CSERDE_READER_OPS_ABI_VERSION),
            cpp_reader_next
        };
        cserde_writer_ops writer_ops = {
            offsetof(cserde_writer_ops, finish) + sizeof(cserde_writer_finish_fn),
            static_cast<std::uint32_t>(CSERDE_WRITER_OPS_ABI_VERSION),
            cpp_writer_write,
            cpp_writer_finish
        };
        cserde_reader reader{};
        cserde_writer writer{};
        cserde_token token{};

        token.kind = CSERDE_NULL;
        check_true(cserde_token_valid(&token));

        check_equal(cserde_reader_init(&reader, &reader_ops, &reader_context),
                    CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &token), CSERDE_OK);
        check_true(token.kind == CSERDE_NULL);
        check_equal(cserde_reader_next(&reader, &token), CSERDE_DONE);

        check_equal(cserde_writer_init(&writer, &writer_ops, &writer_context),
                    CSERDE_OK);
        check_equal(cserde_writer_write(&writer, &token), CSERDE_OK);
        check_equal(cserde_writer_finish(&writer), CSERDE_OK);
        check_equal(writer_context.writes, static_cast<std::size_t>(1u));
        check_equal(writer_context.finishes, static_cast<std::size_t>(1u));
    }
}
