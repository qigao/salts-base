#include <serial.h>

#include <stdio.h>

/* Example of configuring a serial handle through the SaltsSerial ABI. */

int main(int argc, char **argv) {
  serial_config_t config;
  serial_t *serial = NULL;
  serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  serial_config_default(&config);
  config.baudrate = 115200;
  config.bits = 8;
  config.parity = SALTS_SERIAL_PARITY_NONE;
  config.stopbits = 1;
  config.flowcontrol = SALTS_SERIAL_FLOWCONTROL_NONE;
  config.rx_buffer_size = 4096;
  config.tx_buffer_size = 4096;

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

  printf("Opened %s at %d baud, %d data bits, %d stop bit.\n", argv[1], config.baudrate,
         config.bits, config.stopbits);

  serial_destroy(serial);
  return 0;
}
