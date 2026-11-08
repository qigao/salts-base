/*
 * Copyright (c) 2026 Salts Project
 */

#include "tinytest.h"
#include "tlog.h"
#include "serial_internal.h"
#include "serial_test.h"
#include "thread.h"
#include "clock.h"

#include <stdatomic.h>
#include <string.h>

enum {
  MOCK_WAIT_TIMEOUT_MS = 5000,
  MOCK_WAIT_POLL_MS = 1,
  MOCK_THREAD_CREATE_FAILURE = 1
};

static int fake_handle_marker = 0;
#define FAKE_HANDLE ((salts_port_handle_t *)&fake_handle_marker)

/* The original backend injection points record the same call/argument
 * obligations without reflected function overrides. Only the pump worker
 * writes these records; assertions and reset happen after its join. */
typedef struct backend_probe {
  size_t calls;
  void *first_handle;
  int result;
} backend_probe;

static backend_probe read_probe;
static backend_probe write_probe;

static int fake_nonblocking_read(void *handle, void *buffer, size_t count) {
  (void)buffer;
  (void)count;
  if (read_probe.calls++ == 0u) read_probe.first_handle = handle;
  return read_probe.result;
}

static int fake_nonblocking_write(void *handle, const void *buffer, size_t count) {
  (void)buffer;
  (void)count;
  if (write_probe.calls++ == 0u) write_probe.first_handle = handle;
  return write_probe.result;
}

static unsigned char fake_rx_data[16];
static size_t fake_rx_len;
static unsigned char fake_tx_data[16];
static size_t fake_tx_len;
static size_t storage_clone_calls;
static cmeta_thread_cb controlled_worker_entry;
static void *controlled_worker_arg;
static atomic_bool controlled_backend_entered;
static atomic_bool controlled_worker_exited;

static bool wait_for_buffer_count(const salts_serial_t *serial,
                                 size_t (*available)(const salts_serial_t *),
                                 size_t expected) {
  uint64_t started = cmeta_monotonic_ms();
  while (available(serial) != expected) {
    if (cmeta_monotonic_ms() - started >= MOCK_WAIT_TIMEOUT_MS) return false;
    cmeta_sleep_ms(MOCK_WAIT_POLL_MS);
  }
  return true;
}

static void controlled_worker_main(void *arg) {
  (void)arg;
  controlled_worker_entry(controlled_worker_arg);
  atomic_store(&controlled_worker_exited, true);
}

static int create_worker_before_return(cmeta_thread_t *thread,
                                       cmeta_thread_cb entry, void *arg) {
  uint64_t started;
  int result;
  controlled_worker_entry = entry;
  controlled_worker_arg = arg;
  result = cmeta_thread_create(thread, controlled_worker_main, NULL);
  if (result != 0) return result;

  /* Force the worker to observe startup state before its creator returns.
   * A worker that exits before touching the backend exposes the startup race. */
  started = cmeta_monotonic_ms();
  while (!atomic_load(&controlled_backend_entered) &&
         !atomic_load(&controlled_worker_exited)) {
    if (cmeta_monotonic_ms() - started >= MOCK_WAIT_TIMEOUT_MS) break;
    cmeta_sleep_ms(MOCK_WAIT_POLL_MS);
  }
  return result;
}

static int reject_thread_create(cmeta_thread_t *thread,
                                cmeta_thread_cb entry, void *arg) {
  (void)thread;
  (void)entry;
  (void)arg;
  return MOCK_THREAD_CREATE_FAILURE;
}

static bool fail_fourth_storage_clone(tstr *out, tstr source) {
  storage_clone_calls++;
  if (storage_clone_calls == 4) return false;
  return salts_serial_port_info_storage_clone_string(out, source);
}

static void fill_port_storage(salts_serial_port_info_storage_t *storage) {
  memset(storage, 0, sizeof(*storage));
  storage->name = tstr_dup("COM7");
  storage->description = tstr_dup("USB serial port");
  storage->usb_manufacturer = tstr_dup("Turbo");
  storage->usb_product = tstr_dup("Bridge");
  storage->usb_serial = tstr_dup("ABC123");
  storage->bluetooth_address = tstr_dup("00:11:22:33:44:55");
  salts_serial_port_info_storage_refresh_view(storage);
}

static size_t mock_min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static salts_serial_result_t fake_nonblocking_read_impl(salts_port_handle_t *handle, void *buf,
                                                        size_t count, size_t *bytes_read) {
  atomic_store(&controlled_backend_entered, true);
  salts_serial_result_t result = fake_nonblocking_read(handle, buf, count);
  if (result == SALTS_SERIAL_OK && buf && bytes_read) {
    size_t copy_len = mock_min_size(count, fake_rx_len);
    if (copy_len > 0) {
      memcpy(buf, fake_rx_data, copy_len);
      if (fake_rx_len > copy_len) {
        memmove(fake_rx_data, fake_rx_data + copy_len, fake_rx_len - copy_len);
      }
      fake_rx_len -= copy_len;
    }
    *bytes_read = copy_len;
  }
  return result;
}

