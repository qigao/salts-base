#ifndef CSERDE_TOKEN_H
#define CSERDE_TOKEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum cserde_view_lifetime {
    CSERDE_VIEW_TRANSIENT = 0,
    CSERDE_VIEW_STABLE
} cserde_view_lifetime;

typedef struct cserde_slice {
    const unsigned char *data;
    size_t size;
    cserde_view_lifetime lifetime;
} cserde_slice;

typedef enum cserde_token_kind {
    CSERDE_NULL = 0,
    CSERDE_BOOL,
    CSERDE_SINT,
    CSERDE_UINT,
    CSERDE_FLOAT,
    CSERDE_STRING,
    CSERDE_BYTES,
    CSERDE_ARRAY_BEGIN,
    CSERDE_ARRAY_END,
    CSERDE_MAP_BEGIN,
    CSERDE_MAP_END
} cserde_token_kind;

typedef struct cserde_token {
    cserde_token_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double floating;
        cserde_slice slice;
    } value;
} cserde_token;

#ifdef __cplusplus
extern "C" {
#endif

bool cserde_token_kind_valid(cserde_token_kind kind);
bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime);
bool cserde_token_valid(const cserde_token *token);

#ifdef __cplusplus
}
#endif

#endif /* CSERDE_TOKEN_H */
