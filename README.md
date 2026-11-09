# SaltsBase

SaltsBase builds and installs its public libraries under the `Salts::` CMake
namespace. Consumers use `find_package(SaltsBase CONFIG REQUIRED)` and the
required targets, such as `Salts::Core`, `Salts::NativeIO` and `Salts::JsonParser`.

Core owns strings, formatting, logging, buffers, bounded queues, platform
primitives, synchronous filesystem operations and process spawning. The existing
NativeIO module owns completion backends, the standalone readiness reactor,
and the asynchronous file/process and shell adapters.
Their public headers, implementations and adapter tests live in
`native-io/include`, `native-io/src` and `native-io/tests`, respectively, and
compile only in `Salts::NativeIO`. Linking them into Core would create the cycle
Core -> NativeIO -> Coroutine -> Core. Link NativeIO when using `readiness.h`,
`fs_file.h`, `process_async.h` or `shell.h`. Core itself does not depend on NativeIO. `Salts::NativeIO` exports the static
`native_io` archive; CMake carries its Core, Coroutine and platform link
requirements to consumers. It does not produce a NativeIO runtime DLL.

Use Core's `<file/fs.h>` for filesystem metadata and directory operations.
NativeIO's `<fs_file.h>` submits file reads, writes and supported flush requests
to the selected native completion backend.

Readiness consumers include `<readiness.h>` and link `Salts::NativeIO`;
`<salts/readiness.h>` and readiness exports from the Core DLL are removed.
`<salts.h>` remains a Core-only umbrella and does not include readiness.
The reactor implementation and private headers live in `native-io/src/readiness/`,
and its tests live in `native-io/tests/`. NativeIO owns backend selection and
the epoll/kqueue/poll compile definitions. Core retains only general platform,
atomic, thread and synchronous file facilities.

The standalone `readiness_*` reactor retains its background-thread callback
model, one-shot/continuation arms, fixed capacities, generation checks and
shutdown/close quiescence rules. The existing `native_io_backend_*` API retains
its caller-driven submit/observe model and does not create worker threads.
Moving ownership does not merge these progress owners or change their lifecycle
contracts. A reactor and all its registrations must stay with the same linked
NativeIO instance until destruction; do not exchange them between DSOs that
contain separate static copies. A source rollback must move the implementation,
header, tests and backend configuration back together rather than compiling
the same reactor in both Core and NativeIO.

Public C identifiers and first-party filenames use functional names without a
project branding prefix. Core provides the C/C++ umbrella `<salts.h>`, with
atomic, queue and thread headers under `<atomic/...>`, `<queue/...>` and
`<threads/...>`. Other Core utilities use paths such as `<salts/fmt.h>`.
ASN1, Coroutine and Cron retain their `include/salts/` directories.
Parser, Media, SIMD, Serial, NativeIO and Unicode headers
live directly under their modules' `include/` directories. Use `<json_parser.h>`,
`<markdown.h>`, `<capture.h>`, `<simd.h>`, `<serial.h>`, `<native_io.h>` or
`<unicode.h>`. XML retains its module subdirectory, for example
`<xml_parser/xml_parser.h>`.
The `Salts::` CMake namespace, library branding and
uppercase `SALTS_*` configuration, status and export macros remain unchanged.
The header namespace prevents collisions with system headers while functional
prefixes disambiguate symbols where needed: `sync_*` for synchronization
extensions, `time_*` for calendar conversion/formatting, `os_*` for OS identity,
`native_timer_*`, `uuid_*`, and `atomic_order`. UUID values use `uuid_t` from
`<salts/uuid.h>`. Borrowed byte views and bounded byte buffers use `<salts/bytes.h>`;
the owning byte accumulator uses `bytes.h`.

This migration changes source includes and exported C symbol names. Rebuild
all consumers together with their dependencies; old binaries are not ABI
compatible. Object layouts, callback lifetimes, capacity limits and close/drain
protocols retain their existing contracts. Runtime ABI admission is declared by
`salts/api.h`; SDS remains private storage. Public functions and callback types
use the compiler's default C calling convention. Build the library and consumers
with compatible ABI settings; native Windows callback entry points retain their
required OS conventions.

Every file under `core/include/` is installed SDK payload. Keep private,
generated implementation and test-only files outside it; installation copies the
complete directory without exclusions.

- `core/include/salts.h`: Core's installed umbrella, usable from C11 and C++17.
- `core/include/atomic/`, `queue/`, `threads/`: atomic, queue and thread APIs.
  The unified atomic header exposes the compiled ABI to C and C++; C11
  compilers also receive native atomic helpers. C++ uses `<atomic>` for native objects.
