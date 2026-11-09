#ifndef SALTS_NATIVE_IO_SOCKET_H
#define SALTS_NATIVE_IO_SOCKET_H

#include <native_io_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Native status is copied at the failing OS call; it is never thread-local
 * state consulted by the caller. status uses Salts codes, native_status is
 * zero for library validation failures and positive for OS failures. */
typedef struct native_io_socket_result {
  int status;
  int native_status;
} native_io_socket_result;

/** Opaque native address bytes, suitably aligned for the supported socket ABIs.
 * Submit borrows this storage until its matching NativeIO completion. */
typedef union native_io_socket_address {
  uint64_t alignment;
  uint8_t bytes[128];
} native_io_socket_address;

typedef enum native_io_socket_family {
  NATIVE_IO_SOCKET_ANY = 0,
  NATIVE_IO_SOCKET_IPV4 = 4,
  NATIVE_IO_SOCKET_IPV6 = 6,
  NATIVE_IO_SOCKET_LOCAL = 1,
  NATIVE_IO_SOCKET_VSOCK = 40
} native_io_socket_family;

/** Returns an opaque native family token, or a negative Salts error. */
int native_io_socket_domain(native_io_socket_family family);
int native_io_socket_type(bool datagram);
int native_io_socket_protocol(bool datagram);
int native_io_socket_address_family(const void *address, size_t length);
int native_io_socket_parse_ip(const char *text, native_io_stream_peer *out_peer);
bool native_io_socket_reuse_port_supported(void);

/** Successful create/accept transfers one handle to the caller; failure clears
 * out_socket to UINTPTR_MAX and transfers nothing. Management borrows handles;
 * close consumes one live handle exactly once. Closing an attached handle is
 * not an operation terminal: retain endpoint metadata and all borrowed storage
 * until real completions are observed and the endpoint can be released.
 * No worker, backend substitution, observer, or payload allocation is introduced. */
native_io_socket_result native_io_socket_create(native_io_backend_kind backend,
    int native_family, bool datagram, uintptr_t *out_socket);
/** Factory for listeners whose eventual connection/backend owner is not yet
 * selected. It allocates no backend or endpoint metadata. */
native_io_socket_result native_io_socket_create_unattached(int native_family,
    bool datagram, uintptr_t *out_socket);
native_io_socket_result native_io_socket_close(uintptr_t socket);
native_io_socket_result native_io_socket_nonblocking(uintptr_t socket);
/** Prevents inheritance across exec/CreateProcess; all other handle flags remain.
 * Does not transfer ownership. A native failure is reported without retry. */
native_io_socket_result native_io_socket_close_on_exec(uintptr_t socket);
native_io_socket_result native_io_socket_reuse(uintptr_t socket, bool port, bool enabled);
/** Applies native listener defaults and nonblocking mode without taking ownership. */
native_io_socket_result native_io_socket_listener_prepare(uintptr_t socket, bool reuse_port);
native_io_socket_result native_io_socket_bind(uintptr_t socket, const void *address, size_t length);
native_io_socket_result native_io_socket_listen(uintptr_t socket, size_t backlog);

/** Single nonblocking accept. ETIMEDOUT means no accepted handle; EINTR is
 * returned without a handle. Optional peer storage is copied before return.
 * Accepted handles retain native accept flags; the owner applies nonblocking
 * and inheritance policy through management calls before adoption. */
native_io_socket_result native_io_socket_accept(uintptr_t socket, uintptr_t *out_socket,
    void *peer, size_t capacity, size_t *out_length);
/** Bounded readiness wait; EINTR consumes the original deadline. */
native_io_socket_result native_io_socket_wait(uintptr_t socket, uint32_t timeout_ms, int *out_ready);
native_io_socket_result native_io_socket_query(uintptr_t socket, bool remote,
    void *address, size_t capacity, size_t *out_length);

/** Pure bounded address codecs. Stream flow-info retains its native numeric
 * representation; datagram codecs convert its host-order numeric value. */
int native_io_socket_decode_endpoint(const void *address, size_t length,
    native_io_stream_endpoint *out_endpoint, bool datagram);
int native_io_socket_encode_datagram_endpoint(const native_io_stream_endpoint *endpoint,
    void *address, size_t capacity, size_t *out_length);
int native_io_socket_address_port(void *address, size_t length, int family, uint16_t port);
int native_io_socket_decode_vsock(const void *address, size_t length,
    uint32_t *out_cid, uint32_t *out_port);

/** NULL disconnects UDP while preserving the existing explicit local bind. */
native_io_socket_result native_io_socket_associate(uintptr_t socket,
    const void *address, size_t length);

enum { NATIVE_IO_SOCKET_POLL_READ = 1u, NATIVE_IO_SOCKET_POLL_WRITE = 2u,
       NATIVE_IO_SOCKET_POLL_ERROR = 4u, NATIVE_IO_SOCKET_POLL_HANGUP = 8u,
       NATIVE_IO_SOCKET_POLL_INVALID = 16u, NATIVE_IO_SOCKET_POLL_MAX = 16u };
typedef struct native_io_socket_pollfd {
  uintptr_t handle;
  unsigned int events;
  unsigned int revents;
} native_io_socket_pollfd;
/** Nonblocking borrowed-FD observation, 1 through POLL_MAX entries. No handles
 * attach to another backend and no operation borrows survive this call. */
native_io_socket_result native_io_socket_poll(native_io_socket_pollfd *fds, size_t count,
    int *out_ready);

/** Balance each successful network_init with network_shutdown after all owned
 * sockets are closed. POSIX needs no process network initialization. */
int native_io_network_init(void);
void native_io_network_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
