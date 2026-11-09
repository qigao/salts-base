/**
 * Salts Capture Abstraction
 *
 * Unified interface for audio/video/screen capture
 */
#ifndef SALTS_CAPTURE_H
#define SALTS_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Constants
 * ============================================================================= */

#define SALTS_CAPTURE_MAX_DEVICES   16
/* Maximum number of native video modes a device may expose. A 4K UVC
 * device can report several hundred combinations, so callers should not
 * rely on this value as an upper bound; pass a larger buffer and check
 * *out_count against capacity. */
#define SALTS_CAPTURE_MAX_VIDEO_MODES 512

/* =============================================================================
 * Types
 * ============================================================================= */

typedef enum {
    SALTS_CAPTURE_TYPE_AUDIO = 1,
    SALTS_CAPTURE_TYPE_VIDEO = 2,
    SALTS_CAPTURE_TYPE_SCREEN = 3,
    SALTS_CAPTURE_TYPE_GPU = 4
} capture_type_t;

typedef enum {
    SALTS_CAPTURE_OK = 0,
    SALTS_CAPTURE_ERR_NOMEM = -1,
    SALTS_CAPTURE_ERR_DEVICE = -2,
    SALTS_CAPTURE_ERR_FORMAT = -3,
    SALTS_CAPTURE_ERR_BUSY = -4,
    SALTS_CAPTURE_ERR_UNSUPPORTED = -5
} capture_result_t;

typedef enum {
    SALTS_CAPTURE_STATE_STOPPED = 0,
    SALTS_CAPTURE_STATE_STARTING,
    SALTS_CAPTURE_STATE_RUNNING,
    SALTS_CAPTURE_STATE_STOPPING,
    SALTS_CAPTURE_STATE_ERROR
} capture_state_t;

/* =============================================================================
 * Device Info
 * ============================================================================= */

typedef struct {
    int index;
    char name[256];
    char id[128];
    int is_default;
    capture_type_t type;
} capture_device_t;

/* =============================================================================
 * Capture Configuration
 * ============================================================================= */

typedef struct {
    int sample_rate;        /* 8000, 16000, 24000, 48000 */
    int channels;           /* 1 or 2 */
    int bits_per_sample;    /* 16 or 32 */
    int frame_size_ms;      /* Buffer size in ms (10, 20, etc.) */
} audio_capture_config_t;

typedef enum {
    SALTS_VIDEO_CAPTURE_FORMAT_I420 = 0,
    SALTS_VIDEO_CAPTURE_FORMAT_NV12 = 1,
    SALTS_VIDEO_CAPTURE_FORMAT_RGB24 = 2,
    SALTS_VIDEO_CAPTURE_FORMAT_BGRA = 3,
    /* Native MJPEG pass-through; each callback contains one JPEG image. */
    SALTS_VIDEO_CAPTURE_FORMAT_MJPEG = 4
} video_capture_format_t;

typedef enum {
    SALTS_CAMERA_CONTROL_ZOOM = 1,      /* Digital/hardware zoom, value is percent: 100 == 1.0x */
    SALTS_CAMERA_CONTROL_FOCUS = 2,
    SALTS_CAMERA_CONTROL_EXPOSURE = 3,
    SALTS_CAMERA_CONTROL_PAN = 4,
    SALTS_CAMERA_CONTROL_TILT = 5,
    SALTS_CAMERA_CONTROL_BRIGHTNESS = 6,
    SALTS_CAMERA_CONTROL_CONTRAST = 7,
    SALTS_CAMERA_CONTROL_HUE = 8,
    SALTS_CAMERA_CONTROL_WHITE_BALANCE = 9,
    SALTS_CAMERA_CONTROL_AUTO_EXPOSURE = 10 /* Mode: 0 manual, 1 automatic. */
} camera_control_t;

typedef struct {
    int min_value;
    int max_value;
    int step;
    int default_value;       /* Driver/backend default manual value */
    int current_value;
} camera_control_range_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} video_crop_t;

typedef struct {
    int width;
    int height;
    uint32_t framerate_numerator;
    uint32_t framerate_denominator;
    int format;             /* Callback output: video_capture_format_t */
    uint64_t mode_id;       /* Opaque backend-local mode identity */
} video_native_mode_t;

