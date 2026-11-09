#include <native_io_socket.h>
#include <core/clock.h>
#include <tinytest.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <fcntl.h>
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#endif

static native_io_backend_kind socket_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

/* Operation storage outlives every test body and its cancellation drain. */
static struct {
  native_io_backend backend;
  native_io_transport streams[2];
  uintptr_t sockets[4];
  native_io_socket_address address;
  size_t address_size;
  uint8_t receive[64];
} fixture;

static int open_listener(void) {
  int status = native_io_socket_create(socket_backend(), native_io_socket_domain(NATIVE_IO_SOCKET_IPV4),
                                       false, &fixture.sockets[0]).status;
  if (status != SALTS_OK) return status;
  status = native_io_socket_listener_prepare(fixture.sockets[0], false).status;
  if (status != SALTS_OK) return status;
  status = native_io_transport_parse_bind_address("127.0.0.1", 0, &fixture.address,
      sizeof(fixture.address), &fixture.address_size);
  if (status != SALTS_OK) return status;
  status = native_io_socket_bind(fixture.sockets[0], &fixture.address, fixture.address_size).status;
  if (status != SALTS_OK) return status;
  status = native_io_socket_listen(fixture.sockets[0], 4).status;
  if (status != SALTS_OK) return status;
  return native_io_socket_query(fixture.sockets[0], false, &fixture.address,
      sizeof(fixture.address), &fixture.address_size).status;
}

static int observe_request(native_io_request request, native_io_completion *out) {
  const uint64_t started = monotonic_ms();
  while (monotonic_ms() - started < 3000u) {
    native_io_completion events[4];
    size_t count = 0;
    int status = native_io_backend_observe(&fixture.backend, events, 4, 50, &count);
    if (status != SALTS_OK) return status;
    for (size_t i = 0; i < count; ++i) {
      if (events[i].request.slot != request.slot || events[i].request.generation != request.generation)
        return SALTS_EPROTO;
      *out = events[i];
      return SALTS_OK;
    }
  }
  return SALTS_ETIMEDOUT;
}

static int connect_pair(void) {
  native_io_backend_config config = {socket_backend(), 4, 8, 4};
  int status = native_io_backend_init(&fixture.backend, &config);
  if (status != SALTS_OK) return status;
  status = open_listener();
  if (status != SALTS_OK) return status;
  native_io_request request;
  status = native_io_transport_tcp_connect(&fixture.streams[0], &fixture.backend, socket_backend(),
      &fixture.address, fixture.address_size, 7, &request);
  if (status != SALTS_OK) return status;
  native_io_completion completion;
  status = observe_request(request, &completion);
  if (status != SALTS_OK) return status;
  if (completion.kind != NATIVE_IO_COMPLETION_OK) return completion.status;
  int ready;
  status = native_io_socket_wait(fixture.sockets[0], 1000, &ready).status;
  if (status != SALTS_OK) return status;
  if (!ready) return SALTS_ETIMEDOUT;
  status = native_io_socket_accept(fixture.sockets[0], &fixture.sockets[1], NULL, 0, NULL).status;
  if (status != SALTS_OK) return status;
  const native_io_stream_socket_options options = NATIVE_IO_STREAM_SOCKET_OPTIONS_INIT;
  status = native_io_transport_adopt_tcp(&fixture.streams[1], &fixture.backend,
                                         fixture.sockets[1], &options);
  /* These validated arguments transfer the handle on both success and failure. */
  fixture.sockets[1] = UINTPTR_MAX;
  return status;
}

