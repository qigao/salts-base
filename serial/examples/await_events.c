#include "serial.h"

#include <stdio.h>

/* Example of waiting for serial readiness through a SaltsSerial event set. */

int main(int argc, char **argv) {
  salts_serial_t *serial = NULL;
  salts_serial_event_set_t *events = NULL;
  unsigned int ready_events = 0;
  salts_serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  result = salts_serial_create(&serial, NULL);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_create() failed: %s\n", salts_serial_result_name(result));
    return 1;
  }

  result = salts_serial_open(serial, argv[1], SALTS_SERIAL_MODE_READ_WRITE);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_open() failed: %s\n", salts_serial_result_name(result));
    salts_serial_destroy(serial);
    return 1;
  }

  result = salts_serial_event_set_create(&events);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_event_set_create() failed: %s\n", salts_serial_result_name(result));
    salts_serial_destroy(serial);
    return 1;
  }

  result = salts_serial_event_set_add(events, serial,
                                      SALTS_SERIAL_EVENT_RX_READY | SALTS_SERIAL_EVENT_ERROR);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_event_set_add() failed: %s\n", salts_serial_result_name(result));
    salts_serial_event_set_destroy(events);
    salts_serial_destroy(serial);
    return 1;
  }

  result = salts_serial_event_wait_ex(events, 5000, &ready_events);
  if (result == SALTS_SERIAL_OK) {
    printf("Serial events ready: rx=%u tx=%u error=%u\n",
           (ready_events & SALTS_SERIAL_EVENT_RX_READY) ? 1u : 0u,
           (ready_events & SALTS_SERIAL_EVENT_TX_READY) ? 1u : 0u,
           (ready_events & SALTS_SERIAL_EVENT_ERROR) ? 1u : 0u);
  } else {
    printf("salts_serial_event_wait_ex() failed: %s\n", salts_serial_result_name(result));
  }

  salts_serial_event_set_destroy(events);
  salts_serial_destroy(serial);
  return result == SALTS_SERIAL_OK ? 0 : 1;
}
