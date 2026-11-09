# Native CI

`workflows/native-ci.yml` configures, builds, tests, installs and archives the
complete native graph through the versioned `ci-*-user` presets. It does not
publish SDKs or binary packages. Builds never select individual targets; tests
run through CTest. Benchmarks and examples are disabled in this graph.

## Runner provisioning

The repository prohibits toolchain-file execution, including vcpkg's internal
ABI calculation. GitHub-hosted runners cannot reconstruct the dependency tree
under that policy. Use self-hosted runners with the `salts-native-ci` label and
the OS/architecture labels selected by the matrix:

| Platform | Compilers on PATH / in the runner environment | Dependency profiles |
| --- | --- | --- |
| Windows x64 | MSVC via `VSDEVCMD_BAT`; LLVM-MinGW via `LLVM_MINGW_ROOT` | `msvc-release-user`, `llvm-mingw-x64-release-user` |
| Linux x64 | `clang`/`clang++` and `gcc`/`g++` | `llvm-native-release-user`, `gcc-release-user` |
| macOS ARM64 | `clang`/`clang++` | `llvm-native-release-user` |

All runners need CMake 3.27+, Ninja, PowerShell 7, Git and curl on PATH and a
current Actions runner supporting the pinned Actions versions. Python 3.12 is
selected by `actions/setup-python`. No compiler or SDK is installed by this
workflow. Keep the runner's compiler/runtime compatible with its preinstalled
libraries. Native tool selection remains in `presets/toolkits/*.json`.

Set an absolute `SALTS_CI_PREINSTALLED_ROOT` in the runner service's parent
environment, outside `GITHUB_WORKSPACE` so checkout cleanup cannot remove it.
Provision each installation from the qualified binary packages of
[`qigao/vcpkg-cache@v0.1.1`](https://github.com/qigao/vcpkg-cache/releases/tag/v0.1.1),
using this directory layout:

```text
<SALTS_CI_PREINSTALLED_ROOT>/
  msvc-release-user/vcpkg_installed/x64-windows/...
  llvm-mingw-x64-release-user/vcpkg_installed/qigao-x64-windows-llvm-mingw/...
  llvm-native-release-user/vcpkg_installed/x64-linux/...  # Linux
  gcc-release-user/vcpkg_installed/x64-linux/...
  llvm-native-release-user/vcpkg_installed/arm64-osx/...  # macOS
```

Only the profiles belonging to a runner's OS are required. CI maps the selected
profile into `SALTS_CI_VCPKG_INSTALLED_DIR`; the hidden `ci-preinstalled` user
preset owns the dependency-root override. Local development presets retain
their existing per-build dependency paths. SDK output goes to
`build/<ci-preset>/stage`, separate from the preinstalled directory.

Preinstalled prefixes must contain the complete product dependency set from
`vcpkg.json`, including package configs, libraries, runtime files and licenses.
The currently published v0.1.1 index does not contain `x64-windows` packages,
`libyuv` or `libjpeg-turbo`. Those prefixes therefore require an independently
qualified complete binary installation before the corresponding jobs can pass;
the workflow does not manufacture missing binaries or fall back to source
builds. Missing packages fail during normal CMake configuration.

## Released cache and credentials

The workflow checks out vcpkg-cache **v0.1.1** only for its existing CMake and
Python package readers. The registry is aligned to the same release commit,
`4ac631f89e07cebfd347848af642f31fe8c4edec`. It does not invoke the shared
`setup-vcpkg-curl-cache` action, `restore-manifest-cache.py`, vcpkg, or any
toolchain file. Native libraries are read from the provisioned prefix.

Lemon and re2c use the committed `cmake/*.lock.json` identities and hashes through
the shared readers during configure. The workflow caches only these verified
host-tool payloads and the verified Unicode source data. It does not cache a
CMake build tree, copy a different compiler's installed prefix, or write to the
shared binary feed.

Grant this repository package-read access, or set the optional Actions secret
`VCPKG_CACHE_READ_TOKEN` with the required read access. Otherwise the job's
read-only `GITHUB_TOKEN` is used. Tokens are not stored in presets or artifacts.

Fork pull requests do not automatically execute on self-hosted runners. Trusted
same-repository pull requests, pushes to `master`/`main`, and manual dispatches
run the matrix. Installed SDK tar artifacts preserve modes and symlinks; their
third-party dependencies remain external, as required by the package contract.

For a local reproduction with an already provisioned prefix, export
`SALTS_CI_VCPKG_INSTALLED_DIR` and `VCPKG_CACHE_REPOSITORY_ROOT`, then run the
matching user configure/build/test/install presets. On Windows, run them through
`cmake/WithVs.cmd`.
