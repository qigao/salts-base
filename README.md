# SaltsBase

## Upstream dependency versions

Select the upstream SDK release through `VCPKG_CACHE_VERSION` in the parent
environment (for example, by loading the local `.env`). Use the same version
for the `VCPKG_CACHE_REPOSITORY_ROOT` checkout. Values contain the version
without the `v` prefix; missing variables fail rather than choosing `latest`.
The shared `resolve-release` entry point is required (not present in v0.1.2).

The per-triplet files in `cmake/dependencies/` are release descriptors. Their
historical `.lock.json` suffix is retained; `source.kind=github-release`
distinguishes them from generated literal URL/hash locks. Resolve the selected
release into the build directory before restoring it:

```sh
python "$VCPKG_CACHE_REPOSITORY_ROOT/scripts/restore-native-dependency.py" resolve-release \
  "cmake/dependencies/$VCPKG_TARGET_TRIPLET.lock.json" \
  --output-lock "build/dependencies/$VCPKG_TARGET_TRIPLET.lock.json"
python "$VCPKG_CACHE_REPOSITORY_ROOT/scripts/restore-native-dependency.py" locked \
  "build/dependencies/$VCPKG_TARGET_TRIPLET.lock.json" \
  --destination "build/dependencies/$VCPKG_TARGET_TRIPLET-$VCPKG_CACHE_VERSION" \
  --payload vcpkg_installed
```

Set `VCPKG_INSTALLED_DIR` to the reported root for the selected preset. The
resolver requires the published asset's SHA-256 digest; the existing restore
checks downloaded bytes and preserves immutable installations. Changing a
version never overwrites an old SDK. CMake does not load `.env` or download
the dependency SDK implicitly. Existing host-tool package locks remain exact.

CI uses the same named repository Actions variables and selects local shared
actions from the chosen infrastructure checkout. SaltsBase and SaltsCore
prepare jobs resolve once, then distribute generated locks and verified blobs
to all matrix workers. Set the CI variables before enabling these changes;
the selected infrastructure release must contain `resolve-release`.

This keeps consumer version policy in the environment, asset digests in the
upstream release, and generated restore locks in build inputs. Updating a
release no longer requires editing a digest table for every platform. SDK
manifests, compiler/ABI selection and product tests remain mandatory. To roll
back, choose the previous exact release and restore into its own directory.


