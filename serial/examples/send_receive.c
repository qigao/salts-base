#include <serial.h>

#include <stdio.h>
#include <string.h>

/* Example of blocking write/read through one open SaltsSerial handle. */

int main(int argc, char **argv) {
  serial_config_t config;
  serial_t *serial = NULL;
  const char data[] = "Hello!";
  char buffer[sizeof(data)] = {0};
  size_t bytes = 0;
  serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  serial_config_default(&config);
  config.baudrate = 9600;

  result = serial_create(&serial, &config);
  if (result != SALTS_SERIAL_OK) {
    printf("serial_create() failed: %s\n", serial_result_name(result));
    return 1;
  }

  result = serial_open(serial, argv[1], SALTS_SERIAL_MODE_READ_WRITE);
  if (result != SALTS_SERIAL_OK) {
    printf("serial_open() failed: %s\n", serial_result_name(result));
    serial_destroy(serial);
    return 1;
  }

  result = serial_write(serial, data, strlen(data), 1000, &bytes);
  if (result != SALTS_SERIAL_OK) {
    printf("serial_write() failed: %s\n", serial_result_name(result));
    serial_destroy(serial);
    return 1;
  }
  printf("Wrote %zu bytes.\n", bytes);

  result = serial_read(serial, buffer, strlen(data), 1000, &bytes);
  if (result != SALTS_SERIAL_OK) {
    printf("serial_read() failed: %s\n", serial_result_name(result));
    serial_destroy(serial);
    return 1;
  }

  printf("Read %zu bytes: %.*s\n", bytes, (int)bytes, buffer);
  serial_destroy(serial);
  return 0;
}
