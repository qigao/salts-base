#ifndef CSERDE_READER_H
#define CSERDE_READER_H

#include <cserde/status.h>
#include <cserde/token.h>

#include <stddef.h>
#include <stdint.h>

enum { CSERDE_READER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_reader_next_fn)(void *context,
                                                cserde_token *out);

typedef struct cserde_reader_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_reader_next_fn next;
} cserde_reader_ops;

typedef enum cserde_reader_state {
    CSERDE_READER_ZERO = 0,
    CSERDE_READER_READY,
    CSERDE_READER_DONE,
    CSERDE_READER_FAILED
} cserde_reader_state;

typedef struct cserde_reader {
    const cserde_reader_ops *ops;
    void *context;
    cserde_reader_state state;
    cserde_status status;
} cserde_reader;

#ifdef __cplusplus
extern "C" {
#endif

cserde_status cserde_reader_init(cserde_reader *reader,
                                  const cserde_reader_ops *ops,
                                  void *context);

cserde_status cserde_reader_next(cserde_reader *reader, cserde_token *out);

cserde_status cserde_reader_skip_value(cserde_reader *reader,
                                        size_t max_depth);

#ifdef __cplusplus
}
#endif

#endif /* CSERDE_READER_H */
