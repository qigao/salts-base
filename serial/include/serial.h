/*
 * Copyright (c) 2026 Salts Project
 */

#ifndef SALTS_SERIAL_H
#define SALTS_SERIAL_H

#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SALTS_SERIAL_EXPORTS)
    #define SALTS_SERIAL_API __declspec(dllexport)
  #else
    #define SALTS_SERIAL_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SALTS_SERIAL_API __attribute__((visibility("default")))
#else
  #define SALTS_SERIAL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque serial handle owned by serial_create()/serial_destroy(). */
typedef struct serial_t serial_t;

/* Opaque port-list snapshot owned by serial_port_list_destroy(). */
typedef struct serial_port_list serial_port_list_t;

/* Opaque event set owned by serial_event_set_destroy(). */
typedef struct serial_event_set serial_event_set_t;

/* Public result codes returned by the SaltsSerial ABI. */
typedef enum serial_result {
  SALTS_SERIAL_OK = 0,
  SALTS_SERIAL_INVALID_VALUE,
  SALTS_SERIAL_INVALID_STATE,
  SALTS_SERIAL_IO_FAILED,
  SALTS_SERIAL_NO_MEMORY,
  SALTS_SERIAL_NOT_SUPPORTED,
  SALTS_SERIAL_WOULD_BLOCK
} serial_result_t;

/* Access requested when opening a serial port. */
typedef enum serial_mode {
  SALTS_SERIAL_MODE_READ = 1,
  SALTS_SERIAL_MODE_WRITE = 2,
  SALTS_SERIAL_MODE_READ_WRITE = 3
} serial_mode_t;

/* Parity policy used when configuring an opened port.
 *
 * PLATFORM SUPPORT:
 * - Windows: All parity modes supported (NONE, ODD, EVEN, MARK, SPACE).
 * - POSIX: MARK and SPACE parity not universally supported; configure will
 *   return SALTS_SERIAL_INVALID_VALUE on unsupported platforms.
 * - Portable code should use NONE, ODD, or EVEN.
 */
typedef enum serial_parity {
  SALTS_SERIAL_PARITY_INVALID = -1,
  SALTS_SERIAL_PARITY_NONE = 0,
  SALTS_SERIAL_PARITY_ODD = 1,
  SALTS_SERIAL_PARITY_EVEN = 2,
  SALTS_SERIAL_PARITY_MARK = 3,
  SALTS_SERIAL_PARITY_SPACE = 4
} serial_parity_t;

/* Common flow-control presets. */
typedef enum serial_flowcontrol {
  SALTS_SERIAL_FLOWCONTROL_NONE = 0,
  SALTS_SERIAL_FLOWCONTROL_XONXOFF = 1,
  SALTS_SERIAL_FLOWCONTROL_RTSCTS = 2,
  SALTS_SERIAL_FLOWCONTROL_DTRDSR = 3
} serial_flowcontrol_t;

/* Transport class reported by port metadata. */
typedef enum serial_transport {
  SALTS_SERIAL_TRANSPORT_UNKNOWN = 0,
  SALTS_SERIAL_TRANSPORT_NATIVE,
  SALTS_SERIAL_TRANSPORT_USB,
  SALTS_SERIAL_TRANSPORT_BLUETOOTH
} serial_transport_t;

/* Event bits accepted by serial_event_set_add(). */
typedef enum serial_event {
  SALTS_SERIAL_EVENT_RX_READY = 1,
  SALTS_SERIAL_EVENT_TX_READY = 2,
  SALTS_SERIAL_EVENT_ERROR = 4
} serial_event_t;

/* Read-only port metadata. String pointers remain valid until the owning
 * serial_port_list_t is destroyed.
 */
typedef struct serial_port_info {
  const char *name;
  const char *description;
  serial_transport_t transport;

  int has_usb_bus_address;
  int usb_bus;
  int usb_address;

  int has_usb_vid_pid;
  int usb_vid;
  int usb_pid;

  const char *usb_manufacturer;
  const char *usb_product;
  const char *usb_serial;
  const char *bluetooth_address;
} serial_port_info_t;

