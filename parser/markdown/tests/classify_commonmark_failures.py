#!/usr/bin/env python3
"""Classify the reviewed CommonMark backlog without granting pass exemptions.

A category describes remediation ownership, not an alternative expected HTML.
The caller's existing immutable passing-ID gate remains authoritative.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


class ClassificationError(ValueError):
    """Malformed evidence or an unreviewed classification map."""


def fixture_digest(rows):
    payload = [(r["id"], r["markdown"], r["official_expected_html"])
               for r in rows]
    return hashlib.sha256(json.dumps(payload, ensure_ascii=False,
        separators=(",", ":")).encode("utf-8")).hexdigest()


def classify_failures(survey, ledger):
    """Recompute exact equality and partition failures; never rewrite a row."""
    if not isinstance(survey, dict) or not isinstance(ledger, dict):
        raise ClassificationError("survey and ledger must be objects")
    if (survey.get("commonmark_version") != "0.31.2" or
        ledger.get("commonmark_version") != "0.31.2" or
        survey.get("source") != "tools/md/test/spec.txt" or
        ledger.get("source") != survey.get("source") or
        type(survey.get("total")) is not int or survey["total"] != 652 or
        ledger.get("schema_version") != 1 or
        ledger.get("policy_exemptions") is not False):
        raise ClassificationError("version/source/denominator/policy mismatch")
    rows = survey.get("cases")
    if not isinstance(rows, list) or len(rows) != 652:
        raise ClassificationError("the complete 652-row survey is required")
    by_id = {}
    for number, row in enumerate(rows, 1):
        if (not isinstance(row, dict) or
            row.get("id") != f"cm-0.31.2-{number:04d}" or
            not isinstance(row.get("markdown"), str) or
            not isinstance(row.get("official_expected_html"), str) or
            row.get("native_status") not in ("rendered", "unsupported") or
            type(row.get("native_matches_official")) is not bool):
            raise ClassificationError(f"malformed or unordered row #{number}")
        rendered = row["native_status"] == "rendered"
        actual = row.get("native_html")
        if ((rendered and not isinstance(actual, str)) or
            (not rendered and actual is not None)):
            raise ClassificationError(f"inconsistent native output #{number}")
        exact = rendered and actual == row["official_expected_html"]
        if row["native_matches_official"] != exact:
            raise ClassificationError(f"forged exact-match flag #{number}")
        by_id[number] = row
    if fixture_digest(rows) != ledger.get("official_fixture_sha256"):
        raise ClassificationError("official input/expected corpus changed")

    categories = ledger.get("categories")
    if not isinstance(categories, list) or not categories:
        raise ClassificationError("missing reviewed categories")
    assigned, names = {}, set()
    for category in categories:
        if not isinstance(category, dict):
            raise ClassificationError("category must be an object")
        name, ids = category.get("key"), category.get("ids")
        if (not isinstance(name, str) or not name or name in names or
            not isinstance(ids, list) or not ids or
            any(type(n) is not int or not 1 <= n <= 652 for n in ids) or
            ids != sorted(set(ids)) or
            not isinstance(category.get("owner"), str) or
            not category["owner"]):
            raise ClassificationError("invalid category/name/IDs/owner")
        names.add(name)
        for number in ids:
            if number in assigned:
                raise ClassificationError(f"overlapping categories #{number}")
            assigned[number] = name
    historical = ledger.get("reference_unpassed_total")
    if type(historical) is not int or historical != len(assigned):
        raise ClassificationError("reviewed backlog denominator changed")

    failed = {n for n, r in by_id.items() if not r["native_matches_official"]}
    unknown = sorted(failed - assigned.keys())
    # A new regression must not silently be called a known policy difference.
    if unknown:
        raise ClassificationError(f"unclassified failures: {unknown}")
    exact_count = 652 - len(failed)
    unsupported = sum(r["native_status"] == "unsupported" for r in rows)
    different = len(failed) - unsupported
    for field, expected in (("native_matches_official", exact_count),
                            ("native_unsupported", unsupported),
                            ("native_differences", different)):
        if type(survey.get(field)) is not int or survey[field] != expected:
            raise ClassificationError(f"inconsistent survey total: {field}")
    result = {
        "schema_version": 1,
        "reference_head": ledger["reference_head"],
        "official_fixture_sha256": ledger["official_fixture_sha256"],
        "official_total": 652,
        "official_exact": exact_count,
        "official_different": different,
        "native_unsupported": unsupported,
        "unpassed_total": len(failed),
        "policy_exemptions": False,
        "unclassified_ids": [],
        "resolved_since_reference": sorted(assigned.keys() - failed),
        "categories": []
    }
    for category in categories:
        pending = sorted(failed.intersection(category["ids"]))
        status_counts = {status: sum(by_id[n]["native_status"] == status
                                    for n in pending)
                         for status in ("rendered", "unsupported")}
        result["categories"].append({
            "key": category["key"], "owner": category["owner"],
            "remaining": len(pending), "status_counts": status_counts,
            "ids": pending,
            "resolved_ids": sorted(set(category["ids"]) - failed)
        })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--survey", type=Path, required=True)
    parser.add_argument("--ledger", type=Path,
        default=Path(__file__).with_name("commonmark_failure_classes.json"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = classify_failures(json.loads(args.survey.read_text("utf-8")),
                                   json.loads(args.ledger.read_text("utf-8")))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"INFRA: {error}", file=sys.stderr)
        return 2
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for category in result["categories"]:
        print(f"{category['key']}: {category['remaining']} unpassed; "
              f"owner={category['owner']}")
    print(f"Official HTML: {result['official_exact']}/652; "
          "policy classifications do not count as passes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
