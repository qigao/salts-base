#include "serial.h"

#include <stdio.h>
#include <string.h>

/* Example of blocking write/read through one open SaltsSerial handle. */

int main(int argc, char **argv) {
  salts_serial_config_t config;
  salts_serial_t *serial = NULL;
  const char data[] = "Hello!";
  char buffer[sizeof(data)] = {0};
  size_t bytes = 0;
  salts_serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  salts_serial_config_default(&config);
  config.baudrate = 9600;

  result = salts_serial_create(&serial, &config);
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

  result = salts_serial_write(serial, data, strlen(data), 1000, &bytes);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_write() failed: %s\n", salts_serial_result_name(result));
    salts_serial_destroy(serial);
    return 1;
  }
  printf("Wrote %zu bytes.\n", bytes);

  result = salts_serial_read(serial, buffer, strlen(data), 1000, &bytes);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_read() failed: %s\n", salts_serial_result_name(result));
    salts_serial_destroy(serial);
    return 1;
  }

  printf("Read %zu bytes: %.*s\n", bytes, (int)bytes, buffer);
  salts_serial_destroy(serial);
  return 0;
}
