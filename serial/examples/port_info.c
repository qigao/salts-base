#include "serial.h"

#include <stdio.h>

/* Example of how to inspect a serial port through the SaltsSerial ABI. */

static const char *transport_name(salts_serial_transport_t transport) {
  switch (transport) {
  case SALTS_SERIAL_TRANSPORT_NATIVE:
    return "Native";
  case SALTS_SERIAL_TRANSPORT_USB:
    return "USB";
  case SALTS_SERIAL_TRANSPORT_BLUETOOTH:
    return "Bluetooth";
  default:
    return "Unknown";
  }
}

int main(int argc, char **argv) {
  salts_serial_port_list_t *ports = NULL;
  const salts_serial_port_info_t *info = NULL;
  salts_serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  result = salts_serial_port_info_by_name(argv[1], &ports, &info);
  if (result != SALTS_SERIAL_OK) {
    printf("salts_serial_port_info_by_name() failed: %s\n", salts_serial_result_name(result));
    return 1;
  }

  printf("Port name: %s\n", info->name ? info->name : "");
  printf("Description: %s\n", info->description ? info->description : "");
  printf("Type: %s\n", transport_name(info->transport));

  if (info->transport == SALTS_SERIAL_TRANSPORT_USB) {
    printf("Manufacturer: %s\n", info->usb_manufacturer ? info->usb_manufacturer : "");
    printf("Product: %s\n", info->usb_product ? info->usb_product : "");
    printf("Serial: %s\n", info->usb_serial ? info->usb_serial : "");
    if (info->has_usb_vid_pid) {
      printf("VID: %04X PID: %04X\n", info->usb_vid, info->usb_pid);
    }
    if (info->has_usb_bus_address) {
      printf("Bus: %d Address: %d\n", info->usb_bus, info->usb_address);
    }
  } else if (info->transport == SALTS_SERIAL_TRANSPORT_BLUETOOTH) {
    printf("MAC: %s\n", info->bluetooth_address ? info->bluetooth_address : "");
  }

  salts_serial_port_list_destroy(ports);
  return 0;
}
