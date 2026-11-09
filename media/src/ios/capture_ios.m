/**
 * iOS Capture Dispatcher
 *
 * Adapts the iOS native implementations to the Salts capture API.
 */

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include <capture.h>
#include "capture_video_ios_backend.h"
#include <stdlib.h>
#include <string.h>

extern int ios_audio_start(capture_t *capture);
extern void ios_audio_stop(capture_t *capture);
extern void ios_audio_destroy(capture_t *capture);

extern int ios_video_start(capture_t *capture);
extern void ios_video_stop(capture_t *capture);
extern void ios_video_destroy(capture_t *capture);

extern int ios_screen_start(capture_t *capture);
extern void ios_screen_stop(capture_t *capture);
extern void ios_screen_destroy(capture_t *capture);

static void copy_nsstring(char *dst, size_t dst_size, NSString *value) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    const char *utf8 = value ? [value UTF8String] : NULL;
    if (utf8) {
        strncpy(dst, utf8, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}
AVCaptureDevice *ios_video_find_device(const char *device_id) {
    if (device_id && device_id[0]) {
        NSString *unique_id = [NSString stringWithUTF8String:device_id];
        return unique_id ? [AVCaptureDevice deviceWithUniqueID:unique_id] : nil;
    }
    return [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
}

static uint64_t ios_video_mode_id(uint32_t format_index,
                                  uint32_t range_index,
                                  uint32_t endpoint) {
    return ((uint64_t)format_index << 32) |
           ((uint64_t)range_index << 1) |
           endpoint;
}

int ios_video_make_mode(AVCaptureDevice *device,
                        uint32_t format_index,
                        uint32_t range_index,
                        uint32_t endpoint,
                        video_native_mode_t *mode,
                        AVCaptureDeviceFormat **out_format,
                        CMTime *out_duration) {
    if (!device || !mode || endpoint > 1 ||
        format_index >= (uint32_t)device.formats.count) {
        return SALTS_CAPTURE_ERR_FORMAT;
    }

    AVCaptureDeviceFormat *format = device.formats[format_index];
    NSArray<AVFrameRateRange *> *ranges = format.videoSupportedFrameRateRanges;
    if (range_index >= (uint32_t)ranges.count) return SALTS_CAPTURE_ERR_FORMAT;

    AVFrameRateRange *range = ranges[range_index];
    CMTime duration = endpoint == 0 ? range.minFrameDuration
                                    : range.maxFrameDuration;
    CMVideoDimensions dimensions =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    if (!CMTIME_IS_NUMERIC(duration) || duration.value <= 0 ||
        duration.timescale <= 0 ||
        (uint64_t)duration.value > UINT32_MAX ||
        (uint64_t)duration.timescale > UINT32_MAX ||
        dimensions.width <= 0 || dimensions.height <= 0) {
        return SALTS_CAPTURE_ERR_FORMAT;
    }

    mode->width = dimensions.width;
    mode->height = dimensions.height;
    mode->framerate_numerator = (uint32_t)duration.timescale;
    mode->framerate_denominator = (uint32_t)duration.value;
    mode->format = SALTS_VIDEO_CAPTURE_FORMAT_NV12;
    mode->mode_id = ios_video_mode_id(format_index, range_index, endpoint);
    if (out_format) *out_format = format;
    if (out_duration) *out_duration = duration;
    return SALTS_CAPTURE_OK;
}

int ios_video_modes_equal(const video_native_mode_t *lhs,
                          const video_native_mode_t *rhs) {
    return lhs->width == rhs->width && lhs->height == rhs->height &&
           lhs->framerate_numerator == rhs->framerate_numerator &&
           lhs->framerate_denominator == rhs->framerate_denominator &&
           lhs->format == rhs->format && lhs->mode_id == rhs->mode_id;
}

int capture_list_video_devices(capture_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) return -1;

    @autoreleasepool {
        NSMutableArray<AVCaptureDeviceType> *device_types =
            [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 170000
        if (@available(iOS 17.0, *)) {
            [device_types addObject:AVCaptureDeviceTypeExternal];
        }
#endif
        AVCaptureDeviceDiscoverySession *session =
            [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:device_types
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];

        int count = 0;
        for (AVCaptureDevice *device in session.devices) {
            if (count >= max_count) break;
            capture_device_t *out = &devices[count];
            memset(out, 0, sizeof(*out));
            out->index = count;
            out->type = SALTS_CAPTURE_TYPE_VIDEO;
            out->is_default = (count == 0) ? 1 : 0;
            copy_nsstring(out->id, sizeof(out->id), device.uniqueID);
            copy_nsstring(out->name, sizeof(out->name), device.localizedName);
            count++;
        }
        return count;
    }
}

static int ios_video_device_open(const char *device_id, void **backend_ctx) {
    ios_video_device_ctx_t *ctx;

    if (!backend_ctx) return SALTS_CAPTURE_ERR_FORMAT;
    *backend_ctx = NULL;
    @autoreleasepool {
        AVCaptureDevice *device = ios_video_find_device(device_id);
        const char *unique_id;
        if (!device) return SALTS_CAPTURE_ERR_DEVICE;
        unique_id = [device.uniqueID UTF8String];
        if (!unique_id || strlen(unique_id) >= sizeof(ctx->device_id)) {
            return SALTS_CAPTURE_ERR_FORMAT;
        }

        ctx = (ios_video_device_ctx_t *)calloc(1, sizeof(*ctx));
        if (!ctx) return SALTS_CAPTURE_ERR_NOMEM;
        memcpy(ctx->device_id, unique_id, strlen(unique_id) + 1);
        *backend_ctx = ctx;
        return SALTS_CAPTURE_OK;
    }
}

static void ios_video_device_close(void *backend_ctx) {
    free(backend_ctx);
}

static int ios_video_device_list_modes(
    void *backend_ctx,
    video_native_mode_t *modes,
    size_t capacity,
    size_t *out_count) {
    ios_video_device_ctx_t *ctx = (ios_video_device_ctx_t *)backend_ctx;
    size_t count = 0;

    if (!ctx || !modes || capacity == 0 || !out_count) {
        return SALTS_CAPTURE_ERR_FORMAT;
    }
    memset(modes, 0, sizeof(*modes) * capacity);

    @autoreleasepool {
        AVCaptureDevice *device = ios_video_find_device(ctx->device_id);
        if (!device) return SALTS_CAPTURE_ERR_DEVICE;

        for (uint32_t format_index = 0;
             format_index < (uint32_t)device.formats.count;
             ++format_index) {
            AVCaptureDeviceFormat *format = device.formats[format_index];
            NSArray<AVFrameRateRange *> *ranges =
                format.videoSupportedFrameRateRanges;
            for (uint32_t range_index = 0;
                 range_index < (uint32_t)ranges.count;
                 ++range_index) {
                AVFrameRateRange *range = ranges[range_index];
                if (ios_video_make_mode(device, format_index, range_index, 0,
                                        &modes[count], NULL, NULL) ==
                    SALTS_CAPTURE_OK) {
                    if (++count == capacity) goto done;
                }
                if (CMTimeCompare(range.minFrameDuration,
                                  range.maxFrameDuration) != 0 &&
                    ios_video_make_mode(device, format_index, range_index, 1,
                                        &modes[count], NULL, NULL) ==
                        SALTS_CAPTURE_OK) {
                    if (++count == capacity) goto done;
                }
            }
        }
done:
        *out_count = count;
        return SALTS_CAPTURE_OK;
    }
}

const video_backend_ops_t *video_platform_backend(void) {
    static const video_backend_ops_t ops = {
        ios_video_device_open,
        ios_video_device_close,
        ios_video_device_list_modes,
        ios_video_device_create_capture
    };
    return &ops;
}

int capture_list_audio_devices(capture_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) return -1;

    @autoreleasepool {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                            error:&error]) {
            return SALTS_CAPTURE_ERR_DEVICE;
        }
        NSArray<AVAudioSessionPortDescription *> *audio_inputs =
            session.availableInputs;
        AVAudioSessionPortDescription *preferred = session.preferredInput;
        int count = 0;

        for (AVAudioSessionPortDescription *input in audio_inputs) {
            if (count >= max_count) break;
            capture_device_t *out = &devices[count];
            memset(out, 0, sizeof(*out));
            out->index = count;
            out->type = SALTS_CAPTURE_TYPE_AUDIO;
            out->is_default =
                (preferred && [preferred.UID isEqualToString:input.UID]) ||
                (!preferred && count == 0)
                    ? 1
                    : 0;
            copy_nsstring(out->id, sizeof(out->id), input.UID);
            copy_nsstring(out->name, sizeof(out->name), input.portName);
            count++;
        }
        return count;
    }
}

