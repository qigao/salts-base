#include "serial.h"

#include <stdio.h>

/* Example of how to get a snapshot of serial ports through the SaltsSerial ABI. */

int main(void) {
  salts_serial_port_list_t *ports = NULL;
  salts_serial_result_t result;
  size_t count;
  size_t i;

  result = salts_serial_list_ports(&ports);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_list_ports() failed: %s\n", salts_serial_result_name(result));
    return 1;
  }

  count = salts_serial_port_list_count(ports);
  for (i = 0; i < count; ++i) {
    const salts_serial_port_info_t *info = salts_serial_port_list_get(ports, i);
    if (info && info->name) {
      printf("Found port: %s\n", info->name);
    }
  }

  printf("Found %zu ports.\n", count);
  salts_serial_port_list_destroy(ports);
  return 0;
}
