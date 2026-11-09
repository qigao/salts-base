# Native Serial owner

`Salts::Serial` provides the original 30-function Serial C ABI through
`<serial.h>`. It owns port handles, metadata snapshots, event sets and the
per-handle receive/transmit queues. The implementation uses the POSIX or Windows
backend selected at build time, plus the native Core, Platform and Concurrency
owners. It does not require CMeta, CSTL, CFlow, TinyMock or libserialport.

## Source layout

- `include/serial.h`: public C/C++ API, installed as `<serial.h>`.
- `src/serial.c`: handle ownership, queues and asynchronous pump.
- `src/serial_internal.h`: private storage and backend contracts.
- `src/backend_posix.c`, `src/backend_win32.c`: mutually exclusive native backends.
- `test/`: formal module tests and test-only injection support.
- `examples/`: six public-API example programs.

Only `include/` is exposed to build-tree consumers. Internal tests receive
`src/` privately; the internal header is not installed.

## Build and consume

From the SaltsCore repository root, with the configured `.env` and compiler
environment loaded (the commands below use the Windows llvm-mingw user preset):

```sh
cmake --preset native-release-user
cmake --build --preset native-release-user
ctest --preset native-release-user -R '^saltscore[.]serial_'
cmake --build --preset install-native-release-user
```

Serial and all six examples are assembled unconditionally. `BUILD_TESTS`
controls the formal module tests. Other platforms use their corresponding
versioned user presets; no separate Serial build profile is required.

An installed application needs only the public target:

```cmake
find_package(SaltsCore 0.1 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE Salts::Serial)
```

`Salts::Serial` is a shared owner. Its native dependencies remain private.
Windows additionally uses `setupapi` and `cfgmgr32`; Unix uses the native
Platform thread implementation. The public header contains only its own types
and standard C declarations, and retains its original C++ linkage and export
macro. Do not define `SALTS_SERIAL_EXPORTS` in an application.

## Ownership and byte behavior

Create a handle with `serial_create`, open/configure it with
`serial_open`, and release it with `serial_destroy`. Destruction
stops the async pump, closes the port and releases the handle-owned queues.
`serial_close` permits a later reopen and is idempotent while closed.

The receive and transmit paths carry bytes, including NUL and bytes that are
not valid UTF-8. A configured power-of-two queue size reserves one sentinel
slot, so size 8 has capacity 7. Successful buffered operations may accept only
part of a request; callers must inspect the returned byte count. An empty read
or full write reports `SALTS_SERIAL_WOULD_BLOCK` with a zero count.

Async operation has one pump thread, one application receive consumer and one
application transmit producer. Applications must synchronize additional
readers or writers. Blocking port I/O cannot run while the pump is active.
The adapter helpers `serial_buffer_rx` and
`serial_drain_tx_buffer` also require a stopped pump.

Port metadata belongs to its `serial_port_list_t` snapshot. All six
optional strings are owned copies; `NULL` metadata remains distinct from an
allocated empty string. Views expire when the snapshot is destroyed. Windows
keeps the original UTF-16/UTF-8 conversion path; serial payloads do not pass
through that conversion.

An event set borrows opened port handles. Destroy the event set before closing
or destroying those handles. Event waits report the first ready port's mask;
a timeout returns `SALTS_SERIAL_WOULD_BLOCK`. The original POSIX blocking-read
timeout instead returns `SALTS_SERIAL_OK` with a zero byte count.

## Validation scope

The formal tests in `test/` cover the public ABI, handle/configuration behavior,
injected backend failures and asynchronous pump behavior, and private storage
ownership. The registered names are `saltscore.serial_original_c11`,
`saltscore.serial_public_abi`, `saltscore.serial_original_fixes`,
`saltscore.serial_backend_contract` and `saltscore.serial_storage_ownership`.
Unix builds also register `saltscore.serial_posix_pty`.

The native POSIX test uses a private pseudo-terminal pair. It exercises real
backend I/O, readiness, timeout, async startup/teardown, byte preservation and
peer-close errors without opening a physical serial device. Physical UART/USB
serial configuration, flow control and disconnect behavior require hardware
fixtures and remain pending. This owner does not close the repository's frozen
full-migration gate.
