#!/usr/bin/env python3
"""Self-contained negative tests for SPEC-policy immutable CommonMark gate."""
import copy
import hashlib
import json
from pathlib import Path
import unittest

from gate_commonmark_spec_nonregression import InvalidEvidence, evaluate


class SpecGateTests(unittest.TestCase):
    def setUp(self):
        rows = [{"id": f"cm-0.31.2-{n:04d}", "markdown": f"case {n}\n",
                 "official_expected_html": f"<p>{n}</p>\n",
                 "native_status": "rendered", "native_html": f"<p>{n}</p>\n",
                 "native_matches_official": True}
                for n in range(1, 653)]
        fixture = [(r["id"], r["markdown"], r["official_expected_html"])
                   for r in rows]
        digest = hashlib.sha256(json.dumps(
            fixture, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")).hexdigest()
        self.baseline = {"render_policy": "SpecConformanceHtml",
                         "commonmark_version": "0.31.2", "source": "tools/md/test/spec.txt",
                         "official_total": 652, "official_fixture_sha256": digest,
                         "qualified_official_ids": list(range(1, 653))}
        self.survey = {"render_policy": "SpecConformanceHtml",
                       "commonmark_version": "0.31.2", "source": "tools/md/test/spec.txt",
                       "total": 652, "native_matches_official": 652,
                       "native_unsupported": 0, "native_differences": 0, "cases": rows}

    def test_retains_all_652_with_no_unqualified_matches(self):
        before = copy.deepcopy((self.survey, self.baseline))
        result = evaluate(self.survey, self.baseline)
        self.assertEqual(result["baseline_passed"], 652)
        self.assertEqual(result["additional_official_matches"], [])
        self.assertEqual(result["regressions"], [])
        self.assertEqual((self.survey, self.baseline), before)

    def test_real_baseline_keeps_safe_policy_distinct(self):
        root = Path(__file__).resolve().parent
        spec = json.loads((root / "commonmark_0_31_2_spec_baseline.json")
                          .read_text(encoding="utf-8"))
        safe = json.loads((root / "commonmark_0_31_2_baseline.json")
                          .read_text(encoding="utf-8"))
        all_ids = spec["qualified_official_ids"]
        safe_ids = safe["qualified_official_ids"]
        self.assertEqual(all_ids, list(range(1, 653)))
        self.assertEqual(len(safe_ids), 576)
        self.assertEqual(spec["qualified_spec_only_ids"],
                         sorted(set(all_ids) - set(safe_ids)))
        self.assertEqual(len(spec["qualified_spec_only_ids"]), 76)
        self.assertTrue({206, 354, 540}.issubset(set(safe_ids)))
        self.assertEqual(spec["promotion_evidence"]["promoted_official_ids"],
                         [206, 354, 540])
        self.assertEqual(safe["promotion_evidence"]["promoted_official_ids"],
                         [206, 354, 540])

    def test_promoted_unicode_ids_are_strictly_gated(self):
        for n in (206, 354, 540):
            with self.subTest(official_id=n):
                survey = copy.deepcopy(self.survey)
                row = survey["cases"][n - 1]
                row["native_html"] = "invalid"
                row["native_matches_official"] = False
                survey.update(native_matches_official=651,
                              native_differences=1)
                outcome = evaluate(survey, self.baseline)
                self.assertEqual(outcome["baseline_passed"], 651)
                self.assertEqual([r["id"] for r in outcome["regressions"]],
                                 [n])

    def test_one_real_baseline_regression_reported(self):
        row = self.survey["cases"][9]
        row["native_html"] = "incorrect"
        row["native_matches_official"] = False
        self.survey.update(native_matches_official=651, native_differences=1)
        result = evaluate(self.survey, self.baseline)
        self.assertEqual(result["baseline_passed"], 651)
        self.assertEqual([r["id"] for r in result["regressions"]], [10])

    def test_safe_policy_cannot_pass_spec_gate(self):
        self.survey.pop("render_policy")
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_baseline_policy_cannot_change(self):
        self.baseline["render_policy"] = "SafeApplicationHtml"
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_forged_match_bit_rejected(self):
        self.survey["cases"][0]["native_html"] = "wrong"
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_unsupported_is_never_pass(self):
        row = self.survey["cases"][0]
        row["native_status"] = "unsupported"
        row["native_html"] = None
        row["native_matches_official"] = False
        self.survey.update(native_matches_official=651, native_unsupported=1)
        result = evaluate(self.survey, self.baseline)
        self.assertEqual(result["baseline_passed"], 651)
        self.assertEqual(result["native_unsupported"], 1)
        row["native_matches_official"] = True
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_missing_and_duplicate_ids_rejected(self):
        self.survey["cases"][1]["id"] = self.survey["cases"][0]["id"]
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_fixture_mutation_rejected(self):
        self.survey["cases"][0]["markdown"] += "modified"
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_unqualified_id_promotion_rejected(self):
        self.baseline["qualified_official_ids"] = list(range(1, 652))
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)

    def test_untruthful_aggregate_rejected(self):
        self.survey["native_matches_official"] = 651
        with self.assertRaises(InvalidEvidence):
            evaluate(self.survey, self.baseline)


if __name__ == "__main__":
    unittest.main()
