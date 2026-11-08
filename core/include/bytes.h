#ifndef SALTS_CORE_BYTES_H
#define SALTS_CORE_BYTES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-owning counted byte view. The caller retains the underlying storage.
 * data/len layout is intended for ABI-preserving migration from legacy vstr,
 * but vstr compatibility has NOT yet been certified or exported. */
typedef struct salts_core_bytes_view {
  const char *data;
  size_t len;
} salts_core_bytes_view;

enum {
  SALTS_CORE_OK = 0,
  SALTS_CORE_EINVAL = -1
};

/* Commit output only after validation. NULL input is valid for zero length. */
int salts_core_bytes_view_init(salts_core_bytes_view *out,
                               const void *data, size_t length);

/* Borrowed subview. start/length are byte offsets, not Unicode scalars. */
int salts_core_bytes_view_slice(salts_core_bytes_view view,
                                size_t start, size_t length,
                                salts_core_bytes_view *out);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CORE_BYTES_H */
