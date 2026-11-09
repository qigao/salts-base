#include <serial.h>

#include <stdio.h>

/* Example of handling SaltsSerial result codes at the application boundary. */

static int check(serial_result_t result, const char *operation) {
  if (result == SALTS_SERIAL_OK) return 0;

  printf("%s failed: %s\n", operation, serial_result_name(result));
  return 1;
}

int main(void) {
  serial_port_list_t *ports = NULL;
  serial_t *serial = NULL;
  serial_result_t result;

  result = serial_list_ports(&ports);
  if (check(result, "serial_list_ports")) return 1;
  serial_port_list_destroy(ports);

  result = serial_create(&serial, NULL);
  if (check(result, "serial_create")) return 1;

  result = serial_open(serial, "NON-EXISTENT-PORT", SALTS_SERIAL_MODE_READ_WRITE);
  if (result != SALTS_SERIAL_OK) {
    printf("Expected open failure: %s\n", serial_result_name(result));
  }

  serial_destroy(serial);
  return 0;
}
