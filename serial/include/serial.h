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

/* Opaque serial handle owned by salts_serial_create()/salts_serial_destroy(). */
typedef struct salts_serial_t salts_serial_t;

/* Opaque port-list snapshot owned by salts_serial_port_list_destroy(). */
typedef struct salts_serial_port_list salts_serial_port_list_t;

/* Opaque event set owned by salts_serial_event_set_destroy(). */
typedef struct salts_serial_event_set salts_serial_event_set_t;

/* Public result codes returned by the SaltsSerial ABI. */
typedef enum salts_serial_result {
  SALTS_SERIAL_OK = 0,
  SALTS_SERIAL_INVALID_VALUE,
  SALTS_SERIAL_INVALID_STATE,
  SALTS_SERIAL_IO_FAILED,
  SALTS_SERIAL_NO_MEMORY,
  SALTS_SERIAL_NOT_SUPPORTED,
  SALTS_SERIAL_WOULD_BLOCK
} salts_serial_result_t;

/* Access requested when opening a serial port. */
typedef enum salts_serial_mode {
  SALTS_SERIAL_MODE_READ = 1,
  SALTS_SERIAL_MODE_WRITE = 2,
  SALTS_SERIAL_MODE_READ_WRITE = 3
} salts_serial_mode_t;

/* Parity policy used when configuring an opened port.
 *
 * PLATFORM SUPPORT:
 * - Windows: All parity modes supported (NONE, ODD, EVEN, MARK, SPACE).
 * - POSIX: MARK and SPACE parity not universally supported; configure will
 *   return SALTS_SERIAL_INVALID_VALUE on unsupported platforms.
 * - Portable code should use NONE, ODD, or EVEN.
 */
typedef enum salts_serial_parity {
  SALTS_SERIAL_PARITY_INVALID = -1,
  SALTS_SERIAL_PARITY_NONE = 0,
  SALTS_SERIAL_PARITY_ODD = 1,
  SALTS_SERIAL_PARITY_EVEN = 2,
  SALTS_SERIAL_PARITY_MARK = 3,
  SALTS_SERIAL_PARITY_SPACE = 4
} salts_serial_parity_t;

/* Common flow-control presets. */
typedef enum salts_serial_flowcontrol {
  SALTS_SERIAL_FLOWCONTROL_NONE = 0,
  SALTS_SERIAL_FLOWCONTROL_XONXOFF = 1,
  SALTS_SERIAL_FLOWCONTROL_RTSCTS = 2,
  SALTS_SERIAL_FLOWCONTROL_DTRDSR = 3
} salts_serial_flowcontrol_t;

/* Transport class reported by port metadata. */
typedef enum salts_serial_transport {
  SALTS_SERIAL_TRANSPORT_UNKNOWN = 0,
  SALTS_SERIAL_TRANSPORT_NATIVE,
  SALTS_SERIAL_TRANSPORT_USB,
  SALTS_SERIAL_TRANSPORT_BLUETOOTH
} salts_serial_transport_t;

/* Event bits accepted by salts_serial_event_set_add(). */
typedef enum salts_serial_event {
  SALTS_SERIAL_EVENT_RX_READY = 1,
  SALTS_SERIAL_EVENT_TX_READY = 2,
  SALTS_SERIAL_EVENT_ERROR = 4
} salts_serial_event_t;

/* Read-only port metadata. String pointers remain valid until the owning
 * salts_serial_port_list_t is destroyed.
 */
typedef struct salts_serial_port_info {
  const char *name;
  const char *description;
  salts_serial_transport_t transport;

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
} salts_serial_port_info_t;

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
 * - RX ring: pump produces, salts_serial_read_buffered() consumes (single consumer).
 * - TX ring: salts_serial_write_buffered() produces (single producer), pump consumes.
 * - Multiple application readers or writers require external synchronization.
 *
 * USAGE GUIDELINES:
 * - io_chunk_size trades latency vs CPU: larger = fewer syscalls, higher latency.
 * - poll_interval_ms: smaller = lower latency, higher CPU usage when idle.
 * - Default values (256 bytes, 10ms) balance typical UART speeds (~115200 baud).
 */
