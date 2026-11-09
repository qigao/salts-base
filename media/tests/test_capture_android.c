#include <capture.h>
#include <capture_android.h>
#include <tinytest.h>

suite("capture Android MediaProjection surface") {
  it("rejects invalid explicit screen dimensions") {
    android_screen_capture_config_t config = {0, 720, 30};
    capture_t *capture = (capture_t *)1;

    check_equal(android_screen_capture_create(&config, &capture),
                SALTS_CAPTURE_ERR_FORMAT);
    check_null(capture);
  }

  it("creates a screen capture with an observable frame count") {
    android_screen_capture_config_t config = {640, 360, 24};
    capture_t *capture = NULL;
    uint64_t frame_count = UINT64_MAX;
    jobject surface = (jobject)1;

    check_equal(android_screen_capture_create(&config, &capture),
                SALTS_CAPTURE_OK);
    check_not_null(capture);
    if (!capture) return;

    check_equal(android_screen_capture_get_frame_count(
                    capture, &frame_count),
                SALTS_CAPTURE_OK);
    check_equal(frame_count, 0);
    check_equal(android_screen_capture_get_surface(
                    NULL, capture, &surface),
                SALTS_CAPTURE_ERR_FORMAT);
    check_null(surface);
    capture_destroy(capture);
  }
}

suite("capture Android audio device identity") {
  it("rejects unsupported explicit audio identities") {
    check_null(audio_capture_create("not-default", NULL));
  }

  it("rejects unsupported 32-bit audio capture instead of changing format") {
    audio_capture_config_t config = {48000, 1, 32, 20};
    check_null(audio_capture_create(NULL, &config));
  }
}

suite("capture Android lifecycle") {
  it("starts the native screen lifecycle before Java produces frames") {
    screen_capture_config_t config = {0, 30, 1, 0};
    capture_t *capture = screen_capture_create(&config);
    uint64_t frame_count = UINT64_MAX;

    check_not_null(capture);
    if (!capture) return;

    check_equal(capture_get_state(capture),
                SALTS_CAPTURE_STATE_STOPPED);
    check_equal(android_screen_capture_get_frame_count(
                    capture, &frame_count),
                SALTS_CAPTURE_OK);
    check_equal(frame_count, 0);
    check_equal(capture_start(capture), SALTS_CAPTURE_OK);
    check_equal(capture_get_state(capture),
                SALTS_CAPTURE_STATE_RUNNING);

    capture_stop(capture);
    check_equal(capture_get_state(capture),
                SALTS_CAPTURE_STATE_STOPPED);
    check_equal(android_screen_capture_get_frame_count(
                    capture, &frame_count),
                SALTS_CAPTURE_OK);
    check_equal(frame_count, 0);
    capture_destroy(capture);
  }
}