int capture_list_screens(capture_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) return -1;

    memset(&devices[0], 0, sizeof(devices[0]));
    devices[0].index = 0;
    devices[0].type = SALTS_CAPTURE_TYPE_SCREEN;
    devices[0].is_default = 1;
    strncpy(devices[0].id, "screen:0", sizeof(devices[0].id) - 1);
    strncpy(devices[0].name, "Main Screen", sizeof(devices[0].name) - 1);
    return 1;
}

int capture_start(capture_t *capture) {
    if (!capture) return -1;
    if (capture->state == SALTS_CAPTURE_STATE_RUNNING) return 0;

    capture->state = SALTS_CAPTURE_STATE_STARTING;

    int result = -1;
    switch (capture->type) {
        case SALTS_CAPTURE_TYPE_AUDIO:
            result = ios_audio_start(capture);
            break;
        case SALTS_CAPTURE_TYPE_VIDEO:
            result = ios_video_start(capture);
            break;
        case SALTS_CAPTURE_TYPE_SCREEN:
            result = ios_screen_start(capture);
            break;
        default:
            result = -1;
            break;
    }

    capture->state = (result == 0) ? SALTS_CAPTURE_STATE_RUNNING : SALTS_CAPTURE_STATE_ERROR;
    if (capture->state_cb) {
        capture->state_cb(capture, capture->state, capture->user_data);
    }
    return result;
}

