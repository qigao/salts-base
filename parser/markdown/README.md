# Canonical C11 Markdown grammar migration

**Status:** Draft implementation candidate. Upstream migration tracker:
[SaltsUtils #722](https://github.com/qigao/salts-utils/issues/722);
Stun downstream cutover: [Stun #368](https://github.com/qigao/stun/issues/368).

This module originates from qigao/stun `shared/markdown`, source master
`78995ecf8c5365c7f001b4501cb29a109971ba9c`.

Only one production Markdown parser is allowed. `Salts::MarkdownParser` owns
source bytes and RichDoc grammar; `Salts::MarkdownHtml` consumes the validated
RichDoc and performs SAFE (production) / SPEC (trusted test-only) HTML rendering.
The current Stun `shared/markdown` target must be removed on consumer cutover;
until then, this **unmerged** migration branch is not a released provider.
No runtime cmark/MD4C/Mistune fallback.

## Test ownership

Move all parser-only C11 tests, complete immutable CommonMark 0.31.2 652-example
corpus, SPEC 652-ID and SAFE 576-ID SHA-256 baselines, independent cmark-gfm
468-case strict output gates and 360 GFM compositions, OOM/invalid-input,
Unicode17, reference/URL tests, and oracle scripts into this repository.

Stun keeps *only* `ui_core_test.c` (UI state, Rect, CMeta; move any parser
assertions first), `markdown_tango_candidate_test.c`, real
`flexUI/tests/test_markdown_widget.cpp`, and UI/Tango/renderer integration.
Stun's `verify_single_markdown_parser.py` becomes a downstream single
imported-parser guard. It must not retain duplicate compiled grammar files.

## Source-span warning

`salts_md_get_document_view().document.blocks[].byte_begin/end` index into the
**decoded semantic text** arena; `raw_markdown` preserves the exact original
input independently. They are NOT original Markdown byte offsets. Until a
separately qualified original-byte span mapping exists, TurboAgent MUST NOT
publish these semantic offsets as source-file evidence. It can still use the
indexed semantic text for FTS5 with explicit provenance.

## Hard gates

- Run *real* native parser C11 CTests and independent full corpus gates on
  Linux/Windows/macOS; preserve strict EXPECTED HTML and corpus hash. The
  original Stun checkpoint is CI #39017661257, commit `65e9b55b`.
- `Salts::Unicode` remains imported from the canonical Salts SDK. No local
  Unicode provider, dummy header, test-only parser replacement or missing
  dependency fallback.
- Keep SDK packaging and headers in existing SaltsUtils build graph; do not
  create standalone installer/consumer projects or new probe CI workflows.
- Only **after upstream exact-head qualification** migrate Stun actual runtime
  and remove the old parser source and parser-generic test sources. Keep the
  STUN UI wrappers only if their old signatures/ownership are verified.
- Only after the accepted installed upstream SDK and Stun cutover can
  TurboAgent #306/#307 use Markdown parsed structural sections.
