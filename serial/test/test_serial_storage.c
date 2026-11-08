#include "serial_internal.h"
#include <tinytest.h>

static size_t clone_call;
static size_t clone_failure;
static size_t allocation_call;
static size_t allocation_failure;

static bool checked_clone(tstr *out, tstr source) {
  if (++clone_call == clone_failure) return false;
  return salts_serial_port_info_storage_clone_string(out, source);
}

static void *checked_allocate(size_t bytes) {
  if (++allocation_call == allocation_failure) return NULL;
  return malloc(bytes);
}

static void make_storage(salts_serial_port_info_storage_t *storage) {
  memset(storage, 0, sizeof(*storage));
  storage->name = tstr_dup("port-7");
  storage->description = tstr_dup("\xe4\xb8\xb2\xe5\x8f\xa3");
  storage->usb_manufacturer = tstr_dup("");
  storage->usb_product = tstr_dup("bridge");
  storage->usb_serial = tstr_dup("123");
  storage->bluetooth_address = tstr_dup("01:23:45:67:89:ab");
  storage->view.transport = SALTS_SERIAL_TRANSPORT_USB;
  storage->view.has_usb_vid_pid = 1;
  storage->view.usb_vid = 0x1234;
  storage->view.usb_pid = 0x5678;
  salts_serial_port_info_storage_refresh_view(storage);
  check_not_null(storage->name);
  check_not_null(storage->description);
  check_not_null(storage->usb_manufacturer);
  check_not_null(storage->usb_product);
  check_not_null(storage->usb_serial);
  check_not_null(storage->bluetooth_address);
}

static void check_storage(const salts_serial_port_info_storage_t *storage) {
  check_not_null(storage);
  check_equal(storage->view.name, "port-7");
  check_equal(storage->view.description, "\xe4\xb8\xb2\xe5\x8f\xa3");
  check_equal(tstr_len(storage->description), (size_t)6u);
  check_equal(storage->view.usb_manufacturer, "");
  check_equal(storage->view.usb_product, "bridge");
  check_equal(storage->view.usb_serial, "123");
  check_equal(storage->view.bluetooth_address, "01:23:45:67:89:ab");
  check_equal(storage->view.transport, SALTS_SERIAL_TRANSPORT_USB);
  check_equal(storage->view.usb_vid, 0x1234);
  check_equal(storage->view.usb_pid, 0x5678);
  check(storage->view.name == storage->name);
  check(storage->view.description == storage->description);
  check(storage->view.usb_manufacturer == storage->usb_manufacturer);
  check(storage->view.usb_product == storage->usb_product);
  check(storage->view.usb_serial == storage->usb_serial);
  check(storage->view.bluetooth_address == storage->bluetooth_address);
}

suite("Serial private port snapshot ownership") {
  it("keeps the destination and source unchanged at each failed string clone") {
    salts_serial_port_info_storage_t source;
    size_t failure;
    make_storage(&source);
    for (failure = 1u; failure <= 6u; ++failure) {
      salts_serial_port_info_storage_t destination = {0};
      salts_serial_port_info_storage_t before;
      destination.name = tstr_dup("retained");
      salts_serial_port_info_storage_refresh_view(&destination);
      before = destination;
      clone_call = 0u;
      clone_failure = failure;
      check(!salts_serial_port_info_storage_copy_with(
          &destination, &source, checked_clone));
      check_equal(clone_call, failure);
      check_equal(memcmp(&destination, &before, sizeof(before)), 0);
      check_storage(&source);
      salts_serial_port_info_storage_destroy(&destination);
    }
    salts_serial_port_info_storage_destroy(&source);
  }

  it("rolls back preparation and growth failures while preserving owned entries") {
    salts_serial_port_info_vec_t items = {0};
    salts_serial_port_info_storage_t source;
    salts_serial_port_info_storage_t *before;
    tstr first_name;
    size_t index;
    make_storage(&source);
    check_equal(salts_serial_port_info_vec_t_init(&items), SALTS_SERIAL_OK);
    check_equal(salts_serial_port_info_vec_t_init(&items), SALTS_SERIAL_INVALID_VALUE);
    for (index = 0u; index < 8u; ++index)
      check_equal(salts_serial_port_info_vec_t_push(&items, source), SALTS_SERIAL_OK);
    check_equal(items.capacity, (size_t)8u);
    before = items.data;
    first_name = items.data[0].name;
    check(first_name != source.name);

    for (index = 1u; index <= 2u; ++index) {
      allocation_call = 0u;
      allocation_failure = index;
      check_equal(salts_serial_port_info_vec_t_push_with(
          &items, source, salts_serial_port_info_storage_clone_string,
          checked_allocate), SALTS_SERIAL_NO_MEMORY);
      check_equal(allocation_call, index);
      check(items.data == before);
      check_equal(items.capacity, (size_t)8u);
      check_equal(items.size, (size_t)8u);
      check_storage(&items.data[0]);
      check_storage(&items.data[7]);
    }
    clone_call = 0u;
    clone_failure = 6u;
    check_equal(salts_serial_port_info_vec_t_push_with(
        &items, source, checked_clone, malloc), SALTS_SERIAL_NO_MEMORY);
    check(items.data == before);
    check_equal(items.size, (size_t)8u);

    check_equal(salts_serial_port_info_vec_t_push(&items, source), SALTS_SERIAL_OK);
    check_equal(items.capacity, (size_t)16u);
    check_equal(items.size, (size_t)9u);
    check(items.data[0].name == first_name);
    salts_serial_port_info_storage_destroy(&source);
    for (index = 0u; index < 9u; ++index)
      check_storage(salts_serial_port_info_vec_t_at_const(&items, index));
    check_null(salts_serial_port_info_vec_t_at(&items, 9u));
    salts_serial_port_info_vec_t_destroy(&items);
    salts_serial_port_info_vec_t_destroy(&items);
    check_null(items.data);
    check_equal(salts_serial_port_info_vec_t_size(&items), (size_t)0u);
  }

  it("preserves null metadata and rejects count overflow before allocation") {
    salts_serial_port_info_vec_t items = {0};
    salts_serial_port_info_storage_t source = {0};
    check_equal(salts_serial_port_info_vec_t_init(&items), SALTS_SERIAL_OK);
    check_equal(salts_serial_port_info_vec_t_push(&items, source), SALTS_SERIAL_OK);
    check_null(items.data[0].view.name);
    check_null(items.data[0].view.description);
    check_null(items.data[0].view.usb_manufacturer);
    salts_serial_port_info_vec_t_destroy(&items);

    check_equal(salts_serial_port_info_vec_t_init(&items), SALTS_SERIAL_OK);
    items.size = salts_serial_port_list_entry_limit();
    allocation_call = 0u;
    allocation_failure = 0u;
    check_equal(salts_serial_port_info_vec_t_push_with(
        &items, source, salts_serial_port_info_storage_clone_string,
        checked_allocate), SALTS_SERIAL_NO_MEMORY);
    check_equal(allocation_call, (size_t)0u);
    items.size = 0u;
    salts_serial_port_info_vec_t_destroy(&items);
  }
}
