/*
 * Copyright (c) 2026 Salts Project
 *
 * Test suite validating fixes from serial review.
 */

#include "tinytest.h"
#include <serial.h>

#include <string.h>

suite("serial review fixes") {
  it("clamps io_chunk_size to buffer capacity") {
    serial_config_t config;
    serial_t *serial = NULL;

    serial_config_default(&config);
    config.rx_buffer_size = 16;
    config.tx_buffer_size = 16;
    /* io_chunk_size defaults to 256, which exceeds buffer size */
    check_equal(config.io_chunk_size, 256);

    /* Should succeed by clamping io_chunk_size internally */
    check_equal(serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);

    serial_destroy(serial);
  }

  it("rejects zero io_chunk_size") {
    serial_config_t config;
    serial_t *serial = NULL;

    serial_config_default(&config);
    config.io_chunk_size = 0;

    check_equal(serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);
  }

  it("documents actual SPSC capacity correctly") {
    serial_config_t config;
    serial_t *serial = NULL;

    serial_config_default(&config);
    config.rx_buffer_size = 64;
    config.tx_buffer_size = 128;

    check_equal(serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);

    /* Usable capacity = buffer_size - 1 (sentinel slot) */
    check_equal(serial_rx_capacity(serial), 63);
    check_equal(serial_tx_capacity(serial), 127);

    serial_destroy(serial);
  }

  it("sets async_running flag only after thread creation succeeds") {
    serial_t *serial = NULL;

    check_equal(serial_create(&serial, NULL), SALTS_SERIAL_OK);
    check_not_null(serial);

    /* Without an open port, start_async should fail */
    check_equal(serial_start_async(serial), SALTS_SERIAL_INVALID_STATE);

    /* Flag should NOT be set after failed start */
    check_equal(serial_async_running(serial), 0);

    serial_destroy(serial);
  }
}
