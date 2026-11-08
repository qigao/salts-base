# Salts SIMD

`Salts::SIMD` is the independent C11 128-bit SIMD execution module in
SaltsCore. It owns the native carrier, lane descriptors and execution code.
Its public header is `<salts/simd.h>`; it neither includes nor links CMeta,
CFlow, Core or a reflection runtime. SIMDe remains a private implementation
dependency supplied through the repository's pinned vcpkg contract.

## Original source and migration boundary

The original seven `simd/` files come from
[`qigao/salts@386f81328c656f416d61a6d2ef662f05443e1b4a`](https://github.com/qigao/salts/tree/386f81328c656f416d61a6d2ef662f05443e1b4a/simd).
Original paths are retained. The 32 `cmeta_simd_*` entrypoint names, operation
enum values and all numeric implementation branches remain present. The
execution body changes only the descriptor/type/constant identifiers needed
to remove its CMeta dependency. This includes the eight original typed
helpers; their existing behavior has not been deleted.

The old `cmeta_vector_desc` embedded a `cmeta_type_desc *type`. The new
`salts_simd_desc` contains only `lane_kind`, `lane_bits`, `lane_count` and
`is_mask`. **These descriptor ABIs are different.** Do not cast or reinterpret
an old reflected descriptor as a native one. Source consumers of generic
operations must pass the native descriptor explicitly. `salts_simd_v128`
retains the original 16-byte size, 16-byte alignment and neutral byte storage;
`cmeta_v128` names that same native carrier for the existing execution APIs.

CMeta continues to own type identity, traits, builtin-name lookup and
reflection validity. Its future adapter must validate those semantics and
reference the native SIMD descriptor; Core must not call back into CMeta.
CMeta/TurboWasm joint integration, descriptor mapping and runtime acceptance
are separate, still-unqualified work. This standalone migration does not
claim compatibility with the old reflected generic-call ABI. Downstream
components must use this implementation and explicit descriptor mapping.

The frozen **2,975-file / 610-function** inventory remains unchanged and
incomplete. Its original SIMD header entry did not enumerate the 32 plain C
function declarations, so the supplementary SIMD inventory tracks them
separately; a source-presence check cannot qualify their behavior. AC and
Levenshtein are outside this iteration and remain pending.

## Native execution boundary

The public descriptor constants cover ten numeric shapes and four masks:

- `salts_simd_i8x16`, `u8x16`, `i16x8`, `u16x8`, `i32x4`, `u32x4`,
  `i64x2`, `u64x2`, `f32x4`, `f64x2` (each with the `salts_simd_` prefix).
- `salts_simd_b8x16`, `b16x8`, `b32x4`, `b64x2` (same prefix).

Descriptors are immutable values. Callers may copy a descriptor; execution
does not require a particular descriptor address. Static linking can place
descriptor constants in multiple binaries or DSOs, so their addresses have
no cross-DSO identity guarantee. Compare descriptor fields instead. Lane kind
values keep the original signed/unsigned/floating/mask ordering. SIMDe/compiler
vector types are private and must not appear in application storage or installed
headers.

```c
#include <salts/simd.h>

cmeta_v128 left = {{0}}, right = {{0}}, sum = {{0}};
cmeta_simd_i32x4_splat(&left, 7);
cmeta_simd_i32x4_splat(&right, 3);
bool ok = cmeta_simd_binary(&salts_simd_i32x4, SALTS_SIMD_BINARY_ADD,
                            &sum, &left, &right);
```

The execution families cover splat; unary/binary arithmetic; comparisons and
masks; shifts and selects; reductions; saturation; widening/narrowing;
conversions; load/zero/extend transforms; lane extraction/replacement; and
byte shuffle/swizzle. Integer wrap and saturating operations remain distinct.
Floating-point min/max and pseudo-min/max remain distinct. Unsupported
operation/shape pairs return `false` according to the original implementation.

`salts_simd_desc_valid` retains the original numeric structural rules:
nonzero dimensions totaling 128 bits and agreement between BOOL and the mask
flag. It does not validate a CMeta type, infer dimensions from lane kind, or
make an unsupported operation valid. This migration does not silently tighten
those rules or change the original shift dispatch, where operation values
other than `SALTS_SIMD_SHIFT_LEFT` take the right-shift path. Callers should
pass the declared operation enums and canonical shapes.

## Memory, errors and ownership

All carrier/scalar inputs and outputs remain caller-owned. Execution makes
no allocations, retains no pointers and creates no mutable global state.
Descriptor arguments are borrowed for the duration of a call. The module
has no thread, callback, asynchronous or destruction lifecycle.

Memory transforms operate on already-validated byte ranges. The caller owns
effective-address calculation, range/lifetime validation and any language
runtime traps. A full load/store requires 16 accessible bytes; load-splat
requires one lane, load-extend reads 8 bytes, and load-zero reads 4 or 8 bytes
according to `loaded_bits`. Misaligned byte addresses are supported by the
original SIMDe load/store paths; the carrier itself remains 16-byte aligned.

Generic operations reject unsupported pairs or invalid arguments with
`false`. Successful operations publish the computed result to the caller's
output; retained original failure paths must not be replaced with successful
empty results. The void `cmeta_simd_v128_load/store` functions retain their
original no-op behavior for null arguments. Shuffle rejects selectors at
least 32, while swizzle writes zero for indices at least 16.

## Build and validation

Enable `SALTSCORE_BUILD_SIMD=ON` with the repository's `simd` vcpkg feature.
SIMDe is required at configure time and used privately; there is no alternate
provider when it is missing. `Salts::SIMD` is always a static library,
independent of `BUILD_SHARED_LIBS`, and installs an archive plus its public
headers. Function and `extern const` declarations use ordinary C linkage;
the headers keep `extern "C"` for C++ without DLL import/export macros.

The archive is position-independent so an upper-layer DSO can link it.
Non-Windows consumers receive the required `libm` dependency through CMake's
static `LINK_ONLY` interface; private SIMDe include paths do not propagate.
No runtime allocation, mutable global state or shared-library lifecycle is
introduced by static linking. Each descriptor remains a value rather than
a process-wide address identity.

GNU builds compile the private SIMD module with `-fwrapv` in every build
configuration. SIMDe 0.8.2 implements some integer abs/neg/multiply fallbacks
using signed compiler vectors; the minimum signed lane must keep its low bits
for `abs` and `neg`, and modular multiplication must wrap. Without an explicit
arithmetic contract, GCC UBSan reports signed overflow for these paths on a
baseline x86 build. GCC documents `-fwrapv` as defining two's-complement
addition/subtraction/multiplication semantics in its
[code-generation options](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/gcc/Code-Gen-Options.html).
The option is private to this module and does not alter the original numerical
source, the pinned provider, the minimum CPU ISA or consumer compilation.
ASan, UBSan, leak detection and fail-on-report settings remain enabled;
this is the same arithmetic contract in production and instrumented builds.

With `BUILD_TESTING=ON`, `saltscore.simd_native_boundaries` checks real
standalone calls, descriptor borrowing/copying, in-place arithmetic,
unaligned buffers, failure output preservation and null boundaries. It also
checks signed-minimum abs/neg and modular add/subtract/multiply at 8/16/32/64
bits against an independent unsigned oracle, including rejection and output
preservation for unsupported 8-bit multiplication. Enabling
`SALTSCORE_BUILD_TINYTEST=ON` also registers:

- `saltscore.simd_original_c11`: the original 25 case names; 24 numeric test
  bodies retain original behavior after descriptor identifier substitution.
  The first descriptor case now tests native shapes. Its original CMeta
  builtin/type lookup assertions remain an upper-layer acceptance obligation.
- `saltscore.simd_original_cpp17`: the original C++17 case and
  size/alignment/standard-layout assertions, byte-identical to the donor.

Original numeric tests, installed C11/C++17 consumers, sanitizers and relevant
native platforms must be checked on the same exact HEAD. Source parity and
passing local tests do not prove full ISA or floating-point environment
coverage, upper-layer integration or performance parity. No performance
improvement is claimed by this ownership migration.
