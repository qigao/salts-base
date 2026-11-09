#include <native_io_socket.h>
#include <tinytest.hpp>

suite("NativeIO socket C++17 admission") {
  static uintptr_t socket_value;
  before_each() {
    socket_value = UINTPTR_MAX;
    check_equal(native_io_network_init(), SALTS_OK);
  }
  after_each() {
    if (socket_value != UINTPTR_MAX) check_equal(native_io_socket_close(socket_value).status, SALTS_OK);
    native_io_network_shutdown();
  }
  it("uses the C socket result ABI from a C++ owner") {
    auto result = native_io_socket_create_unattached(native_io_socket_domain(NATIVE_IO_SOCKET_IPV4),
      false, &socket_value);
    check_equal(result.status, SALTS_OK);
    check_equal(result.native_status, 0);
    native_io_stream_socket_options options = NATIVE_IO_STREAM_SOCKET_OPTIONS_INIT;
    check_equal(native_io_transport_apply_stream_socket_options(socket_value, &options), SALTS_OK);
  }
}
