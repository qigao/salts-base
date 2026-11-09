# Native and Android CI

`workflows/native-ci.yml` uses GitHub-hosted runners and the versioned
`ci-*-user` CMake presets to configure the complete graph, build, install and
archive the SDK. Native jobs build and run the formal tests through CTest.
Android jobs cross-build on Linux; device tests require a separate device
runner. Benchmarks and examples are disabled. This workflow does not publish
product SDKs or third-party packages.

## Hosted runners and presets

| Jobs | GitHub image | CI configure/build preset | Installed triplet |
| --- | --- | --- | --- |
| Windows MSVC x64 | `windows-2025` | `ci-msvc-release-user` | `x64-windows` |
| Windows LLVM-MinGW x64 | `windows-2025` | `ci-llvm-mingw-x64-release-user` | `qigao-x64-windows-llvm-mingw` |
| Linux Clang x64 | `ubuntu-24.04` | `ci-llvm-native-release-user` | `x64-linux` |
| Linux GCC x64 | `ubuntu-24.04` | `ci-gcc-release-user` | `x64-linux` |
| macOS Clang ARM64 | `macos-15` | `ci-llvm-native-release-user` | `arm64-osx` |
| Android arm64-v8a | `ubuntu-24.04` | `ci-android-arm64-release-user` | `arm64-android` |
| Android x86_64 | `ubuntu-24.04` | `ci-android-x64-release-user` | `x64-android` |

These are standard [GitHub-hosted image labels](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
No self-hosted registration, custom runner labels or machine-local dependency
paths are needed. Fork PRs use the normal read-only `pull_request` permissions;
repository secrets are not supplied to fork code.

Configure, build, test and install all use user presets. The matrix selects the
image, base profile, triplet and whether target tests execute on the host.
Commands are `cmake --preset`, `cmake --build --preset`, `ctest --preset`
(native only), and `cmake --build --preset install-...`. The build never selects
individual targets. `ci-preinstalled` owns the dependency-root override and
cross-build options; `ci-native-tests` enables the native formal test graph.

## Released preinstalled dependencies

CI restores the complete preinstalled reference dependency archives from
[vcpkg-cache v0.1.2](https://github.com/qigao/vcpkg-cache/releases/tag/v0.1.2).
Each triplet has a committed `cmake/dependencies/<triplet>.lock.json` containing
the exact public release URL and whole-archive SHA-256. The release's existing
`restore-native-dependency.py` verifies the hash and safely extracts the
`vcpkg_installed` payload. Its returned root supplies
`SALTS_CI_VCPKG_INSTALLED_DIR`; the product does not reconstruct an installation.
Each CI job has an independent restored prefix and build/install directories.

The independent cache producer builds dependencies and stages these generic
preinstalled archives. Product CI does not invoke vcpkg, its ABI resolver,
`restore-manifest-cache.py`, dependency source builds, or toolchain files.
Missing assets, hash mismatches, incomplete installations and package discovery
errors fail explicitly. `vcpkg.json` still owns this product's dependency set,
and `vcpkg-configuration.json` selects the matching infrastructure revision.
No repository variable containing SDK URLs or preinstalled roots is required.

One preparation job downloads each locked dependency archive, the Lemon/re2c
packages from the public cache release, the LLVM-MinGW compiler archive and pinned Unicode data before the
platform matrix starts. It also archives the released infrastructure sources
that supply the shared restore and CMake readers. Actions cache reuses verified
raw inputs; one shared artifact distributes them to the independent runners.
Lemon and re2c retain their existing `cmake/*.lock.json` identities and hashes.

Matrix jobs set `QIGAO_PREPARED_INPUTS`, verify the prepared blobs again and
extract their own host/target payloads. Missing inputs fail without downloading
from the external source again. Unicode configure checks the prepared data
hashes and likewise refuses missing inputs. Preparation failure prevents all
matrix builds from starting. Artifact distribution happens once per runner;
only external dependency acquisition is centralized. Compiler installations,
CMake build trees and restored SDK prefixes remain separate for each profile.

## Compilers and system packages

Windows locates Visual Studio with `vswhere` and exports `VSDEVCMD_BAT` for the
existing `cmake/WithVs.cmd` entry point. LLVM-MinGW's archive identity and hash
come from the released infrastructure's compiler contract; the shared reader
extracts it and exports only `LLVM_MINGW_ROOT`. Compiler executables and target
metadata remain in `presets/toolkits/LlvmMinGW.json`; no generated compiler
toolchain file is used.

Linux uses the image's Clang/GCC and installs the binary PipeWire, X11/Xext and
pkg-config development packages needed by Media. macOS uses the image's Clang.
Android uses the image's `ANDROID_NDK_HOME`; the existing Android presets own
API 26, ABI and `c++_shared`. The installed target prefix participates in the
Android find roots alongside the NDK sysroot. It uses
[CMake's native Android support](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-android-with-the-ndk)
without loading an NDK or vcpkg toolchain file.

CMake, Ninja, PowerShell and curl come from the hosted images. Python 3.12 is
selected by `actions/setup-python`. A changed compiler/image must remain
compatible with the qualified dependency archive; formal product CTest is the
native runtime evidence, and Android cross-builds do not claim device execution.

## Credentials, artifacts and local reproduction

CI reads public release assets and needs no additional package-read secret or
cross-repository package grant. The preparation job uses the normal read-only
GitHub token for `gh release download`; the shared reader verifies each host
package against its existing NuGet identity and SHA-256 before extracting it.
Matrix jobs read prepared artifacts without a package token. Local CMake use
without prepared inputs retains the existing authenticated NuGet reader.
Credentials are not stored in presets or artifacts.

Installed SDK tar artifacts preserve file modes and symlinks. Third-party
dependencies remain external under the existing package contract. Test and
configure diagnostics upload even if a build fails.

For local reproduction, restore the matching committed dependency lock using
the shared CLI, export its returned root as `SALTS_CI_VCPKG_INSTALLED_DIR`, set
`VCPKG_CACHE_REPOSITORY_ROOT` to the matching source checkout, then invoke the
same CI user presets. Native Linux/macOS profiles also need the matching
`VCPKG_TARGET_TRIPLET`; named Windows/Android profiles set it themselves. Use
`cmake/WithVs.cmd` on Windows. Android has build and install presets, with no
host CTest preset for target executables.
