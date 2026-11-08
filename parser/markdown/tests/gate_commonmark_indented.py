#!/usr/bin/env python3
"""Gate explicit CommonMark 0.31.2 official HTML IDs, never parse-only success.

All 12 official indented-code cases now gate strictly, including #108/#109
whose loose/nested list semantics are supplied by the canonical C11 container
frames. Numbered list precedence examples also gate without skip behavior.
The full 652-case survey is a separate non-gating CI step.
"""
from __future__ import annotations

import argparse
from importlib import metadata
import json
from pathlib import Path
import sys

from survey_commonmark_spec import (
    EXPECTED_EXAMPLES, SPEC_VERSION, actual_html, first_diff,
    read_official_spec,
)

SECTION_IDS = frozenset(range(107, 119))
GATED_IDS = frozenset({48, 69, 107, 108, 109, 110, 111, 112, 113, 114,
                       115, 116, 117, 118, 225,
                       228, 229, 230, 232, 235, 236, 237, 239, 240, 250, 251, 252,
                       254, 256, 259, 260, 301, 302, 304, 305})
EXPECTED_GATES = 35


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--renderer", type=Path, required=True)
    p.add_argument("--spec", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    if not args.renderer.is_file():
        print("INFRA: missing native C11 renderer", file=sys.stderr)
        return 2
    examples = read_official_spec(args.spec)
    if len(examples) != EXPECTED_EXAMPLES or len(SECTION_IDS) != 12 or \
            len(GATED_IDS) != EXPECTED_GATES:
        print("INFRA: CommonMark fixture denominator changed", file=sys.stderr)
        return 2

    selected = SECTION_IDS | GATED_IDS
    rows = []
    gate_passed = 0
    section_passed = 0
    try:
        for example in examples:
            if example.number not in selected:
                continue
            kind, result, detail = actual_html(args.renderer, example.markdown)
            expected = example.expected_html.encode("utf-8")
            passed = kind == "rendered" and result == expected
            if passed and example.number in GATED_IDS:
                gate_passed += 1
            if passed and example.number in SECTION_IDS:
                section_passed += 1
            rows.append({
                "id": f"cm-0.31.2-{example.number:04d}",
                "source_line": example.start_line,
                "gate": example.number in GATED_IDS,
                "status": kind,
                "pass": passed,
                "source": example.markdown,
                "actual_html": result.decode("utf-8", "strict"),
                "official_html": example.expected_html,
                "detail": detail or first_diff(result, expected),
            })
    except Exception as exc:
        print(f"INFRA/RUNTIME error: {exc}", file=sys.stderr)
        return 2

    if len(rows) != len(selected):
        print(f"INFRA: expected {len(selected)} selected examples, "
              f"found {len(rows)}", file=sys.stderr)
        return 2
    report = {
        "schema_version": 1,
        "commonmark_version": SPEC_VERSION,
        "cmarkgfm_version": metadata.version("cmarkgfm"),
        "official_total": EXPECTED_EXAMPLES,
        "gate": {"passed": gate_passed, "total": EXPECTED_GATES},
        "indented_section": {"passed": section_passed, "total": 12},
        "diagnostic_ids": [],
        "cases": rows,
        "note": "Strict official HTML subset, not full CommonMark compliance",
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False)
                           + "\n", encoding="utf-8")
    for row in rows:
        print(f"[{'GATE' if row['gate'] else 'SURVEY'}]"
              f"[{'PASS' if row['pass'] else 'DIFF'}] {row['id']}"
              + (f": {row['detail']}" if not row['pass'] else ""))
    print(f"Official CommonMark indented gate: {gate_passed}/"
          f"{EXPECTED_GATES}; section diagnostic: {section_passed}/12")
    return 0 if gate_passed == EXPECTED_GATES else 1


if __name__ == "__main__":
    sys.exit(main())