- `core/include/file/`, `memory/`: filesystem APIs and buffer/pool/mapping APIs;
  their implementations reside in `core/src/file/` and `core/src/memory/`.
- `core/include/salts/`: installed utility headers and public
  C++ formatting helpers. Caller-facing formatter macros live inside `fmt.h`
  with `FMT_DETAIL_*` names; no separate `fmt_pp.h` is installed.
  String ownership and mutation helpers are unified in `<salts/tstr.h>`:
  `VSTR_LIT`, `tstr_view`, `tstr_capacity`, `tstr_append` and
  `tstr_reserve_capacity` no longer require a separate `str.h` facade.
- `core/src/`: private implementation contracts for threads, filesystem watches,
  regex scanning and thread pools. Tests opt in per target.
- `core/src/atomic/`, `queue/`, `threads/`: implementations mirroring the public
  capability groups. Private thread headers remain beside their implementations;
  CMake recursively collects production files and filters OS suffixes.
  Thread `platform_*` backend types, constants and operations are declared only
  in `core/src/threads/thread_backend.h`, which is not installed. Public thread
  headers expose C11 operations and the existing `sync_*` extensions. C11
  condition and once records keep their pointer/state layouts without exposing
  the backend aliases. Backend linkage remains available for Core's internal
  ABI tests; it is not an installed SDK interface. Coroutine and clock consumers
  use the public C11 operations.
- `core/src/file/`: filesystem operations/watch backends, with private headers
  beside their implementations.
- `core/include/salts/scan.h` and `core/src/scan.c`: bounded C11 byte scanning
  owned by `Salts::Core`, independent of the SIMD module. Space/tab and any4/any5
  scans use private SIMDe kernels with scalar tails in `core/src/scan_internal.h`.
  SIMDe comes from the existing vcpkg-cache and is a private header dependency;
  applications include only `<salts/scan.h>`. The former public `simd_scan.h`
  is removed; replace its three `simd_*` calls with `scan_skip_sp_tab`,
  `scan_to_any4` and `scan_to_any5`. The latter two now resolve through Core's
  shared library rather than embedding scalar loops in application objects.
  Byte ranges, first-match behavior and end-on-miss semantics are unchanged.
- `core/parser/`: private formatter/log-pattern lexer headers and sources.
- `native-io/include/`: NativeIO's public backend, reactor and file/process/shell
  adapter headers. Private reactor headers stay in `native-io/src/readiness/`.

Build-tree consumers receive the module's `include/` root; installed consumers
receive the SDK include root. Both use the same header paths. Windows
thread implementations can include the CRT's `<process.h>` directly because the
public child-process header is now `<salts/process.h>`.

The bounded scan implementation is compiled once in Core; the independent SIMD
execution API remains in `Salts::SIMD`. The private scalar kernels also serve as
the consistency-test and benchmark baseline. No runtime backend registry or
new ISA requirement is introduced; SIMDe supplies the target implementation.

With `-DBUILD_BENCHMARKS=ON`, Core builds `benchmark_scan` and registers
`saltscore.scan_benchmark` with CTest. It compares scalar kernels, SIMDe kernels
and public calls on unaligned short fields, 64-KiB searches and whitespace.
Timed samples contain the scan and an observable result store; input setup and
correctness checks are outside timing. Byte counts end at the first match or
the input limit and represent logical search work, not physical memory traffic.

On Windows x64 / Ryzen 9 7940HX / MSVC 14.44 `/O2`, 64 batches of 32 searches
measured the following average times per call with warm, unaligned input:

| Search | Scalar baseline | Public Core call |
| --- | ---: | ---: |
| any4, 15 bytes, last-byte match | 7.67 ns | 12.11 ns |
| any4, 64 KiB, last-byte match | 30.99 us | 2.81 us |
| any5, 64 KiB, last-byte match | 31.38 us | 3.27 us |
| space/tab, 64 KiB | 31.84 us | 2.08 us |

These microbenchmarks support the long-range kernel change, not an end-to-end
parser speedup claim. Short searches can cost more because the formerly inline
any4/any5 calls now cross the DLL boundary. The LLVM-MinGW cache used for the
correctness run had empty Release optimization flags; its timings do not
establish optimized-LLVM performance. Kernel rollback can keep the public ABI
and route the three functions to the private scalar kernels; it need not restore
the public SIMDe header or cross-module source ownership.

