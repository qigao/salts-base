# NativeIO composition across SDKs

The public contracts are in `include/native_io.h`, `native_io_sharded.h`,
`native_ipc.h` and `native_io_accept.h`. Link `Salts::NativeIO` from SaltsBase.

## Listener accept ownership

CNet moved into the separate salts-core network SDK. Its external-progress
listener already used two NativeIO accept functions through a repository-private
header. That header was not installed, so the split could not compile against
SaltsBase alone. The authorized solution publishes their existing declarations
in `<native_io_accept.h>`; implementation, symbol names and request lifecycle
stay unchanged. The historical `native_io_internal_*` names retain their ABI.

A successful submit reserves a bounded backend request, not an accepted socket.
The existing single progress owner observes and routes its real completion.
A successful take transfers the accepted OS socket once to the transport owner;
that owner adopts or closes it. Until take, the backend owns its escrow. Cancel,
close and destroy still require terminal routing and existing drain semantics.
Capacity and backend failures propagate; no hidden worker, retry or alternate
observer is added. The public header specifies arguments, outputs and errors.

Copying private declarations into CNet would create a second contract source;
borrowing a sibling source directory would defeat installed-SDK isolation.
Neither is used. Publishing the two existing declarations adds a supported
composition surface but does not expose backend structure layouts or add a
parallel API. C and C++ formal tests cover admission failures/output ownership
and linkage; the CNet owner suite covers the real external-listener lifecycle.

Rollback requires removing the dependent CNet change together with this header
move and using a separate installation prefix. Keep exactly one NativeIO
implementation; do not install or compile a compatibility backend.

## Startup repair retained during extraction

The extracted runtime also retains the upstream bootstrap cancellation repair:
an accepted executor bootstrap publishes its original cancellation status and
terminal flag before creation joins and reclaims borrowed shard storage. Failed
creation leaves the result NULL. A vacant Disruptor consumer slot uses strong
compare-exchange because each candidate slot is attempted once. The formal
Core registration test keeps the one-slot/four-slot 4096-round regression;
CNet's repeated sharded lifecycle/isolation cases exercise the composition.

## Socket and listener ownership boundary

CNet's platform adapters have moved to NativeIO so the network SDK uses
one platform I/O boundary. NativeIO owns native socket creation, options,
address encoding/decoding, bind/listen, synchronous nonblocking accept,
readiness waits, shutdown and native close. CNet retains protocol policy,
connection state, admission limits and application callbacks. Existing NativeIO
submission/completion semantics remain the data path; this change adds no
worker, observer, implicit retry or alternate backend.

A created or accepted handle has exactly one owner. Attaching borrows the OS
handle for generation-checked endpoint metadata. TCP/stream adoption consumes
its input once transport/backend/handle arguments are valid, including policy
or attach failure; pipe adoption transfers only on success. The adopted-connect
variant consumes its socket on every return. These boundaries preserve CNet's
existing cleanup contract.
Rejected admission retains no payload borrow. After close/cancellation the
owner continues observing real terminal completions, then releases endpoint
metadata. Accepted handles held by the external-accept escrow transfer exactly
once through the existing take operation. A timeout does not grant ownership
or permit early storage reuse.

All address and operation buffers remain caller-owned and bounded. NativeIO
copies address metadata only where the API promises a copy; operation storage
remains borrowed until the matching terminal is observed. Socket management
calls run on the existing owner and propagate the original Salts status codes.
Unsupported options and platforms fail explicitly. UDP disconnect preserves the bound local port; local IP selection follows the
native platform and must be queried again, as in CNet's existing contract.
Socket shutdown direction,
IPv6 scope/flow information, option units and rounding, listener reuse policy,
and detached-accept ownership preserve the existing CNet behavior.

The alternative of leaving native calls in CNet retains duplicated platform
boundaries after the SDK split. Moving protocol and connection policy into
NativeIO would invert module ownership. The chosen boundary moves only the
platform adapters and leaves CNet's public API unchanged. Migration requires a
new SaltsBase SDK before the consuming CNet change can be released. Rollback
must pair the previous CNet revision with its matching SaltsBase SDK; do not
install parallel socket implementations or overwrite an existing SDK release.

The public entry points are `<native_io_socket.h>` and
`<native_io_transport.h>`, linked through `Salts::NativeIO`. Management returns
`native_io_socket_result`: `status` is the Salts error, and `native_status` is
the captured OS code (zero for a library validation failure). Endpoint/option
codecs return a Salts status directly. IPv4/IPv6 addresses and ports in the
portable descriptors are host-order metadata; address bytes retain network
byte order. Zero-init transport storage and never reuse a live instance.

For example, on the existing backend owner, a numeric listener is constructed
with the following sequence; stop on each non-OK `status` and close the owned
handle on any later failure:

```c
native_io_socket_address address;
size_t length = 0;
uintptr_t listener = UINTPTR_MAX;
int status = native_io_transport_parse_bind_address("127.0.0.1", 0,
    &address, sizeof(address), &length);
if (status != SALTS_OK) return status;
native_io_socket_result result = native_io_socket_create(backend_kind,
    native_io_socket_domain(NATIVE_IO_SOCKET_IPV4), false, &listener);
if (result.status != SALTS_OK) return result.status;
result = native_io_socket_listener_prepare(listener, false);
if (result.status == SALTS_OK)
  result = native_io_socket_bind(listener, &address, length);
if (result.status == SALTS_OK)
  result = native_io_socket_listen(listener, 16);
if (result.status != SALTS_OK) {
  (void)native_io_socket_close(listener);
  return result.status;
}
/* Retain listener until its owner explicitly closes it. */
```

For IPC, preserve the existing inheritance policy with
`native_io_socket_close_on_exec` on both listener and accepted handles. An empty
nonblocking accept returns `SALTS_ETIMEDOUT` and `UINTPTR_MAX`; it does not retry
or create an endpoint. `native_io_socket_wait` bounds interruption handling by
its original deadline. `native_io_socket_poll` observes 1–16 borrowed descriptors
without attaching them or becoming another completion owner. Unsupported
families/options return `SALTS_EAFNOSUPPORT`/`SALTS_ENOTSUP`. Invalid sizes and
out-of-range option values fail before borrowing operation buffers.
