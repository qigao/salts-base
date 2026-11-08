"""Negative regression for immutable Promela source ownership."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location(
    "promela_source_gate",ROOT/"tools/check_original_promela_sources.py")
module=importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(module)

class PromelaIntegrityTests(unittest.TestCase):
    def test_original_models_present_and_exact(self):
        self.assertEqual(module.problems(ROOT), [])
    def test_missing_sources_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertTrue(module.problems(Path(tmp)))
    def test_changed_model_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"verification"
            path.mkdir()
            (path/"ring_spsc.pml").write_text("active proctype init() { skip }\n")
            self.assertTrue(any("ring_spsc.pml" in e for e in module.problems(Path(tmp))))
if __name__ == "__main__":
    unittest.main()
