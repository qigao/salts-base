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
| ASN.1 BER/DER | `Salts::ASN1` | [`asn1.h`](asn1/include/asn1.h), `asn1_value.h` |
| SIMD | `Salts::SIMD` | [`simd.h`](simd/include/simd.h) |
| Serial ports | `Salts::Serial` | [`serial.h`](serial/include/serial.h) |
| Media | `Salts::Media` | [`capture.h`](media/include/capture.h), `playback.h` |
| Scheduling | `Salts::Cron` | [`cron.h`](cron/include/cron.h) |
| Tests and benchmarks | `Salts::TinyTest` | [`tinytest.h`](tinytest/include/tinytest.h), `tinytest.hpp` |

Core builds as a shared library, including a DLL on Windows. NativeIO builds as
the static `native_io` archive. `<salts.h>` is the Core umbrella; other modules
expose their own headers.

## Quick start

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
- [Markdown](parser/markdown/README.md), [XML](parser/xml_parser/README.md),
  [YAML](parser/cyaml/README.md), [TOML](parser/toml/README.md) and [TOON](parser/toon/README.md)
- [Serial ports](serial/README.md), [Cron](cron/README.md) and [TinyTest](tinytest/README.md)
- [Dependency restoration and hosted CI](.github/README.md)

## License

SaltsBase is licensed under [Apache License 2.0](LICENSE).
Bundled third-party components retain their upstream licenses; dependency
licenses are also installed with the SDK.

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
