#!/usr/bin/env python3
"""#229 test-only independent GFM root List -> Table oracle.

The *shipping legacy C++* MarkdownWidget incorrectly nests a root TABLE when
the TASK list is followed by a blank-only separator. A visible root thematic
break is a distinct legal control and currently works. This independent
cmark-gfm oracle freezes normative root ownership without modifying the C++
parser, accepting its bad output as correct, or introducing a runtime fallback.

Use the already-installed cmarkgfm Python environment from #186. Both
independent normative root-ownership semantics and, when --renderer is given,
the actual C11 HTML byte output are STRICT 5/5 gates. Their separate scope is
never silently folded into or used to weaken the pinned 468-case GFM baseline.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import cmarkgfm

_VOID = frozenset(("area", "base", "br", "col", "embed", "hr", "img",
                   "input", "link", "meta", "param", "source", "track", "wbr"))
EXPECTED_SOURCE_MANIFEST_SHA256 = "513f381d284cca78f48611992bb591eae640dbf6cc19b3fbfa8e821dd962e207"
_SOURCE_COMMON = ("- [x] {done}\n- [ ] {pending}\n\n{separator}"
                  "| City | Price |\n| :--- | ---: |\n| {city} | {price} |\n")


@dataclass(frozen=True)
class Fixture:
    id: str
    markdown: str
    root_tags: tuple[str, ...]
    texts: tuple[str, ...]
    upstream_legacy_gap: bool


def source(heading: str, done: str, pending: str, city: str, price: str,
           separator: str, eol: str = "\n") -> str:
    markdown = (f"# {heading}\n\n" + _SOURCE_COMMON.format(
        done=done, pending=pending, city=city, price=price,
        separator=separator))
    return markdown.replace("\n", eol)


FIXTURES: tuple[Fixture, ...] = (
    Fixture(
        "unicode-root-task-table-blank",
        source("日本語 café 😀 Alpha", "発送済み", "待機中", "東京", "€20", ""),
        ("h1", "ul", "table"),
        ("日本語", "発送済み", "待機中", "東京", "€20"),
        True,
    ),
    Fixture(
        "unicode-root-task-table-blank-crlf",
        source("日本語 café 😀 Alpha", "発送済み", "待機中", "東京", "€20",
               "", "\r\n"),
        ("h1", "ul", "table"),
        ("日本語", "発送済み", "待機中", "東京", "€20"),
        True,
    ),
    Fixture(
        "ascii-root-task-table-blank",
        source("Stage alpha", "shipped", "pending", "Tokyo", "20", ""),
        ("h1", "ul", "table"),
        ("Stage alpha", "shipped", "pending", "Tokyo", "20"),
        True,
    ),
    Fixture(
        "unicode-root-task-hr-table",
        source("日本語 café 😀 Alpha", "発送済み", "待機中", "東京", "€20",
               "---\n\n"),
        ("h1", "ul", "hr", "table"),
        ("日本語", "発送済み", "待機中", "東京", "€20"),
        False,
    ),
    Fixture(
        "ascii-root-task-hr-table",
        source("Stage alpha", "shipped", "pending", "Tokyo", "20",
               "---\n\n"),
        ("h1", "ul", "hr", "table"),
        ("Stage alpha", "shipped", "pending", "Tokyo", "20"),
        False,
    ),
)


class Structure(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.stack: list[str] = []
        self.roots: list[str] = []
        self.inputs: list[dict[str, str | None]] = []
        self.elements: list[str] = []
        self.text: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if not self.stack:
            self.roots.append(tag)
        self.elements.append(tag)
        if tag == "input":
            self.inputs.append(dict(attrs))
        if tag not in _VOID:
            self.stack.append(tag)

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.handle_starttag(tag, attrs)
        if tag not in _VOID:
            self.handle_endtag(tag)

    def handle_endtag(self, tag: str) -> None:
        if not self.stack or self.stack[-1] != tag:
            raise AssertionError(f"unbalanced HTML close {tag}: {self.stack!r}")
        self.stack.pop()

    def handle_data(self, data: str) -> None:
        self.text.append(data)


def inspect(fixture: Fixture, html: str) -> dict[str, object]:
    dom = Structure()
    dom.feed(html)
    dom.close()
    if dom.stack:
        raise AssertionError(f"{fixture.id}: unclosed tags {dom.stack}")
    if tuple(dom.roots) != fixture.root_tags:
        raise AssertionError(
            f"{fixture.id}: root blocks are {dom.roots!r}; "
            f"normative expected {fixture.root_tags!r}"
        )
    if (dom.elements.count("table") != 1 or dom.elements.count("tr") != 2
            or dom.elements.count("th") != 2 or dom.elements.count("td") != 2):
        raise AssertionError(f"{fixture.id}: table row/cell cardinality drift")
    if len(dom.inputs) != 2:
        raise AssertionError(f"{fixture.id}: missing GFM TASK checkboxes")
    if any(item.get("type") != "checkbox" for item in dom.inputs):
        raise AssertionError(f"{fixture.id}: wrong task input type")
    if "checked" not in dom.inputs[0] or "checked" in dom.inputs[1]:
        raise AssertionError(f"{fixture.id}: checked/unchecked task semantics changed")
    text = "".join(dom.text)
    if any(value not in text for value in fixture.texts):
        raise AssertionError(f"{fixture.id}: Unicode/ASCII payload lost in oracle")
    return {
        "source_sha256": hashlib.sha256(fixture.markdown.encode()).hexdigest(),
        "oracle_html_sha256": hashlib.sha256(html.encode()).hexdigest(),
        "root_blocks": dom.roots,
        "table_rows": 2,
        "task_inputs": 2,
        "legacy_blank_separator_misnest": fixture.upstream_legacy_gap,
    }


def native_output(renderer: Path, markdown: str) -> tuple[bytes | None, str]:
    try:
        output = subprocess.run(
            [str(renderer)], input=markdown.encode("utf-8"),
            capture_output=True, timeout=15, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, f"error: {exc}"
    if output.returncode != 0 or output.stderr:
        return None, (f"exit {output.returncode}: " +
                      output.stderr.decode("utf-8", "replace")[:250])
    return output.stdout, "OK"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--report", type=Path, required=True)
    p.add_argument("--renderer", type=Path, help="Optional C11 HTML strict byte oracle")
    args = p.parse_args()
    if args.renderer is not None and not args.renderer.is_file():
        print("INFRASTRUCTURE ERROR: missing C11 diagnostic renderer", file=sys.stderr)
        return 2
    try:
        ids = [f.id for f in FIXTURES]
        assert len(ids) == 5 and len(set(ids)) == 5
        assert sum(f.upstream_legacy_gap for f in FIXTURES) == 3
        report: list[dict[str, object]] = []
        native_passed = 0
        for fixture in FIXTURES:
            independent_html = cmarkgfm.github_flavored_markdown_to_html(
                fixture.markdown)
            item = {"id": fixture.id, **inspect(fixture, independent_html)}
            if args.renderer is not None:
                native, status = native_output(args.renderer, fixture.markdown)
                byte_matches = native == independent_html.encode("utf-8")
                if not byte_matches:
                    first = -1
                    if native is not None:
                        first = next((
                            k for k in range(min(len(native), len(independent_html.encode("utf-8"))))
                            if native[k] != independent_html.encode("utf-8")[k]
                        ), min(len(native), len(independent_html.encode("utf-8"))))
                    raise AssertionError(
                        f"{fixture.id}: native HTML diverges from independent cmark-gfm "
                        f"at byte {first}, status={status}"
                    )
                native_passed += 1
                item["c11_strict"] = {
                    "status": status,
                    "byte_matches_oracle": True,
                    "html_sha256": hashlib.sha256(native).hexdigest(),
                }
            report.append(item)
        manifest = json.dumps(
            [(f.id, f.markdown) for f in FIXTURES],
            ensure_ascii=False, separators=(",", ":"),
        ).encode("utf-8")
        manifest_digest = hashlib.sha256(manifest).hexdigest()
        assert manifest_digest == EXPECTED_SOURCE_MANIFEST_SHA256, (
            "fixed #229 fixture corpus changed without independent review"
        )
        result = {
            "oracle": "independent cmark-gfm HTML root ownership",
            "test_only": True,
            "not_an_old_cpp_parser_fix": True,
            "fixtures": 5,
            "normative_passed": len(report),
            "c11_strict_passed": native_passed,
            "c11_strict_total": len(FIXTURES) if args.renderer is not None else 0,
            "blank_separator_positive_root_expected": 3,
            "explicit_hr_controls": 2,
            "source_manifest_sha256": manifest_digest,
            "cases": report,
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8")
        print("RESULT: independent #229 GFM root owner oracle "
              f"{len(report)}/5 (blank 3, thematic-break 2), "
              f"C11 strict HTML {native_passed}/{result['c11_strict_total']}; "
              f"source_manifest_sha256={result['source_manifest_sha256']}")
        return 0
    except (AssertionError, ValueError) as exc:
        print(f"FAIL: independent #229 GFM oracle: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
