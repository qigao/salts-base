#ifndef SALTS_CAPTURE_VIDEO_BACKEND_H
#define SALTS_CAPTURE_VIDEO_BACKEND_H

#include <capture.h>

typedef struct {
    int (*open_device)(const char *device_id, void **backend_ctx);
    void (*close_device)(void *backend_ctx);
    int (*list_modes)(void *backend_ctx,
                      video_native_mode_t *modes,
                      size_t capacity,
                      size_t *out_count);
    int (*create_capture)(void *backend_ctx,
                          const video_native_mode_t *mode,
                          capture_t **out_capture);
} video_backend_ops_t;

const video_backend_ops_t *video_platform_backend(void);

#endif
