#include "serial.h"
#include "tinytest.h"

suite("salts_serial public ABI") {
  it("uses only turbo serial public types") {
    salts_serial_config_t config;
    salts_serial_t *serial = NULL;

    salts_serial_config_default(&config);

    check_equal(config.parity, SALTS_SERIAL_PARITY_NONE);
    check_equal(config.flowcontrol, SALTS_SERIAL_FLOWCONTROL_NONE);
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    check_equal(salts_serial_open(serial, "not-a-real-port", (salts_serial_mode_t)0),
                 SALTS_SERIAL_INVALID_VALUE);

    salts_serial_destroy(serial);
  }

  it("exposes port metadata without libserialport types") {
    salts_serial_port_list_t *ports = NULL;
    const salts_serial_port_info_t *info = NULL;
    salts_serial_result_t result;
    size_t count;

    result = salts_serial_list_ports(&ports);
    check_true(result == SALTS_SERIAL_OK || result == SALTS_SERIAL_NOT_SUPPORTED);

    if (result == SALTS_SERIAL_OK) {
      count = salts_serial_port_list_count(ports);
      if (count > 0) {
        info = salts_serial_port_list_get(ports, 0);
        check_not_null(info);
        check_not_null(info->name);
        check_true(info->transport == SALTS_SERIAL_TRANSPORT_NATIVE ||
                   info->transport == SALTS_SERIAL_TRANSPORT_USB ||
                   info->transport == SALTS_SERIAL_TRANSPORT_BLUETOOTH ||
                   info->transport == SALTS_SERIAL_TRANSPORT_UNKNOWN);
      }
      check_null(salts_serial_port_list_get(ports, count));
      salts_serial_port_list_destroy(ports);
    } else {
      check_null(ports);
    }

    check_equal(salts_serial_port_info_by_name(NULL, &ports, &info),
                 SALTS_SERIAL_INVALID_VALUE);
    check_null(ports);
    check_null(info);
  }

  it("exposes event sets without libserialport types") {
    salts_serial_event_set_t *event_set = NULL;
    salts_serial_t *serial = NULL;
    unsigned int ready_events = 99;

    check_equal(salts_serial_event_set_create(&event_set), SALTS_SERIAL_OK);
    check_not_null(event_set);

    check_equal(salts_serial_create(&serial, NULL), SALTS_SERIAL_OK);
    check_not_null(serial);
    check_equal(salts_serial_event_set_add(event_set, serial, SALTS_SERIAL_EVENT_RX_READY),
                 SALTS_SERIAL_INVALID_STATE);
    check_equal(salts_serial_event_set_add(event_set, serial, 0x8000u),
                 SALTS_SERIAL_INVALID_VALUE);
    check_equal(salts_serial_event_wait(event_set, 1), SALTS_SERIAL_INVALID_STATE);
    check_equal(salts_serial_event_wait_ex(event_set, 1, &ready_events),
                 SALTS_SERIAL_INVALID_STATE);
    check_equal(ready_events, 0);

    salts_serial_destroy(serial);
    salts_serial_event_set_destroy(event_set);
  }
}
