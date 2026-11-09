# ASN.1 C11 module

The ASN.1 protocol module belongs to SaltsCore's root build and SDK export.
Its implementation is provider-neutral and does not link GmSSL, CMeta, CFlow
or Crypto. The SaltsCore package resolves the external dependencies needed
by the other static targets that it exports.

`Salts::ASN1` is one **static** C11 library, exporting
`<asn1.h>` and `<asn1_value.h>` with `asn1_*` APIs:
bounded BER/DER Reader, Validator, Encoder, borrowed TLV views, OID and
opaque owned Value Tree. On successful child admission, the parent owns
the child; on error it remains with the caller.

This module is used by both `Salts::Crypto` (signature/key formats) and
`SaltsNet` (SNMP, LDAP). ASN.1 MIB schema-language compilation is
currently a separate protocol tooling concern, not a duplicate binary BER
parser. Certificate trust verification stays with its crypto/TLS owner.

Build and test from the SaltsCore root with the configured user environment:

```sh
cmake --preset native-release-user
cmake --build --preset native-release-user
ctest --preset native-release-user -R '^asn1[.]'
cmake --build --preset install-native-release-user
```

Other platforms use their matching user presets. Installed users consume the
unified package and the existing module target:

```cmake
find_package(SaltsCore CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE Salts::ASN1)
```

The archive name and public header paths are preserved. There is no separate
ASN1 package, version, export set or configuration template. Licensing belongs
to the repository root. No separate installed-consumer project is maintained.
Public C11/C++17
headers and behavior are covered by the module's formal source-tree tests.
No old `crypto_asn1_reader_*`, `SaltsCore::ASN1` or dynamically exported
`Salts::ASN1` compatibility facade is retained. The original strict
parser implementation retains its existing semantics and ownership contracts.
