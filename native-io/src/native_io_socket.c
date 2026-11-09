#include <native_io_socket.h>
#include <core/clock.h>
#include <limits.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_handle;
#else
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/vm_sockets.h>
#endif
typedef int socket_handle;
#endif

_Static_assert(sizeof(struct sockaddr_storage) <= sizeof(native_io_socket_address),
               "native address carrier must hold sockaddr_storage");
static native_io_socket_result socket_status(int status) {
  return (native_io_socket_result){status, 0};
}
static int socket_native_status(int error) {
#if defined(_WIN32)
  if (error == WSAEADDRINUSE) return SALTS_EADDRINUSE;
  if (error == WSAEADDRNOTAVAIL) return SALTS_EADDRNOTAVAIL;
  if (error == WSAEAFNOSUPPORT) return SALTS_EAFNOSUPPORT;
  if (error == WSAEALREADY) return SALTS_EALREADY;
  if (error == WSAEBADF) return SALTS_EBADF;
  if (error == WSAEACCES) return SALTS_EPERM;
  if (error == WSAECONNABORTED) return SALTS_ECONNABORTED;
  if (error == WSAECONNREFUSED) return SALTS_ECONNREFUSED;
  if (error == WSAECONNRESET) return SALTS_ECONNRESET;
  if (error == WSAEDESTADDRREQ) return SALTS_EDESTADDRREQ;
  if (error == WSAEFAULT) return SALTS_EFAULT;
  if (error == WSAEHOSTUNREACH) return SALTS_EHOSTUNREACH;
  if (error == WSAEINTR) return SALTS_EINTR;
  if (error == WSAEINVAL) return SALTS_EINVAL;
  if (error == WSAEISCONN) return SALTS_EISCONN;
  if (error == WSAEMFILE) return SALTS_EMFILE;
  if (error == WSAEMSGSIZE) return SALTS_EMSGSIZE;
  if (error == WSAENETDOWN) return SALTS_ENETDOWN;
  if (error == WSAENETUNREACH) return SALTS_ENETUNREACH;
  if (error == WSAENOBUFS) return SALTS_ENOBUFS;
  if (error == WSAENOPROTOOPT) return SALTS_ENOPROTOOPT;
  if (error == WSAENOTCONN) return SALTS_ENOTCONN;
  if (error == WSAENOTSOCK) return SALTS_ENOTSOCK;
  if (error == WSAEOPNOTSUPP) return SALTS_ENOTSUP;
  if (error == WSAEPROTONOSUPPORT) return SALTS_EPROTONOSUPPORT;
  if (error == WSAEPROTOTYPE) return SALTS_EPROTOTYPE;
  if (error == WSAESHUTDOWN) return SALTS_ESHUTDOWN;
  if (error == WSAETIMEDOUT) return SALTS_ETIMEDOUT;
  if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) return SALTS_EBUSY;
#else
  if (error == EADDRINUSE) return SALTS_EADDRINUSE;
  if (error == EADDRNOTAVAIL) return SALTS_EADDRNOTAVAIL;
  if (error == EAFNOSUPPORT) return SALTS_EAFNOSUPPORT;
  if (error == EALREADY) return SALTS_EALREADY;
  if (error == EBADF) return SALTS_EBADF;
  if (error == EBUSY) return SALTS_EBUSY;
  if (error == EACCES || error == EPERM) return SALTS_EPERM;
  if (error == ECONNABORTED) return SALTS_ECONNABORTED;
  if (error == ECONNREFUSED) return SALTS_ECONNREFUSED;
  if (error == ECONNRESET) return SALTS_ECONNRESET;
  if (error == EDESTADDRREQ) return SALTS_EDESTADDRREQ;
  if (error == EFAULT) return SALTS_EFAULT;
  if (error == EHOSTUNREACH) return SALTS_EHOSTUNREACH;
  if (error == EINTR) return SALTS_EINTR;
  if (error == EINVAL) return SALTS_EINVAL;
  if (error == EISCONN) return SALTS_EISCONN;
  if (error == EMFILE) return SALTS_EMFILE;
  if (error == EMSGSIZE) return SALTS_EMSGSIZE;
  if (error == ENETDOWN) return SALTS_ENETDOWN;
  if (error == ENETUNREACH) return SALTS_ENETUNREACH;
  if (error == ENFILE) return SALTS_ENFILE;
  if (error == ENODEV) return SALTS_ENODEV;
  if (error == ENOBUFS) return SALTS_ENOBUFS;
  if (error == ENOMEM) return SALTS_ENOMEM;
  if (error == ENOPROTOOPT) return SALTS_ENOPROTOOPT;
  if (error == ENOTCONN) return SALTS_ENOTCONN;
  if (error == ENOTSOCK) return SALTS_ENOTSOCK;
  if (error == EOPNOTSUPP) return SALTS_ENOTSUP;
  if (error == EPROTONOSUPPORT) return SALTS_EPROTONOSUPPORT;
  if (error == EPROTOTYPE) return SALTS_EPROTOTYPE;
  if (error == EPIPE) return SALTS_EPIPE;
  if (error == ETIMEDOUT) return SALTS_ETIMEDOUT;
  if (error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS) return SALTS_EBUSY;
