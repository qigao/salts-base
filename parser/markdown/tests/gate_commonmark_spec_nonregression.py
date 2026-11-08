#!/usr/bin/env python3
"""Strict byte-exact gate for all 652 official, test-only SPEC HTML examples.

This gate never reads or changes the independent 576-ID SafeApplicationHtml
baseline. SPEC output must never be selected by an untrusted UI consumer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


class InvalidEvidence(ValueError):
    """The complete official oracle or immutable baseline was altered."""


def evaluate(survey: dict, baseline: dict) -> dict:
    ids = baseline.get("qualified_official_ids")
    if (survey.get("render_policy") != "SpecConformanceHtml" or
        baseline.get("render_policy") != "SpecConformanceHtml" or
        survey.get("commonmark_version") != "0.31.2" or
        baseline.get("commonmark_version") != "0.31.2" or
        survey.get("source") != baseline.get("source") or
        survey.get("total") != 652 or baseline.get("official_total") != 652 or
        not isinstance(ids, list) or len(ids) != 652 or
        any(type(n) is not int or n < 1 or n > 652 for n in ids) or
        ids != sorted(set(ids))):
        raise InvalidEvidence("wrong policy, corpus or full 652-ID baseline")

    rows = survey.get("cases")
    if not isinstance(rows, list) or len(rows) != 652:
        raise InvalidEvidence("incomplete full official survey")
    if any(not isinstance(row, dict) for row in rows):
        raise InvalidEvidence("invalid survey row")
    try:
        fixture = [(row["id"], row["markdown"], row["official_expected_html"])
                   for row in rows]
        if any(not all(isinstance(field, str) for field in triple)
               for triple in fixture):
            raise InvalidEvidence("invalid official fixture field")
        digest = hashlib.sha256(json.dumps(
            fixture, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")).hexdigest()
        if digest != baseline.get("official_fixture_sha256"):
            raise InvalidEvidence("official fixture provenance drift")

        seen = set()
        exact_ids = set()
        regressions = []
        unsupported = 0
        for row in rows:
            ident = row["id"]
            if len(ident) != len("cm-0.31.2-0001") or not ident.startswith("cm-0.31.2-"):
                raise InvalidEvidence(f"invalid example ID {ident!r}")
            suffix = ident[-4:]
            if not suffix.isascii() or not suffix.isdecimal():
                raise InvalidEvidence(f"invalid example ID {ident!r}")
            number = int(suffix)
            if number < 1 or number > 652 or number in seen:
                raise InvalidEvidence(f"duplicate/out-of-range ID {ident!r}")
            seen.add(number)
            status = row.get("native_status")
            actual = row.get("native_html")
            if status == "rendered":
                if not isinstance(actual, str):
                    raise InvalidEvidence(f"rendered example missing HTML #{number}")
            elif status == "unsupported":
                unsupported += 1
                if actual is not None:
                    raise InvalidEvidence(f"unsupported example emitted HTML #{number}")
            else:
                raise InvalidEvidence(f"invalid renderer status #{number}")
            exact = status == "rendered" and actual == row["official_expected_html"]
            if type(row.get("native_matches_official")) is not bool or row["native_matches_official"] != exact:
                raise InvalidEvidence(f"forged or inconsistent equality flag #{number}")
            if exact:
                exact_ids.add(number)
            elif number in ids:
                regressions.append({
                    "id": number,
                    "status": status,
                    "first_diff": row.get("first_native_official_diff"),
                })
        if seen != set(range(1, 653)):
            raise InvalidEvidence("missing official ID")
        if (type(survey.get("native_matches_official")) is not int or
            survey["native_matches_official"] != len(exact_ids) or
            type(survey.get("native_unsupported")) is not int or
            survey["native_unsupported"] != unsupported or
            type(survey.get("native_differences")) is not int or
            survey["native_differences"] != 652 - len(exact_ids) - unsupported):
            raise InvalidEvidence("untruthful aggregate survey counts")
    except (KeyError, TypeError, ValueError) as exc:
        if isinstance(exc, InvalidEvidence):
            raise
        raise InvalidEvidence(f"malformed official survey: {exc}") from exc

    return {
        "render_policy": "SpecConformanceHtml",
        "baseline_total": len(ids),
        "baseline_passed": len(ids) - len(regressions),
        "regressions": regressions,
        "additional_official_matches": sorted(exact_ids - set(ids)),
        "diagnostic_matches": len(exact_ids),
        "official_diagnostic_total": 652,
        "native_unsupported": unsupported,
        "baseline_qualified_reference_head": baseline.get("qualified_reference_head"),
        "baseline_qualified_reference_run": baseline.get("qualified_reference_run"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--survey", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = evaluate(json.loads(args.survey.read_text(encoding="utf-8")),
                          json.loads(args.baseline.read_text(encoding="utf-8")))
    except (OSError, ValueError) as exc:
        print(f"INFRA: SPEC CommonMark gate: {exc}", file=sys.stderr)
        return 2
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"SPEC official HTML: {report['baseline_passed']}/"
          f"{report['baseline_total']} retained; "
          f"{len(report['additional_official_matches'])} unqualified new matches; "
          f"{report['diagnostic_matches']}/652 diagnostic exact")
    for regression in report["regressions"]:
        print(f"REGRESSED SPEC #{regression['id']}: {regression['status']}: "
              f"{regression['first_diff']}", file=sys.stderr)
    return 1 if report["regressions"] else 0


if __name__ == "__main__":
    sys.exit(main())
