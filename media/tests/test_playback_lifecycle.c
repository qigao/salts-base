#include <playback.h>
#include <tinytest.h>

#include <stdint.h>

typedef struct state_trace {
  playback_state_t states[8];
  size_t count;
} state_trace_t;

void playback_test_simulate_unexpected_stop(playback_t *playback);
void playback_test_stop_during_next_start(playback_t *playback);

static void record_state(playback_t *playback, playback_state_t state,
                         void *user_data) {
  state_trace_t *trace = (state_trace_t *)user_data;
  (void)playback;
  if (trace->count < sizeof(trace->states) / sizeof(trace->states[0])) {
    trace->states[trace->count++] = state;
  }
}

static playback_t *create_playback(void) {
  playback_config_t config = {48000u, 2u, SALTS_PLAYBACK_FORMAT_F32, 50u};
  playback_t *playback = NULL;
  check_equal(playback_create(NULL, &config, &playback), SALTS_PLAYBACK_OK);
  check_not_null(playback);
  return playback;
}

suite("playback lifecycle with deterministic null device") {
  it("creates every public PCM format with bounded storage") {
    const playback_format_t formats[] = {SALTS_PLAYBACK_FORMAT_S16, SALTS_PLAYBACK_FORMAT_S32,
                                               SALTS_PLAYBACK_FORMAT_F32};

    for (size_t index = 0u; index < sizeof(formats) / sizeof(formats[0]); ++index) {
      playback_config_t config = {48000u, 2u, formats[index], 50u};
      playback_t *playback = NULL;

      check_equal(playback_create(NULL, &config, &playback), SALTS_PLAYBACK_OK);
      check_not_null(playback);
      check_greater(playback_get_available(playback), (size_t)0u);
      playback_destroy(playback);
    }
  }

  it("reports synchronous state transitions and idempotent control") {
    playback_t *playback = create_playback();
    state_trace_t trace = {{0}, 0u};

    check_equal(playback_set_state_callback(playback, record_state, &trace),
                SALTS_PLAYBACK_OK);
    check_equal(playback_start(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_start(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_pause(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_pause(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_resume(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_stop(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_stop(playback), SALTS_PLAYBACK_OK);

    check_equal(trace.count, (size_t)6u);
    check_equal(trace.states[0], SALTS_PLAYBACK_STATE_STARTING);
    check_equal(trace.states[1], SALTS_PLAYBACK_STATE_PLAYING);
    check_equal(trace.states[2], SALTS_PLAYBACK_STATE_PAUSED);
    check_equal(trace.states[3], SALTS_PLAYBACK_STATE_PLAYING);
    check_equal(trace.states[4], SALTS_PLAYBACK_STATE_STOPPING);
    check_equal(trace.states[5], SALTS_PLAYBACK_STATE_STOPPED);
    playback_destroy(playback);
  }

  it("copies PCM before return and exposes bounded backpressure") {
    playback_t *playback = create_playback();
    float samples[1024] = {0.0f};
    size_t written = 0u;
    size_t initial_capacity = playback_get_available(playback);

    check_greater(initial_capacity, (size_t)0u);
    check_equal(playback_write(playback, samples, sizeof(samples), &written),
                SALTS_PLAYBACK_OK);
    check_equal(written, sizeof(samples));
    samples[0] = 1.0f;
    check_equal(playback_get_buffered(playback), sizeof(samples));
    check_equal(playback_get_available(playback), initial_capacity - sizeof(samples));
    check_equal(playback_clear(playback), SALTS_PLAYBACK_OK);
    check_equal(playback_get_buffered(playback), (size_t)0u);
    playback_destroy(playback);
  }

  it("rejects unaligned PCM and a stopped drain") {
    playback_t *playback = create_playback();
    uint8_t samples[16] = {0u};
    size_t written = 99u;

    check_equal(playback_write(playback, samples, 3u, &written), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(written, (size_t)0u);
    check_equal(playback_write(playback, samples, sizeof(samples), &written),
                SALTS_PLAYBACK_OK);
    check_equal(written, sizeof(samples));
    check_equal(playback_drain(playback, 10u), SALTS_PLAYBACK_ERR_BUSY);
    playback_destroy(playback);
  }

  it("delegates format-independent volume to the native device") {
    playback_t *playback = create_playback();
    float volume = 0.0f;

    check_equal(playback_set_volume(playback, -0.1f), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(playback_set_volume(playback, 0.25f), SALTS_PLAYBACK_OK);
    check_equal(playback_get_volume(playback, &volume), SALTS_PLAYBACK_OK);
    check_equal(volume, 0.25f);
    playback_destroy(playback);
  }

  it("surfaces an unexpected native stop as a device error") {
    playback_t *playback = create_playback();
    uint8_t samples[8] = {0u};
    size_t written = 99u;

    check_equal(playback_start(playback), SALTS_PLAYBACK_OK);
    playback_test_simulate_unexpected_stop(playback);
    check_equal(playback_get_state(playback), SALTS_PLAYBACK_STATE_ERROR);
    check_equal(playback_write(playback, samples, sizeof(samples), &written),
                SALTS_PLAYBACK_ERR_DEVICE);
    check_equal(written, (size_t)0u);
    playback_destroy(playback);
  }

  it("does not overwrite an error delivered during native start") {
    playback_t *playback = create_playback();

    playback_test_stop_during_next_start(playback);
    check_equal(playback_start(playback), SALTS_PLAYBACK_ERR_DEVICE);
    check_equal(playback_get_state(playback), SALTS_PLAYBACK_STATE_ERROR);
    playback_destroy(playback);
  }

  it("rejects an invalid opaque device identity") {
    playback_device_t device = {0};
    playback_config_t config = {48000u, 2u, SALTS_PLAYBACK_FORMAT_F32, 50u};
    playback_t *playback = (playback_t *)(uintptr_t)1u;

    check_equal(playback_create(&device, &config, &playback), SALTS_PLAYBACK_ERR_FORMAT);
    check_null(playback);
  }
}
