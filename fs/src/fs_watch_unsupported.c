#include "fs_watch_internal.h"

#include <error_codes.h>

int salts_fs_watch_backend_open(salts_fs_watch_impl *impl,
                                const char *path,
                                const salts_fs_watch_config *config) {
    (void)impl;
    (void)path;
    (void)config;
    return SALTS_ENOTSUP;
}

void salts_fs_watch_backend_request_close(salts_fs_watch_impl *impl) {
    (void)impl;
}

int salts_fs_watch_backend_destroy(salts_fs_watch_impl *impl) {
    (void)impl;
    return SALTS_ENOTSUP;
}