typedef struct {
    int monitor_index;      /* -1 for all monitors */
    int framerate;
    int capture_cursor;
    int capture_audio;      /* Capture system audio too */
} screen_capture_config_t;

/* =============================================================================
 * Callbacks
 * ============================================================================= */

/* Forward declaration */
typedef struct capture_s capture_t;
typedef struct video_device_s video_device_t;

/**
 * Called when audio samples are captured
 *
 * @param capture   Capture instance
 * @param samples   PCM samples (16-bit or 32-bit based on config)
 * @param len       Length in bytes
 * @param timestamp Capture timestamp in microseconds
 * @param user_data User context
 */
typedef void (*audio_capture_cb)(capture_t *capture,
                                        const uint8_t *samples, size_t len,
                                        uint64_t timestamp, void *user_data);

/**
 * Called when video frame is captured
 *
 * @param capture   Capture instance
 * @param frame     Borrowed frame data, valid only until the callback returns.
 *                  MJPEG frames contain one complete JPEG image.
 * @param len       Frame length in bytes
 * @param width     Frame width
 * @param height    Frame height
 * @param timestamp Capture timestamp in microseconds
 * @param user_data User context
 */
typedef void (*video_capture_cb)(capture_t *capture,
                                        const uint8_t *frame, size_t len,
                                        int width, int height,
                                        uint64_t timestamp, void *user_data);

/**
 * Called when capture state changes
 */
typedef void (*capture_state_cb)(capture_t *capture,
                                        capture_state_t state,
                                        void *user_data);

/* =============================================================================
 * Capture Instance
 * ============================================================================= */

struct capture_s {
    capture_type_t type;
    capture_state_t state;
    void *platform_ctx;     /* Platform-specific context */
    void *user_data;

    /* Callbacks */
    union {
        audio_capture_cb audio_cb;
        video_capture_cb video_cb;
    };
    capture_state_cb state_cb;
};

/* =============================================================================
 * Device Enumeration
 * ============================================================================= */

/**
 * List audio input devices (microphones)
 *
 * @param devices   Output array
 * @param max_count Maximum devices to return
 * @return          Number of devices found, or negative on error
 */
 int capture_list_audio_devices(capture_device_t *devices, int max_count);

/**
 * List video input devices (cameras)
 */
 int capture_list_video_devices(capture_device_t *devices, int max_count);

/**
 * List screens/monitors
 */
 int capture_list_screens(capture_device_t *devices, int max_count);

/**
 * List GPU adapters.
 *
 * Device indexes are platform adapter indexes and are not compacted when
 * unusable software adapters are skipped.
 */
 int capture_list_gpu_devices(capture_device_t *devices, int max_count);

/* =============================================================================
 * Audio Capture
 * ============================================================================= */

/**
 * Create audio capture instance
 *
 * @param device_id     Device ID returned by audio enumeration, or NULL/empty
 *                      for the default device. A non-empty ID is exact: unknown
 *                      or unsupported IDs fail instead of falling back.
 * @param config        Capture configuration
 * @return              Capture instance, or NULL on error
 */
 capture_t *audio_capture_create(const char *device_id,
                                                        const audio_capture_config_t *config);

/**
 * Set audio capture callback
 */
 void audio_capture_set_callback(capture_t *capture,
                                                  audio_capture_cb cb,
                                                  void *user_data);

/* =============================================================================
 * Video Capture
 * ============================================================================= */

/**
 * Nearest-integer frame rate of a native mode, in fps.
 * Returns 0 when the frame rate cannot be represented.
 */
 int video_mode_fps(const video_native_mode_t *mode);

/**
 * Standard broadcast/webcam frame rate check: 24/25/30/50/60/90/120 fps,
 * matched on the rounded integer rate (29.97 -> 30, 59.94 -> 60, ...).
 */
 int video_mode_is_standard_fps(
    const video_native_mode_t *mode);

/**
 * Open a video device adapter. The returned handle owns the platform device
 * enumeration context and must be closed with video_device_close().
 */
 int video_device_open(
    const char *device_id,
    video_device_t **out_device);

/** Close a video device adapter. Existing captures remain independently owned. */
 void video_device_close(video_device_t *device);

