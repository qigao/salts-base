#include "serial.h"

#include <stdio.h>

/* Example of handling SaltsSerial result codes at the application boundary. */

static int check(salts_serial_result_t result, const char *operation) {
  if (result == SALTS_SERIAL_OK) return 0;

  printf("%s failed: %s\n", operation, salts_serial_result_name(result));
  return 1;
}

int main(void) {
  salts_serial_port_list_t *ports = NULL;
  salts_serial_t *serial = NULL;
  salts_serial_result_t result;

  result = salts_serial_list_ports(&ports);
  if (check(result, "salts_serial_list_ports")) return 1;
  salts_serial_port_list_destroy(ports);

  result = salts_serial_create(&serial, NULL);
  if (check(result, "salts_serial_create")) return 1;

  result = salts_serial_open(serial, "NON-EXISTENT-PORT", SALTS_SERIAL_MODE_READ_WRITE);
  if (result != SALTS_SERIAL_OK) {
    printf("Expected open failure: %s\n", salts_serial_result_name(result));
  }

  salts_serial_destroy(serial);
  return 0;
}