suite("NativeIO socket and transport ownership") {
  before_each() {
    memset(&fixture, 0, sizeof(fixture));
    for (size_t i = 0; i < 4; ++i) fixture.sockets[i] = UINTPTR_MAX;
    check_equal(native_io_network_init(), SALTS_OK);
  }
  after_each() {
    if (fixture.backend.impl) {
      check_equal(native_io_backend_close(&fixture.backend), SALTS_OK);
      const uint64_t started = monotonic_ms();
      native_io_backend_stats stats;
      for (;;) {
        check(native_io_backend_get_stats(&fixture.backend, &stats));
        if (!stats.active_requests) break;
        /* A failed drain cannot safely reuse operation buffers in another case. */
        if (monotonic_ms() - started >= 3000u) abort();
        native_io_completion events[4]; size_t count;
        check_equal(native_io_backend_observe(&fixture.backend, events, 4, 50, &count), SALTS_OK);
      }
      for (size_t i = 0; i < 2; ++i)
        if (native_io_transport_active(&fixture.streams[i]))
          check_equal(native_io_transport_close(&fixture.streams[i], &fixture.backend), SALTS_OK);
      check_equal(native_io_backend_destroy(&fixture.backend), SALTS_OK);
    }
    for (size_t i = 0; i < 4; ++i)
      if (fixture.sockets[i] != UINTPTR_MAX)
        check_equal(native_io_socket_close(fixture.sockets[i]).status, SALTS_OK);
    native_io_network_shutdown();
  }

  it("rejects invalid factories without returning an owned handle") {
    fixture.sockets[0] = 42;
    check_equal(native_io_socket_create((native_io_backend_kind)INT_MAX,
      native_io_socket_domain(NATIVE_IO_SOCKET_IPV4), false, &fixture.sockets[0]).status, SALTS_ENOTSUP);
    check_equal(fixture.sockets[0], UINTPTR_MAX);
    check_equal(native_io_socket_create_unattached(native_io_socket_domain(NATIVE_IO_SOCKET_IPV4),
      false, NULL).status, SALTS_EINVAL);
    check_equal(native_io_socket_domain((native_io_socket_family)1234), SALTS_EAFNOSUPPORT);
  }

  it("preserves IPv4 and IPv6 metadata when patching an unaligned port") {
    for (size_t version = 0; version < 2; ++version) {
      uint8_t storage[80]; memset(storage, 0xa5, sizeof(storage));
      native_io_stream_endpoint source = {0}, decoded;
      source.family = version ? NATIVE_IO_DATAGRAM_ADDRESS_IPV6 : NATIVE_IO_DATAGRAM_ADDRESS_IPV4;
      source.port = 50693; source.address[version ? 15 : 0] = version ? 1 : 127;
      source.flow_info = version ? 0x123456 : 0; source.scope_id = version ? 7 : 0;
      size_t length;
      check_equal(native_io_socket_encode_datagram_endpoint(&source, storage + 3,
        sizeof(storage) - 3, &length), SALTS_OK);
      int family = native_io_socket_address_family(storage + 3, length);
      check_equal(native_io_socket_address_port(storage + 3, length, family, 50443), SALTS_OK);
      check_equal(native_io_socket_decode_endpoint(storage + 3, length, &decoded, true), SALTS_OK);
      check_equal(decoded.port, (uint16_t)50443);
      check_equal(decoded.family, source.family);
      check_equal(decoded.flow_info, source.flow_info);
      check_equal(decoded.scope_id, source.scope_id);
      check_equal(decoded.address, source.address, sizeof(source.address));
      for (size_t i = 0; i < 3; ++i) check_equal(storage[i], (uint8_t)0xa5);
      for (size_t i = length + 3; i < sizeof(storage); ++i) check_equal(storage[i], (uint8_t)0xa5);
      check_not_equal(native_io_socket_decode_endpoint(storage + 3, 1, &decoded, true), SALTS_OK);
    }
  }

  it("keeps an empty listener nonblocking and transfers no handle on timeout") {
    check_equal(open_listener(), SALTS_OK);
    int ready = -1;
    check_equal(native_io_socket_wait(fixture.sockets[0], 0, &ready).status, SALTS_OK);
    check_equal(ready, 0);
    check_equal(native_io_socket_accept(fixture.sockets[0], &fixture.sockets[1], NULL, 0, NULL).status,
                SALTS_ETIMEDOUT);
    check_equal(fixture.sockets[1], UINTPTR_MAX);
  }

  it("connects through NativeIO and adopts one accepted socket into the same backend") {
    check_equal(connect_pair(), SALTS_OK);
    native_io_backend_stats stats;
    check(native_io_backend_get_stats(&fixture.backend, &stats));
    check_equal(stats.endpoint_count, (size_t)2);
    check_equal(stats.active_requests, (size_t)0);
    native_io_stream_endpoint local, remote;
    check_equal(native_io_transport_tcp_local_endpoint(&fixture.streams[1], &local), SALTS_OK);
    check_equal(native_io_transport_tcp_remote_endpoint(&fixture.streams[0], &remote), SALTS_OK);
    check_equal(local.port, remote.port);
    check_equal(local.address, remote.address, sizeof(local.address));
    check_equal(native_io_transport_tcp_option_set(&fixture.streams[0], NATIVE_IO_TCP_SOCKET_NODELAY, 1), SALTS_OK);
    uint64_t value;
    check_equal(native_io_transport_tcp_option_get(&fixture.streams[0], NATIVE_IO_TCP_SOCKET_NODELAY, &value), SALTS_OK);
    check_equal(value, UINT64_C(1));
  }

  it("prevents listener and accepted socket inheritance") {
    check_equal(connect_pair(), SALTS_OK);
    const uintptr_t handles[] = {fixture.sockets[0], fixture.streams[1].native_handle};
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
      check_equal(native_io_socket_close_on_exec(handles[i]).status, SALTS_OK);
#if defined(_WIN32)
      DWORD flags = 0;
      check(GetHandleInformation((HANDLE)handles[i], &flags));
      check_equal(flags & HANDLE_FLAG_INHERIT, (DWORD)0);
#else
      const int flags = fcntl((int)handles[i], F_GETFD, 0);
      check(flags >= 0);
      check_not_equal(flags & FD_CLOEXEC, 0);
#endif
    }
  }

  it("observes real EOF after a transport half shutdown") {
    check_equal(connect_pair(), SALTS_OK);
    native_io_operation receive = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
      .endpoint = native_io_transport_read_endpoint(&fixture.streams[1]),
      .buffer = fixture.receive, .length = sizeof(fixture.receive)};
    native_io_request request;
    check_equal(native_io_backend_submit(&fixture.backend, &receive, &request), SALTS_OK);
    check_equal(native_io_transport_tcp_shutdown(&fixture.streams[0], NATIVE_IO_TCP_SHUTDOWN_SEND), SALTS_OK);
    native_io_completion completion;
    check_equal(observe_request(request, &completion), SALTS_OK);
    check_equal(completion.kind, NATIVE_IO_COMPLETION_EOF);
    check_equal(completion.bytes, (size_t)0);
  }

  it("retains attached metadata until cancellation is observed") {
    check_equal(connect_pair(), SALTS_OK);
    native_io_operation receive = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
      .endpoint = native_io_transport_read_endpoint(&fixture.streams[0]),
      .buffer = fixture.receive, .length = sizeof(fixture.receive)};
    native_io_request request;
    check_equal(native_io_backend_submit(&fixture.backend, &receive, &request), SALTS_OK);
    check_equal(native_io_backend_cancel(&fixture.backend, request), SALTS_OK);
    check_equal(native_io_transport_close(&fixture.streams[0], &fixture.backend), SALTS_EBUSY);
    check(native_io_transport_active(&fixture.streams[0]));
    native_io_completion completion;
    check_equal(observe_request(request, &completion), SALTS_OK);
    check_equal(completion.kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_equal(native_io_transport_close(&fixture.streams[0], &fixture.backend), SALTS_OK);
    check_false(native_io_transport_active(&fixture.streams[0]));
  }