CSV and TOON do not depend on CSTL. CSV owns its row-pointer index and reusable
cursor view allocation; rows and strings remain arena-owned. Index capacity is
checked before publishing rows, and cursor views expire on advance/rewind/free.
DSV build storage is bounded by `max_entries`. Its version-1 file layout and
source digest are preserved. TOON conversion owns a temporary pointer-identity
table that rejects shared/cyclic nodes, checks allocation arithmetic, and is
released on success or error. Source nodes remain borrowed for the call.
These private structures avoid introducing a public container API or dependency.

## Build

GitHub Actions uses the [native CI workflow](.github/workflows/native-ci.yml).
It consumes preinstalled released binaries on self-hosted runners and never
invokes vcpkg, ABI resolution, dependency builds or toolchain files. See
[runner provisioning and cache requirements](.github/README.md).

Targets collect module sources through `cmake_add_source()` from
`cmake/CmakeUtils.cmake`, using module directories and glob patterns tracked by
`CONFIGURE_DEPENDS`. Exclusion globs keep platform backends and adapter owners
separate. Core platform files in `core/src` and NativeIO reactor files in
`native-io/src/readiness` use
`<feature>_<os>.<ext>`,
with `_win`, `_linux`, `_posix`, `_macos`, `_bsd` or `_unsupported` suffixes.
Examples are `thread_win.c`, `fs_watch_linux.c`, `readiness_epoll_linux.c`
and `portable_thread_posix.c`. CMake selects the matching suffix groups. Portable public headers retain their shared names. Android shares Linux
readiness backends while retaining the unsupported filesystem-watch implementation.
Generated lexer/parser outputs remain explicit build outputs. Each
standalone test/example keeps its own entry-point mapping rather than combining
multiple programs into one target. Vendored components retain their selected
component directories; upstream sources and licenses are unchanged.

Use the versioned user presets. Compiler selection belongs to
`presets/toolkits/*.json`. For LLVM-MinGW, provide only `LLVM_MINGW_ROOT`;
`presets/toolkits/LlvmMinGW.json` selects the x64/ARM64 C/C++ compilers,
archiver, ranlib, resource compiler, target system and processor directly.
MSVC uses the active Visual Studio environment, native LLVM/GCC use `PATH`,
and Android uses CMake's native NDK support with `ANDROID_NDK_HOME`.
Product configure/build does not load a toolchain file or chainload one.

On Windows, invoke presets through `cmake/WithVs.cmd` with `VSDEVCMD_BAT`
in the parent environment. `.env.example` lists host paths; CMake does not
implicitly load `.env`. Keep environment paths in forward-slash form.
Each preset has its own dependency installation, SDK prefix and runtime search
paths under `build/<preset>`. Installation defaults to `build/<preset>/stage`;
override it with `-DCMAKE_INSTALL_PREFIX=<absolute-path>`.

