#include "recording.h"

#include <stddef.h>

static cserde_status cserde_recording_reader_next(void *context,
                                                   cserde_token *out) {
    cserde_recording_reader_context *state =
        (cserde_recording_reader_context *)context;

    if (state == NULL || out == NULL)
        return CSERDE_SOURCE_ERROR;
    if (state->index > state->count)
        return CSERDE_SOURCE_ERROR;
    if (state->count != 0u && state->tokens == NULL)
        return CSERDE_SOURCE_ERROR;
    if (state->index == state->count)
        return CSERDE_DONE;

    *out = state->tokens[state->index++];
    return CSERDE_OK;
}

static cserde_status cserde_recording_writer_write(
    void *context,
    const cserde_token *token) {
    cserde_recording_writer_context *state =
        (cserde_recording_writer_context *)context;

    if (state == NULL || token == NULL)
        return CSERDE_SINK_ERROR;
    if (state->count > state->capacity)
        return CSERDE_SINK_ERROR;
    if (state->capacity != 0u && state->tokens == NULL)
        return CSERDE_SINK_ERROR;
    if (state->count == state->capacity)
        return CSERDE_LIMIT_EXCEEDED;

    state->tokens[state->count++] = *token;
    return CSERDE_OK;
}

static cserde_status cserde_recording_writer_finish(void *context) {
    cserde_recording_writer_context *state =
        (cserde_recording_writer_context *)context;

    if (state == NULL)
        return CSERDE_SINK_ERROR;
    if (state->count > state->capacity)
        return CSERDE_SINK_ERROR;
    if (state->capacity != 0u && state->tokens == NULL)
        return CSERDE_SINK_ERROR;

    state->finished = true;
    return CSERDE_OK;
}

const cserde_reader_ops cserde_recording_reader_ops = {
    .struct_size = offsetof(cserde_reader_ops, next) +
                   sizeof(((cserde_reader_ops *)0)->next),
    .abi_version = CSERDE_READER_OPS_ABI_VERSION,
    .next = cserde_recording_reader_next
};

const cserde_writer_ops cserde_recording_writer_ops = {
    .struct_size = offsetof(cserde_writer_ops, finish) +
                   sizeof(((cserde_writer_ops *)0)->finish),
    .abi_version = CSERDE_WRITER_OPS_ABI_VERSION,
    .write = cserde_recording_writer_write,
    .finish = cserde_recording_writer_finish
};
