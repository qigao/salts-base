#include <serial.h>

#include <stdio.h>

/* Example of how to get a snapshot of serial ports through the SaltsSerial ABI. */

int main(void) {
  serial_port_list_t *ports = NULL;
  serial_result_t result;
  size_t count;
  size_t i;

  result = serial_list_ports(&ports);
  if (result != SALTS_SERIAL_OK) {
    printf("serial_list_ports() failed: %s\n", serial_result_name(result));
    return 1;
  }

  count = serial_port_list_count(ports);
  for (i = 0; i < count; ++i) {
    const serial_port_info_t *info = serial_port_list_get(ports, i);
    if (info && info->name) {
      printf("Found port: %s\n", info->name);
    }
  }

  printf("Found %zu ports.\n", count);
  serial_port_list_destroy(ports);
  return 0;
}