#if defined(__linux__)
  it("binds a local address shorter than an Internet sockaddr") {
    struct sockaddr_un address = {0};
    const uint32_t process = (uint32_t)getpid();
    const uint32_t stamp = (uint32_t)monotonic_ms();
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path + 1, &process, sizeof(process));
    memcpy(address.sun_path + 1 + sizeof(process), &stamp, sizeof(stamp));
    const size_t length = offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(process) + sizeof(stamp);
    check(length < sizeof(struct sockaddr));
    check_equal(native_io_socket_create_unattached(native_io_socket_domain(NATIVE_IO_SOCKET_LOCAL),
      false, &fixture.sockets[0]).status, SALTS_OK);
    check_equal(native_io_socket_bind(fixture.sockets[0], &address, length).status, SALTS_OK);
    check_equal(native_io_socket_listen(fixture.sockets[0], 1).status, SALTS_OK);
  }
#endif

  it("preserves a UDP explicit bind when removing an association") {
    for (size_t i = 0; i < 2; ++i) {
      check_equal(native_io_socket_create(socket_backend(), native_io_socket_domain(NATIVE_IO_SOCKET_IPV4),
        true, &fixture.sockets[i]).status, SALTS_OK);
      check_equal(native_io_transport_parse_bind_address("127.0.0.1", 0, &fixture.address,
        sizeof(fixture.address), &fixture.address_size), SALTS_OK);
      check_equal(native_io_socket_bind(fixture.sockets[i], &fixture.address, fixture.address_size).status, SALTS_OK);
    }
    native_io_socket_address local, after;
    size_t local_size, after_size;
    native_io_stream_endpoint before_endpoint, after_endpoint;
    check_equal(native_io_socket_query(fixture.sockets[0], false, &local, sizeof(local), &local_size).status, SALTS_OK);
    check_equal(native_io_socket_decode_endpoint(&local, local_size, &before_endpoint, true), SALTS_OK);
    check_equal(native_io_socket_query(fixture.sockets[1], false, &fixture.address,
      sizeof(fixture.address), &fixture.address_size).status, SALTS_OK);
    check_equal(native_io_socket_associate(fixture.sockets[0], &fixture.address, fixture.address_size).status, SALTS_OK);
    check_equal(native_io_socket_associate(fixture.sockets[0], NULL, 0).status, SALTS_OK);
    check_equal(native_io_socket_query(fixture.sockets[0], false, &after, sizeof(after), &after_size).status, SALTS_OK);
    check_equal(native_io_socket_decode_endpoint(&after, after_size, &after_endpoint, true), SALTS_OK);
    check_equal(after_endpoint.port, before_endpoint.port);
    check_equal(after_endpoint.address, before_endpoint.address, sizeof(before_endpoint.address));
  }

  it("rejects oversized observation batches before accessing native descriptors") {
    native_io_socket_pollfd fds[NATIVE_IO_SOCKET_POLL_MAX + 1] = {0};
    int ready;
    check_equal(native_io_socket_poll(fds, NATIVE_IO_SOCKET_POLL_MAX + 1, &ready).status, SALTS_EINVAL);
  }
}
