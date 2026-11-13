# ASN.1 C11 module

The ASN.1 module belongs to SaltsBase's root build and SDK export. The relocated
`parser/asn1` sources are merged here: binary BER/DER, X.509 parsing, the CSPRNG
wrapper and basic MIB/OID tooling share the existing static `Salts::ASN1` target.
There is no separate SaltsNet support library or export set. Core supplies the
OS CSPRNG and UTC conversion; SIMD supplies bounded binary scanning. No crypto
provider, CMeta, CFlow or new third-party dependency is introduced.

`Salts::ASN1` is one **static** C11 library, exporting
`<asn1.h>` and `<asn1_value.h>` with `asn1_*` APIs:
bounded BER/DER Reader, Validator, Encoder, borrowed TLV views, OID and
opaque owned Value Tree. On successful child admission, the parent owns
the child; on error it remains with the caller.

This module is used by both `Salts::Crypto` (signature/key formats) and
`SaltsNet` (SNMP, LDAP). Schema-language parsing and binary BER/DER parsing
remain separate implementations in this module. The schema compiler reuses
the binary value API to validate resolved OIDs. Certificate signature, chain
and trust verification stay with the crypto/TLS owner.

Build and test from the SaltsBase root with the configured user environment:

```sh
cmake --preset native-release-user -DBUILD_TESTS=ON
cmake --build --preset native-release-user
ctest --preset native-release-user -R '^asn1[.]'
cmake --build --preset install-native-release-user
```

Other platforms use their matching user presets. Installed users consume the
unified package and the existing module target:

```cmake
find_package(SaltsBase CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE Salts::ASN1)
```

The binary codec's archive name, `asn1_*` APIs and public header paths are
preserved. Migrated callers use `<asn1/crypto_random.h>`, `<asn1/x509_cert.h>`
and `<mib_compiler.h>` through the same target. The obsolete `asn1_api.h`,
`salts_asn1_*` names and `SaltsNet::SNMPSupport` target are not required.
There is no separate
ASN1 package, version, export set or configuration template. Licensing belongs
to the repository root. No separate installed-consumer project is maintained.
Public C11/C++17
headers and behavior are covered by the module's formal source-tree tests.
No old `crypto_asn1_reader_*`, `SaltsCore::ASN1` or dynamically exported
`Salts::ASN1` compatibility facade is retained. The original strict
parser implementation retains its existing semantics and ownership contracts.

OID validation privately links `Salts::SIMD` and uses the public `simd.h`
high-bit mask to skip complete 16-byte blocks of one-octet subidentifiers.
The first block containing a continuation octet hands the entire remainder
back to the base-128 parser. Short tails, nonminimal encodings and truncation
keep their existing BER/DER errors; only complete vectors are loaded from
the borrowed input. This remains O(n) time and O(1) extra storage, without
allocations or public API changes. Static consumers receive the SIMD archive
dependency through `Salts::ASN1`; SIMD's SIMDe headers remain private.

With `BUILD_BENCHMARKS=ON`, CTest's `asn1.scan_benchmark` covers short/long,
single-/multi-octet OIDs and integer magnitude access. The integer zero-prefix
loop remains scalar: the measured public SIMD reduction path cost more than
the existing byte loop on the tested LLVM-MinGW Release build.

## Basic MIB/OID compilation

`mib_compiler` is built and installed with the module. It accepts numeric OID
arcs, named numeric arcs, and a previously defined OID as the first prefix:

```asn1
Example-MIB DEFINITIONS ::= BEGIN
internet OBJECT IDENTIFIER ::= { iso(1) org(3) dod(6) internet(1) }
example-root OBJECT IDENTIFIER ::= { internet 4 1 32473 }
example-node OBJECT IDENTIFIER ::= { example-root 7 }
END
```

```sh
mib_compiler -o generated Example-MIB.mib
```

The CLI creates the output directory when its parent exists. It writes
`Example-MIB.h`, `Example-MIB.c` and `Example-MIB_runtime.c`. The header exposes
OID arrays and lengths; the runtime source defines `Example_MIB_module` using
the immutable `mib_module_t`/`mib_oid_view_t` descriptors in `<mib_compiler.h>`.
ASN.1 hyphens become underscores in C identifiers. Runtime output is
self-contained and does not depend on SNMP headers. `--no-c` and `--no-runtime`
select the output kinds independently; disabling both still validates input.
The formal test build generates, compiles, links and checks the output.

The schema grammar also builds ASTs for INTEGER, BOOLEAN, OCTET STRING,
BIT STRING, NULL, type references, SEQUENCE, SET, CHOICE and OF constructs.
Code generation currently emits OID data, not C representations of those
types. IMPORTS, forward OID references, SMI OBJECT-TYPE/macros, constraints,
tagging and other unsupported syntax fail; this is a basic OID compiler,
not a complete SMI implementation. Comments end at a newline, a second `--`,
or end of input. Duplicate definitions, invalid root arcs, unresolved prefixes
and values exceeding uint32 fail before any output file is opened.

Each synchronous compilation borrows its input only for the call and owns a
local parse state. AST nodes copy their strings; semantic reductions transfer
child ownership, and failure releases partial trees. A context owns its
`module_name`, `output_dir` and `root`; release it with
`mib_compile_destroy_context`. `mib_extract_oids` returns an independent owned
table, released with `mib_oid_table_destroy`, that can outlive the AST.
Generated runtime views borrow static immutable storage. Independent calls can
run concurrently when they write distinct files; sharing a mutable context or
AST requires caller synchronization. There are no background workers or drain
requirements.

Public limits are 1 MiB of input, 4096 created AST nodes per parse, 127 bytes
per identifier and 128 resolved uint32 arcs per OID. The Lemon stack holds
128 parser states. Limits, allocation failures and unsupported input return
`-1`; there is no fallback or partial-success status. Library entry points
require an existing output directory. Output I/O errors propagate, including
flush/close errors; earlier output files may remain on a later I/O failure.
The CLI reports failure with a nonzero exit code. For example:

```c
#include <mib_compiler.h>
int result = mib_compile_string(
    "Demo DEFINITIONS ::= BEGIN root OBJECT IDENTIFIER ::= { 1 3 6 1 } END",
    ".");
```

The unified target avoids duplicate codecs and downstream export ownership.
Keeping a second shared SaltsNet support target would preserve the wrong
package boundary and require a second Windows export ABI. The tradeoff is
that the static ASN.1 target now carries a Core runtime dependency. Migrating
callers change their link target to `Salts::ASN1`; rolling back requires the
matching earlier source/package and caller target selection, not parallel
versioned APIs.

## Migrated certificate and random APIs

`crypto_random_bytes` preserves the migrated 0/-1 contract and rejects null
buffers or zero length. It delegates to Core's OS CSPRNG without adding a
provider or recovery path. Its header is usable from C11 and C++17.

`x509_cert_parse` copies the supported DER certificate fields and owns its
result, released by `x509_cert_free`. It clears a valid output pointer on
failure. Validity timestamps use UTC regardless of local timezone. Parsing
does not establish trust; `x509_cert_is_self_signed` only compares the parsed
issuer/subject CN, organization and country. Optional extensions, raw TBS/name
DER, algorithm parameters and EC curve OIDs are not populated by the migrated
parser. The original complete DER is retained. Callers needing those fields
or cryptographic verification must use their crypto/TLS owner's certificate
validation facilities.