/* Configuration copied into each handle at creation time.
 *
 * IMPORTANT CONSTRAINTS:
 * - rx_buffer_size and tx_buffer_size must be powers of two and at least 2.
 * - Actual usable capacity = buffer_size - 1 (one slot reserved for SPSC sentinel).
 * - io_chunk_size must be > 0; will be clamped to min(rx_buffer_size, tx_buffer_size).
 * - poll_interval_ms must be non-zero.
 * - baudrate must be positive; supported values depend on platform.
 * - bits must be 5-8; parity and stopbits depend on platform capabilities.
 *
 * THREADING MODEL:
 * - Async mode: one background thread pumps I/O to/from internal SPSC rings.
 * - RX ring: pump produces, serial_read_buffered() consumes (single consumer).
 * - TX ring: serial_write_buffered() produces (single producer), pump consumes.
 * - Multiple application readers or writers require external synchronization.
 *
 * USAGE GUIDELINES:
 * - io_chunk_size trades latency vs CPU: larger = fewer syscalls, higher latency.
 * - poll_interval_ms: smaller = lower latency, higher CPU usage when idle.
 * - Default values (256 bytes, 10ms) balance typical UART speeds (~115200 baud).
 */
typedef struct serial_config {
  int baudrate;
  int bits;
  serial_parity_t parity;
  int stopbits;
  serial_flowcontrol_t flowcontrol;
  size_t rx_buffer_size;
  size_t tx_buffer_size;
  size_t io_chunk_size;
  unsigned int poll_interval_ms;
} serial_config_t;

/* Fill config with portable defaults: 115200 8N1, no flow control. */
SALTS_SERIAL_API void serial_config_default(serial_config_t *config);

/* Return a stable static name for a SaltsSerial result code. */
SALTS_SERIAL_API const char *serial_result_name(serial_result_t result);

/* Create a snapshot of ports currently visible to the system. */
SALTS_SERIAL_API serial_result_t serial_list_ports(serial_port_list_t **ports);

/* Create a one-port metadata snapshot by system port name.
 *
 * On success, *info points into the returned *ports snapshot and remains valid
 * until serial_port_list_destroy(*ports) is called. Passing NULL for
 * info is allowed when only the port-list snapshot is needed.
 */
SALTS_SERIAL_API serial_result_t
serial_port_info_by_name(const char *port_name, serial_port_list_t **ports,
                               const serial_port_info_t **info);

/* Release a port-list snapshot and all string views returned from it. */
SALTS_SERIAL_API void serial_port_list_destroy(serial_port_list_t *ports);

/* Query a port-list snapshot. A NULL result from get means index is out of range. */
SALTS_SERIAL_API size_t serial_port_list_count(const serial_port_list_t *ports);
SALTS_SERIAL_API const serial_port_info_t *
serial_port_list_get(const serial_port_list_t *ports, size_t index);

/* Create a closed handle. The supplied config is copied and may be NULL for defaults. */
SALTS_SERIAL_API serial_result_t serial_create(serial_t **serial,
                                                          const serial_config_t *config);

/* Stop async I/O if needed, close the port, and release the handle. */
SALTS_SERIAL_API void serial_destroy(serial_t *serial);

/* Open a named serial port and apply the handle configuration. */
SALTS_SERIAL_API serial_result_t serial_open(serial_t *serial,
                                                        const char *port_name,
                                                        serial_mode_t mode);

/* Stop async I/O if needed and close the current port. */
SALTS_SERIAL_API serial_result_t serial_close(serial_t *serial);

/* Blocking read/write calls use the OS serial handle directly. They are valid
 * only while the async pump is stopped.
 *
 * USE CASES:
 * - Blocking I/O: simple request-response protocols, low-frequency polling.
 * - Async I/O: continuous streaming, event-driven apps, concurrent operations.
 *
 * CANNOT MIX: serial_read/write are mutually exclusive with async mode.
 * Call serial_stop_async() before using blocking I/O.
 */
