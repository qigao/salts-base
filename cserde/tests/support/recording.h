#ifndef CSERDE_TEST_RECORDING_H
#define CSERDE_TEST_RECORDING_H

#include <cserde/cserde.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct cserde_recording_reader_context {
    const cserde_token *tokens;
    size_t count;
    size_t index;
} cserde_recording_reader_context;

typedef struct cserde_recording_writer_context {
    cserde_token *tokens;
    size_t capacity;
    size_t count;
    bool finished;
} cserde_recording_writer_context;

#ifdef __cplusplus
extern "C" {
#endif

extern const cserde_reader_ops cserde_recording_reader_ops;
extern const cserde_writer_ops cserde_recording_writer_ops;

#ifdef __cplusplus
}
#endif

#endif /* CSERDE_TEST_RECORDING_H */
