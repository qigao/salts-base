"""Require every original regex entrypoint and all 25 old TinyTest cases.

This is source/coverage admission, not a replacement for the original tests
or a claim that the native SIMD throughput matches the old SIMDe build.
"""
from pathlib import Path
import json
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = (
    "re_limits_default", "re_validate_n", "re_compile_n", "re_destroy",
    "re_matchn", "re_match_n", "re_status_string", "re_compile",
    "re_matchp", "re_match",
)
ORIGINAL_BLOBS = {
    "utils/include/re.h": "2b99ec662352677b21aceddfc13dea6d624df15f",
    "utils/src/re.c": "33468540fe02a2adcc7fd7a5303eb990d1521a31",
    "utils/src/re_scan.h": "616b381ecae509b54b9f96f90c1309a189462981",
    "utils/tests/test_re.c": "a4fef94edb32f4225c001550a6d15f86bb455225",
}
CASES = [
  "matches inverted character classes",
  "matches branches without leaking group scope",
  "matches quantified groups and backtracks for a suffix",
  "returns zero-length matches",
  "matches byte slices containing NUL",
  "keeps separately compiled patterns independent",
  "rejects malformed patterns without returning a handle",
  "reports pattern and text limits separately",
  "stops when the recursion depth budget is exhausted",
  "stops when the execution step budget is exhausted",
  "stops before allocating beyond the workspace budget",
  "rejects malformed limit structures",
  "returns stable diagnostics",
  "matches mandatory literal prefixes at the correct offset",
  "probes past failed candidates before a real match",
  "keeps branches and quantified first atoms working",
  "finds first bytes identically with native SIMD and scalar scans",
  "matches non-capturing groups without retaining captures",
  "matches word boundaries",
  "matches interval quantifiers",
  "keeps braces without a valid interval as literals",
  "rejects malformed interval quantifiers",
  "matches lazy quantifiers",
  "matches positive and negative lookahead without consuming",
  "rejects unsupported group prefixes"
]


def check_core(owner: Path = ROOT) -> list[str]:
    errors = []
    required = [
        "core/include/re.h", "core/src/re.c", "core/src/re_scan.h",
        "core/CMakeLists.txt", "core/tests/test_re.c", "core/tests/test_re_smoke.c",
        "core/tests/CMakeLists.txt",
    ]
    contents = {}
    for rel in required:
        p = owner / rel
        if not p.is_file():
            errors.append("Original regex source/test missing: " + rel)
        else:
            contents[rel] = p.read_text(encoding="utf-8")
    if errors:
        return errors
    h = contents["core/include/re.h"]
    s = contents["core/src/re.c"]
    tests = contents["core/tests/test_re.c"]
    scan = contents["core/src/re_scan.h"]
    cmake = contents["core/CMakeLists.txt"]
    suite = contents["core/tests/CMakeLists.txt"]
    for symbol in PUBLIC:
        pattern = r"\b" + re.escape(symbol) + r"\s*\("
        if not re.search(pattern, h):
            errors.append("Original regex public API missing: " + symbol)
        if not re.search(pattern, s):
            errors.append("Original regex engine function missing: " + symbol)
    found = re.findall(r'\bit\("([^"]+)"\)', tests)
    if found != CASES:
        errors.append("Original 25 regex TinyTest cases changed/missing")
    if cmake.count("src/re.c") != 1 or cmake.count("include/re.h") != 1:
        errors.append("Core must compile and expose ONE original regex owner")
    for marker in ("saltscore.original_re_smoke",
                   "saltscore.original_re_25_case_contract",
                   "Salts::Core", "Salts::TinyTest"):
        if marker not in suite:
            errors.append("Original regex CTest coverage missing: " + marker)
    if "re_scan_first_byte_native" not in s or "re_scan_first_byte_native" not in tests:
        errors.append("Real original regex test must use the Core-native SIMD helper")
    for marker in ("re_scan_first_byte_scalar", "SALTS_RE_NATIVE_SSE2",
                   "SALTS_RE_NATIVE_NEON"):
        if marker not in scan:
            errors.append("Native regex 16-byte scan implementation missing: " + marker)
    for rel in ("core/include/re.h", "core/src/re.c", "core/src/re_scan.h"):
        contents2 = contents[rel]
        if "#include <cmeta/" in contents2 or "#include <cflow/" in contents2 or                 "#include <simde/" in contents2 or '#include "platform.h"' in contents2:
            errors.append("Core regex unexpectedly depends on old CMeta/CFlow/SIMDe: " + rel)
    if len(s) < 30000:
        errors.append("Original bounded regex matcher body was truncated")
    return errors


class OriginalRegexSourceContract(unittest.TestCase):
    def test_exact_four_donor_blob_identifiers_unchanged(self):
        manifest = json.loads(
            (ROOT / "tools/migration/original-salts-files.json").read_text())
        entries = {x["path"]: x["git_blob"] for x in manifest["files"]}
        for path, blob in ORIGINAL_BLOBS.items():
            with self.subTest(path=path):
                self.assertEqual(entries[path], blob)
        self.assertEqual(len(CASES), 25)
        self.assertEqual(len(PUBLIC), 10)

    def test_real_core_source_and_all_25_original_cases(self):
        self.assertEqual(check_core(), [])

    def test_deleted_original_case_is_never_quietly_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp)
            for relative in (
                "core/include/re.h", "core/src/re.c", "core/src/re_scan.h",
                "core/CMakeLists.txt", "core/tests/test_re.c", "core/tests/test_re_smoke.c",
                "core/tests/CMakeLists.txt",
            ):
                target = src / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / relative).read_bytes())
            bad = src / "core/tests/test_re.c"
            source = bad.read_text()
            bad.write_text(source.replace('it("matches inverted character classes")',
                                          'it("deleted original regex assertion")', 1))
            self.assertTrue(check_core(src))
            bad.write_text(source)
            self.assertEqual(check_core(src), [])

    def test_public_symbol_deletion_or_second_engine_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)
            files = ["core/include/re.h", "core/src/re.c", "core/src/re_scan.h",
                     "core/CMakeLists.txt", "core/tests/test_re.c",
                     "core/tests/test_re_smoke.c", "core/tests/CMakeLists.txt",]
            for rel in files:
                to = src / rel
                to.parent.mkdir(parents=True, exist_ok=True)
                to.write_bytes((ROOT / rel).read_bytes())
            header = src / "core/include/re.h"
            original = header.read_text()
            header.write_text(original.replace("re_compile_n(", "renamed_compile_n(", 1))
            self.assertTrue(check_core(src))
            header.write_text(original)
            scan = src / "core/src/re_scan.h"
            original_scan = scan.read_text()
            scan.write_text(original_scan + '\n#include <simde/x86/sse2.h>\n')
            self.assertTrue(check_core(src))


if __name__ == "__main__":
    unittest.main()
