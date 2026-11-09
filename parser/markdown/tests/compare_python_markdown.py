#!/usr/bin/env python3
"""Independent Python parser comparison for the native Stun Markdown C11 AST.

The C11 subprocess emits its real RichDoc nodes; we normalize Mistune's
independently parsed AST into the same small semantic vocabulary. This is
NOT HTML conformance: cmarkgfm supplies canonical GFM/CommonMark HTML for
diagnostics, and a later gate must compare actual rendered HTML too.

Every gate case is executed; unsupported survey cases are accounted for
separately and remain visibly different instead of being reported as passes.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from importlib import metadata
import json
from pathlib import Path
import subprocess
import struct
import sys

import cmarkgfm
import mistune


@dataclass(frozen=True)
class Case:
    case_id: str
    profile: str
    markdown: str
    gate: bool = True
    note: str = ""


CASES: tuple[Case, ...] = (
    Case("cm.atx-heading", "commonmark", "## Hello\n"),
    Case("cm.strong", "commonmark", "Normal **Bold** text\n"),
    Case("cm.emphasis", "commonmark", "A *bright* day.\n"),
    Case("cm.blockquote", "commonmark", "> quote\n"),
    Case("cm.ordered-list", "commonmark", "1. alpha\n2. beta\n"),
    Case("cm.unordered-list", "commonmark", "- one\n- two\n"),
    Case("cm.fenced-code", "commonmark", "\x60\x60\x60" "c\nint v;\n" "\x60\x60\x60\n"),
    Case("cm.inline-link", "commonmark", "A [B](dest)\n"),
    Case("cm.inline-code", "commonmark", "a " "\x60" "code" "\x60" " b\n"),
    Case("cm.rule", "commonmark", "---\n"),
    Case("cm.setext-heading", "commonmark", "first\n===\n"),
    Case("cm.softbreak", "commonmark", "first\nsecond\n"),
    Case("cm.escape", "commonmark", "a \\*b\n"),
    Case("cm.emoji", "commonmark", "中😀 👩‍💻\n"),
    Case("cm.empty", "commonmark", ""),
    Case("gfm.strike", "gfm", "a ~~red~~ day\n"),
    Case("gfm.table", "gfm", "| A | B |\n| --- | --- |\n| one | two |\n"),
    Case("gfm.table-escaped-pipe", "gfm",
         "| A | B |\n| --- | --- |\n| a\\|b | x |\n"),
    Case("gfm.task-unordered", "gfm", "- [x] done\n- [ ] later\n"),
    Case("gfm.task-ordered", "gfm", "1. [x] done\n2. [ ] later\n"),
    # Survey cases do not count toward gate success. They make known gaps
    # and Python-reference deviations reproducible with the same artifacts.
    Case("survey.gfm.table-code-pipe", "gfm",
         "| A | B |\n| --- | --- |\n| " "\x60" "x|y" "\x60" " | z |\n",
         False, "Mistune 3 table plugin treats some code-span pipes as delimiters; inspect cmarkgfm HTML"),
    Case("gfm.table-align", "gfm",
         "| A | B |\n| :--- | ---: |\n| one | two |\n"),
    Case("survey.cm.reference-link", "commonmark",
         "[reference][id]\n\n[id]: https://example.org\n",
         False, "C11 reference-link grammar not yet implemented"),
    Case("survey.cm.entity", "commonmark", "Tom &amp; Jerry\n",
         False, "C11 HTML entity unescaping not yet implemented"),
)


def text_node(value: str) -> dict:
    return {"kind": "text", "text": value}


def group(kind: str, children: list[dict] | None = None,
          *, level: int = 0, meta: str = "") -> dict:
    result: dict = {"kind": kind}
    if level:
        result["level"] = level
    if meta:
        result["meta"] = meta
    if children is not None:
        result["children"] = children
    return result


def normalize_children(children: list[dict]) -> list[dict]:
    """Merge adjacent text only; retain all semantic block boundaries."""
    out: list[dict] = []
    for child in children:
        if child["kind"] == "text" and out and out[-1]["kind"] == "text":
            out[-1]["text"] += child["text"]
        elif child["kind"] != "text" or child["text"]:
            out.append(child)
    return out


def normalize_mistune_token(token: dict) -> list[dict]:
    kind = token["type"]
    attrs = token.get("attrs") or {}
    children = token.get("children") or []

    if kind == "blank_line":
        return []
    if kind == "text":
        return [text_node(token.get("raw", ""))]
    if kind in ("softbreak", "linebreak"):
        return [group("line_break")]
    if kind in ("paragraph", "block_text"):
        return [group("paragraph", normalize_mistune_nodes(children))]
    if kind == "heading":
        return [group("heading", normalize_mistune_nodes(children),
                      level=int(attrs["level"]))]
    if kind == "block_quote":
        return [group("quote", normalize_mistune_nodes(children))]
    if kind == "list":
        ordered = bool(attrs.get("ordered"))
        list_kind = "ordered_list" if ordered else "list"
        node = group(list_kind, normalize_mistune_nodes(children))
        marker = token.get("bullet")
        start = int(attrs.get("start", 1)) if ordered else 0
        allowed_markers = (".", ")") if ordered else ("-", "+", "*")
        if marker not in allowed_markers:
            raise ValueError(f"Unexpected Mistune list marker: {marker!r}")
        if not 0 <= start <= 999999999 or (not ordered and start != 0):
            raise ValueError(f"Invalid independent list start: {start}")
        node["list_semantics"] = {
            "start": start, "marker": marker,
            "loose": not bool(token.get("tight", True)),
        }
        return [node]
    if kind == "list_item":
        return [group("list_item", normalize_mistune_nodes(children))]
    if kind == "task_list_item":
        checked = "x" if attrs.get("checked", False) else " "
        return [group("task_item",
                      [group("checkbox", meta=checked)] +
                      normalize_mistune_nodes(children))]
    if kind == "block_code":
        info = str(attrs.get("info") or "").strip()
        return [group("code_fence",
                      [text_node(token.get("raw", ""))] if token.get("raw") else [],
                      meta=info)]
    if kind == "codespan":
        value = token.get("raw", "")
        return [group("code_span", [text_node(value)] if value else [])]
    if kind == "strong":
        return [group("strong", normalize_mistune_nodes(children))]
    if kind == "emphasis":
        return [group("emphasis", normalize_mistune_nodes(children))]
    if kind == "strikethrough":
        return [group("strikethrough", normalize_mistune_nodes(children))]
    if kind in ("link", "image"):
        return [group(kind, normalize_mistune_nodes(children),
                      meta=str(attrs.get("url", "")))]
    if kind == "thematic_break":
        return [group("rule")]
    if kind == "table":
        flattened: list[dict] = []
        for child in children:
            if child["type"] == "table_head":
                flattened.append(group(
                    "table_row", normalize_mistune_nodes(child.get("children") or [])))
            elif child["type"] == "table_body":
                flattened.extend(normalize_mistune_nodes(child.get("children") or []))
            else:
                raise ValueError("Unknown Mistune table section " + child["type"])
        return [group("table", flattened)]
    if kind == "table_row":
        return [group("table_row", normalize_mistune_nodes(children))]
    if kind == "table_cell":
        # Preserve known GFM alignment semantics; missing C11 metadata is
        # explicitly diagnosed instead of silently discarded as equivalent.
        alignment = attrs.get("align")
        meta = f"align={alignment}" if alignment else ""
        return [group("table_cell", normalize_mistune_nodes(children), meta=meta)]
    raise ValueError(f"Unsupported Mistune AST kind: {kind!r}")


def normalize_mistune_nodes(tokens: list[dict]) -> list[dict]:
    out: list[dict] = []
    for token in tokens:
        out.extend(normalize_mistune_token(token))
    return normalize_children(out)


def reference_ast(case: Case, commonmark, gfm) -> dict:
    markdown = commonmark if case.profile == "commonmark" else gfm
    return group("document", normalize_mistune_nodes(markdown(case.markdown)))


def native_ast(parser: str, source: str) -> dict:
    completed = subprocess.run(
        [parser], input=source.encode("utf-8"), capture_output=True,
        timeout=15, check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"C11 RichDoc dump exited {completed.returncode}: "
            f"{completed.stderr.decode('utf-8', 'replace')[:1500]}")
    nodes: list[dict] = []
    for raw_line in completed.stdout.decode("ascii").splitlines():
        parts = raw_line.split("\t")
        if len(parts) != 6:
            raise ValueError(f"Malformed C11 AST dump row: {raw_line!r}")
        index_text, kind, parent_text, level_text, hex_text, hex_meta = parts
        index = int(index_text)
        parent = int(parent_text)
        level = int(level_text)
        if index != len(nodes):
            raise ValueError("C11 node IDs are not contiguous")
        body = bytes.fromhex(hex_text).decode("utf-8", "strict")
        raw_meta = bytes.fromhex(hex_meta)
        if kind == "text":
            if raw_meta:
                raise ValueError("Unexpected metadata on plain text")
            node = text_node(body)
        elif kind == "table":
            # #186: TABLE metadata is one bounded alignment byte per column.
            # Translate it into per-cell attributes only for the test oracle;
            # the production renderer consumes the original column vector.
            if not raw_meta or any(b > 3 for b in raw_meta):
                raise ValueError("Invalid typed GFM table column alignment vector")
            node = group(kind, [])
            node["_column_alignment"] = list(raw_meta)
        elif kind in ("list", "ordered_list"):
            # ABI-stable C11 stun_ui_list_semantics is 8 bytes: uint32_t
            # start + marker + loose + two reserved bytes. Decode explicitly
            # rather than silently discarding new semantic facts as "text".
            if len(raw_meta) != 8:
                raise ValueError(f"Expected eight typed list metadata bytes: {len(raw_meta)}")
            start, marker_byte, loose, reserved0, reserved1 = struct.unpack(
                "<IBBBB", raw_meta)
            ordered = kind == "ordered_list"
            marker = chr(marker_byte)
            allowed_markers = (".", ")") if ordered else ("-", "+", "*")
            if (marker not in allowed_markers or
                    not 0 <= start <= 999999999 or
                    (not ordered and start != 0) or
                    loose not in (0, 1) or
                    reserved0 != 0 or reserved1 != 0):
                raise ValueError("Invalid typed native list semantic metadata")
            node = group(kind, [])
            node["list_semantics"] = {
                "start": start, "marker": marker, "loose": bool(loose)}
        else:
            meta = raw_meta.decode("utf-8", "strict")
            node = group(kind, [], level=level, meta=meta)
        if index == 0:
            if kind != "document" or parent != 2**32-1:
                raise ValueError("C11 root is not the expected document")
        else:
            if parent >= index:
                raise ValueError(f"Invalid C11 parent index {parent} for {index}")
            nodes[parent]["children"].append(node)
        nodes.append(node)
    if not nodes:
        raise ValueError("C11 parser returned no AST root")

    def clean(node: dict) -> dict:
        if node["kind"] == "text":
            return node
        if "children" in node:
            node["children"] = normalize_children([clean(c) for c in node["children"]])
        if node["kind"] == "table":
            alignments = node.pop("_column_alignment", [])
            rows = node["children"]
            if not rows or not alignments:
                raise ValueError("GFM table without typed header and alignments")
            labels = ("", "left", "center", "right")
            for row in rows:
                cells = row.get("children", [])
                if row["kind"] != "table_row" or len(cells) != len(alignments):
                    raise ValueError("Malformed GFM table row alignment ownership")
                for col, cell in enumerate(cells):
                    if cell["kind"] != "table_cell":
                        raise ValueError("Malformed GFM table cell")
                    if alignments[col]:
                        cell["meta"] = f"align={labels[alignments[col]]}"
        # Omit optional empty children for leaf semantic nodes.
        if not node.get("children") and node["kind"] in ("rule", "line_break", "checkbox"):
            node.pop("children", None)
        return node

    return clean(nodes[0])


def first_difference(actual, expected, location="$") -> str | None:
    if type(actual) is not type(expected):
        return f"{location}: value type {type(actual).__name__} != {type(expected).__name__}"
    if isinstance(expected, dict):
        for key in sorted(set(expected) | set(actual)):
            if key not in actual:
                return f"{location}: missing key {key!r}"
            if key not in expected:
                return f"{location}: extra key {key!r}"
            diff = first_difference(actual[key], expected[key], location + "." + key)
            if diff:
                return diff
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            return f"{location}: {len(actual)} children != {len(expected)} children"
        for index, (left, right) in enumerate(zip(actual, expected)):
            diff = first_difference(left, right, f"{location}[{index}]")
            if diff:
                return diff
    elif actual != expected:
        return f"{location}: {actual!r} != {expected!r}"
    return None


EXPECTED_GATE_CASES = 21
EXPECTED_SURVEY_CASES = 3


def main() -> int:
    cmd = argparse.ArgumentParser(description=__doc__)
    cmd.add_argument("--parser", required=True, help="Built native C11 RichDoc dump executable")
    cmd.add_argument("--report", type=Path, required=True, help="JSON evidence output")
    args = cmd.parse_args()
    got_gate = sum(case.gate for case in CASES)
    got_survey = len(CASES) - got_gate
    if (got_gate, got_survey) != (EXPECTED_GATE_CASES, EXPECTED_SURVEY_CASES):
        print("INFRASTRUCTURE ERROR: Markdown oracle fixture denominator changed"
              f": gate={got_gate} survey={got_survey}", file=sys.stderr)
        return 2
    binary = Path(args.parser)
    if not binary.is_file():
        print(f"INFRASTRUCTURE ERROR: missing C11 parser binary: {binary}", file=sys.stderr)
        return 2

    cm = mistune.create_markdown(renderer="ast", plugins=[])
    gfm = mistune.create_markdown(
        renderer="ast", plugins=["table", "task_lists", "strikethrough"])
    results: list[dict] = []
    gate_total=0
    gate_pass=0
    survey_total=0
    survey_match=0
    for case in CASES:
        if case.gate:
            gate_total += 1
        else:
            survey_total += 1
        record: dict = {
            "id": case.case_id,
            "profile": case.profile,
            "gate": case.gate,
            "markdown": case.markdown,
            "note": case.note,
        }
        try:
            reference = reference_ast(case, cm, gfm)
            actual = native_ast(args.parser, case.markdown)
            diff = first_difference(actual, reference)
            record.update(reference_ast=reference, actual_ast=actual,
                          matched=diff is None, difference=diff)
            if case.gate and diff is None:
                gate_pass += 1
            if not case.gate and diff is None:
                survey_match += 1
        except (UnicodeError, ValueError, RuntimeError, OSError,
                subprocess.TimeoutExpired) as exc:
            record.update(matched=False, difference=str(exc))
        # Cross-reference HTML is always preserved, including for cases where
        # Mistune and cmark-gfm disagree about GFM extension grammar.
        html = (
            cmarkgfm.markdown_to_html(case.markdown)
            if case.profile == "commonmark"
            else cmarkgfm.github_flavored_markdown_to_html(case.markdown)
        )
        record["cmarkgfm_html"] = html
        results.append(record)
        state="PASS" if record["matched"] else "DIFF"
        scope="gate" if case.gate else "survey"
        print(f"[{scope}][{state}] {case.case_id}"
              + (f": {record['difference']}" if not record["matched"] else ""))

    report={
        "schema_version": 1,
        "reference_ast": "mistune",
        "reference_ast_version": metadata.version("mistune"),
        "reference_html": "cmarkgfm",
        "reference_html_version": metadata.version("cmarkgfm"),
        "note": "Gated subset compares actual C11 RichDoc to independent Mistune AST."
                " cmarkgfm HTML is separately recorded, not yet compared to a"
                " complete C11 HTML renderer. Survey cases are never counted as passed gates.",
        "gate": {"passed":gate_pass, "total":gate_total},
        "survey": {"matched":survey_match, "total":survey_total},
        "cases":results,
    }
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",
                           encoding="utf-8")
    print(f"RESULT: AST gate {gate_pass}/{gate_total}, "
          f"diagnostic survey matched {survey_match}/{survey_total}; "
          f"report={args.report}")
    return 0 if gate_pass == gate_total else 1


if __name__ == "__main__":
    sys.exit(main())