static salts_serial_result_t fake_nonblocking_write_impl(salts_port_handle_t *handle,
                                                          const void *buf, size_t count,
                                                          size_t *bytes_written) {
  salts_serial_result_t result = fake_nonblocking_write(handle, buf, count);
  if (result == SALTS_SERIAL_OK && buf && bytes_written) {
    size_t copy_len = mock_min_size(count, sizeof(fake_tx_data) - fake_tx_len);
    if (copy_len > 0) {
      memcpy(fake_tx_data + fake_tx_len, buf, copy_len);
      fake_tx_len += copy_len;
    }
    *bytes_written = copy_len;
  }
  return result;
}

static void fake_close(salts_port_handle_t *handle) {
  (void)handle;
}

static const salts_serial_backend_ops_t fake_backend_ops = {
    .close = fake_close,
    .nonblocking_read = fake_nonblocking_read_impl,
    .nonblocking_write = fake_nonblocking_write_impl,
};

suite("salts_serial mocked backend") {
#if defined(_WIN32) || defined(__CYGWIN__)
  it("owns converted Windows metadata through tstr storage traits") {
    salts_serial_port_info_storage_t source = {0};
    salts_serial_port_info_storage_t copy = {0};
    source.name = salts_serial_test_wchar_to_utf8(L"COM7");
    source.description = salts_serial_test_wchar_to_utf8(L"\u4e32\u53e3");
    source.usb_manufacturer = salts_serial_test_wchar_to_utf8(L"");
    check_not_null(source.name);
    check_not_null(source.description);
    check_not_null(source.usb_manufacturer);
    check_equal(tstr_len(source.name), 4u);
    check_equal(tstr_len(source.description), 6u);
    check_equal(tstr_len(source.usb_manufacturer), 0u);
    check_true(salts_serial_port_info_storage_copy(&copy, &source));
    salts_serial_port_info_storage_destroy(&source);
    check_equal(copy.view.name, "COM7");
    check_equal(copy.view.description, "\xe4\xb8\xb2\xe5\x8f\xa3");
    check_equal(copy.view.usb_manufacturer, "");
    salts_serial_port_info_storage_destroy(&copy);
  }
#endif
  static salts_serial_t *serial;

  before_each() {
    serial = NULL;
    memset(fake_rx_data, 0, sizeof(fake_rx_data));
    memset(fake_tx_data, 0, sizeof(fake_tx_data));
    fake_rx_len = 0;
    fake_tx_len = 0;
    controlled_worker_entry = NULL;
    controlled_worker_arg = NULL;
    atomic_store(&controlled_backend_entered, false);
    atomic_store(&controlled_worker_exited, false);
    salts_serial_test_set_thread_create(NULL);

    memset(&read_probe, 0, sizeof(read_probe));
    memset(&write_probe, 0, sizeof(write_probe));
    read_probe.result = SALTS_SERIAL_OK;
    write_probe.result = SALTS_SERIAL_OK;
    check_equal(read_probe.result, SALTS_SERIAL_OK);
    check_equal(write_probe.result, SALTS_SERIAL_OK);

    salts_serial_set_backend_ops_for_testing(&fake_backend_ops);
  }

  after_each() {
    /* Destroy joins the sole pump worker before its borrowed mock history and
     * return state are released, including fatal assertion paths. */
    salts_serial_destroy(serial);
    serial = NULL;
    salts_serial_set_backend_ops_for_testing(NULL);
    salts_serial_test_set_thread_create(NULL);
    memset(&read_probe, 0, sizeof(read_probe));
    memset(&write_probe, 0, sizeof(write_probe));
  }

  it("pumps mocked rx bytes into the rx SPSC ring") {
    salts_serial_config_t config;
    char out[4] = {0};
    size_t bytes_read = 0;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    fake_rx_data[0] = 'A';
    fake_rx_data[1] = 'B';
    fake_rx_data[2] = 'C';
    fake_rx_len = 3;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    check_true(wait_for_buffer_count(serial, salts_serial_rx_available, 3));
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);

    check_equal(salts_serial_rx_available(serial), 3);
    check_equal(salts_serial_read_buffered(serial, out, sizeof(out), &bytes_read),
                SALTS_SERIAL_OK);
    check_equal(bytes_read, 3);
    check_equal(out, "ABC", 3);

    check(read_probe.calls >= 1u);
    check(read_probe.first_handle == FAKE_HANDLE);

    TLOG_DEBUGF("mocked rx pump delivered {} bytes", bytes_read);
  }

  it("drains tx bytes through the mocked backend") {
    salts_serial_config_t config;
    size_t bytes_buffered = 0;

    salts_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    check_not_null(serial);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    check_equal(salts_serial_write_buffered(serial, "XY", 2, &bytes_buffered),
                SALTS_SERIAL_OK);
    check_equal(bytes_buffered, 2);

    check_true(wait_for_buffer_count(serial, salts_serial_tx_available, 0));
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);

    check_equal(fake_tx_len, 2);
    check_equal(fake_tx_data, "XY", 2);

    check(write_probe.calls >= 1u);
    check(write_probe.first_handle == FAKE_HANDLE);

    TLOG_DEBUGF("mocked tx pump drained {} bytes", fake_tx_len);
  }

  it("pumps data when the worker runs before thread creation returns") {
    salts_serial_config_t config;
    char out = 0;
    size_t bytes_read = 0;

    salts_serial_config_default(&config);
    fake_rx_data[0] = 'R';
    fake_rx_len = 1;
    salts_serial_test_set_thread_create(create_worker_before_return);

    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);
    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    check_true(atomic_load(&controlled_backend_entered));
    check_equal(salts_serial_async_running(serial), 1);
    check_true(wait_for_buffer_count(serial, salts_serial_rx_available, 1));
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);
    check_true(atomic_load(&controlled_worker_exited));
    check_equal(salts_serial_read_buffered(serial, &out, sizeof(out), &bytes_read),
                SALTS_SERIAL_OK);
    check_equal(bytes_read, sizeof(out));
    check_equal(out, 'R');
  }

  it("rolls back failed thread creation and allows a later start") {
    salts_serial_config_t config;
    size_t bytes_buffered = 0;

    salts_serial_config_default(&config);
    check_equal(salts_serial_create(&serial, &config), SALTS_SERIAL_OK);
    salts_serial_test_set_handle(serial, FAKE_HANDLE);
    salts_serial_test_set_thread_create(reject_thread_create);

    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_IO_FAILED);
    check_equal(salts_serial_async_running(serial), 0);
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);
    salts_serial_test_set_thread_create(create_worker_before_return);
    check_equal(salts_serial_start_async(serial), SALTS_SERIAL_OK);
    check_true(atomic_load(&controlled_backend_entered));
    check_equal(salts_serial_write_buffered(serial, "XY", 2, &bytes_buffered),
                SALTS_SERIAL_OK);
    check_equal(bytes_buffered, 2);
    check_true(wait_for_buffer_count(serial, salts_serial_tx_available, 0));
    check_equal(salts_serial_stop_async(serial), SALTS_SERIAL_OK);
    check_equal(salts_serial_async_running(serial), 0);
    check_equal(fake_tx_len, 2);
    check_equal(fake_tx_data, "XY", 2);
  }

  it("owns all port-info strings through copy move and repeated destroy") {
    salts_serial_port_info_storage_t source;
    salts_serial_port_info_storage_t moved = {0};
    salts_serial_port_info_vec_t items = {0};
    salts_serial_port_info_storage_t *stored;

    fill_port_storage(&source);
    check_equal(salts_serial_port_info_vec_t_init(&items), SALTS_SERIAL_OK);
    check_equal(salts_serial_port_info_vec_t_push(&items, source), SALTS_SERIAL_OK);
    stored = salts_serial_port_info_vec_t_at(&items, 0);
    check_not_null(stored);
    check_equal(tstr_cmp(stored->name, source.name), 0);
    check_equal(tstr_cmp(stored->description, source.description), 0);
    check_equal(tstr_cmp(stored->usb_manufacturer, source.usb_manufacturer), 0);
    check_equal(tstr_cmp(stored->usb_product, source.usb_product), 0);
    check_equal(tstr_cmp(stored->usb_serial, source.usb_serial), 0);
    check_equal(tstr_cmp(stored->bluetooth_address, source.bluetooth_address), 0);
    check(stored->name != source.name);

    salts_serial_port_info_storage_destroy(&source);
    check_equal(stored->view.name, "COM7");
    salts_serial_port_info_storage_move(&moved, stored);
    check_null(stored->name);
    check_equal(moved.view.usb_serial, "ABC123");
    salts_serial_port_info_storage_move(&moved, &moved);
    check_equal(moved.view.usb_serial, "ABC123");
    salts_serial_port_info_storage_destroy(&moved);
    salts_serial_port_info_storage_destroy(&moved);
    salts_serial_port_info_vec_t_destroy(&items);
  }

  it("rolls back every temporary string when an entry copy fails") {
    salts_serial_port_info_storage_t source;
    salts_serial_port_info_storage_t destination = {0};

    fill_port_storage(&source);
    storage_clone_calls = 0;
    check(!salts_serial_port_info_storage_copy_with(
        &destination, &source, fail_fourth_storage_clone));
    check_equal(storage_clone_calls, 4);
    check_null(destination.name);
    check_null(destination.description);
    check_null(destination.usb_manufacturer);
    check_null(destination.usb_product);
    check_null(destination.usb_serial);
    check_null(destination.bluetooth_address);
    salts_serial_port_info_storage_destroy(&source);
    salts_serial_port_info_storage_destroy(&destination);
  }
}
