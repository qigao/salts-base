#include "serial.h"

#include <stdio.h>

/* Example of configuring a serial handle through the SaltsSerial ABI. */

int main(int argc, char **argv) {
  salts_serial_config_t config;
  salts_serial_t *serial = NULL;
  salts_serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  salts_serial_config_default(&config);
  config.baudrate = 115200;
  config.bits = 8;
  config.parity = SALTS_SERIAL_PARITY_NONE;
  config.stopbits = 1;
  config.flowcontrol = SALTS_SERIAL_FLOWCONTROL_NONE;
  config.rx_buffer_size = 4096;
  config.tx_buffer_size = 4096;

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

  printf("Opened %s at %d baud, %d data bits, %d stop bit.\n", argv[1], config.baudrate,
         config.bits, config.stopbits);

  salts_serial_destroy(serial);
  return 0;
}
