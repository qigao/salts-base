#include <capture.h>
#include <tinytest.h>

#include <stddef.h>

suite("Windows capture integration") {
  it("rejects invalid enumeration buffers") {
    capture_device_t device;

    check_equal(capture_list_audio_devices(NULL, 1), -1);
    check_equal(capture_list_video_devices(NULL, 1), -1);
    check_equal(capture_list_screens(NULL, 1), -1);
    check_equal(capture_list_gpu_devices(NULL, 1), -1);
    check_equal(capture_list_audio_devices(&device, 0), -1);
    check_equal(capture_list_video_devices(&device, 0), -1);
    check_equal(capture_list_screens(&device, 0), -1);
    check_equal(capture_list_gpu_devices(&device, 0), -1);
  }

  it("enumerates each device class within caller capacity") {
    capture_device_t devices[SALTS_CAPTURE_MAX_DEVICES];
    int audio_count = capture_list_audio_devices(
        devices, SALTS_CAPTURE_MAX_DEVICES);
    int video_count = capture_list_video_devices(
        devices, SALTS_CAPTURE_MAX_DEVICES);
    int screen_count = capture_list_screens(
        devices, SALTS_CAPTURE_MAX_DEVICES);
    int gpu_count = capture_list_gpu_devices(
        devices, SALTS_CAPTURE_MAX_DEVICES);

    check_greater_equal(audio_count, 0);
    check_less_equal(audio_count, SALTS_CAPTURE_MAX_DEVICES);
    check_greater_equal(video_count, 0);
    check_less_equal(video_count, SALTS_CAPTURE_MAX_DEVICES);
    check_greater_equal(screen_count, 0);
    check_less_equal(screen_count, SALTS_CAPTURE_MAX_DEVICES);
    check_greater_equal(gpu_count, 0);
    check_less_equal(gpu_count, SALTS_CAPTURE_MAX_DEVICES);
  }

  it("rejects an unknown explicit audio device without fallback") {
    check_null(audio_capture_create("not-a-capture-device", NULL));
  }

  it("rejects unsupported audio PCM formats without coercion") {
    audio_capture_config_t config = {48000, 1, 24, 20};
    check_null(audio_capture_create(NULL, &config));
  }

  it("makes null lifecycle operations safe and deterministic") {
    check_equal(capture_start(NULL), -1);
    check_equal(capture_get_state(NULL), SALTS_CAPTURE_STATE_STOPPED);
    capture_stop(NULL);
    capture_destroy(NULL);
  }

  it("keeps enumerated camera mode identities unique per device") {
    capture_device_t devices[SALTS_CAPTURE_MAX_DEVICES];
    int device_count = capture_list_video_devices(
        devices, SALTS_CAPTURE_MAX_DEVICES);

    for (int device_index = 0; device_index < device_count; ++device_index) {
      video_native_mode_t modes[SALTS_CAPTURE_MAX_VIDEO_MODES];
      video_device_t *device = NULL;
      size_t mode_count = 0u;

      if (video_device_open(devices[device_index].id, &device) !=
          SALTS_CAPTURE_OK)
        continue;
      check_equal(video_device_list_modes_all(
                      device, modes, SALTS_CAPTURE_MAX_VIDEO_MODES,
                      &mode_count),
                  SALTS_CAPTURE_OK);
      for (size_t index = 0u; index < mode_count; ++index) {
        check_greater(modes[index].width, 0);
        check_greater(modes[index].height, 0);
        check_greater(modes[index].framerate_numerator, 0u);
        check_greater(modes[index].framerate_denominator, 0u);
        for (size_t prior = 0u; prior < index; ++prior)
          check_not_equal(modes[index].mode_id, modes[prior].mode_id);
      }
      video_device_close(device);
    }
  }
}
