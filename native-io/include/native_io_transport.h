#ifndef NATIVE_IO_TRANSPORT_H
#define NATIVE_IO_TRANSPORT_H

#include <native_io.h>

/* Caller-owned, zero-initialized transport storage; fields are managed only by
 * these functions. Do not copy a live transport. Calls run on the backend owner.
 * Socket-option descriptors are borrowed only during the call; connect address
 * storage remains borrowed until its real NativeIO terminal completion.
 * Successful factories own their created handles. Adoption follows the stated
 * transfer boundary; failed post-validation TCP adoption closes its input.
 * Close can close native resources before returning EBUSY for retained endpoint
 * metadata: continue routing real completions, then release the drained state.
 * The backend is always borrowed, never destroyed by a transport operation. */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum native_io_datagram_address_family {
  NATIVE_IO_DATAGRAM_ADDRESS_IPV4 = 4,
  NATIVE_IO_DATAGRAM_ADDRESS_IPV6 = 6
} native_io_datagram_address_family;

typedef struct native_io_stream_peer {
  native_io_datagram_address_family family;
  uint16_t port;
  uint32_t scope_id;
  uint8_t address[16];
} native_io_stream_peer;

typedef struct native_io_stream_endpoint {
  native_io_datagram_address_family family;
  uint16_t port;
  uint32_t flow_info;
  uint32_t scope_id;
  uint8_t address[16];
} native_io_stream_endpoint;

typedef struct native_io_stream_socket_options {
  size_t size;
  size_t receive_buffer_bytes;
  size_t send_buffer_bytes;
  uint32_t keepalive_idle_ms;
  uint32_t keepalive_interval_ms;
  uint32_t keepalive_count;
  uint32_t linger_ms;
  int keepalive;
  int linger;
  int nodelay;
} native_io_stream_socket_options;

typedef enum native_io_tcp_socket_option {
  NATIVE_IO_TCP_SOCKET_KEEPALIVE_ENABLED = 1,
  NATIVE_IO_TCP_SOCKET_KEEPALIVE_IDLE_MS,
  NATIVE_IO_TCP_SOCKET_KEEPALIVE_INTERVAL_MS,
  NATIVE_IO_TCP_SOCKET_KEEPALIVE_COUNT,
  NATIVE_IO_TCP_SOCKET_HOP_LIMIT,
  NATIVE_IO_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
  NATIVE_IO_TCP_SOCKET_SEND_BUFFER_BYTES,
  NATIVE_IO_TCP_SOCKET_NODELAY
} native_io_tcp_socket_option;

typedef enum native_io_tcp_shutdown {
  NATIVE_IO_TCP_SHUTDOWN_RECEIVE = 1,
  NATIVE_IO_TCP_SHUTDOWN_SEND = 2,
  NATIVE_IO_TCP_SHUTDOWN_BOTH = 3
} native_io_tcp_shutdown;

#define NATIVE_IO_STREAM_ENDPOINT_INIT {0}
#define NATIVE_IO_STREAM_SOCKET_OPTIONS_INIT {sizeof(native_io_stream_socket_options), 0u, 0u, 0u, 0u, 0u, 0u, 0, 0, 0}
enum { NATIVE_IO_TRANSPORT_PIPE_NAME_CAPACITY = 1025 };
#define NATIVE_IO_VSOCK_CID_ANY UINT32_MAX
#define NATIVE_IO_VSOCK_PORT_ANY UINT32_MAX

int native_io_stream_socket_options_validate(const native_io_stream_socket_options *options);

typedef enum native_io_transport_resource_kind {
  NATIVE_IO_TRANSPORT_RESOURCE_NONE = 0,
  NATIVE_IO_TRANSPORT_RESOURCE_SOCKET,
  NATIVE_IO_TRANSPORT_RESOURCE_PIPE
} native_io_transport_resource_kind;

typedef struct native_io_transport {
  uintptr_t native_handle;
  native_io_endpoint endpoint;
  uintptr_t write_native_handle;
  native_io_endpoint write_endpoint;
  native_io_transport_resource_kind resource_kind;
  bool native_open;
  bool attached;
  bool write_native_open;
  bool write_attached;
} native_io_transport;

/** Applies one validated policy to an unattached TCP socket. */
int native_io_transport_apply_stream_socket_options(
    uintptr_t native_socket, const native_io_stream_socket_options *options);

