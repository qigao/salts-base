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