Use [vcpkg-cache v0.1.1](https://github.com/qigao/vcpkg-cache/releases/tag/v0.1.1)
for shared dependency packages and host-tool restoration. `vcpkg.json` owns the
product dependency set; `vcpkg-configuration.json` selects the shared registry
at `v0.1.1`, baseline `4ac631f89e07cebfd347848af642f31fe8c4edec`.
The manifests keep dependency selection with this product; they do not copy
ports, patches or build recipes from the cache repository. The unused
aklomp-base64 dependency is omitted because Core owns its Base64 implementation.

The build consumes an existing installation at
`build/<preset>/vcpkg_installed/<triplet>`. The `vcpkg-cache` preset owns this
installation root. Windows MSVC selects `x64-windows`; LLVM-MinGW selects
`qigao-x64-windows-llvm-mingw` or `qigao-arm64-windows-llvm-mingw`. Named Android
presets select their matching triplets. Native Linux/macOS and the generic
Android profile receive `VCPKG_TARGET_TRIPLET` from the parent environment.
For a different preinstalled location, set `VCPKG_INSTALLED_DIR` in the consuming
user preset's environment. Compiler, architecture, runtime and Debug/Release
must match that installation; the product does not recompute vcpkg ABI hashes.

This preserves the explicit ban on all toolchain-file execution: product
configure does not invoke vcpkg, its ABI resolver, port builds or an
ExternalProject source-build alternative. Missing installations/packages fail.
Use the shared infrastructure to supply the required prebuilt installation
before configuring. Its standard `restore-manifest-cache.py` entry point runs
`vcpkg install --dry-run` to resolve package ABIs; do not invoke that resolver
inside this product configure under the no-toolchain policy. Package restoration
and the existing installed libraries are reusable without loading a compiler
toolchain. No second dependency build graph is maintained here.

Zstd remains a private static Core dependency; JPEG/libyuv remain shared Media
dependencies. SIMDe/miniaudio headers and upstream patches come from the
installed packages, including miniaudio 0.11.22#1's PulseAudio cleanup fix.
The install exports the SaltsBase libraries and licenses; third-party packages
remain in their own installation. SDK consumers must supply the matching vcpkg
prefix for package discovery and its runtime library path. Build/test presets
already add the selected package runtime paths.

Set `VCPKG_CACHE_REPOSITORY_ROOT` to the v0.1.1 checkout and provide
`GITHUB_TOKEN` where the shared package feed needs authentication. When
migrating an existing build tree, configure with `--fresh` to discard obsolete
package locations. If a previous installation bundled dependencies, select a
fresh SDK install prefix with `-DCMAKE_INSTALL_PREFIX=<absolute-path>` so stale
files do not survive the changed installation model or the header namespace migration.

Lemon and re2c are restored by the infrastructure's existing CMake modules using
`cmake/lemon.lock.json` and `cmake/re2c.lock.json`. Their host RID is independent
of the target ABI. Lemon's executable and template come from the same verified
package; there is no system-tool or source-build fallback. Update tool locks
explicitly with the infrastructure's `restore-native-dependency.py`, and run the
existing parser tests after changing tool versions.

```powershell
cmake\WithVs.cmd cmake --preset msvc-release-user
cmake\WithVs.cmd cmake --build --preset msvc-release-user
cmake\WithVs.cmd ctest --preset msvc-release-user
cmake\WithVs.cmd cmake --build --preset install-msvc-release-user
```

Core's migrated behavior suites have the CTest label `core`. Test executables
are not installed. Installed SDKs contain `SaltsBaseConfig.cmake` and a unified
`SaltsBaseTargets.cmake`; third-party dependencies are resolved from the
consumer's configured package prefixes. Linux, macOS and cross builds use their
matching user presets and platform implementations.

The Markdown reference gate requires `cmarkgfm` and `mistune` in the selected
Python interpreter. Use an isolated virtual environment and select it with
`-DPython3_EXECUTABLE=<absolute-path>` when configuring the user preset.

For LLVM-MinGW x64, use `llvm-mingw-x64-release-user` for configure/build/test.
Enable the existing test graph with `-DBUILD_TESTS=ON` when configuring if tests
are needed. Install destinations are isolated by preset even when switching
compilers in the same environment.

## C11 atomics and compiler boundaries

For C11 compilers, `<atomic/atomic.h>` includes `<stdatomic.h>` and provides
checked finite typed declarations. `SALTS_ATOMIC_TYPE` supports initialization,
load/store/exchange, strong/weak CAS and lock-free queries.
`SALTS_ATOMIC_INTEGER_TYPE` adds add/sub/and/or/xor RMW operations;
`SALTS_ATOMIC_POINTER_TYPE` adds element-scaled pointer add/sub. Invalid arguments
and order combinations return `SALTS_EINVAL` before changing storage or outputs.
The standard atomic types, flags and fences remain available from `<stdatomic.h>`.
Core C sources and the C branches of its public headers include
`<atomic/atomic.h>` as the common entry point. Their existing C11 atomic types and
operations retain their semantics and layouts. C++ branches use `<atomic>`
for native objects and the same `<atomic/atomic.h>` for Core's compiled ABI.

The same header declares the compiled C ABI for shared 8/16/32/64-bit
and pointer slots, including for TinyCC and C implementations without native
atomics. These slots are ordinary fixed-layout storage accessed only
through Core's operations. Never cast them to a compiler-native `_Atomic` or
`std::atomic`, and never copy a live slot. Publication, modification and reads use
the same Core provider. Initialization finishes before any concurrent access.

MSVC's provider uses Interlocked; GCC and Clang providers use `__atomic` builtins
on Windows as well as POSIX. All requested orders are checked; the compiled ABI
currently executes them with sequential consistency, which is stronger than
weaker requested orders. Compiler-native C11 objects use their requested orders.
MSVC consumers inherit the required C11 atomics option from `Salts::Core`.
See [C11 section 7.17](https://open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) and
[GCC atomic builtins](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html).

The time/system API in `<salts/platform.h>` uses `timeval_now`, `time_utc`,
`time_local`, `time_from_utc`, `time_from_local`, `time_format_utc/local`,
`os_name`, `os_version`, `cpu_arch`, `user_name`, `host_name`, `cpu_info`,
`memory_info`, `load_average` and `network_interfaces`. Time values use
`time_value_t`; timezone information uses `timezone_info_t`. These names avoid
colliding with libc functions having different signatures. `secure_random` has
one OS-backed implementation. Its declaration and the `time_value_t` definition
are both provided by `<salts/platform.h>`.
`<salts/clock.h>` owns monotonic/realtime clock queries and the `native_timer_*`
API. Its Windows timer-queue and POSIX manager-thread implementations live in
`core/src/clock.c`, including callback completion and destroy coordination.
Rebuild consumers when migrating from the former prefixed symbols; rollback
requires restoring the matching SDK headers and binaries together.

With `BUILD_TESTS=ON`, set `SALTS_TEST_GCC` to an additional GCC executable to
compile the production atomic/thread provider and the existing C11 test cases
with that compiler. On Windows, `SALTS_TEST_GCC` and `SALTS_TEST_TCC` also build
the existing DLL ABI cases; GCC also compiles the standard C11 cases as clients
of the SDK DLL to verify thread/TSS/timeout behavior across CRT boundaries.
All tests are registered with CTest and are part of
the normal user-preset build graph. TinyCC is an ABI client; compiling the C11
provider requires a C11-capable MSVC, GCC or Clang compiler. No TLS declaration
is required merely to include the ABI headers; an actual TLS declaration still
requires compiler TLS and never falls back to global storage.

## C11 threads

`core/include/threads/threads.h` is the Salts C11 thread implementation. Include
`<threads/threads.h>` and link `Salts::Core`. Public types, object layouts and
initializer/status constants are declared in `<threads/threads_types.h>`;
`threads.h` includes that file and declares operations and inline adapters.
The OS backend's handles, relative waits and callbacks are private declarations
in `core/src/threads/thread_backend.h`. Public C11 carriers retain their storage
layouts without depending on those backend aliases or including private files.

Standard operations use their C11 names directly: `thrd_*`, `mtx_*`, `cnd_*`,
`tss_*` and `call_once`. The same header exposes `sync_mutex_*`, `sync_cond_*`,
`sync_rwlock_*`, `sync_thread_*`, `sync_once` and policy extensions from the
former thread header. C++ clients can use `std::once_flag` and `std::call_once`
in the same translation unit, with `<mutex>` included before or after
`<threads/threads.h>`.

Use Core's matching thread types and runtime throughout a program; do not mix
these types with the host libc's `<threads.h>` types or link a second C11 thread
provider. Sleep and timed-wait inline adapters marshal `timespec` values through
fixed-width `*_abi` helpers across compiler/DLL boundaries.

The API follows [C11 section 7.26](https://open-std.org/JTC1/SC22/WG14/www/docs/n1570.pdf):
all four plain/timed/recursive mutex combinations, condition variables,
thread creation/join/detach/exit, `call_once`, `thread_local` and `tss_*`.
Timed mutex/condition operations accept absolute `TIME_UTC` deadlines. The
implementation rechecks UTC in bounded 100 ms slices to follow calendar
corrections without imposing the platform API's maximum relative duration.
`thrd_sleep` returns 0 on completion, -1 on interruption (with the remaining
duration if requested), and -2 on another error; Windows and POSIX sleep
backends stay in their respective source files.

Each mutex owns a guard and an availability condition; that guard protects
its holder and recursive acquisition count. Condition waits release logical
ownership atomically with waiting, then restore ownership before returning,
including after timeout. Destruction requires no concurrent users/waiters.

TSS values are borrowed. Replacing, clearing or deleting a key does not call
its destructor. Thread exit clears a value before calling its destructor and
performs at most `TSS_DTOR_ITERATIONS` (4) passes for reinstalled values.
Deleting a key retires it; existing thread slots retain only metadata until
they are pruned or their thread exits. At most 1024 live/retired key records
are retained; allocation/limit failures return `thrd_error` from `tss_create`
or `tss_set`. A single process-lifetime native TSS key provides the exit hook
for threads created outside the C11 API as well. Synchronize key deletion
with uses of that key, and keep Core and callback modules loaded until the
last thread using them exits.

The existing CTest entry `saltscore.c11_threads_facade` covers the C11 API,
recursive lock ownership, TSS deletion/repeated destructors and foreign
threads. On POSIX it also covers signal-interrupted sleep and remaining time.
