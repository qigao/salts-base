#!/usr/bin/env python3
"""Integrity tests for failure classification, not a second Markdown parser."""
import copy
import unittest
from classify_commonmark_failures import (
    ClassificationError, classify_failures, fixture_digest,
)


class ClassificationTests(unittest.TestCase):
    def setUp(self):
        # Synthetic records test integrity; actual conformance uses spec.txt.
        rows = [{"id": f"cm-0.31.2-{n:04d}", "markdown": f"input {n}\n",
                 "official_expected_html": f"<p>{n}</p>\n",
                 "native_status": "rendered", "native_html": f"<p>{n}</p>\n",
                 "native_matches_official": True} for n in range(1, 653)]
        rows[0]["native_html"] = "<p>escaped policy output</p>\n"
        rows[0]["native_matches_official"] = False
        self.survey = {"source": "tools/md/test/spec.txt",
                       "commonmark_version": "0.31.2", "total": 652,
                       "native_matches_official": 651, "native_differences": 1,
                       "native_unsupported": 0, "cases": rows}
        self.ledger = {"schema_version": 1,
                       "source": "tools/md/test/spec.txt",
                       "commonmark_version": "0.31.2",
                       "reference_head": "0" * 40,
                       "official_fixture_sha256": fixture_digest(rows),
                       "reference_unpassed_total": 1,
                       "policy_exemptions": False,
                       "categories": [{"key": "raw_html", "owner": "test#1",
                                       "ids": [1]}]}

    def run_report(self):
        return classify_failures(self.survey, self.ledger)

    def test_policy_difference_is_not_a_pass(self):
        result = self.run_report()
        self.assertEqual(result["official_exact"], 651)
        self.assertEqual(result["unpassed_total"], 1)
        self.assertEqual(result["categories"][0]["ids"], [1])
        self.assertIs(result["policy_exemptions"], False)

    def test_genuine_resolution_keeps_history(self):
        row = self.survey["cases"][0]
        row["native_html"] = row["official_expected_html"]
        row["native_matches_official"] = True
        self.survey.update(native_matches_official=652, native_differences=0)
        result = self.run_report()
        self.assertEqual(result["resolved_since_reference"], [1])
        self.assertEqual(result["categories"][0]["remaining"], 0)

    def test_inputs_are_never_rewritten(self):
        before = copy.deepcopy((self.survey, self.ledger))
        self.run_report()
        self.assertEqual((self.survey, self.ledger), before)

    def test_missing_row_rejected(self):
        self.survey["cases"].pop()
        with self.assertRaises(ClassificationError): self.run_report()

    def test_duplicate_id_rejected(self):
        self.survey["cases"][1]["id"] = self.survey["cases"][0]["id"]
        with self.assertRaises(ClassificationError): self.run_report()

    def test_forged_pass_bit_rejected(self):
        self.survey["cases"][0]["native_matches_official"] = True
        with self.assertRaises(ClassificationError): self.run_report()

    def test_unsupported_output_never_passes(self):
        row = self.survey["cases"][0]
        row.update(native_status="unsupported", native_html=None)
        self.survey.update(native_differences=0, native_unsupported=1)
        result = self.run_report()
        self.assertEqual(result["native_unsupported"], 1)
        self.assertEqual(result["official_exact"], 651)
        row["native_matches_official"] = True
        with self.assertRaises(ClassificationError): self.run_report()

    def test_unsupported_cannot_supply_html(self):
        self.survey["cases"][0]["native_status"] = "unsupported"
        with self.assertRaises(ClassificationError): self.run_report()

    def test_corpus_change_rejected(self):
        self.survey["cases"][0]["markdown"] += "modified fixture"
        with self.assertRaises(ClassificationError): self.run_report()

    def test_category_overlap_rejected(self):
        self.ledger["categories"].append({"key": "images", "owner": "test#2",
                                           "ids": [1]})
        with self.assertRaises(ClassificationError): self.run_report()

    def test_duplicate_category_rejected(self):
        self.ledger["categories"].append({"key": "raw_html", "owner": "test#2",
                                           "ids": [2]})
        with self.assertRaises(ClassificationError): self.run_report()

    def test_unreviewed_regression_rejected(self):
        self.survey["cases"][1].update(native_html="wrong",
                                       native_matches_official=False)
        self.survey.update(native_matches_official=650, native_differences=2)
        with self.assertRaisesRegex(ClassificationError, "unclassified.*2"):
            self.run_report()

    def test_false_aggregate_count_rejected(self):
        self.survey["native_matches_official"] = 652
        with self.assertRaises(ClassificationError): self.run_report()

    def test_policy_exemptions_rejected(self):
        self.ledger["policy_exemptions"] = True
        with self.assertRaises(ClassificationError): self.run_report()

    def test_reviewed_denominator_rejected(self):
        self.ledger["reference_unpassed_total"] = 2
        with self.assertRaises(ClassificationError): self.run_report()

    def test_boolean_id_is_not_integer_id(self):
        self.ledger["categories"][0]["ids"] = [True]
        with self.assertRaises(ClassificationError): self.run_report()


if __name__ == "__main__":
    unittest.main()
