/*
 * Copyright (c) 2026 Salts Project
 */

#include "tinytest.h"
#include "serial.h"

#include <string.h>

suite("salts_serial native ABI") {
  it("keeps turbo serial public configuration independent from OS ABI") {
    salts_serial_config_t config;

    salts_serial_config_default(&config);

    check_equal(config.parity, SALTS_SERIAL_PARITY_NONE);
    check_equal(config.flowcontrol, SALTS_SERIAL_FLOWCONTROL_NONE);
    check_equal(SALTS_SERIAL_MODE_READ, 1);
    check_equal(SALTS_SERIAL_MODE_WRITE, 2);
    check_equal(SALTS_SERIAL_MODE_READ_WRITE, 3);
    check_equal(salts_serial_result_name(SALTS_SERIAL_WOULD_BLOCK), "would block");
  }

  it("creates a per-instance turbo serial buffer") {
    salts_serial_config_t config;
    salts_serial_t *serial = NULL;

    salts_serial_config_default(&config);
    check_equal(config.io_chunk_size, 256);
    check_equal((int)config.poll_interval_ms, 10);
    config.rx_buffer_size = 16;
    config.tx_buffer_size = 16;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    check_equal(salts_serial_rx_capacity(serial), 15);
    check_equal(salts_serial_tx_capacity(serial), 15);
    check_equal(salts_serial_rx_available(serial), 0);
    check_equal(salts_serial_tx_available(serial), 0);

    salts_serial_destroy(serial);
  }

  it("rejects invalid turbo serial buffer sizes") {
    salts_serial_config_t config;
    salts_serial_t *serial = NULL;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 15;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);

    salts_serial_config_default(&config);
    config.io_chunk_size = 0;
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);

    salts_serial_config_default(&config);
    config.poll_interval_ms = 0;
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);

    salts_serial_config_default(&config);
    config.bits = 9;
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);

    salts_serial_config_default(&config);
    config.stopbits = 3;
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);

    salts_serial_config_default(&config);
    config.parity = SALTS_SERIAL_PARITY_INVALID;
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_INVALID_VALUE);
    check_null(serial);
  }

  it("requires an open port before starting async pump") {
    salts_serial_t *serial = NULL;

    check_equal(salts_serial_create(&serial, NULL), SALTS_SERIAL_OK);
    check_not_null(serial);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_INVALID_STATE);
    check_equal(salts_serial_async_running(serial), 0);
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);
    check_equal(salts_serial_last_error(serial), SALTS_SERIAL_OK);

    salts_serial_destroy(serial);
  }

  it("buffers rx and tx data through per-handle SPSC rings") {
    salts_serial_config_t config;
    salts_serial_t *serial = NULL;
    const char rx_data[] = "sensor:1234";
    const char tx_data[] = "command:go";
    char out[32] = {0};
    size_t count = 0;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 32;
    config.tx_buffer_size = 32;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);

    check_equal(salts_serial_buffer_rx(serial, rx_data, strlen(rx_data), &count),
                 SALTS_SERIAL_OK);
    check_equal(count, strlen(rx_data));
    check_equal(salts_serial_rx_available(serial), strlen(rx_data));

    check_equal(salts_serial_read_buffered(serial, out, sizeof(out), &count),
                 SALTS_SERIAL_OK);
    check_equal(count, strlen(rx_data));
    check_equal(out, rx_data, strlen(rx_data));
    check_equal(salts_serial_rx_available(serial), 0);

    memset(out, 0, sizeof(out));
    check_equal(salts_serial_write_buffered(serial, tx_data, strlen(tx_data), &count),
                 SALTS_SERIAL_OK);
    check_equal(count, strlen(tx_data));
    check_equal(salts_serial_tx_available(serial), strlen(tx_data));

    salts_serial_destroy(serial);
  }
}
