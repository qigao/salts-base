# XmlParser

`Salts::XmlParser` is the bounded, namespace-aware XML DOM facade exported
as `<xml_parser/xml_parser.h>`. It accepts explicit-length input, copies it into
document-owned storage, and returns opaque document/node/attribute handles with
borrowed string views. Views and handles become invalid when
`salts_xml_document_destroy()` is called.

The facade reports zero-based byte offsets and one-based line/column locations.
Input bytes, tree depth, nodes, attributes, and retained string bytes all have
configurable hard limits. Embedded NUL bytes and DTD declarations fail fast;
malformed input, allocation failure, and limit failure are distinct statuses,
and failed parsing leaves the output handle empty.

cxml is vendored solely as a private implementation. Its headers, targets, and
types are not installed or exposed through `Salts::XmlParser`.
