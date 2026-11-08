"""Bounded restoration of the original error owner, not semantic acceptance.

Each allowed change is literal and reversible. Reversing only those changes
must reproduce the three frozen upstream blobs, including every native test
assertion. Runtime tests qualify behavior separately; reflection stays pending.
"""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = "tools/migration/original-error-inventory.json"
INVENTORY_BLOB = "a8fa92068d5a5b0912066bff0605edf8836411f8"
BASELINES = {
    "tools/migration/original-salts-files.json": "05bc04582efc1e000b35f76288ee3b35fbe45e06",
    "tools/migration/original-salts-utils-files.json": "f70913c46ba4596b40d336d171024d4c4762cf3e",
    "tools/migration/original-public-functions.json": "19db69a508c6049afcfe6922e19448f5f9c5d875",
}


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def tracked_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def frozen_json(root: Path, relative: str, expected: str) -> dict:
    raw = tracked_bytes(root / relative)
    if git_blob(raw) != expected:
        raise ValueError("frozen error provenance or original ledger modified: " + relative)
    return json.loads(raw)


def public_declarations(text: str) -> list[dict[str, str]]:
    return [{"name": match[1], "declaration_tokens": " ".join(
        re.findall(r"[A-Za-z_]\w*|\d+|[^\s]", match[0]))}
        for match in re.finditer(
            r"^SALTS_C_API\s+[^;{}]+\b(cmeta_(?:strerror|error_\w+))\s*\([^;{}]*\)\s*;",
            text, re.M)]


def verify_original_inventory(root: Path = ROOT) -> dict:
    inventory = frozen_json(root, INVENTORY, INVENTORY_BLOB)
    salts, utils, api = [frozen_json(root, name, blob) for name, blob in BASELINES.items()]
    if (len(salts["files"]) + len(utils["files"]) != 2975 or
            sum(len(row["explicit_functions"]) for row in api["headers"]) != 610):
        raise ValueError("original 2975-file / 610-function obligation changed")
    original_files = {row["path"]: row for row in salts["files"]}
    if len(inventory["files"]) != 3:
        raise ValueError("original error header, implementation or tests lost")
    for row in inventory["files"]:
        original = original_files.get(row["path"], {})
        if original.get("git_blob") != row["git_blob"] or original.get("size") != row["bytes"]:
            raise ValueError("original error source blob lost: " + row["path"])
    original_api = next(row for row in api["headers"] if row["path"] == inventory["public_api"]["header"])
    names = sorted(row["name"] for row in inventory["public_api"]["functions"])
    if (names != original_api["explicit_functions"] or len(names) != 4 or
            inventory["public_api"]["frozen_610_functions_accepted"] != 0):
        raise ValueError("original error four-function obligation changed or falsely accepted")
    reflection = inventory["upper_layer_reflection"]
    original_test = next(row for row in inventory["files"] if row["destination"] == "core/tests/test_cmeta_error.c")
    if (reflection["status"] != "pending-upstream-reflection-acceptance" or
            reflection["original_block"] != original_test["changes"][0]["original"] or
            git_blob(reflection["original_block"].encode()) != reflection["original_block_git_blob"] or
            inventory["acceptance"]["full_migration_complete"] or
            inventory["acceptance"]["original_file_obligations_accepted"] or
            inventory["acceptance"]["original_public_api_obligations_accepted"]):
        raise ValueError("original error reflection or full migration falsely accepted")
    return inventory


def verify_module(root: Path = ROOT) -> None:
    inventory = verify_original_inventory(root)
    header = tracked_bytes(root / "core/include/cmeta_error.h").decode()
    if public_declarations(header) != inventory["public_api"]["functions"]:
        raise ValueError("original error public declaration missing or changed")
    for row in inventory["files"]:
        native = tracked_bytes(root / row["destination"]).decode()
        restored = native
        for change in reversed(row["changes"]):
            if restored.count(change["native"]) != 1:
                raise ValueError("bounded error transformation missing or changed: " + row["destination"])
            restored = restored.replace(change["native"], change["original"], 1)
        if git_blob(restored.encode()) != row["git_blob"]:
            raise ValueError("original error source or native assertion changed: " + row["destination"])
        if row["destination"] == "core/tests/test_cmeta_error.c":
            cases = re.findall(r'\bit\("([^"]+)"\)', native)
            if cases != inventory["native_original_cases"]:
                raise ValueError("original error native case registration missing")