void capture_stop(capture_t *capture) {
    if (!capture || capture->state == SALTS_CAPTURE_STATE_STOPPED) return;

    capture->state = SALTS_CAPTURE_STATE_STOPPING;

    switch (capture->type) {
        case SALTS_CAPTURE_TYPE_AUDIO:
            ios_audio_stop(capture);
            break;
        case SALTS_CAPTURE_TYPE_VIDEO:
            ios_video_stop(capture);
            break;
        case SALTS_CAPTURE_TYPE_SCREEN:
            ios_screen_stop(capture);
            break;
        default:
            break;
    }

    capture->state = SALTS_CAPTURE_STATE_STOPPED;
    if (capture->state_cb) {
        capture->state_cb(capture, capture->state, capture->user_data);
    }
}

void capture_destroy(capture_t *capture) {
    if (!capture) return;

    switch (capture->type) {
        case SALTS_CAPTURE_TYPE_AUDIO:
            ios_audio_destroy(capture);
            break;
        case SALTS_CAPTURE_TYPE_VIDEO:
            ios_video_destroy(capture);
            break;
        case SALTS_CAPTURE_TYPE_SCREEN:
            ios_screen_destroy(capture);
            break;
        default:
            free(capture);
            break;
    }
}

capture_state_t capture_get_state(capture_t *capture) {
    return capture ? capture->state : SALTS_CAPTURE_STATE_STOPPED;
}

void capture_on_state(capture_t *capture, capture_state_cb cb) {
    if (capture) capture->state_cb = cb;
}

void audio_capture_set_callback(capture_t *capture,
                                      audio_capture_cb cb,
                                      void *user_data) {
    if (!capture || capture->type != SALTS_CAPTURE_TYPE_AUDIO) return;
    capture->audio_cb = cb;
    capture->user_data = user_data;
}

void video_capture_set_callback(capture_t *capture,
                                      video_capture_cb cb,
                                      void *user_data) {
    if (!capture || capture->type != SALTS_CAPTURE_TYPE_VIDEO) return;
    capture->video_cb = cb;
    capture->user_data = user_data;
}

void screen_capture_set_callback(capture_t *capture,
                                       video_capture_cb cb,
                                       void *user_data) {
    if (!capture || capture->type != SALTS_CAPTURE_TYPE_SCREEN) return;
    capture->video_cb = cb;
    capture->user_data = user_data;
}

/* Explicit iOS platform API contract; no generated CMake compatibility unit. */
int capture_list_gpu_devices(capture_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) return -1;
    return 0;
}

int video_capture_get_control_range(
        capture_t *capture, camera_control_t control,
        camera_control_range_t *range) {
    (void)capture;
    (void)control;
    if (!range) return SALTS_CAPTURE_ERR_DEVICE;
    memset(range, 0, sizeof(*range));
    return SALTS_CAPTURE_ERR_UNSUPPORTED;
}

int video_capture_set_control(
        capture_t *capture, camera_control_t control, int value) {
    (void)capture;
    (void)control;
    (void)value;
    return SALTS_CAPTURE_ERR_UNSUPPORTED;
}

int video_capture_get_control(
        capture_t *capture, camera_control_t control, int *value) {
    (void)capture;
    (void)control;
    if (value) *value = 0;
    return SALTS_CAPTURE_ERR_UNSUPPORTED;
}

int video_capture_set_crop(
        capture_t *capture, const video_crop_t *crop) {
    (void)capture;
    (void)crop;
    return SALTS_CAPTURE_ERR_UNSUPPORTED;
}

int video_capture_get_crop(
        capture_t *capture, video_crop_t *crop) {
    (void)capture;
    if (!crop) return SALTS_CAPTURE_ERR_DEVICE;
    memset(crop, 0, sizeof(*crop));
    return SALTS_CAPTURE_ERR_UNSUPPORTED;
}