/**
 * List video capture modes, defaulting to standard frame rates
 * (24/25/30/50/60/90/120 fps, matched on the rounded integer rate).
 * Modes are not rounded or deduplicated, and are valid only for this
 * device handle and connection. Use video_device_list_modes_all()
 * to receive every native mode including non-standard frame rates.
 * The list is written up to capacity and *out_count reports the number
 * written. If *out_count reaches capacity, the device may expose more
 * modes; the caller should retry with a larger buffer.
 */
 int video_device_list_modes(
    video_device_t *device,
    video_native_mode_t *modes,
    size_t capacity,
    size_t *out_count);

/**
 * List every native video mode, including non-standard frame rates.
 * Same contract as video_device_list_modes() except no frame-rate
 * filtering is applied.
 */
 int video_device_list_modes_all(
    video_device_t *device,
    video_native_mode_t *modes,
    size_t capacity,
    size_t *out_count);

/**
 * Create a capture from an exact mode returned by
 * video_device_list_modes(). The backend validates mode_id and every
 * public field before creating the stream.
 */
 int video_device_create_capture(
    video_device_t *device,
    const video_native_mode_t *mode,
    capture_t **out_capture);

/**
 * Set video capture callback
 */
 void video_capture_set_callback(capture_t *capture,
                                                  video_capture_cb cb,
                                                  void *user_data);

/**
 * Query a camera control range.
 *
 * Not every backend/device supports every control. Unsupported controls return
 * SALTS_CAPTURE_ERR_UNSUPPORTED. Backends should prefer hardware controls and
 * may fall back to software controls when a hardware control is unavailable.
 * AUTO_EXPOSURE is hardware-only (currently Windows). Its bounds reflect the
 * supported modes (0/1), step is 1, and current_value is read from the driver.
 * default_value is -1 for this control: the driver does not expose a default mode.
 */
 int video_capture_get_control_range(capture_t *capture,
                                                     camera_control_t control,
                                                     camera_control_range_t *range);

/**
 * Set a camera control value.
 *
 * For SALTS_CAMERA_CONTROL_ZOOM, value is percent: 100 means 1.0x. Backends
 * should apply hardware zoom first and fall back to software zoom if needed.
 * For AUTO_EXPOSURE, value must be 0 (manual) or 1 (automatic); any other value
 * returns ERR_FORMAT. Switching mode retains the current exposure value. Missing
 * hardware/capability returns ERR_UNSUPPORTED; driver read/write errors return
 * ERR_DEVICE. There is no software fallback. Setting EXPOSURE still sets a
 * manual exposure value, independently of this mode control.
 * Example: video_capture_set_control(camera, SALTS_CAMERA_CONTROL_AUTO_EXPOSURE, 1).
 * Controls must be called by the capture owner, serialized with stop/destroy.
 */
 int video_capture_set_control(capture_t *capture,
                                               camera_control_t control,
                                               int value);

/** Read the driver value. AUTO_EXPOSURE returns 0/1, not the exposure magnitude.
 * On failure the output is unchanged. Null capture/output returns ERR_DEVICE.
 */
 int video_capture_get_control(capture_t *capture,
                                               camera_control_t control,
                                               int *value);

/**
 * Set a software crop region in capture output coordinates.
 *
 * Passing NULL or a rectangle with non-positive width/height disables crop.
 */
 int video_capture_set_crop(capture_t *capture,
                                            const video_crop_t *crop);

 int video_capture_get_crop(capture_t *capture,
                                            video_crop_t *crop);

/* =============================================================================
 * Screen Capture
 * ============================================================================= */

/**
 * Create screen capture instance
 */
 capture_t *screen_capture_create(const screen_capture_config_t *config);

/**
 * Set screen capture callback (uses video callback signature)
 */
 void screen_capture_set_callback(capture_t *capture,
                                                   video_capture_cb cb,
                                                   void *user_data);

/* =============================================================================
 * Common Functions
 * ============================================================================= */

/**
 * Start capture
 *
 * @return  0 on success
 */
 int capture_start(capture_t *capture);

/**
 * Stop capture
 */
 void capture_stop(capture_t *capture);

/**
 * Destroy capture instance
 */
 void capture_destroy(capture_t *capture);

/**
 * Get capture state
 */
 capture_state_t capture_get_state(capture_t *capture);

/**
 * Set state change callback
 */
 void capture_on_state(capture_t *capture,
                                        capture_state_cb cb);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CAPTURE_H */