/**
 * Converts an IPv4 or IPv6 literal plus host-order port into native sockaddr
 * storage. Returns `SALTS_ENOENT` for a hostname so the caller can route it to
 * the resolver. `out_address_length` is cleared on every failure.
 */
int native_io_transport_parse_numeric_address(const char *host, uint16_t port, void *out_address,
                                         size_t address_capacity, size_t *out_address_length);

/** Validates one IPv6 literal without consulting DNS or creating a socket. */
int native_io_transport_validate_ipv6_literal(const char *host);

/** Listener variant that additionally accepts port zero for ephemeral bind. */
int native_io_transport_parse_bind_address(const char *host, uint16_t port, void *out_address,
                                      size_t address_capacity, size_t *out_address_length);

/** Converts one portable TCP peer to native sockaddr storage. */
int native_io_transport_stream_peer_address(const native_io_stream_peer *peer, bool allow_zero_port,
                                       void *out_address, size_t address_capacity,
                                       size_t *out_address_length);

int native_io_transport_stream_endpoint_address(
    const native_io_stream_endpoint *endpoint, bool allow_zero_port,
    void *out_address, size_t address_capacity,
    size_t *out_address_length);

/** Creates and attaches a TCP socket, then describes its connect operation. */
int native_io_transport_tcp_prepare_connect(native_io_transport *transport, native_io_backend *backend,
                                       native_io_backend_kind backend_kind, const void *address,
                                       size_t address_length,
                                       const native_io_stream_socket_options *socket_options,
                                       uintptr_t user_data,
                                       native_io_operation *out_operation);

/**
 * TCP connect variant that binds an optional numeric local endpoint before
 * attaching/submitting the asynchronous connect.
 */
int native_io_transport_tcp_prepare_connect_bound(
    native_io_transport *transport, native_io_backend *backend,
    native_io_backend_kind backend_kind, const void *remote_address,
    size_t remote_address_length, const void *local_address,
    size_t local_address_length,
    const native_io_stream_socket_options *socket_options,
    uintptr_t user_data, native_io_operation *out_operation);

/** Returns whether this build can drive Linux AF_VSOCK on the selected backend. */
bool native_io_transport_vsock_supported(native_io_backend_kind backend_kind);

/** Converts host-order CID/port values into copied native VSOCK address storage. */
int native_io_transport_parse_vsock_address(uint32_t cid, uint32_t port, bool allow_any,
                                       void *out_address, size_t address_capacity,
                                       size_t *out_address_length);

/** Creates and attaches a VSOCK stream, then describes its connect operation. */
int native_io_transport_vsock_prepare_connect(native_io_transport *transport, native_io_backend *backend,
                                         native_io_backend_kind backend_kind, const void *address,
                                         size_t address_length,
                                         const native_io_stream_socket_options *socket_options,
                                         uintptr_t user_data,
                                         native_io_operation *out_operation);

/** Takes ownership of one connected AF_VSOCK SOCK_STREAM socket. */
int native_io_transport_adopt_vsock(native_io_transport *transport, native_io_backend *backend,
                               uintptr_t native_socket,
                               const native_io_stream_socket_options *socket_options);

/** Socket-family-neutral stream connect; native family/protocol tokens are borrowed values. */
int native_io_transport_stream_prepare_connect(native_io_transport *transport, native_io_backend *backend,
                                          native_io_backend_kind backend_kind, int family,
                                          int protocol, bool socket_options_supported,
                                          const void *address, size_t address_length,
                                          const native_io_stream_socket_options *socket_options,
                                          uintptr_t user_data,
                                          native_io_operation *out_operation);

/** Connected stream adoption; once transport/backend/handle are valid, the handle is consumed even on policy or attach failure. */
int native_io_transport_adopt_stream(native_io_transport *transport, native_io_backend *backend,
                                uintptr_t native_socket, bool socket_options_supported,
                                const native_io_stream_socket_options *socket_options);

/**
 * Creates a NativeIO-owned stream socket, attaches it to NativeIO, and submits one
 * asynchronous connect. `address` stays borrowed until the request completion.
 */
int native_io_transport_tcp_connect(native_io_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request);

/**
 * Takes ownership of one connected TCP socket and attaches it to NativeIO.
 * The handle is closed on every failure after argument validation.
 */
