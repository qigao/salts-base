# XML parser maintenance notes

## Lexer error-token ownership on parser recovery

The vendored lexer builds malformed lexical tokens with a temporary allocated
diagnostic buffer. The DOM parser handles those tokens through `setjmp` /
`longjmp`; before this patch, the local lexer token was never adopted by the
parser and its buffer became unreachable when the error handler jumped back to
the parse boundary.

The local token record now carries a private `owns_start` bit. Lexer-created
error buffers and stream-owned volatile token buffers mark that ownership
explicitly. The parser error handler copies an owned error token into the
parser diagnostic, releases the token buffer, then performs the existing
longjmp. Normal borrowed source tokens are unchanged.

This was found by TurboSCXML's deterministic VoiceXML fuzz smoke in sanitizer
run `37181219319`: normal CTests passed, then malformed base VoiceXML
mutations produced an LSan report of 342 bytes in 8 allocations rooted at
`_cxml_callocate_r`. Parser-level regressions now assert that malformed
lexical inputs leave `cxml_test_outstanding_allocations()` at the exact
pre-parse baseline. No sanitizer suppression is used.

## XPath parser-error recovery and cleanup

The upstream XPath parser treated malformed expressions as a process-fatal condition: its recursive-descent error path printed the expression to stderr, released partially built state, and called `exit(EXIT_FAILURE)`. Embeddable library code cannot terminate its host for invalid input.

The local patch now gives the XPath parser its own `setjmp` recovery point. Syntax rejection releases partial parser state and returns `QVM_STATUS_INVALID_PROGRAM` through `cxml_xpath_ex()`, with a stable diagnostic message. It does not print the supplied XPath and does not terminate the process.

`vendor/cxml/src/xpath/cxxpmemdeb.c`, `cxml_xp_fvisit`, is also null-safe because parser recovery can leave a null placeholder in the AST-node stack during partial construction.

Reproduced by the DataBind XML format-provider regression at exact head `16d4655aa28806d8166b360ea5f397f0c35f1ead` with the invalid expression `//*[`: the first failure exposed a null dereference under UBSan, and after the cleanup guard the provider test still exited nonzero because the vendor syntax-error path intentionally terminated the process. Both behaviors are now covered by the parser-level malformed-XPath regression. No sanitizer suppression or syntax relaxation is used.

## cxml local-name initialization

`vendor/cxml/src/query/cxqapi.c`, `cxml_set_name`: skip clearing the old local-name buffer when its length is zero. A newly allocated node has a null `qname` buffer; even a zero-length `memset` must not evaluate null-pointer arithmetic or pass null to an annotated nonnull parameter.

Reproduced in Native XML SAX run 34597251013 at head f30fb55a9588b0d7212e80449cac841953314ae3. The existing `xml_parser_test` case "builds and serializes a document through opaque handles" reaches `salts_xml_document_create` and reports UBSan at cxqapi.c:1547. This change keeps the existing rename path and removes only the undefined operation on a new name. No sanitizer suppression is used.

## Incremental SAX ownership

`Salts::XmlParser` owns both the document parser and the incremental lexical SAX parser exposed by `<xml_parser/xml_sax.h>`. SAX callback spans are borrowed for the callback duration. Attribute and text spans retain entity spelling; document binding uses the native DOM for decoded values. Each instance owns its diagnostic, open-element stack, and bounded pending-token buffer. Callback failure is terminal; a failed or finished instance is not reusable.

The SAX regression checks each input chunk size, callbacks before EOF, sticky callback failure, independent diagnostics, embedded NUL rejection, malformed documents, and explicit pending-buffer limits. The default DataBind consumer must not duplicate these parser implementations.

Run the exact committed source with `xml_parser_test` and `xml_sax_parser_test` under both ASan and UBSan. A successful SAX test alone does not establish a successful DOM test; both are required by `.github/workflows/xml-sax.yml`.
