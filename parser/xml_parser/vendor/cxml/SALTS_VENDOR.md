# cxml vendor record for Salts

- Upstream: <https://github.com/ziord/cxml>
- Declared upstream version: `0.1.0` (`CMakeLists.txt` in the imported tree)
- License: MIT (`LICENSE.txt`)
- Import source: `turbo-parser/vendor/cxml` at TurboParser commit
  `f2cdcb1d7b48584e16b5e67dee2d9a67c5efbede` (2026-08-29)
- Upstream commit: not recorded by the source tree from which this copy was
  imported; the declared version and complete imported sources are retained so
  the provenance gap is explicit rather than guessed.

Salts builds cxml core, DOM XML, query, XPath, and utility sources. SAX,
upstream tests, examples, and tools are retained as source reference where
present but are not compiled into `salts_cxml`. Query and XPath remain
private implementations behind `Salts::XmlParser`.

Local patches:

1. Parser errors use `setjmp` recovery instead of terminating after a malformed
   document. This patch existed in the TurboParser import source.
2. Non-MSVC builds do not promote third-party warnings to errors. This patch
   existed in the TurboParser import source.
3. DOM elements, attributes, text, comments, processing instructions, and
   namespaces retain zero-based byte offsets and one-based line/byte-column
   locations captured directly in lexer tokens, so location lookup is O(1).
4. `cxml_parse_xml_ex()` returns the first structured parse error without
   printing parser failures or recoverable warnings to stderr. The original
   `cxml_parse_xml()` remains a private compatibility wrapper.
5. Thread-local fatal/allocation handlers convert cxml failures during
   `cxml_parse_xml_ex()` into structured errors; a private allocation-failure
   injection hook and outstanding-allocation counter verify that the installed
   facade neither terminates nor leaks on tested failure paths.
6. The compiled XML lexer returns an error token instead of aborting on an
   invalid scanner state.
7. Pointer hashing uses `uintptr_t` so 64-bit pointer bits are not truncated.
8. `cxml_parse_xml_limited()` checks node, attribute, and retained-string
   budgets, including XML declarations and DTDs, before the corresponding DOM
   retention and reports a distinct structured limit error. Nodes under
   construction remain explicitly tracked until a DOM owner adopts them, so a
   limit-triggered `longjmp` releases every unowned node.
9. Hashtable rehash and insertion publish their new storage and value only
   after all fallible allocations succeed, so allocation-handler `longjmp`
   preserves cleanup ownership. XML declarations require the exact lowercase
   `xml` target at the document start; ordinary processing instructions reject
   every case variant of the reserved target. The start position is relative to
   an admitted UTF-8 BOM. Parser-owned pending state also covers comment values
   and expanded namespace attribute names during allocation failure.
10. The enclosing `XmlParser` archive, rather than the private object target,
   carries the non-MSVC `libm` link requirement used by cxml literals.
11. XML declarations enforce the XML 1.x `version`, optional `encoding`, and
    optional `standalone` field order and value grammar.

No cxml header, target, or allocation/lifetime contract is installed or
exported. `Salts::XmlParser` is the only supported consumer boundary.
