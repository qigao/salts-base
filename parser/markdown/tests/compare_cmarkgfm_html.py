#!/usr/bin/env python3
"""Compare actual C11 Markdown-rendered HTML to independent cmark-gfm.

This is not an AST comparison: stdin -> Stun C11 parser -> C11 RichDoc HTML
and stdin -> cmarkgfm HTML are two independent processing paths. Every gated
case requires exact byte-for-byte equality, including tags and whitespace.
Survey cases are counted separately and are never silently ignored or marked
as conformance passes. The production C11 SDK has no Python dependency.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from importlib import metadata
import json
from pathlib import Path
import subprocess
import sys

import cmarkgfm
from gfm_combinations import (
    BASELINE_COUNT, BASELINE_SHA256, EXPECTED_COUNT,
    generated_cases, manifest_sha256,
)


@dataclass(frozen=True)
class Case:
    case_id: str
    markdown: str
    profile: str = "commonmark"
    gate: bool = True
    note: str = ""
    normative_expected_html: str | None = None


# #186: deterministic 16-column × 16-body-row reference payload, exactly
# mirroring the root/double-quote source constructed in the native C11
# bounded-ownership stress test. Neither width nor height is generated from
# untrusted input, and no additional Markdown frontend is introduced.
GFM_WIDE_TABLE_16 = (
    "".join(f" | h{col}" for col in range(16)) + " |\n"
    + "".join(" | :---" for _ in range(16)) + " |\n"
    + "".join(
        "".join(f" | v{row}_{col}" for col in range(16)) + " |\n"
        for row in range(16)
    )
)
GFM_WIDE_DOUBLE_QUOTE_16 = "".join(
    "> > " + line + "\n" for line in GFM_WIDE_TABLE_16.splitlines()
)

CASES: tuple[Case, ...] = (
    Case("cm.atx", "## Hello\n"),
    Case("cm.strong", "Normal **Bold** text\n"),
    Case("cm.emphasis", "A *bright* day.\n"),
    Case("cm.quote", "> quote\n"),
    Case("cm.ordered", "1. alpha\n2. beta\n"),
    Case("cm.bullet", "- one\n- two\n"),
    Case("cm.fence", "\x60\x60\x60" "c\nint v;\n" "\x60\x60\x60\n"),
    Case("cm.link", "A [B](dest)\n"),
    Case("cm.inline-code", "a " "\x60" "code" "\x60" " b\n"),
    Case("cm.rule", "---\n"),
    Case("cm.setext", "first\n===\n"),
    Case("cm.softbreak", "first\nsecond\n"),
    Case("cm.escaped", "a \\*b\n"),
    Case("cm.emoji", "中😀 👩‍💻\n"),
    Case("cm.empty", ""),
    Case("gfm.strikethrough", "a ~~red~~ day\n", "gfm"),
    Case("gfm.table", "| A | B |\n| --- | --- |\n| one | two |\n", "gfm"),
    Case("survey.cm.hardbreak", "first  \nsecond\n", gate=False,
         note="RichDoc has no hard-vs-soft-break bit yet"),
    Case("survey.cm.reference", "[reference][id]\n\n[id]: https://example.org\n",
         gate=False, note="Reference-link definition resolution is pending"),
    Case("survey.cm.entity", "Tom &amp; Jerry\n", gate=False,
         note="Entity decoding is not yet implemented in C11 parser"),
    Case("survey.cm.ordered-start", "4. alpha\n5. beta\n", gate=False,
         note="Ordered-list start index not represented in RichDoc"),
    # #186: true independent cmark-gfm GFM extension byte-exact gates.
    Case("gfm.table-align",
         "| A | B |\n| :--- | ---: |\n| x | y |\n", "gfm"),
    Case("gfm.table-escaped-uneven",
         "| A | B | C |\n| :--- | :---: | ---: |\n"
         "| a\\|b | middle | z |\n| short |\n", "gfm"),
    # Normative GFM cell boundaries are recognized BEFORE inline spans:
    # an UNescaped pipe splits cells even inside backticks. Example 200
    # independently covers the escaped form which must remain one cell.
    Case("gfm.table-code-pipe-and-uneven",
         "| A | B | C |\n| :--- | :---: | ---: |\n"
         "| a\\|b | " "\x60" "x|y" "\x60" " | z |\n| short |\n", "gfm"),
    Case("gfm.table-escaped-code-pipe-example-200",
         "| f\\|oo |\n| ------ |\n"
         "| b " "\x60" "\\|" "\x60" " az |\n"
         "| b **\\|** im |\n", "gfm"),
    Case("gfm.task", "- [x] completed\n- [ ] remaining\n", "gfm"),
    Case("gfm.task-ordered", "3. [X] done\n4. [ ] later\n", "gfm"),
    # GFM spec §4.10 Examples 198-205 and §5.3 Examples 279-280.
    # Exact-byte HTML parity is with the installed independent cmark-gfm
    # reference; official tasklist examples use a different HTML attribute
    # ordering, so normative semantic equivalence is not mislabeled as
    # official-example literal serialization. These are GFM-only gates.
    Case("gfm.official-198",
         "| foo | bar |\n| --- | --- |\n| baz | bim |\n", "gfm"),
    Case("gfm.official-199",
         "| abc | defghi |\n:-: | -----------:\nbar | baz\n", "gfm"),
    Case("gfm.official-200",
         "| f\\|oo  |\n| ------ |\n| b " "\x60" "\\|" "\x60" " az |\n"
         "| b **\\|** im |\n", "gfm"),
    Case("gfm.official-201",
         "| abc | def |\n| --- | --- |\n| bar | baz |\n> bar\n", "gfm"),
    Case("gfm.official-202",
         "| abc | def |\n| --- | --- |\n| bar | baz |\nbar\n\nbar\n", "gfm"),
    Case("gfm.official-203",
         "| abc | def |\n| --- |\n| bar |\n", "gfm"),
    Case("gfm.official-204",
         "| abc | def |\n| --- | --- |\n| bar |\n"
         "| bar | baz | boo |\n", "gfm"),
    Case("gfm.official-205",
         "| abc | def |\n| --- | --- |\n", "gfm"),
    Case("gfm.official-279",
         "- [ ] foo\n- [x] bar\n", "gfm"),
    Case("gfm.official-280",
         "- [x] foo\n  - [ ] bar\n  - [x] baz\n- [ ] bim\n", "gfm"),
    # #186 second GFM admission/security slice: parity of backslash runs,
    # nested vs loose task ownership, invalid markers, and inline content.
    # These are extension-specific cmark-gfm byte gates, not CommonMark
    # official 652 identities or published official GFM numbered examples.
    Case("gfm.table-odd-backslash-run",
         "| Name | Value |\n| --- | --- |\n"
         + r"| a\\\|b | tail |" + "\n", "gfm"),
    Case("gfm.table-even-backslash-run",
         "| Name | Value |\n| --- | --- |\n"
         + r"| a\\|b | tail |" + "\n", "gfm"),
    Case("gfm.task-empty-marker", "- [ ]\n- [x]\n", "gfm"),
    Case("gfm.task-loose-list",
         "- [x] shipped\n\n- [ ] pending\n", "gfm"),
    Case("gfm.task-nested-ordered",
         "- [x] parent\n  1. [ ] nested\n  2. [X] done\n- [ ] tail\n", "gfm"),
    Case("gfm.task-inline-rich-text",
         "- [x] **bold** " "\x60" "code" "\x60" " and [link](/go)\n", "gfm"),
    Case("gfm.task-invalid-markers",
         "- [x]done\n- [y] not task\n- \\[x] escaped\n", "gfm"),
    # Quoted GFM TABLEs: exact explicit quote-depth matching only. The same
    # cmark-gfm HTML byte oracle guards root and nested GFM table rendering.
    Case("gfm.quote-table-align",
         "> | H | N |\n> | :--- | ---: |\n"
         "> | a\\|b | **bold** |\n", "gfm"),
    Case("gfm.nested-quote-table",
         "> > | A | B |\n> > | --- | --- |\n> > | x |\n", "gfm"),
    Case("gfm.quote-table-then-paragraph",
         "> | Key | Value |\n> | --- | --- |\n"
         "> | a | b |\n>\n> After\n", "gfm"),
    # #186: explicit QUOTE-owned table admission; verify complete
    # container ownership / source-boundary transitions independently
    # against cmark-gfm byte-for-byte, without a second parser.
    Case("gfm.quote-table-crlf",
         "> | L | R |\r\n> | :--- | ---: |\r\n"
         "> | a\\|b | z |\r\n", "gfm"),
    Case("gfm.quote-table-no-final-lf",
         "> | L | R |\n> | --- | --- |\n> | x | y |", "gfm"),
    Case("gfm.quote-table-root-dedent",
         "> | L | R |\n> | --- | --- |\n> | x | y |\n"
         "Outside\n", "gfm"),
    Case("gfm.quote-table-then-nested-quote",
         "> | L | R |\n> | --- | --- |\n> | x | y |\n"
         "> > nested\n", "gfm"),
    Case("gfm.quote-two-tables-separated",
         "> | A | B |\n> | --- | --- |\n> | a | b |\n>\n"
         "> | C | D |\n> | --- | --- |\n> | c | d |\n", "gfm"),
    Case("gfm.quote-table-after-fence",
         "> ```\n> | A | B |\n> | --- | --- |\n"
         "> ```\n> | C | D |\n> | --- | --- |\n> | c | d |\n", "gfm"),
    Case("gfm.quote-table-unmarked-delimiter",
         "> | A | B |\n| --- | --- |\n", "gfm"),
    Case("gfm.quote-table-unmarked-row",
         "> | A | B |\n> | --- | --- |\n> | a | b |\n"
         "| c | d |\n", "gfm"),
    # #186: list-owned TABLE admission. A real list/quote owner must
    # retain typed TABLE metadata; no root table or detached parser pass.
    Case("gfm.list-table-marker-header",
         "- | Name | Qty |\n  | :--- | ---: |\n"
         "  | apple | 2 |\n", "gfm"),
    Case("gfm.list-table-then-sibling",
         "- | A | B |\n  | --- | --- |\n  | x | y |\n"
         "- second\n", "gfm"),
    Case("gfm.list-table-after-blank",
         "- intro\n\n  | A | B |\n  | --- | --- |\n"
         "  | x | y |\n", "gfm"),
    Case("gfm.quoted-list-table",
         "> - | A | B |\n>   | :--- | ---: |\n"
         ">   | x | y |\n", "gfm"),
    Case("gfm.quoted-list-table-dedent",
         "> - | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n> after\n", "gfm"),
    # #186: GFM TASK_ITEM and nested list TABLE compositions. The list,
    # quote and table must share the existing bounded C11 block owners.
    Case("gfm.task-table-after-blank",
         "- [x] parent\n\n  | A | B |\n  | --- | --- |\n"
         "  | a | b |\n", "gfm"),
    # GFM §5.3: a legal [x] in the first paragraph of a list item remains
    # a TASK even when the list lives inside a QUOTE and becomes loose.
    # Current cmark-gfm 2025.10.22 instead emits literal [x] here. Enforce
    # positive normative HTML bytes; keep the disagreeing cmark-gfm bytes as
    # separate report evidence, not an XFAIL or silent equivalence claim.
    Case("gfm.quoted-task-table",
         "> - [x] parent\n>\n>   | A | B |\n"
         ">   | --- | --- |\n>   | a | b |\n", "gfm",
         normative_expected_html=(
             "<blockquote>\n<ul>\n"
             "<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> \n"
             "<p>parent</p>\n<table>\n<thead>\n<tr>\n"
             "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
             "<tbody>\n<tr>\n<td>a</td>\n<td>b</td>\n"
             "</tr>\n</tbody>\n</table>\n</li>\n</ul>\n"
             "</blockquote>\n")),
    Case("gfm.nested-list-table",
         "- parent\n  - | A | B |\n    | --- | --- |\n"
         "    | a | b |\n", "gfm"),
    Case("gfm.quoted-nested-list-table",
         "> - outer\n>   - | A | B |\n"
         ">     | --- | --- |\n>     | a | b |\n", "gfm"),
    # #186: distinguish an already-open LIST paragraph from a *new* GFM
    # TABLE header; cmark-gfm is the strict byte oracle, not a guess that
    # any pipe line must interrupt ordinary paragraph continuations.
    Case("gfm.list-paragraph-then-table-no-blank",
         "- intro\n  | A | B |\n  | --- | --- |\n"
         "  | x | y |\n", "gfm"),
    Case("gfm.quote-list-paragraph-then-table-no-blank",
         "> - intro\n>   | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n", "gfm"),
    Case("gfm.list-multiline-paragraph-then-table",
         "- intro\n  continued\n  | A | B |\n  | --- | --- |\n"
         "  | x | y |\n", "gfm"),
    Case("gfm.list-inline-header-no-leading-pipe",
         "- left | right\n  --- | ---\n  x | y\n", "gfm"),
    Case("gfm.list-table-after-fenced-block",
         "- " "\x60\x60\x60" "\n  code\n  " "\x60\x60\x60" "\n"
         "  | A | B |\n  | --- | --- |\n  | x | y |\n", "gfm"),
    Case("gfm.list-mismatched-table-delimiter",
         "- | A | B |\n  | --- |\n  | x | y |\n", "gfm"),
    # #186: an independently resolved root reference MUST remain available
    # inside list/quote-owned typed TABLE_CELL text. A table data row that
    # happens to look like a [label]: definition MUST NOT create one.
    Case("gfm.list-table-forward-root-reference",
         "- | Item | Link |\n  | --- | --- |\n"
         "  | row | [go][ref] |\n# Root\n"
         "[ref]: /uri\n\n[go][ref]\n", "gfm"),
    Case("gfm.quoted-list-table-forward-root-reference",
         "> - | Item | Link |\n>   | --- | --- |\n"
         ">   | row | [go][ref] |\n# Root\n"
         "[ref]: /uri\n\n[go][ref]\n", "gfm"),
    Case("gfm.nested-list-table-forward-root-reference",
         "- outer\n  - | Item | Link |\n    | --- | --- |\n"
         "    | row | [go][ref] |\n# Root\n"
         "[ref]: /uri\n\n[go][ref]\n", "gfm"),
    Case("gfm.list-table-fake-definition-row",
         "- | Item | Link |\n  | --- | --- |\n"
         "  [shadow]: /blocked\n"
         "  | row | [go][real] |\n# Root\n"
         "[real]: /uri\n\n[shadow] [go][real]\n", "gfm"),
    Case("gfm.list-table-crlf-tab-owned",
         "- | A | B |\r\n\t| --- | --- |\r\n"
         "\t| x | y |\r\n", "gfm"),
    Case("gfm.quoted-nested-list-table-dedent-root",
         "> - outer\n>   - | A | B |\n"
         ">     | --- | --- |\n>     | x | y |\n"
         "After\n", "gfm"),
    # #186: bounded LIST/TASK/QUOTE GFM TABLE integration stress.
    # All cases use the independent byte-for-byte cmark-gfm oracle.
    # Extension-only results never enter the official CommonMark-652 gate.
    Case("gfm.ordered-list-table-start-seven",
         "7. | Name | Qty |\n   | :--- | ---: |\n"
         "   | tea | 2 |\n", "gfm"),
    Case("gfm.plus-marker-list-table",
         "+ | A | B |\n  | --- | --- |\n  | x | y |\n", "gfm"),
    Case("gfm.nested-ordered-list-table",
         "- outer\n  2. | A | B |\n     | --- | --- |\n"
         "     | x | y |\n", "gfm"),
    # #186: CommonMark ordered-list interruption must be respected inside
    # an already-open LIST_ITEM paragraph. '2.' needs a blank boundary;
    # '1.' can interrupt, and '2.' can begin a list after a blank.
    Case("gfm.nested-ordered-one-interrupts-paragraph",
         "- outer\n  1. | A | B |\n     | --- | --- |\n"
         "     | x | y |\n", "gfm"),
    Case("gfm.nested-ordered-two-after-blank",
         "- outer\n\n  2. | A | B |\n     | --- | --- |\n"
         "     | x | y |\n", "gfm"),
    Case("gfm.task-item-two-tables",
         "- [x] done\n\n  | A | B |\n  | --- | --- |\n"
         "  | a | b |\n\n  | C | D |\n  | --- | --- |\n"
         "  | c | d |\n", "gfm"),
    Case("gfm.quoted-list-table-sibling",
         "> - | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n> - second\n", "gfm"),
    Case("gfm.list-table-fence-interruption",
         "- | A | B |\n  | --- | --- |\n"
         "  | x | y |\n  " "\x60\x60\x60" "\n"
         "  | a | b |\n  " "\x60\x60\x60" "\n", "gfm"),
    Case("gfm.list-table-then-heading",
         "- | A | B |\n  | --- | --- |\n"
         "  # done\n", "gfm"),
    # #186: explicitly marked nested QUOTE/LIST TABLE ownership and
    # table-stop boundaries; keep the cmark-gfm byte oracle authoritative.
    Case("gfm.double-quote-list-table",
         "> > - | A | B |\n> >   | --- | --- |\n"
         "> >   | x | y |\n", "gfm"),
    Case("gfm.quote-list-two-tables-blank",
         "> - | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n>\n"
         ">   | C | D |\n>   | --- | --- |\n"
         ">   | u | v |\n", "gfm"),
    Case("gfm.quote-list-unmarked-row-stops-table",
         "> - | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n"
         "| root | text |\n", "gfm"),
    Case("gfm.nested-quote-table-outer-heading",
         "> > | A | B |\n> > | --- | --- |\n"
         "> > | x | y |\n> # Outer\n", "gfm"),
    # #186: a ListItem paragraph inside explicit QUOTE may receive
    # unmarked lazy continuation text. Such a line may NEVER initiate a
    # new TABLE, and a block/blank/competing marker ends its ownership.
    # cmark-gfm remains a fully independent strict HTML byte oracle.
    Case("gfm.quote-list-unmarked-lazy-paragraph",
         "> - alpha\nbeta\n", "gfm"),
    Case("gfm.quote-list-unmarked-lazy-then-table",
         "> - alpha\nbeta\n"
         ">   | A | B |\n>   | --- | --- |\n"
         ">   | x | y |\n", "gfm"),
    Case("gfm.quote-list-unmarked-atx-closes",
         "> - alpha\n# Root\n", "gfm"),
    Case("gfm.quote-list-lazy-blank-barrier",
         "> - alpha\n>\nbeta\n", "gfm"),
    Case("gfm.quote-list-unmarked-root-list-break",
         "> - alpha\n- root\n", "gfm"),
    Case("gfm.double-quote-list-partial-lazy-marker",
         "> > - alpha\n> beta\n", "gfm"),
    Case("gfm.quote-list-unmarked-table-delimiter-rejected",
         "> - | A | B |\n"
         "  | --- | --- |\n"
         ">   | x | y |\n", "gfm"),
    # #186: code-span pipe escapes, two independent header schemas and
    # empty-cell padding inside genuine nested GFM table owners. All bytes
    # are compared with the independent cmark-gfm engine.
    Case("gfm.quote-list-escaped-code-pipe",
         "> - | A | B |\n>   | :--- | ---: |\n"
         ">   | " "\x60" "a\\|b" "\x60" " | c |\n", "gfm"),
    Case("gfm.nested-list-two-table-alignments",
         "- outer\n  - | A | B |\n    | :--- | ---: |\n"
         "    | x | y |\n\n"
         "    | C | D |\n    | ---: | :---: |\n"
         "    | u | v |\n", "gfm"),
    # Like the already-qualified quoted unordered TASK/TABLE, cmark-gfm
    # 2025.10.22 serializes a loose ordered quoted task's [x] as literal
    # paragraph text. GFM §5.3 still defines a TASK by the first paragraph
    # marker; keep the fixed disabled CHECKBOX HTML as a strict positive
    # normative oracle and record the cmark-gfm byte discrepancy separately.
    Case("gfm.quote-task-table-ordered-nested",
         "> 4. [x] parent\n>\n>    | A | B |\n"
         ">    | --- | --- |\n>    | x | y |\n", "gfm",
         normative_expected_html=(
             "<blockquote>\n<ol start=\"4\">\n"
             "<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> \n"
             "<p>parent</p>\n<table>\n<thead>\n<tr>\n"
             "<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
             "<tbody>\n<tr>\n<td>x</td>\n<td>y</td>\n"
             "</tr>\n</tbody>\n</table>\n</li>\n</ol>\n"
             "</blockquote>\n")),
    Case("gfm.list-table-empty-cells",
         "- | A | B | C |\n  | --- | --- | --- |\n"
         "  | x |\n  | | y | |\n", "gfm"),
    Case("gfm.quote-list-table-data-pipe-backslashes",
         "> - | A | B |\n>   | --- | --- |\n"
         + r">   | a\\|b | \| |" + "\n", "gfm"),
    # CommonMark lazy continuation vs actual blank barrier, identical
    # to the native quoted-list owner control; cmark-gfm is authoritative
    # for exact HTML bytes, neither fixture is downgraded to a survey.
    Case("gfm.quote-list-second-item-lazy",
         "> - foo\n> - bar\nplain\n", "gfm"),
    Case("gfm.quote-list-second-item-blank-root",
         "> - foo\n> - bar\n\nplain\n", "gfm"),
    # #186: a root TABLE delimiter cannot be indented 4+ physical columns
    # (or a four-column tab); the same code indent that is legal INSIDE an
    # item must not accidentally allow ROOT table admission.
    Case("gfm.root-table-four-space-delimiter-rejected",
         "| A | B |\n    | --- | --- |\n"
         "| x | y |\n", "gfm"),
    Case("gfm.root-table-tab-delimiter-rejected",
         "| A | B |\n\t| --- | --- |\n"
         "| x | y |\n", "gfm"),
    Case("gfm.list-table-four-space-delimiter-admitted",
         "- | A | B |\n    | --- | --- |\n"
         "    | x | y |\n", "gfm"),
    Case("gfm.quote-table-four-space-delimiter-rejected",
         "> | A | B |\n>     | --- | --- |\n"
         "> | x | y |\n", "gfm"),
    # #186/#227 overlap reconciliation: retain these three *independent*
    # isolated cmark-gfm regression inputs from Draft #227 on the more
    # complete parent #226 without cherry-picking its duplicate C11
    # TABLE admission or altering either PR's branch history.
    Case("gfm.list-table-separated",
         "- intro\n\n  | H | V |\n  | :--- | ---: |\n"
         "  | a\\|b | **yes** |\n", "gfm"),
    Case("gfm.quote-list-table",
         "> - intro\n>\n>   | H | V |\n>   | --- | --- |\n"
         ">   | x |\n", "gfm"),
    Case("gfm.child227-nested-list-table-separated",
         "- outer\n  - inner\n\n"
         "    | H | V |\n    | --- | --- |\n"
         "    | x | y |\n", "gfm"),
    Case("gfm.wide-table-16-columns-16-rows",
         GFM_WIDE_TABLE_16, "gfm"),
    Case("gfm.double-quote-wide-table-16-columns-16-rows",
         GFM_WIDE_DOUBLE_QUOTE_16, "gfm"),
    Case("survey.cm.image", "![alt](image.png)\n", gate=False,
         note="C11 renderer rejects unsupported image semantics"),
    Case("survey.cm.raw-html", "<script>unsafe()</script>\n", gate=False,
         note="Raw HTML not supported; unsafe output must not be passed through"),
)

# #186: strict additive Cartesian cases. No new parser, hidden survey
# downgrade, or change to the 652 immutable CommonMark fixture identities.
_GFM_MATRIX = generated_cases()
CASES = CASES + tuple(
    Case(item.case_id, item.markdown, "gfm", note=(
        f"matrix owner={item.owner} table={item.table} "
        f"eol={item.eol} tail={item.tail}"
    ))
    for item in _GFM_MATRIX
)


def reference_html(case: Case) -> bytes:
    fn = (cmarkgfm.markdown_to_html if case.profile == "commonmark"
          else cmarkgfm.github_flavored_markdown_to_html)
    return fn(case.markdown).encode("utf-8")


def native_html(executable: Path, case: Case) -> bytes:
    process = subprocess.run(
        [str(executable)], input=case.markdown.encode("utf-8"),
        capture_output=True, check=False, timeout=15,
    )
    if process.returncode:
        error = process.stderr.decode("utf-8", "replace").strip()
        raise ValueError(f"native status {process.returncode}: {error[:1600]}")
    if process.stderr:
        raise ValueError("native executable printed unexpected stderr: "
                         + process.stderr.decode("utf-8", "replace")[:400])
    return process.stdout


def describe_difference(got: bytes, expected: bytes) -> str | None:
    if got == expected:
        return None
    size = min(len(got), len(expected))
    pos = next((i for i in range(size) if got[i] != expected[i]), size)
    left = got[max(0, pos - 32):pos + 60].decode("utf-8", "backslashreplace")
    right = expected[max(0, pos - 32):pos + 60].decode("utf-8", "backslashreplace")
    return f"byte {pos}, lengths {len(got)} != {len(expected)}; got={left!r} expected={right!r}"


# SAFE rendering deliberately differs from cmark-gfm's omitted-HTML
# comment: show literal escaped source, never executable HTML. This is a
# policy gate independent of byte-exact reference parity.
SAFE_RAW_HTML_SURVEY_ID = "survey.cm.raw-html"
SAFE_RAW_HTML_EXPECTED = b"&lt;script&gt;unsafe()&lt;/script&gt;\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not args.renderer.is_file():
        print(f"INFRASTRUCTURE ERROR: C11 HTML executable absent: "
              f"{args.renderer}", file=sys.stderr)
        return 2
    if not CASES:
        print("INFRASTRUCTURE ERROR: no test fixtures", file=sys.stderr)
        return 2
    ids = tuple(c.case_id for c in CASES)
    if (len(ids) != len(set(ids)) or len(_GFM_MATRIX) != EXPECTED_COUNT
            or any(not c.gate for c in CASES if c.case_id.startswith("gfm.mx."))):
        print("INFRASTRUCTURE ERROR: GFM matrix denominator, gate or ID drift",
              file=sys.stderr)
        return 2
    official_ids = tuple(
        c.case_id.split("-", 1)[1] for c in CASES
        if c.case_id.startswith("gfm.official-") and c.gate
    )
    expected_ids = tuple(map(str, list(range(198, 206)) + [279, 280]))
    if official_ids != expected_ids:
        print("INFRASTRUCTURE ERROR: GFM official case identity drift:"
              f" {official_ids!r} != {expected_ids!r}", file=sys.stderr)
        return 2
    # Exactly two pinned positive GFM §5.3 task-semantics exceptions,
    # never XFAILs or skips. The report preserves cmark-gfm raw reference
    # bytes and separate counts, so differences are not misreported as
    # independent cmark-gfm passes.
    normative_ids = tuple(c.case_id for c in CASES
                          if c.normative_expected_html is not None)
    if normative_ids != ("gfm.quoted-task-table",
                         "gfm.quote-task-table-ordered-nested"):
        print("INFRASTRUCTURE ERROR: unexpected GFM normative-oracle override:"
              f" {normative_ids!r}", file=sys.stderr)
        return 2

    results: list[dict] = []
    gated_total = gated_passed = survey_total = survey_matched = 0
    gfm_official_total = gfm_official_passed = 0
    safe_raw_html_valid = False
    cmark_gate_total = cmark_gate_passed = 0
    matrix_gate_total = matrix_gate_passed = 0
    normative_gate_total = normative_gate_passed = 0
    for case in CASES:
        record: dict = {
            "id": case.case_id, "profile": case.profile,
            "gate": case.gate, "note": case.note,
            "markdown": case.markdown,
        }
        try:
            cmark_reference = reference_html(case)
            expected = (case.normative_expected_html.encode("utf-8")
                        if case.normative_expected_html is not None
                        else cmark_reference)
            record["oracle"] = ("gfm-spec-tasklist-positive"
                                if case.normative_expected_html is not None
                                else "independent-cmarkgfm")
            record["reference_html"] = cmark_reference.decode("utf-8")
            record["reference_sha256"] = hashlib.sha256(cmark_reference).hexdigest()
            record["oracle_expected_sha256"] = hashlib.sha256(expected).hexdigest()
            if case.normative_expected_html is not None:
                record["normative_expected_html"] = case.normative_expected_html
            actual = native_html(args.renderer, case)
            record["actual_html"] = actual.decode("utf-8", "strict")
            if case.case_id == SAFE_RAW_HTML_SURVEY_ID:
                safe_raw_html_valid = actual == SAFE_RAW_HTML_EXPECTED
                record["safe_raw_html_policy_match"] = safe_raw_html_valid
                record["safe_raw_html_expected"] = (
                    SAFE_RAW_HTML_EXPECTED.decode("ascii"))
            record["actual_sha256"] = hashlib.sha256(actual).hexdigest()
            difference = describe_difference(actual, expected)
            record["matched"] = difference is None
            record["difference"] = difference
            record["cmarkgfm_difference"] = describe_difference(actual,cmark_reference)
        except (RuntimeError, UnicodeError, ValueError, OSError,
                subprocess.TimeoutExpired) as exc:
            record["matched"] = False
            record["difference"] = str(exc)

        if case.gate:
            gated_total += 1
            gated_passed += int(record["matched"])
            if case.normative_expected_html is not None:
                normative_gate_total += 1
                normative_gate_passed += int(record["matched"])
            else:
                cmark_gate_total += 1
                cmark_gate_passed += int(record["matched"])
            if case.case_id.startswith("gfm.mx."):
                matrix_gate_total += 1
                matrix_gate_passed += int(record["matched"])
            if case.case_id.startswith("gfm.official-"):
                gfm_official_total += 1
                gfm_official_passed += int(record["matched"])
        else:
            survey_total += 1
            survey_matched += int(record["matched"])
        status = "PASS" if record["matched"] else "DIFF"
        kind = "gate" if case.gate else "survey"
        print(f"[{kind}][{status}] {case.case_id}"
              + (": " + record["difference"] if record["difference"] else ""))
        results.append(record)

    report = {
        "schema_version": 1,
        "test": "C11 RichDoc HTML -> independent cmarkgfm exact-byte comparison",
        "cmarkgfm_version": metadata.version("cmarkgfm"),
        "gate": {"passed": gated_passed, "total": gated_total},
        "independent_cmarkgfm_gate": {"passed": cmark_gate_passed,
                                      "total": cmark_gate_total},
        "normative_gfm_task_gate": {"passed": normative_gate_passed,
                                    "total": normative_gate_total,
                                    "ids": list(normative_ids),
                                    "reference": "GFM §5.3 first-paragraph TASK semantics; exact SAFE HTML golden"},
        "gfm_official_subset": {"passed": gfm_official_passed,
                                "total": gfm_official_total,
                                "ids": list(expected_ids),
                                "reference": "cmarkgfm HTML byte equality",
                                "official_html_note": "HTML attribute serialization may differ from the published GFM prose examples"},
        "gfm_composition_matrix": {
            "passed": matrix_gate_passed, "total": matrix_gate_total,
            "expected_total": EXPECTED_COUNT,
            "source_manifest_sha256": manifest_sha256(_GFM_MATRIX),
            "retained_baseline": {
                "total": BASELINE_COUNT,
                "source_manifest_sha256": BASELINE_SHA256,
            },
            "extension_cases": EXPECTED_COUNT - BASELINE_COUNT,
            "dimensions": {
                "owners": 15, "tables": 6, "eol_styles": 2, "eof_forms": 2,
            },
            "oracle": "strict independent cmark-gfm HTML byte equality",
        },
        "survey": {"matched": survey_matched, "total": survey_total},
        "safe_raw_html_policy": {
            "passed": int(safe_raw_html_valid), "total": 1,
            "invariant": "SAFE render must escape script as literal text",
            "comparison": "independent from cmark-gfm survey byte parity",
        },
        "cases": results,
        "note": "Strict oracle profiles are reported separately: all but the one"
                " pinned GFM task-first-paragraph normative case compare to independent"
                " cmark-gfm exact bytes. Its cmark-gfm discrepancy is explicitly recorded"
                " and is not called a cmark-gfm match. Neither profile claims complete"
                " normative GFM conformance; diagnostic survey is not counted.",
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"RESULT: GFM composition matrix {matrix_gate_passed}/{matrix_gate_total} "
          f"sha256={manifest_sha256(_GFM_MATRIX)}")
    print(f"RESULT: independent cmarkgfm HTML {cmark_gate_passed}/{cmark_gate_total}, "
          f"normative GFM task {normative_gate_passed}/{normative_gate_total}, "
          f"combined strict {gated_passed}/{gated_total}, "
          f"diagnostics {survey_matched}/{survey_total}; "
          f"report={args.report}")
    print(f"RESULT: SAFE raw HTML literal policy {int(safe_raw_html_valid)}/1 "
          "(distinct from cmark-gfm diagnostic byte parity)")
    return 0 if gated_passed == gated_total and safe_raw_html_valid else 1


if __name__ == "__main__":
    sys.exit(main())