typedef struct salts_serial_config {
  int baudrate;
  int bits;
  salts_serial_parity_t parity;
  int stopbits;
  salts_serial_flowcontrol_t flowcontrol;
  size_t rx_buffer_size;
  size_t tx_buffer_size;
  size_t io_chunk_size;
  unsigned int poll_interval_ms;
} salts_serial_config_t;

/* Fill config with portable defaults: 115200 8N1, no flow control. */
SALTS_SERIAL_API void salts_serial_config_default(salts_serial_config_t *config);

/* Return a stable static name for a SaltsSerial result code. */
SALTS_SERIAL_API const char *salts_serial_result_name(salts_serial_result_t result);

/* Create a snapshot of ports currently visible to the system. */
SALTS_SERIAL_API salts_serial_result_t salts_serial_list_ports(salts_serial_port_list_t **ports);

/* Create a one-port metadata snapshot by system port name.
 *
 * On success, *info points into the returned *ports snapshot and remains valid
 * until salts_serial_port_list_destroy(*ports) is called. Passing NULL for
 * info is allowed when only the port-list snapshot is needed.
 */
SALTS_SERIAL_API salts_serial_result_t
salts_serial_port_info_by_name(const char *port_name, salts_serial_port_list_t **ports,
                               const salts_serial_port_info_t **info);

/* Release a port-list snapshot and all string views returned from it. */
SALTS_SERIAL_API void salts_serial_port_list_destroy(salts_serial_port_list_t *ports);

/* Query a port-list snapshot. A NULL result from get means index is out of range. */
SALTS_SERIAL_API size_t salts_serial_port_list_count(const salts_serial_port_list_t *ports);
SALTS_SERIAL_API const salts_serial_port_info_t *
salts_serial_port_list_get(const salts_serial_port_list_t *ports, size_t index);

/* Create a closed handle. The supplied config is copied and may be NULL for defaults. */
SALTS_SERIAL_API salts_serial_result_t salts_serial_create(salts_serial_t **serial,
                                                          const salts_serial_config_t *config);

/* Stop async I/O if needed, close the port, and release the handle. */
SALTS_SERIAL_API void salts_serial_destroy(salts_serial_t *serial);

/* Open a named serial port and apply the handle configuration. */
SALTS_SERIAL_API salts_serial_result_t salts_serial_open(salts_serial_t *serial,
                                                        const char *port_name,
                                                        salts_serial_mode_t mode);

/* Stop async I/O if needed and close the current port. */
SALTS_SERIAL_API salts_serial_result_t salts_serial_close(salts_serial_t *serial);

/* Blocking read/write calls use the OS serial handle directly. They are valid
 * only while the async pump is stopped.
 *
 * USE CASES:
 * - Blocking I/O: simple request-response protocols, low-frequency polling.
 * - Async I/O: continuous streaming, event-driven apps, concurrent operations.
 *
 * CANNOT MIX: salts_serial_read/write are mutually exclusive with async mode.
 * Call salts_serial_stop_async() before using blocking I/O.
 */
SALTS_SERIAL_API salts_serial_result_t salts_serial_read(salts_serial_t *serial, void *buf,
                                                        size_t count, unsigned int timeout_ms,
                                                        size_t *bytes_read);
SALTS_SERIAL_API salts_serial_result_t salts_serial_write(salts_serial_t *serial,
                                                         const void *buf, size_t count,
                                                         unsigned int timeout_ms,
                                                         size_t *bytes_written);

/* Start or stop the background pump that moves bytes between the port and SPSC buffers. */
SALTS_SERIAL_API salts_serial_result_t salts_serial_start_async(salts_serial_t *serial);
SALTS_SERIAL_API salts_serial_result_t salts_serial_stop_async(salts_serial_t *serial);

