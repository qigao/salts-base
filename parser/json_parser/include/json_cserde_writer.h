/**
 * @file json_cserde_writer.h
 * @brief Streaming CSerde writer adapter for compact JSON.
 */
#ifndef JSON_CSERDE_WRITER_H
#define JSON_CSERDE_WRITER_H

#include <cserde/writer.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a streaming compact-JSON writer.
 *
 * The adapter owns no output buffer. Every emitted byte is forwarded to the
 * caller-owned sink. max_depth bounds simultaneously open arrays/objects;
 * scalar roots therefore accept zero. MAP keys must be STRING tokens.
 * CSERDE_BYTES is intentionally unsupported because JSON has no canonical
 * bytes representation in this adapter.
 */
cserde_writer *json_cserde_writer_create(
    cserde_byte_sink_fn sink,
    void *sink_context,
    size_t max_depth);

/** Destroy a writer without owning or freeing the caller sink context. */
void json_cserde_writer_destroy(cserde_writer *writer);

#ifdef __cplusplus
}
#endif

#endif /* JSON_CSERDE_WRITER_H */
