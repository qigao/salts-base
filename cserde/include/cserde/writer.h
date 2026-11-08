#ifndef CSERDE_WRITER_H
#define CSERDE_WRITER_H

#include <cserde/status.h>
#include <cserde/token.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CSERDE_WRITER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_writer_write_token_fn)(
    void *context,
    const cserde_token *token);

typedef cserde_status (*cserde_writer_finish_fn)(void *context);

typedef struct cserde_writer_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_writer_write_token_fn write;
    cserde_writer_finish_fn finish;
} cserde_writer_ops;

typedef enum cserde_writer_state {
    CSERDE_WRITER_ZERO = 0,
    CSERDE_WRITER_READY,
    CSERDE_WRITER_FINISHED,
    CSERDE_WRITER_FAILED
} cserde_writer_state;

typedef struct cserde_writer {
    const cserde_writer_ops *ops;
    void *context;
    cserde_writer_state state;
    cserde_status status;
} cserde_writer;

typedef cserde_status (*cserde_byte_sink_fn)(
    void *context,
    const void *data,
    size_t size);

cserde_status cserde_writer_init(cserde_writer *writer,
                                  const cserde_writer_ops *ops,
                                  void *context);

cserde_status cserde_writer_write(cserde_writer *writer,
                                   const cserde_token *token);

cserde_status cserde_writer_finish(cserde_writer *writer);

#ifdef __cplusplus
}
#endif

#endif /* CSERDE_WRITER_H */