@contextmanager
def isolated_copy():
    inventory = verify_original_inventory()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        paths = [INVENTORY, *BASELINES] + [row["destination"] for row in inventory["files"]]
        for relative in paths:
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        yield root


class OriginalErrorProvenanceTests(unittest.TestCase):
    def test_original_files_and_four_declarations_remain_in_the_frozen_baseline(self):
        inventory = verify_original_inventory()
        self.assertEqual(len(inventory["native_original_cases"]), 9)
        self.assertEqual(inventory["upper_layer_reflection"]["assertion_count"], 7)
        self.assertEqual(inventory["acceptance"]["original_public_api_obligations_accepted"], 0)

    def test_allowed_source_changes_reconstruct_all_three_original_blobs(self):
        verify_module()

    def test_removed_or_retyped_public_api_is_rejected(self):
        for mutation in ("deleted", "retyped"):
            with self.subTest(mutation=mutation), isolated_copy() as root:
                path = root / "core/include/cmeta_error.h"
                text = path.read_text()
                original = "SALTS_C_API cmeta_error_info_t cmeta_error_info(int err);"
                replacement = "" if mutation == "deleted" else original.replace("int err", "long err")
                self.assertIn(original, text)
                path.write_text(text.replace(original, replacement, 1))
                with self.assertRaisesRegex(ValueError, "public declaration"):
                    verify_module(root)

    def test_registry_output_and_overflow_fix_cannot_be_silently_rewritten(self):
        for old, new in (
                ("#define SALTS_ERROR_MAX_CUSTOM_DOMAINS 64", "#define SALTS_ERROR_MAX_CUSTOM_DOMAINS 63"),
                ("info.message = entry->message;", 'info.message = "copied message";'),
                ("return SALTS_ENOSPC;", "return SALTS_OK;"),
                ("if (err == INT_MIN)", "if (err == INT_MAX)")):
            with self.subTest(mutation=old), isolated_copy() as root:
                path = root / "core/src/cmeta_error.c"
                text = path.read_text()
                self.assertIn(old, text)
                path.write_text(text.replace(old, new, 1))
                with self.assertRaisesRegex(ValueError, "transformation|source or native assertion"):
                    verify_module(root)

    def test_original_native_assertion_or_case_cannot_disappear(self):
        for old in (
                '      check_equal(cmeta_error_unregister_domain(TEST_ERROR_DOMAIN), SALTS_OK);\n',
                '    it("describes negative errno values") {',
                '      check_equal(domain, SALTS_ERROR_DOMAIN_POSIX);\n'):
            with self.subTest(mutation=old), isolated_copy() as root:
                path = root / "core/tests/test_cmeta_error.c"
                text = path.read_text()
                self.assertIn(old, text)
                path.write_text(text.replace(old, "", 1))
                with self.assertRaisesRegex(ValueError, "source or native assertion"):
                    verify_module(root)

    def test_reflection_cannot_be_faked_or_marked_accepted(self):
        with isolated_copy() as root:
            path = root / "core/include/cmeta_error.h"
            path.write_text(path.read_text() + "\n#include <cmeta/enum.h>\n")
            with self.assertRaisesRegex(ValueError, "source or native assertion"):
                verify_module(root)
        for field in ("status", "original_block"):
            with self.subTest(field=field), isolated_copy() as root:
                path = root / INVENTORY
                data = json.loads(path.read_text())
                data["upper_layer_reflection"][field] = "accepted" if field == "status" else ""
                path.write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError, "frozen error provenance"):
                    verify_module(root)

    def test_a_dropped_original_ledger_obligation_is_rejected(self):
        for relative in BASELINES:
            with self.subTest(baseline=relative), isolated_copy() as root:
                path = root / relative
                data = json.loads(path.read_text())
                data["headers" if "public-functions" in relative else "files"].pop()
                path.write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError, "original ledger modified"):
                    verify_module(root)


if __name__ == "__main__":
    unittest.main()