#endif
  return SALTS_EIO;
}


static native_io_socket_result socket_error(void) {
#if defined(_WIN32)
  const int code = WSAGetLastError();
#else
  const int code = errno;
#endif
  return (native_io_socket_result){socket_native_status(code), code};
}

int native_io_network_init(void) {
#if defined(_WIN32)
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? SALTS_OK : SALTS_EIO;
#else
  return SALTS_OK;
#endif
}
void native_io_network_shutdown(void) {
#if defined(_WIN32)
  (void)WSACleanup();
#endif
}
int native_io_socket_domain(native_io_socket_family family) {
  switch (family) {
  case NATIVE_IO_SOCKET_ANY: return AF_UNSPEC;
  case NATIVE_IO_SOCKET_IPV4: return AF_INET;
  case NATIVE_IO_SOCKET_IPV6: return AF_INET6;
#if !defined(_WIN32)
  case NATIVE_IO_SOCKET_LOCAL: return AF_UNIX;
#endif
#if defined(__linux__)
  case NATIVE_IO_SOCKET_VSOCK: return AF_VSOCK;
#endif
  default: return SALTS_EAFNOSUPPORT;
  }
}
int native_io_socket_type(bool datagram) { return datagram ? SOCK_DGRAM : SOCK_STREAM; }
int native_io_socket_protocol(bool datagram) { return datagram ? IPPROTO_UDP : IPPROTO_TCP; }
int native_io_socket_address_family(const void *address, size_t length) {
  struct sockaddr header = {0};
  const size_t needed = offsetof(struct sockaddr, sa_family) + sizeof(header.sa_family);
  if (!address || length < needed) return SALTS_EINVAL;
  memcpy(&header, address, length < sizeof(header) ? length : sizeof(header));
  return header.sa_family;
}
int native_io_socket_parse_ip(const char *text, native_io_stream_peer *out) {
  if (!text || !out) return SALTS_EINVAL;
  *out = (native_io_stream_peer){0};
  if (inet_pton(AF_INET, text, out->address) == 1) out->family = NATIVE_IO_DATAGRAM_ADDRESS_IPV4;
  else if (inet_pton(AF_INET6, text, out->address) == 1) out->family = NATIVE_IO_DATAGRAM_ADDRESS_IPV6;
  else return SALTS_ENOENT;
  return SALTS_OK;
}
bool native_io_socket_reuse_port_supported(void) {
#ifdef SO_REUSEPORT
  return true;
#else
  return false;
#endif
}

