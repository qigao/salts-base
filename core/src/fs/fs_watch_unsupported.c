#include "fs_watch_internal.h"

#include <salts/error_codes.h>

int fs_watch_backend_open(fs_watch_impl *impl,
                                const char *path,
                                const fs_watch_config *config) {
    (void)impl;
    (void)path;
    (void)config;
    return SALTS_ENOTSUP;
}

void fs_watch_backend_request_close(fs_watch_impl *impl) {
    (void)impl;
}

int fs_watch_backend_destroy(fs_watch_impl *impl) {
    (void)impl;
    return SALTS_ENOTSUP;
}