int native_io_transport_adopt_tcp(native_io_transport *transport, native_io_backend *backend,
                             uintptr_t native_socket,
                             const native_io_stream_socket_options *socket_options);

int native_io_transport_adopt_tcp_prepare_connect(
    native_io_transport *transport, native_io_backend *backend,
    uintptr_t native_socket, const void *remote_address,
    size_t remote_address_length,
    const native_io_stream_socket_options *socket_options,
    uintptr_t user_data, native_io_operation *out_operation);

/** Closes one unattached native socket transferred through a failed command. */
void native_io_transport_close_socket(uintptr_t native_socket);

/** Creates, connects, and attaches one NativeIO-owned datagram socket. */
int native_io_transport_udp_connect(native_io_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length);

/**
 * Adopts one connected byte-pipe connection. On success NativeIO owns both native
 * handles; the read and write handles may be identical for a duplex pipe. On
 * failure ownership remains with the caller.
 */
int native_io_transport_adopt_pipe(native_io_transport *transport, native_io_backend *backend,
                              uintptr_t read_handle, uintptr_t write_handle);

/**
 * Opens one platform byte-pipe client endpoint and attaches it to NativeIO.
 * Windows maps `name` to one duplex overlapped named pipe. POSIX maps `name`
 * to the nonblocking FIFO pair `name.rx` (read) and `name.tx` (write).
 */
int native_io_transport_pipe_connect(native_io_transport *transport, native_io_backend *backend,
                                native_io_backend_kind backend_kind, const char *name);

int native_io_transport_ipc_validate_name(const char *name, size_t length);
int native_io_transport_ipc_native_name(const char *name, char **out_name);
int native_io_transport_ipc_address(const char *name, void *address, size_t capacity, size_t *out_length);
int native_io_transport_ipc_prepare_connect(native_io_transport *transport, native_io_backend *backend,
                                        native_io_backend_kind backend_kind, const char *name,
                                        void *address, size_t address_length,
                                        native_io_operation *out_operation, bool *out_immediate);
int native_io_transport_adopt_ipc(native_io_transport *transport, native_io_backend *backend,
                              uintptr_t native_handle);
int native_io_transport_close_ipc(uintptr_t native_handle);

native_io_endpoint native_io_transport_read_endpoint(const native_io_transport *transport);
native_io_endpoint native_io_transport_write_endpoint(const native_io_transport *transport);

/** Copies portable endpoints for an attached/open TCP stream transport. */
int native_io_transport_tcp_local_peer(const native_io_transport *transport, native_io_stream_peer *out_peer);
int native_io_transport_tcp_remote_peer(const native_io_transport *transport, native_io_stream_peer *out_peer);

int native_io_transport_tcp_local_endpoint(
    const native_io_transport *transport,
    native_io_stream_endpoint *out_endpoint);
int native_io_transport_tcp_remote_endpoint(
    const native_io_transport *transport,
    native_io_stream_endpoint *out_endpoint);

int native_io_transport_tcp_option_get(const native_io_transport *transport,
                                  native_io_tcp_socket_option option,
                                  uint64_t *out_value);
int native_io_transport_tcp_option_set(native_io_transport *transport,
                                  native_io_tcp_socket_option option,
                                  uint64_t value);

/** Raw-owner option helpers; these calls borrow handles and never close them. */
int native_io_transport_tcp_native_option_get(uintptr_t native_socket,
                                         native_io_tcp_socket_option option,
                                         uint64_t *out_value);
int native_io_transport_tcp_native_option_set(uintptr_t native_socket,
                                         native_io_tcp_socket_option option,
                                         uint64_t value);
/* Family-aware variants are for unbound internal TCP owners where
 * getsockname() cannot portably recover the socket family yet. */
int native_io_transport_tcp_native_option_get_family(
    uintptr_t native_socket, int native_family,
    native_io_tcp_socket_option option, uint64_t *out_value);
int native_io_transport_tcp_native_option_set_family(
    uintptr_t native_socket, int native_family,
    native_io_tcp_socket_option option, uint64_t value);

int native_io_transport_tcp_shutdown(native_io_transport *transport,
                                native_io_tcp_shutdown how);

bool native_io_transport_active(const native_io_transport *transport);

/** Closes owned native resources, then releases drained NativeIO metadata. */
int native_io_transport_close(native_io_transport *transport, native_io_backend *backend);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_IO_TRANSPORT_H */
