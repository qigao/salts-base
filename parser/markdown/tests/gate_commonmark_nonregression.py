#!/usr/bin/env python3
"""Gated no-regression check of 576 independently qualified SAFE official HTML IDs.

The full 652-case report remains diagnostic; every baseline ID must still
render *exactly* the official CommonMark 0.31.2 expected HTML.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from classify_commonmark_failures import classify_failures

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--survey",type=Path,required=True)
    parser.add_argument("--baseline",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    data=json.loads(args.survey.read_text(encoding="utf-8"))
    baseline=json.loads(args.baseline.read_text(encoding="utf-8"))
    ids=baseline.get("qualified_official_ids")
    if (data.get("commonmark_version")!="0.31.2" or
        baseline.get("commonmark_version")!="0.31.2" or
        data.get("source")!=baseline.get("source") or
        data.get("total")!=652 or baseline.get("official_total")!=652 or
        not isinstance(ids,list) or len(ids)!=576 or
        any(type(x) is not int or x<1 or x>652 for x in ids) or
        ids!=sorted(set(ids))):
        print("INFRA: immutable CommonMark baseline differs",file=sys.stderr)
        return 2
    rows=data.get("cases")
    if not isinstance(rows,list) or len(rows)!=652:
        print("INFRA: malformed full official survey",file=sys.stderr)
        return 2
    # Bind the whole official corpus (ID, Markdown source and expected HTML)
    # to the previously qualified run; a fixture rewrite is NOT a regression
    # fix. Keep UTF-8 and compact separators stable across hosts.
    fixture_bytes=json.dumps(
        [(row["id"],row["markdown"],row["official_expected_html"])
         for row in rows],
        ensure_ascii=False,separators=(",",":")).encode("utf-8")
    if hashlib.sha256(fixture_bytes).hexdigest()!=baseline.get(
            "official_fixture_sha256"):
        print("INFRA: CommonMark official fixture provenance drift",
              file=sys.stderr)
        return 2
    by_id={}
    for row in rows:
        number=int(row["id"].rsplit("-",1)[-1])
        if row["id"]!=f"cm-0.31.2-{number:04d}" or number in by_id:
            print("INFRA: malformed/repeated official example",file=sys.stderr)
            return 2
        # The survey's declared match bit is not conformance evidence by
        # itself: recompute byte-exact HTML equality from the actual output.
        # This rejects silently forged flags and unsupported-as-pass errors.
        if (row.get("native_status") not in ("rendered","unsupported") or
            not isinstance(row.get("markdown"),str) or
            not isinstance(row.get("official_expected_html"),str) or
            type(row.get("native_matches_official")) is not bool):
            print(f"INFRA: malformed survey row #{number}",file=sys.stderr)
            return 2
        exact=(row["native_status"]=="rendered" and
               isinstance(row.get("native_html"),str) and
               row["native_html"]==row["official_expected_html"])
        if row["native_matches_official"]!=exact:
            print(f"INFRA: untruthful exact HTML result #{number}",
                  file=sys.stderr)
            return 2
        by_id[number]=row
    if set(by_id)!=set(range(1,653)):
        print("INFRA: survey example missing or out of range",file=sys.stderr)
        return 2
    regressions=[
        {"id":n,"status":by_id[n].get("native_status"),
         "first_diff":by_id[n].get("first_native_official_diff")}
        for n in ids
        if by_id[n].get("native_status")!="rendered" or
           by_id[n].get("native_matches_official") is not True
    ]
    additional=[n for n in range(1,653) if n not in ids and
                by_id[n].get("native_matches_official") is True]
    result={
        "baseline_total":len(ids),
        "baseline_passed":len(ids)-len(regressions),
        "regressions":regressions,
        "additional_official_matches":additional,
        "official_diagnostic_total":652
    }
    # Classifications are remediation owners, never alternate pass rules.
    # Persist the partition inside the existing CI artifact; no new workflow.
    try:
        ledger_path=Path(__file__).with_name("commonmark_failure_classes.json")
        ledger=json.loads(ledger_path.read_text(encoding="utf-8"))
        result["failure_classification"]=classify_failures(data,ledger)
    except (OSError,ValueError,KeyError,TypeError) as error:
        print(f"INFRA: failure classification: {error}",file=sys.stderr)
        return 2
    for category in result["failure_classification"]["categories"]:
        print(f"Unpassed {category['key']}: {category['remaining']}; "
              f"owner={category['owner']}")
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(f"Prior official HTML equalities: {result['baseline_passed']}/{len(ids)};"
          f" additional official matches: {len(additional)}")
    for item in regressions:
        print(f"REGRESSED #{item['id']}: {item['status']}:"
              f" {item['first_diff']}",file=sys.stderr)
    return 1 if regressions else 0

if __name__=="__main__":
    sys.exit(main())