SALTS_SERIAL_API serial_result_t serial_read(serial_t *serial, void *buf,
                                                        size_t count, unsigned int timeout_ms,
                                                        size_t *bytes_read);
SALTS_SERIAL_API serial_result_t serial_write(serial_t *serial,
                                                         const void *buf, size_t count,
                                                         unsigned int timeout_ms,
                                                         size_t *bytes_written);

/* Start or stop the background pump that moves bytes between the port and SPSC buffers. */
SALTS_SERIAL_API serial_result_t serial_start_async(serial_t *serial);
SALTS_SERIAL_API serial_result_t serial_stop_async(serial_t *serial);

/* Query async pump state and the last non-OK async pump error. */
SALTS_SERIAL_API int serial_async_running(const serial_t *serial);
SALTS_SERIAL_API serial_result_t serial_last_error(const serial_t *serial);

/* Event sets wait on currently opened SaltsSerial handles.
 * A timeout is reported as SALTS_SERIAL_WOULD_BLOCK.
 *
 * BEHAVIOR: Returns when ANY port in the set has a matching event ready.
 * Only the FIRST ready port's events are returned in the events mask.
 * To wait on all ports, call serial_event_wait_ex() in a loop.
 *
 * PLATFORM NOTES:
 * - Windows: Uses WaitCommEvent + WaitForMultipleObjects.
 * - POSIX: Uses poll(2) on file descriptors.
 * - Both return the first signaled port; application must handle fairness.
 *
 * serial_event_wait_ex() additionally returns the SaltsSerial event mask
 * that caused the wait to complete. The mask may combine multiple
 * SALTS_SERIAL_EVENT_* bits. The legacy serial_event_wait() keeps the
 * result-only behavior for source compatibility.
 */
SALTS_SERIAL_API serial_result_t
serial_event_set_create(serial_event_set_t **event_set);
SALTS_SERIAL_API void serial_event_set_destroy(serial_event_set_t *event_set);
SALTS_SERIAL_API serial_result_t
serial_event_set_add(serial_event_set_t *event_set, const serial_t *serial,
                           unsigned int events);
SALTS_SERIAL_API serial_result_t
serial_event_wait(serial_event_set_t *event_set, unsigned int timeout_ms);
SALTS_SERIAL_API serial_result_t
serial_event_wait_ex(serial_event_set_t *event_set, unsigned int timeout_ms,
                           unsigned int *events);

/* Buffered byte counts and capacities exclude the reserved SPSC sentinel slot. */
SALTS_SERIAL_API size_t serial_rx_available(const serial_t *serial);
SALTS_SERIAL_API size_t serial_tx_available(const serial_t *serial);
SALTS_SERIAL_API size_t serial_rx_capacity(const serial_t *serial);
SALTS_SERIAL_API size_t serial_tx_capacity(const serial_t *serial);

/* Buffered APIs operate on the handle-owned SPSC queues and are the async data
 * path used with serial_start_async().
 *
 * Threading contract per handle:
 * - RX ring: one producer is the async pump, one consumer calls read_buffered.
 * - TX ring: one producer calls write_buffered, one consumer is the async pump.
 * Multiple application readers or multiple application writers require
 * external synchronization.
 *
 * serial_buffer_rx() and serial_drain_tx_buffer() are intended for
 * tests and adapters and require the async pump to be stopped.
 */
SALTS_SERIAL_API serial_result_t serial_buffer_rx(serial_t *serial,
                                                             const void *buf, size_t count,
                                                             size_t *bytes_buffered);
SALTS_SERIAL_API serial_result_t serial_read_buffered(serial_t *serial,
                                                                 void *buf, size_t count,
                                                                 size_t *bytes_read);
SALTS_SERIAL_API serial_result_t serial_write_buffered(serial_t *serial,
                                                                  const void *buf, size_t count,
                                                                  size_t *bytes_buffered);
SALTS_SERIAL_API serial_result_t serial_drain_tx_buffer(serial_t *serial,
                                                                   void *buf, size_t count,
                                                                   size_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SERIAL_H */