/* Query async pump state and the last non-OK async pump error. */
SALTS_SERIAL_API int salts_serial_async_running(const salts_serial_t *serial);
SALTS_SERIAL_API salts_serial_result_t salts_serial_last_error(const salts_serial_t *serial);

/* Event sets wait on currently opened SaltsSerial handles.
 * A timeout is reported as SALTS_SERIAL_WOULD_BLOCK.
 *
 * BEHAVIOR: Returns when ANY port in the set has a matching event ready.
 * Only the FIRST ready port's events are returned in the events mask.
 * To wait on all ports, call salts_serial_event_wait_ex() in a loop.
 *
 * PLATFORM NOTES:
 * - Windows: Uses WaitCommEvent + WaitForMultipleObjects.
 * - POSIX: Uses poll(2) on file descriptors.
 * - Both return the first signaled port; application must handle fairness.
 *
 * salts_serial_event_wait_ex() additionally returns the SaltsSerial event mask
 * that caused the wait to complete. The mask may combine multiple
 * SALTS_SERIAL_EVENT_* bits. The legacy salts_serial_event_wait() keeps the
 * result-only behavior for source compatibility.
 */
SALTS_SERIAL_API salts_serial_result_t
salts_serial_event_set_create(salts_serial_event_set_t **event_set);
SALTS_SERIAL_API void salts_serial_event_set_destroy(salts_serial_event_set_t *event_set);
SALTS_SERIAL_API salts_serial_result_t
salts_serial_event_set_add(salts_serial_event_set_t *event_set, const salts_serial_t *serial,
                           unsigned int events);
SALTS_SERIAL_API salts_serial_result_t
salts_serial_event_wait(salts_serial_event_set_t *event_set, unsigned int timeout_ms);
SALTS_SERIAL_API salts_serial_result_t
salts_serial_event_wait_ex(salts_serial_event_set_t *event_set, unsigned int timeout_ms,
                           unsigned int *events);

/* Buffered byte counts and capacities exclude the reserved SPSC sentinel slot. */
SALTS_SERIAL_API size_t salts_serial_rx_available(const salts_serial_t *serial);
SALTS_SERIAL_API size_t salts_serial_tx_available(const salts_serial_t *serial);
SALTS_SERIAL_API size_t salts_serial_rx_capacity(const salts_serial_t *serial);
SALTS_SERIAL_API size_t salts_serial_tx_capacity(const salts_serial_t *serial);

/* Buffered APIs operate on the handle-owned SPSC queues and are the async data
 * path used with salts_serial_start_async().
 *
 * Threading contract per handle:
 * - RX ring: one producer is the async pump, one consumer calls read_buffered.
 * - TX ring: one producer calls write_buffered, one consumer is the async pump.
 * Multiple application readers or multiple application writers require
 * external synchronization.
 *
 * salts_serial_buffer_rx() and salts_serial_drain_tx_buffer() are intended for
 * tests and adapters and require the async pump to be stopped.
 */
SALTS_SERIAL_API salts_serial_result_t salts_serial_buffer_rx(salts_serial_t *serial,
                                                             const void *buf, size_t count,
                                                             size_t *bytes_buffered);
SALTS_SERIAL_API salts_serial_result_t salts_serial_read_buffered(salts_serial_t *serial,
                                                                 void *buf, size_t count,
                                                                 size_t *bytes_read);
SALTS_SERIAL_API salts_serial_result_t salts_serial_write_buffered(salts_serial_t *serial,
                                                                  const void *buf, size_t count,
                                                                  size_t *bytes_buffered);
SALTS_SERIAL_API salts_serial_result_t salts_serial_drain_tx_buffer(salts_serial_t *serial,
                                                                   void *buf, size_t count,
                                                                   size_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SERIAL_H */