native_io_socket_result native_io_socket_create(native_io_backend_kind backend,
    int family, bool datagram, uintptr_t *out_socket) {
  if (!out_socket) return socket_status(SALTS_EINVAL);
  *out_socket = UINTPTR_MAX;
  if (!native_io_backend_kind_supported(backend)) return socket_status(SALTS_ENOTSUP);
  return native_io_socket_create_unattached(family, datagram, out_socket);
}
native_io_socket_result native_io_socket_create_unattached(int family,
    bool datagram, uintptr_t *out_socket) {
  if (!out_socket) return socket_status(SALTS_EINVAL);
  *out_socket = UINTPTR_MAX;
  int protocol = family == AF_INET || family == AF_INET6 ?
      (datagram ? IPPROTO_UDP : IPPROTO_TCP) : 0;
  socket_handle handle;
#if defined(_WIN32)
  handle = WSASocketW(family, datagram ? SOCK_DGRAM : SOCK_STREAM, protocol,
                      NULL, 0u, WSA_FLAG_OVERLAPPED);
  if (handle == INVALID_SOCKET) return socket_error();
#else
  handle = socket(family, datagram ? SOCK_DGRAM : SOCK_STREAM, protocol);
  if (handle < 0) return socket_error();
#endif
  *out_socket = (uintptr_t)handle;
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_close(uintptr_t handle) {
  if (handle == UINTPTR_MAX) return socket_status(SALTS_EINVAL);
#if defined(_WIN32)
  if (closesocket((socket_handle)handle) != 0) return socket_error();
#else
  if (close((socket_handle)handle) != 0) return socket_error();
#endif
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_nonblocking(uintptr_t handle) {
#if defined(_WIN32)
  u_long enabled = 1;
  if (ioctlsocket((socket_handle)handle, FIONBIO, &enabled) != 0) return socket_error();
#else
  const int flags = fcntl((socket_handle)handle, F_GETFL, 0);
  if (flags < 0 || fcntl((socket_handle)handle, F_SETFL, flags | O_NONBLOCK) != 0)
    return socket_error();
#endif
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_close_on_exec(uintptr_t handle) {
#if defined(_WIN32)
  if (!SetHandleInformation((HANDLE)handle, HANDLE_FLAG_INHERIT, 0))
    return (native_io_socket_result){SALTS_EIO, (int)GetLastError()};
#else
  const int flags = fcntl((socket_handle)handle, F_GETFD, 0);
  if (flags < 0 || fcntl((socket_handle)handle, F_SETFD, flags | FD_CLOEXEC) != 0)
    return socket_error();
#endif
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_reuse(uintptr_t handle, bool port, bool enabled) {
  const int value = enabled ? 1 : 0;
  int option = SO_REUSEADDR;
  if (port) {
#ifdef SO_REUSEPORT
    option = SO_REUSEPORT;
#else
    return socket_status(SALTS_ENOTSUP);
#endif
  }
#if defined(_WIN32)
  if (setsockopt((socket_handle)handle, SOL_SOCKET, option, (const char *)&value, sizeof(value)) != 0)
#else
  if (setsockopt((socket_handle)handle, SOL_SOCKET, option, &value, sizeof(value)) != 0)
#endif
    return socket_error();
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_bind(uintptr_t handle, const void *address, size_t length) {
  if (length > INT_MAX || native_io_socket_address_family(address, length) < 0)
    return socket_status(SALTS_EINVAL);
  if (bind((socket_handle)handle, address, (int)length) != 0) return socket_error();
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_listener_prepare(uintptr_t handle, bool reuse_port) {
  native_io_socket_result result = socket_status(SALTS_OK);
#if !defined(_WIN32)
  result = native_io_socket_reuse(handle, false, true);
#endif
  if (result.status == SALTS_OK && reuse_port) result = native_io_socket_reuse(handle, true, true);
  if (result.status == SALTS_OK) result = native_io_socket_nonblocking(handle);
  return result;
}
native_io_socket_result native_io_socket_listen(uintptr_t handle, size_t backlog) {
  if (!backlog || backlog > INT_MAX) return socket_status(SALTS_EINVAL);
  if (listen((socket_handle)handle, (int)backlog) != 0) return socket_error();
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_query(uintptr_t handle, bool remote,
    void *address, size_t capacity, size_t *out_length) {
  if (!out_length) return socket_status(SALTS_EINVAL);
  *out_length = 0;
  if (!address || capacity < sizeof(struct sockaddr_storage) || capacity > INT_MAX)
    return socket_status(SALTS_EINVAL);
#if defined(_WIN32)
  int length = (int)capacity;
#else
  socklen_t length = (socklen_t)capacity;
#endif
  memset(address, 0, sizeof(struct sockaddr_storage));
  if ((remote ? getpeername((socket_handle)handle, address, &length) :
                getsockname((socket_handle)handle, address, &length)) != 0) return socket_error();
  *out_length = (size_t)length;
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_accept(uintptr_t handle, uintptr_t *out_socket,
    void *peer, size_t capacity, size_t *out_length) {
  if (!out_socket) return socket_status(SALTS_EINVAL);
  *out_socket = UINTPTR_MAX;
  if (out_length) *out_length = 0;
  if (peer && (!out_length || capacity < sizeof(struct sockaddr_storage) || capacity > INT_MAX))
    return socket_status(SALTS_EINVAL);
#if defined(_WIN32)
  int length = (int)capacity;
#else
  socklen_t length = (socklen_t)capacity;
#endif
  socket_handle accepted = accept((socket_handle)handle, peer, peer ? &length : NULL);
#if defined(_WIN32)
  if (accepted == INVALID_SOCKET) {
#else
  if (accepted < 0) {
#endif
    native_io_socket_result result = socket_error();
#if defined(_WIN32)
    if (result.native_status == WSAEWOULDBLOCK || result.native_status == WSAEINPROGRESS)
#else
    if (result.native_status == EAGAIN || result.native_status == EWOULDBLOCK || result.native_status == EINPROGRESS)
#endif
      result.status = SALTS_ETIMEDOUT;
    return result;
  }
  *out_socket = (uintptr_t)accepted;
  if (peer) *out_length = (size_t)length;
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_wait(uintptr_t handle, uint32_t timeout_ms, int *out_ready) {
  if (!out_ready) return socket_status(SALTS_EINVAL);
  *out_ready = 0;
  int timeout = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
#if defined(_WIN32)
  WSAPOLLFD fd = {(SOCKET)handle, POLLIN, 0};
  int result = WSAPoll(&fd, 1, timeout);
#else
  struct pollfd fd = {(int)handle, POLLIN, 0};
  const uint64_t started = monotonic_ms();
  int result;
  for (;;) {
    fd.revents = 0;
    result = poll(&fd, 1, timeout);
    if (result >= 0 || errno != EINTR) break;
    const uint64_t elapsed = monotonic_ms() - started;
    if (elapsed >= timeout_ms) { result = 0; break; }
    uint64_t remaining = timeout_ms - elapsed;
    timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
  }
#endif
  if (result < 0) return socket_error();
  if (result > 0 && (fd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) *out_ready = 1;
  return socket_status(SALTS_OK);
}

int native_io_socket_decode_endpoint(const void *address, size_t length,
    native_io_stream_endpoint *out, bool datagram) {
  if (!out || !address) return SALTS_EINVAL;
  *out = (native_io_stream_endpoint){0};
  const int family = native_io_socket_address_family(address, length);
  if (family == AF_INET && length >= sizeof(struct sockaddr_in)) {
    struct sockaddr_in storage;
    memcpy(&storage, address, sizeof(storage));
    const struct sockaddr_in *a = &storage;
    out->family = NATIVE_IO_DATAGRAM_ADDRESS_IPV4; out->port = ntohs(a->sin_port);
    memcpy(out->address, &a->sin_addr, 4);
  } else if (family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
    struct sockaddr_in6 storage;
    memcpy(&storage, address, sizeof(storage));
    const struct sockaddr_in6 *a = &storage;
    out->family = NATIVE_IO_DATAGRAM_ADDRESS_IPV6; out->port = ntohs(a->sin6_port);
    out->scope_id = a->sin6_scope_id;
    out->flow_info = datagram ? ntohl(a->sin6_flowinfo) : a->sin6_flowinfo;
    memcpy(out->address, &a->sin6_addr, 16);
  } else return SALTS_EAFNOSUPPORT;
  return SALTS_OK;
}
int native_io_socket_encode_datagram_endpoint(const native_io_stream_endpoint *endpoint,
    void *address, size_t capacity, size_t *out_length) {
  if (!out_length) return SALTS_EINVAL;
  *out_length = 0;
  if (!endpoint || !address) return SALTS_EINVAL;
  if (endpoint->family == NATIVE_IO_DATAGRAM_ADDRESS_IPV4) {
    if (capacity < sizeof(struct sockaddr_in)) return SALTS_ERANGE;
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(endpoint->port);
    memcpy(&a.sin_addr, endpoint->address, 4); memcpy(address, &a, sizeof(a)); *out_length = sizeof(a);
  } else if (endpoint->family == NATIVE_IO_DATAGRAM_ADDRESS_IPV6) {
    if (capacity < sizeof(struct sockaddr_in6)) return SALTS_ERANGE;
    struct sockaddr_in6 a = {0}; a.sin6_family = AF_INET6; a.sin6_port = htons(endpoint->port);
    a.sin6_flowinfo = htonl(endpoint->flow_info); a.sin6_scope_id = endpoint->scope_id;
    memcpy(&a.sin6_addr, endpoint->address, 16); memcpy(address, &a, sizeof(a)); *out_length = sizeof(a);
  } else return SALTS_EAFNOSUPPORT;
  return SALTS_OK;
}
int native_io_socket_address_port(void *address, size_t length, int family, uint16_t port) {
  if (!address || !port || native_io_socket_address_family(address, length) != family)
    return SALTS_EINVAL;
  if (family == AF_INET) {
    if (length < sizeof(struct sockaddr_in)) return SALTS_EINVAL;
    struct sockaddr_in value;
    memcpy(&value, address, sizeof(value)); value.sin_port = htons(port);
    memcpy(address, &value, sizeof(value));
  } else if (family == AF_INET6) {
    if (length < sizeof(struct sockaddr_in6)) return SALTS_EINVAL;
    struct sockaddr_in6 value;
    memcpy(&value, address, sizeof(value)); value.sin6_port = htons(port);
    memcpy(address, &value, sizeof(value));
  }
  else return SALTS_EAFNOSUPPORT;
  return SALTS_OK;
}
int native_io_socket_decode_vsock(const void *address, size_t length, uint32_t *cid, uint32_t *port) {
  if (!address || !cid || !port) return SALTS_EINVAL;
#if defined(__linux__)
  struct sockaddr_vm value;
  if (length < sizeof(value)) return SALTS_EAFNOSUPPORT;
  memcpy(&value, address, sizeof(value));
  if (value.svm_family != AF_VSOCK) return SALTS_EAFNOSUPPORT;
  *cid = value.svm_cid;
  *port = value.svm_port;
  return SALTS_OK;
#else
  (void)length;
  return SALTS_ENOTSUP;
#endif
}
native_io_socket_result native_io_socket_associate(uintptr_t handle, const void *address, size_t length) {
  struct sockaddr_storage local, disconnect_address = {0};
#if defined(_WIN32)
  int local_size = sizeof(local);
#else
  socklen_t local_size = sizeof(local);
#endif
  if (address && (length < sizeof(struct sockaddr) || length > INT_MAX)) return socket_status(SALTS_EINVAL);
  if (!address) {
    if (getsockname((socket_handle)handle, (struct sockaddr *)&local, &local_size) != 0) return socket_error();
    disconnect_address.ss_family = AF_UNSPEC;
    address = &disconnect_address; length = sizeof(struct sockaddr);
  }
#if defined(__APPLE__)
  if (address == &disconnect_address) {
    if (disconnectx((socket_handle)handle, SAE_ASSOCID_ANY, SAE_CONNID_ANY) != 0 && errno != ENOTCONN)
      return socket_error();
  } else
#endif
  if (connect((socket_handle)handle, address, (int)length) != 0) return socket_error();
  if (address == &disconnect_address) {
    native_io_socket_address current;
    size_t current_size;
    native_io_stream_endpoint endpoint;
    native_io_socket_result result = native_io_socket_query(handle, false, &current, sizeof(current), &current_size);
    if (result.status != SALTS_OK) return result;
    int status = native_io_socket_decode_endpoint(&current, current_size, &endpoint, true);
    if (status != SALTS_OK) return socket_status(status);
    if (endpoint.port == 0 && bind((socket_handle)handle, (struct sockaddr *)&local, (int)local_size) != 0)
      return socket_error();
  }
  return socket_status(SALTS_OK);
}
native_io_socket_result native_io_socket_poll(native_io_socket_pollfd *fds, size_t count, int *out_ready) {
  if (!fds || !out_ready || count == 0 || count > NATIVE_IO_SOCKET_POLL_MAX) return socket_status(SALTS_EINVAL);
  *out_ready = 0;
#if defined(_WIN32)
  WSAPOLLFD native[NATIVE_IO_SOCKET_POLL_MAX] = {0};
#else
  struct pollfd native[NATIVE_IO_SOCKET_POLL_MAX] = {0};
#endif
  for (size_t i = 0; i < count; ++i) {
    native[i].fd = (socket_handle)fds[i].handle;
    if (fds[i].events & NATIVE_IO_SOCKET_POLL_READ) native[i].events |= POLLIN;
    if (fds[i].events & NATIVE_IO_SOCKET_POLL_WRITE) native[i].events |= POLLOUT;
    fds[i].revents = 0;
  }
#if defined(_WIN32)
  const int ready = WSAPoll(native, (ULONG)count, 0);
#else
  const int ready = poll(native, (nfds_t)count, 0);
#endif
  if (ready < 0) return socket_error();
  for (size_t i = 0; i < count; ++i) {
    if (native[i].revents & POLLIN) fds[i].revents |= NATIVE_IO_SOCKET_POLL_READ;
    if (native[i].revents & POLLOUT) fds[i].revents |= NATIVE_IO_SOCKET_POLL_WRITE;
    if (native[i].revents & POLLERR) fds[i].revents |= NATIVE_IO_SOCKET_POLL_ERROR;
    if (native[i].revents & POLLHUP) fds[i].revents |= NATIVE_IO_SOCKET_POLL_HANGUP;
    if (native[i].revents & POLLNVAL) fds[i].revents |= NATIVE_IO_SOCKET_POLL_INVALID;
  }
  *out_ready = ready;
  return socket_status(SALTS_OK);
}