[![Native and Android CI](https://github.com/qigao/salts-base/actions/workflows/native-ci.yml/badge.svg)](https://github.com/qigao/salts-base/actions/workflows/native-ci.yml)
[![C11](https://img.shields.io/badge/C-11-A8B9CC?logo=c&logoColor=white)](CMakeLists.txt)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![CMake 3.27+](https://img.shields.io/badge/CMake-3.27%2B-064F8C?logo=cmake&logoColor=white)](CMakeUserPresets.json)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

A modular C11 foundation for cross-platform applications, with C++17 integration.
Strings, concurrency, native I/O, parsers and Unicode ship in one CMake package
under the `Salts::` target namespace.

[Features](#features) · [Modules](#modules) · [Quick start](#quick-start) ·
[Usage](#usage) · [Testing](#testing) · [Documentation](#documentation)

## Features

- **Core utilities** — owned `tstr` strings, borrowed `vstr` views, typed formatting,
  logging, buffers, memory pools, filesystem operations and process spawning.
- **Concurrency** — C11 atomics and threads, synchronization extensions, bounded
  queues, thread pools, coroutines and a bounded sharded executor.
- **Native I/O** — completion backends, a standalone readiness reactor, asynchronous
  file and process adapters, IPC and shell integration.
- **Parsers** — JSON, XML, YAML, TOML, INI, dotenv, CSV, TOON, Markdown, URI,
  selectors, date/time and binary protocols, plus format-neutral serialization APIs.
- **Unicode 17.0.0** — strict UTF-8 decoding, character properties, normalization,
  case folding, text segmentation and bidirectional processing.
- **Portable SIMD** — 128-bit vector operations through `simd.h`, with bounded
  scanning shared by Core strings and selected parser paths.
- **Device and media support** — serial ports, audio/video/screen capture and playback.

## Modules

Link the targets your application uses; CMake carries their link dependencies.

| Module | CMake target | Public entry point |
| --- | --- | --- |
| Core | `Salts::Core` | [`salts.h`](core/include/salts.h), `<core/...>`, `<atomic/...>`, `<threads/...>` |
| Native I/O | `Salts::NativeIO` | [`native_io.h`](native-io/include/native_io.h), `readiness.h`, `fs_file.h` |
| Coroutines | `Salts::Coroutine` | [`salts/coro.h`](coroutine/include/salts/coro.h), `salts/coro_executor.h` |
| Parsers | `Salts::JsonParser`, `Salts::IniParser`, `Salts::XmlParser`, [and more](parser/) | `json_parser.h`, `ini_parser.h`, `xml_parser/xml_parser.h` |
| Serialization | `Salts::CSerde` | [`cserde/cserde.h`](cserde/include/cserde/cserde.h) |
| Unicode | `Salts::Unicode` | [`unicode.h`](unicode/include/unicode.h) |
| ASN.1 BER/DER, X.509 and MIB/OID tooling | `Salts::ASN1` | [`asn1.h`](asn1/include/asn1.h), `asn1_value.h`, `asn1/x509_cert.h`, [`mib_compiler.h`](asn1/include/mib_compiler.h) |
| SIMD | `Salts::SIMD` | [`simd.h`](simd/include/simd.h) |
| Serial ports | `Salts::Serial` | [`serial.h`](serial/include/serial.h) |
| Media | `Salts::Media` | [`capture.h`](media/include/capture.h), `playback.h` |
| Scheduling | `Salts::Cron` | [`cron.h`](cron/include/cron.h) |
| Tests and benchmarks | `Salts::TinyTest` | [`tinytest.h`](tinytest/include/tinytest.h), `tinytest.hpp` |

Core builds as a shared library, including a DLL on Windows. NativeIO builds as
the static `native_io` archive. `<salts.h>` is the Core umbrella; other modules
expose their own headers.

## Quick start

All modules, tests and examples belong to the root `SaltsBase` CMake project.
The root project version identifies the SDK package; module libraries use
unversioned filenames and carry no separate ABI version. The former Serial
filename `serial-0.1.dll` on MSVC becomes `serial.dll`; applications that load
it by filename must use the new name. On Unix, rebuild and relink applications
against the unversioned shared libraries when upgrading from a versioned SDK.

Language standards, required-standard policy, position-independent code and
output directories are set once by the root project. Public targets declare
their consumer compile features; extension modes, symbol visibility and
platform constraints remain with the targets that need them.

### Prerequisites

- CMake **3.27+**, Ninja, Git and Python **3.12+**.
- A supported compiler: MSVC, Clang, GCC or LLVM-MinGW; Android builds use the NDK.
- The matching preinstalled dependencies and infrastructure from
  [vcpkg-cache](https://github.com/qigao/vcpkg-cache/releases) release selected by `VCPKG_CACHE_VERSION`.
- On Linux, the Media development packages listed in the
  [CI setup guide](.github/README.md#compilers-and-system-packages).

Restore the matching [locked preinstalled archive](.github/README.md#released-preinstalled-dependencies)
before configuring. Its compiler, architecture, runtime and build configuration
must match your preset. Product configuration consumes that installation;
it does not execute toolchain files, resolve vcpkg ABIs or build dependency ports.
Lemon and re2c are restored from the locked host-tool packages.

### Build on Linux or macOS

Use the same preinstalled-dependency profile as CI, with your restored paths:

```sh
git clone https://github.com/qigao/salts-base.git
cd salts-base

export VCPKG_CACHE_REPOSITORY_ROOT=/absolute/path/to/vcpkg-cache
export SALTS_CI_VCPKG_INSTALLED_DIR=/absolute/path/to/vcpkg_installed
export VCPKG_TARGET_TRIPLET=x64-linux  # arm64-linux on Linux ARM64; arm64-osx on macOS ARM64

cmake --preset ci-llvm-native-release-user
cmake --build --preset ci-llvm-native-release-user
ctest --preset ci-llvm-native-release-user --output-on-failure
cmake --build --preset install-ci-llvm-native-release-user
```

The native test graph uses `cmarkgfm` and `mistune` as Markdown oracles. Install
the pinned [oracle requirements](parser/markdown/tests/oracle-requirements.txt)
in a virtual environment and select its interpreter with
`-DPython3_EXECUTABLE=<absolute-path>` when configuring. Local host-tool
restoration may also require `GITHUB_TOKEN`; see the
[restoration guide](.github/README.md#credentials-artifacts-and-local-reproduction).

<details>
<summary><strong>Build on Windows</strong></summary>

Run from the repository root with the Visual Studio entry point and restored
dependency paths set in the parent PowerShell session:

```powershell
$env:VSDEVCMD_BAT = "C:/path/to/VisualStudio/Common7/Tools/VsDevCmd.bat"
$env:VCPKG_CACHE_REPOSITORY_ROOT = "C:/path/to/vcpkg-cache"
$env:SALTS_CI_VCPKG_INSTALLED_DIR = "C:/path/to/vcpkg_installed"

cmake\WithVs.cmd cmake --preset ci-msvc-release-user
cmake\WithVs.cmd cmake --build --preset ci-msvc-release-user
cmake\WithVs.cmd ctest --preset ci-msvc-release-user --output-on-failure
cmake\WithVs.cmd cmake --build --preset install-ci-msvc-release-user
```

For LLVM-MinGW, set `LLVM_MINGW_ROOT` and use
`ci-llvm-mingw-x64-release-user` and its matching install preset.
Compiler selection lives in [`presets/toolkits/`](presets/toolkits/).
Apply the same Python test-oracle setup described above.

</details>

### Platform profiles

| Platform / compiler | CI user preset | Validation |
| --- | --- | --- |
| Windows x64 / MSVC | `ci-msvc-release-user` | Build, CTest, install |
| Windows x64 / LLVM-MinGW | `ci-llvm-mingw-x64-release-user` | Build, CTest, install |
| Linux x64 / Clang | `ci-llvm-native-release-user` | Build, CTest, install |
| Linux x64 / Clang TSan | `ci-llvm-native-tsan-user` | Instrumented build, CTest, install |
| Linux x64 / GCC | `ci-gcc-release-user` | Build, CTest, install |
| Linux ARM64 / Clang | `ci-llvm-native-release-user` | Native ARM64 build, CTest, install |
| Linux ARM64 / GCC | `ci-gcc-arm64-release-user` | Native ARM64 build, CTest, install |
| macOS ARM64 / Clang | `ci-llvm-native-release-user` | Build, CTest, install |
| Android arm64-v8a | `ci-android-arm64-release-user` | Cross-build, install, 16 KiB ELF load alignment |
| Android x86_64 | `ci-android-x64-release-user` | Cross-build, install, 16 KiB ELF load alignment |

Android profiles use `ANDROID_NDK_HOME`; target tests require a device runner.
Windows and named Android profiles select their triplets automatically.
Each preset installs to `build/<preset>/stage` by default; override it with
`-DCMAKE_INSTALL_PREFIX=<absolute-path>`. [`.env.example`](.env.example)
documents the parent environment; CMake does not load `.env` automatically.

The `linux-clang-x64-tsan` CI artifact uses the existing Debug/ThreadSanitizer
profile for the complete SaltsBase graph, including Core's buffer recycling
atomics. Consume it only from a matching TSan application; the ordinary Release
SDK does not expose those atomic synchronization events to the detector.
Preinstalled third-party dependencies retain their original instrumentation
status. Its existing software audio-monitor CTest requires PulseAudio and its
utilities; CI installs them before configuring. No physical device is required.
CI uploads this SDK only after its formal tests and installation pass;
adding the artifact does not publish a release or change existing release assets.

## Usage

### Link the installed SDK

Add the installed SDK and matching third-party triplet prefix to your
consumer's `CMAKE_PREFIX_PATH`, then link the required imported targets:

```cmake
cmake_minimum_required(VERSION 3.27)
project(example LANGUAGES C)

find_package(SaltsBase CONFIG REQUIRED)

add_executable(example main.c)
target_compile_features(example PRIVATE c_std_11)
target_link_libraries(example PRIVATE Salts::Core Salts::IniParser)
```

JPEG and libyuv belong to Media. Applications using other SaltsBase targets
do not need to find either package. Applications linking `Salts::Media` must
explicitly resolve `JPEG` and `libyuv` before linking that static target.

Third-party packages remain in their preinstalled prefix. Make their shared
libraries and the Core shared library available on the application's runtime
search path. Build-tree and installed consumers use the same header paths.

### Strings and borrowed views

```c
#include <core/tstr.h>

int main(void) {
    tstr message = tstr_dup("Hello");
    if (!message) return 1;

    if (!tstr_append(&message, VSTR_LIT(", Salts!"))) {
        tstr_free(message);
        return 1;
    }

    const size_t position = vstr_find(tstr_view(message), VSTR_LIT("Salts"));
    tstr_free(message);
    return position == 7u ? 0 : 1;
}
```

`tstr` owns its storage; `vstr` borrows it. Mutation or reallocation can invalidate
existing views. The owner-handle helpers update the string pointer when storage
moves. Include `<core/fmt.h>` for typed formatting and `<core/tlog.h>` for logging.

### Parse an INI document

```c
#include <ini_parser.h>

int main(void) {
    const char config[] = "[server]\nport=8080\n";
    ini_t *ini = ini_parse(config, sizeof(config) - 1u);
    if (!ini) return 1;

    const int port = ini_get_int(ini, "server", "port", 80);
    ini_free(ini);
    return port == 8080 ? 0 : 1;
}
```

The parser reads exactly the supplied byte count. Returned strings belong to the
parsed document and expire when `ini_free()` releases it.

### API boundaries

- Include `<threads/threads.h>` for the C11 `thrd_*`, `mtx_*`, `cnd_*`, `tss_*`
  and `call_once` operations, plus the `sync_*` extensions. Use the matching
  Core thread types and runtime throughout the program.
- Include `<atomic/atomic.h>` for the compiled atomic ABI and C11 helpers.
  C++ clients use `<atomic>` for native C++ atomic objects.
- Use `<file/fs.h>` for Core filesystem operations and `<fs_file.h>` with
  `Salts::NativeIO` for asynchronous file operations. The readiness reactor owns
  its background thread; completion backends use caller-driven submit/observe.
- UUIDs are 16-byte `uuid_t` arrays from `<core/uuid.h>`, with `uuid_*`
  operations and `uuid_parse_text()`. Wall-clock values use `wall_time_t`.

When migrating from the earlier header layout or symbol names, update includes
and rebuild consumers together with their dependencies.

## Testing

Native CI builds the complete configured graph and runs tests through CTest.
One preparation job downloads and verifies shared inputs before the independent
GitHub-hosted platform jobs start. See the [workflow](.github/workflows/native-ci.yml)
and [CI guide](.github/README.md).

To select existing tests from a configured native test build:

```sh
ctest --preset ci-llvm-native-release-user -L core --output-on-failure
ctest --preset ci-llvm-native-release-user -R '^test_ini_' --output-on-failure
```

Benchmarks use a separate build profile. For example, with the POSIX environment
from Quick start:

```sh
cmake --preset llvm-native-release-user \
  -DVCPKG_INSTALLED_DIR="$SALTS_CI_VCPKG_INSTALLED_DIR" \
  -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=ON
cmake --build --preset llvm-native-release-user
ctest --preset llvm-native-release-user -L benchmark --output-on-failure
```

Benchmark results depend on the compiler, CPU and workload. Registered cases
cover Core scanning, parser workloads, Unicode and ASN.1; they are not a claim
of uniform end-to-end speedups.

## Documentation

- [Public Core umbrella](core/include/salts.h) and [CMake helpers](cmake/CmakeUtils.cmake)
- [NativeIO backends, lifetime and progress contracts](native-io/README.md)
- [Unicode API and pinned data](unicode/README.md)
- [ASN.1 BER/DER reader and owned values](asn1/README.md)
- [Portable SIMD API](simd/README.md)
- [Markdown](parser/markdown/README.md), [XML](parser/data/xml_parser/README.md),
  [YAML](parser/data/cyaml/README.md), [TOML](parser/utils/toml/README.md) and [TOON](parser/utils/toon/README.md)
- [Serial ports](serial/README.md), [Cron](cron/README.md) and [TinyTest](tinytest/README.md)
- [Dependency restoration and hosted CI](.github/README.md)

## License

SaltsBase is licensed under [Apache License 2.0](LICENSE).
Bundled third-party components retain their upstream licenses; dependency
licenses are also installed with the SDK.

## Choosing a C11 mutex

Use `mtx_plain` for ordinary locking and for a predicate shared with a
condition variable. Both `cnd_wait` and `cnd_timedwait` accept a plain mutex.
Check the predicate under that mutex after every wake. For a timed wait,
reuse the same absolute UTC deadline throughout the loop and check the
predicate again under the reacquired mutex before acting on a timeout.

Choose `mtx_timed` when a caller needs `mtx_timedlock` to put a deadline on
acquiring the mutex itself. Add `mtx_recursive` when the same thread needs
to acquire the mutex recursively.

## Windows thread runtime

Core owns the Windows implementation of the C11 mutex, condition and once
operations and their Salts synchronization counterparts. The installed C and
C++ object layouts and the UTC seconds/nanoseconds ABI stay compatible. The
Windows backend resolves `NtWaitForKeyedEvent`, `NtReleaseKeyedEvent` and
`NtWaitForSingleObject` from ntdll once. Initialization reports an
unsupported backend if a required export is absent. A non-alertable wait on the default keyed
event matches one release in the same process. An unexpected status after a
waiter has registered is a broken rendezvous and terminates the process.

The two private keyed-event `NTAPI` declarations take a null event handle, an aligned
key, a false alertable flag and an optional signed 100 ns timeout. The
upstream inventory also includes section, handle, memory and error handling
imports for mcfgthread's runtime. Salts uses the object wait for durable
event notifications with absolute UTC or monotonic remaining time.
Its private x64 and ARM64 accessors read the SDK's TEB fields for OS-owned
TLS slots, including the expansion array beyond the first 64. The slot
owner must keep the index live until every reader has finished.
[Upstream import inventory](https://github.com/lhmouse/mcfgthread/blob/006e1860d90f0a3a4bfa2bbdbe3f275c627c6a8c/mcfgthread/src/ntdll.def.in)
and [upstream architecture accessors](https://github.com/lhmouse/mcfgthread/blob/006e1860d90f0a3a4bfa2bbdbe3f275c627c6a8c/mcfgthread/teb.h).

- A mutex stores ownership and the number of registered sleepers in one
  interlocked word. A contender spins briefly while there are no registered
  sleepers, then parks. Unlock publishes availability and pairs with at most
  one sleeper. An awakened thread competes for the lock. This permits barging
  and gives no FIFO or starvation bound. Shared/exclusive `sync_rwlock_*`
  continues to use the Windows SRW policy, which also has no writer fairness
  guarantee.
- A timed mutex uses the original deadline across retries, registration and
  kernel waits. It needs no blocking admission guard. UTC deadlines and the
  private monotonic duration API retain separate clock semantics. On timeout
  the waiter removes its own registration without waiting for a releaser.
  If release precedes kernel registration, the releaser makes finite wake
  offers while the mutex is free and registered sleepers remain.
  A waiter that has paired with a release claims an available mutex before
  retiring its registration. This lets a second sleeper make progress if
  the first waiter's deadline passes as the kernel wakes it.
- An untimed condition waiter lends one aligned stack record to its condition's FIFO
  before releasing the associated mutex. Signal selects the first registered
  waiter. Broadcast selects the current queue under the same lock. Each
  selection publishes one notification to that waiter. On an untimed wait,
  the thread can yield once to a runnable peer and briefly poll its stack
  record. If it commits to parking first, the signaler pairs with it through
  the keyed event. After a kernel wait crosses a Windows clock tick, the
  same thread tries parking directly on its next untimed wait. A short
  handoff restores polling. A timed waiter shares an event record with the
  queue. Timeout and notification compete for its eligibility. The waiter
  can retire its reference at the original deadline even if the notifier is
  paused. A queue operation or quiescent destroy reclaims expired records.
  A contended queue guard posts a durable wake on release. Returning from
  a wait includes reacquiring the user mutex and restoring its depth.
- Once keeps the four-byte flag. Normal completion publishes the callback's
  writes to all waiters. On `thrd_exit` each active initializer hands execution
  to one registered caller or resets its flag for a later caller. This also
  applies to nested initializers and precedes C11 TSS destructor delivery.
- The x64 and ARM64 private TEB accessors support thread identity and reads
  from an OS-allocated TLS slot. The benchmark compares both operations
  with compiler TLS and compares pointer reads with TLS and FLS. Thread
  identity and writable state use compiler TLS by default. Public TSS
  retains its FLS exit hook and bounded destructor
  passes. After all participating threads and callbacks have finished, an
  explicit DLL unload releases the native hook. Join or otherwise establish
  quiescence before destroying objects or unloading Core. Asynchronous
  termination of a thread inside a wait cannot complete its rendezvous.

The existing Core CTests exercise recursive reacquisition, timed contention,
once publication, signal and broadcast, foreign threads, and repeated DLL
loads. With `BUILD_BENCHMARKS=ON`, run `ctest --preset <windows-preset>
-R saltscore.win_threads_benchmark -V` for acquisition latency percentiles,
throughput, process CPU time, context switches, failed first attempts, and
the least/most successful worker ratio. The workload uses one, two and eight
workers on at most two logical processors. It pools bounded per-worker latency
samples. The condition benchmark restricts both threads to one and then two
available logical processors. It reports single-exchange latency, the interval
between successive requests under the mutex (one full round trip), and CPU
cycles per trip. A delayed responder exercises the cost of parking. SRW and,
on pthread-enabled toolchains, direct pthread calls use the same critical
section for the immediate-response comparison. To roll back the Windows
implementation, rebuild Core from the preceding revision and replace it after
normal process shutdown.

A separate 350 ms workload records reader and writer acquisition counts and
the longest observed writer wait with seven concurrent readers. The readers
stop independently so the measurement can finish even if the writer makes
no progress during the window.

The design references are [Windows runtime #17](https://github.com/qigao/salts-base/issues/17),
[mcfgthread synchronization](https://github.com/lhmouse/mcfgthread), and
[Microsoft's SRW contract](https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks).

## POSIX C11 thread runtime

C11 timed mutex and condition operations take an absolute UTC deadline. The
Salts platform duration API measures elapsed monotonic time. Linux uses
pthread timed mutex and condition operations with the corresponding clock.
C11 mutexes on Android use pthreads directly. The older Bionic platform
duration API uses a nonblocking semaphore-mode eventfd and waits for readiness
with the remaining monotonic budget. Darwin uses pthreads for ordinary C11
mutexes and a binary dispatch semaphore for timed mutexes and platform
durations. Semaphore admission, including admission to a condition wait,
uses the caller's original deadline.

Conditions paired with native pthread mutexes release and reacquire them
through pthreads. On the two semaphore paths, a condition enrolls each waiter
in an open group. The first signal seals that group. New waiters then join
a new group, while remaining members of the sealed group retain priority.
A signal grants one permit and broadcast grants the current outstanding permits
in both groups. Timeout withdraws eligibility atomically. Each waiting call
keeps its group alive until it returns. At most two groups remain attached
to a condition, and no per-thread queue node is needed.

A recursive mutex records its owner and depth. A condition wait releases
all levels and restores them before returning, including on timeout. A caller
must recheck its predicate after each wake. An expired condition deadline can
return later while another thread owns the mutex.

Linux uses `pthread_once` with the existing four-byte flag. Darwin, Android
and ThreadSanitizer builds use a pthread condition while another thread runs
the initializer. The TSan interceptor publishes its once flag only when
the initializer returns.
If that initializer exits, its cleanup handler makes the flag available again.
POSIX threads use pthread keys for TSS. A detached thread retains its live
worker reference through return or `thrd_exit`. Synchronize outstanding key
users and callbacks before releasing their data, and quiesce users before
destroying synchronization objects.

With `BUILD_BENCHMARKS=ON`, run the Core thread benchmark through CTest
with `-R threads_benchmark -V`. The POSIX comparison reports elapsed and
CPU time for mutex acquisition with one and two workers, condition round trips
and timed waits. It measures plain and timed-capable C11 mutexes separately.
The long-initializer benchmark measures the waiting thread's
CPU time separately. Darwin provides a direct pthread baseline for conditions
and untimed mutex acquisition.

## Thread-pool callback identity

`<threads/thread_pool.h>` exposes `threadpool_is_current(pool)` for consumers
that must reject a synchronous wait on their current pool. It returns 1 during
that pool's run/cancel/finalize callback and 0 for other live pools, outside
callbacks or for NULL. It borrows the pool for the call and performs no work
submission or ownership transfer. The existing Core thread-local owner remains
the sole authority across linked DSOs. Both ordinary and Leader/Followers pool
profiles have formal run/cancel/finalize and foreign-pool controls.

## Thread-pool concurrent admission

The ordinary bounded pool serializes its short Disruptor claim/copy/publication
transfer with the existing dispatch monitor. Disruptor's one-shot try-claim may
lose a publisher CAS while capacity is available; the pool must not translate
that producer contention into its documented queue-full `SALTS_ENOBUFS` result.
The monitor is released before waiting for capacity, waking workers, or executing
run/cancel/finalize callbacks. Queued-work capacity, callback identity, terminal
ownership and the explicit Leader/Followers implementation are unchanged.

This keeps the change inside the pool instead of changing shared Disruptor
try-claim semantics or adding consumer retries. It serializes fixed-size metadata
transfer, not business work; contention cost needs measurement rather than a
lock-free or throughput claim. The formal existing thread-pool CTest includes
four producers, each with one unsettled submission, against capacity 16. Every
admission must succeed once and every accepted callback must settle; rejected
admissions are recorded without retries. Existing actual-full and callback
self-blocking controls remain in the same suite. Reverting this fix restores the
old false-full risk; consumers must not hide it with retry or executor fallback.
