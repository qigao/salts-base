#!/usr/bin/env python3
"""Deterministic #186 GFM composition fixtures for the *test-only* HTML oracle.

All cases use public Markdown input. This file is not a parser, transformer,
reference implementation, or production dependency. The existing C11
renderer and independently installed cmark-gfm remain the two execution paths.

Two disjoint deterministic cohorts preserve the prior 160-case proof:
10 original owners x 4 original tables x LF/CRLF x EOF forms = 160;
five deeper owners and two richer inline-table shapes add 200 strict cases.
All 360 compare against independent cmark-gfm byte-for-byte. The distinct
GFM §5.3 normative task/cmark-gfm disagreements are not folded into this set.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json


@dataclass(frozen=True)
class GfmCombination:
    case_id: str
    markdown: str
    owner: str
    table: str
    eol: str
    tail: str


_TICK = chr(96)
_TABLE_SHAPES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("plain", (
        "| A | B |",
        "| --- | --- |",
        "| alpha | beta |",
        "| next | last |",
    )),
    ("alignment", (
        "| Name | Value |",
        "| :--- | ---: |",
        "| a | 1 |",
        "| b | 2 |",
    )),
    ("escaped-inline", (
        "| L | R |",
        "| :---: | ---: |",
        r"| a\|b | **bold** |",
        f"| {_TICK}x\\|y{_TICK} | [link](/go) |",
    )),
    ("ragged", (
        "| A | B | C |",
        "| --- | :---: | ---: |",
        "| short |",
        "| one | two | three | surplus |",
    )),
)

# Two new GFM table shapes exercise interleaved spans and escaped cell
# delimiters without changing the existing 160-case baseline.
_EXTRA_TABLE_SHAPES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("rich-inline", (
        "| Name | Value |",
        "| :--- | ---: |",
        "| ~~old~~ and *new* | **bold** &amp; [go](/go) |",
        r"| a\|b | " + _TICK + r"x\|y" + _TICK + " |",
    )),
    ("unicode-code-link", (
        "| L | R |",
        "| :---: | ---: |",
        "| café 😀 | [go](/go) &amp; *yes* |",
        "| " + _TICK + r"a\|b" + _TICK + " | ~~done~~ and **bold** |",
    )),
)

# (id, source prelude, first-table-line prefix, next-table-line prefix)
# A list-owned table's first line is the list-item paragraph; its delimiter
# and body rows must instead be indented to the actual list content column.
_OWNER_FRAMES: tuple[tuple[str, str, str, str], ...] = (
    ("root", "", "", ""),
    ("quote1", "", "> ", "> "),
    ("quote2", "", "> > ", "> > "),
    ("list-bullet", "", "- ", "  "),
    ("list-ordered7", "", "7. ", "   "),
    ("quote-list", "", "> - ", ">   "),
    ("nested-list", "- outer\n", "  - ", "    "),
    ("quote-nested-list", "> - outer\n", ">   - ", ">     "),
    ("task-checked", "- [x] parent\n\n", "  ", "  "),
    ("task-open", "- [ ] parent\n\n", "  ", "  "),
)

# Extra frames deliberately stack two Quote depths, nested LIST ownership,
# and checked/unchecked TASK within a nested list. A genuine content-column
# prefix is mandatory for every TABLE delimiter/data row.
_EXTRA_OWNER_FRAMES: tuple[tuple[str, str, str, str], ...] = (
    ("quote2-list", "", "> > - ", "> >   "),
    ("quote2-nested-list", "> > - outer\n", "> >   - ", "> >     "),
    ("nested-ordered-one", "- outer\n", "  1. ", "     "),
    ("nested-task-checked", "- outer\n  - [x] parent\n\n", "    ", "    "),
    ("nested-task-open", "- outer\n  - [ ] parent\n\n", "    ", "    "),
)

BASELINE_COUNT = 10 * 4 * 2 * 2
BASELINE_SHA256 = "7b1904b68bcb8b39163ca36ee64702611e44996a5b950f949b76ddfdf514f408"
EXPECTED_COUNT = (10 * 4 + 5 * (4 + 2) + 10 * 2) * 2 * 2


def generated_cases() -> tuple[GfmCombination, ...]:
    """Return source-stable, strictly gated cases; never random samples."""
    output: list[GfmCombination] = []

    def append(owner_frames: tuple[tuple[str, str, str, str], ...],
               table_shapes: tuple[tuple[str, tuple[str, ...]], ...]) -> None:
        for owner, prelude, first_prefix, row_prefix in owner_frames:
            for table, lines in table_shapes:
                source = prelude + first_prefix + lines[0] + "\n"
                source += "".join(row_prefix + line + "\n" for line in lines[1:])
                for eol, delimiter in (("lf", "\n"), ("crlf", "\r\n")):
                    converted = source.replace("\n", delimiter)
                    for tail in ("nl", "eof"):
                        markdown = (converted if tail == "nl"
                                    else converted[:-len(delimiter)])
                        assert markdown and (
                            (tail == "nl") == markdown.endswith(delimiter))
                        output.append(GfmCombination(
                            case_id=f"gfm.mx.{owner}.{table}.{eol}.{tail}",
                            markdown=markdown,
                            owner=owner,
                            table=table,
                            eol=eol,
                            tail=tail,
                        ))

    # Keep the byte-identical earlier 160 fixtures in their original order.
    append(_OWNER_FRAMES, _TABLE_SHAPES)
    if (len(output) != BASELINE_COUNT
            or manifest_sha256(tuple(output)) != BASELINE_SHA256):
        raise AssertionError("previously qualified GFM 160-case corpus drift")
    # Add new nested owners with *all* shapes, then all established owners
    # with just the new inline shapes. Never silently replace older cases.
    append(_EXTRA_OWNER_FRAMES, _TABLE_SHAPES + _EXTRA_TABLE_SHAPES)
    append(_OWNER_FRAMES, _EXTRA_TABLE_SHAPES)

    ids = [case.case_id for case in output]
    if len(output) != EXPECTED_COUNT or len(ids) != len(set(ids)):
        raise AssertionError("GFM matrix cardinality/identity drift")
    return tuple(output)


def manifest_sha256(cases: tuple[GfmCombination, ...]) -> str:
    """Fingerprint *all* generated fixture source bytes and stable identities."""
    payload = [(c.case_id, c.markdown) for c in cases]
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


if __name__ == "__main__":
    result = generated_cases()
    print(f"GFM #186 deterministic matrix {len(result)}/{EXPECTED_COUNT}"
          f" sha256={manifest_sha256(result)}")
