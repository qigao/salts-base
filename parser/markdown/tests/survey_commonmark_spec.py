#!/usr/bin/env python3
"""Full CommonMark 0.31.2 example survey of the actual C11 HTML renderer.

This is intentionally NON-GATING on semantic differences until every official
fixture is supported, but infrastructure/crashes and missing fixtures FAIL CI.
All 652 official examples must be accounted for and saved with independent
cmark-gfm HTML and official expected HTML; no silent skip or fake pass rate.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from importlib import metadata
import json
from pathlib import Path
import subprocess
import sys

import cmarkgfm

FENCE = "\x60" * 32
OPEN = FENCE + " example"
EXPECTED_EXAMPLES = 652
SPEC_VERSION = "0.31.2"


@dataclass(frozen=True)
class Example:
    number: int
    start_line: int
    markdown: str
    expected_html: str


def read_official_spec(path: Path) -> list[Example]:
    content = path.read_text(encoding="utf-8")
    if f"version: '{SPEC_VERSION}'" not in content:
        raise ValueError(f"Unexpected CommonMark spec version: {path}")
    examples: list[Example] = []
    state = "outside"
    markdown: list[str] = []
    html: list[str] = []
    start_line = 0
    for line_num, line in enumerate(content.splitlines(), 1):
        if state == "outside":
            if line == OPEN:
                state = "markdown"
                markdown = []
                html = []
                start_line = line_num
        elif state == "markdown":
            if line == ".":
                state = "html"
            elif line == FENCE:
                raise ValueError(f"Missing Markdown/HTML delimiter near {start_line}")
            else:
                markdown.append(line)
        else:
            if line == FENCE:
                # Official spec represents literal U+0009 as right-arrow.
                src = ("\n".join(markdown) + ("\n" if markdown else "")).replace("→", "\t")
                expected = ("\n".join(html) + ("\n" if html else "")).replace("→", "\t")
                examples.append(Example(len(examples) + 1, start_line,
                                        src, expected))
                state = "outside"
            else:
                html.append(line)
    if state != "outside":
        raise ValueError(f"Unclosed CommonMark example starting line {start_line}")
    if len(examples) != EXPECTED_EXAMPLES:
        raise ValueError(
            f"CommonMark example denominator changed: "
            f"got {len(examples)}, expected {EXPECTED_EXAMPLES}")
    return examples


def first_diff(actual: bytes, expected: bytes) -> str | None:
    if actual == expected:
        return None
    count = min(len(actual), len(expected))
    pos = next((i for i in range(count) if actual[i] != expected[i]), count)
    return (f"byte {pos}: actual length={len(actual)}, expected length={len(expected)}; "
            f"actual={actual[max(0,pos-24):pos+50].decode('utf-8','backslashreplace')!r}, "
            f"expected={expected[max(0,pos-24):pos+50].decode('utf-8','backslashreplace')!r}")


def actual_html(binary: Path, source: str, render_policy: str = "safe"
                ) -> tuple[str, bytes, str]:
    command=[str(binary)]
    if render_policy == "spec":
        command.append("--spec-html")
    proc = subprocess.run(command, input=source.encode("utf-8"),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=12, check=False)
    err = proc.stderr.decode("utf-8", "replace").strip()
    if proc.returncode < 0 or proc.returncode not in (0, 2):
        # Crash/sanitizer failures are never reduced to "unsupported grammar".
        raise RuntimeError(f"native C11 HTML crashed/exited unexpectedly: "
                           f"returncode={proc.returncode}, stderr={err[:500]}")
    if "AddressSanitizer" in err or "runtime error:" in err:
        raise RuntimeError(f"native C11 sanitizer diagnostic: {err[:500]}")
    if proc.returncode == 2:
        if not err.startswith("markdown HTML test CLI: render/count error ") and not err.startswith(
                "markdown HTML test CLI: parse error "):
            raise RuntimeError(f"Unexpected C11 HTML test CLI failure: {err[:500]}")
        if proc.stdout:
            raise RuntimeError("Native rejected input after partial stdout write")
        return "unsupported", b"", err
    if err:
        raise RuntimeError(f"Native C11 HTML logged unexpected stderr: {err[:500]}")
    return "rendered", proc.stdout, ""


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--renderer", type=Path, required=True)
    p.add_argument("--spec", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    p.add_argument("--render-policy", choices=("safe","spec"),
                   default="safe", help="explicit C11 HTML render profile")
    args = p.parse_args()
    if not args.renderer.is_file():
        print(f"INFRASTRUCTURE ERROR: C11 HTML executable absent: {args.renderer}",
              file=sys.stderr)
        return 2
    try:
        examples = read_official_spec(args.spec)
    except (OSError, ValueError) as exc:
        print(f"INFRASTRUCTURE ERROR: {exc}", file=sys.stderr)
        return 2

    rows: list[dict] = []
    native_official_matches = native_reference_matches = 0
    reference_official_matches = unsupported = 0
    for example in examples:
        reference = cmarkgfm.markdown_to_html(example.markdown).encode("utf-8")
        official = example.expected_html.encode("utf-8")
        reference_matches = reference == official
        reference_official_matches += int(reference_matches)
        try:
            status, actual, detail = actual_html(args.renderer, example.markdown,
                                                     args.render_policy)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"RUNTIME FAILURE: CommonMark #{example.number} at "
                  f"line {example.start_line}: {exc}", file=sys.stderr)
            return 2
        matched_official = status == "rendered" and actual == official
        matched_reference = status == "rendered" and actual == reference
        native_official_matches += int(matched_official)
        native_reference_matches += int(matched_reference)
        unsupported += int(status == "unsupported")
        rows.append({
            "id": f"cm-0.31.2-{example.number:04d}",
            "source_line": example.start_line,
            "markdown": example.markdown,
            "native_status": status,
            "native_failure_detail": detail or None,
            "native_html": (actual.decode("utf-8", "replace") if status == "rendered" else None),
            "official_expected_html": example.expected_html,
            "cmarkgfm_html": reference.decode("utf-8", "replace"),
            "native_matches_official": matched_official,
            "native_matches_cmarkgfm": matched_reference,
            "cmarkgfm_matches_official": reference_matches,
            "first_native_official_diff": (
                first_diff(actual, official) if status == "rendered" else None),
        })
    report = {
        "schema_version": 1,
        "source": "tools/md/test/spec.txt",  # immutable original corpus provenance ID
        "commonmark_version": SPEC_VERSION,
        "cmarkgfm_version": metadata.version("cmarkgfm"),
        "total": EXPECTED_EXAMPLES,
        "native_matches_official": native_official_matches,
        "native_matches_cmarkgfm": native_reference_matches,
        "cmarkgfm_matches_official": reference_official_matches,
        "native_unsupported": unsupported,
        "native_differences": EXPECTED_EXAMPLES - native_official_matches - unsupported,
        "conformance": "DIAGNOSTIC ONLY; all examples counted, mismatches do not pass",
        "cases": rows,
    }
    if args.render_policy == "spec":
        # Separate diagnostic only, NEVER feed into the production SAFE gate.
        report["render_policy"] = "SpecConformanceHtml"
        report["conformance"] = (
            "TEST-ONLY explicit raw HTML rendering; no SAFE policy exemptions"
        )
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",
                           encoding="utf-8")
    print(f"CommonMark {SPEC_VERSION} survey: "
          f"C11 vs official {native_official_matches}/{EXPECTED_EXAMPLES}; "
          f"C11 vs cmarkgfm {native_reference_matches}/{EXPECTED_EXAMPLES}; "
          f"cmarkgfm vs official {reference_official_matches}/{EXPECTED_EXAMPLES}; "
          f"unsupported {unsupported}; differences "
          f"{EXPECTED_EXAMPLES-native_official_matches-unsupported}; "
          f"full evidence={args.report}")
    return 0  # Fail only on infrastructure, crashes and missing official cases.


if __name__ == "__main__":
    sys.exit(main())
