"""Bounded native Zstd restoration; source retention does not accept migration."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE_COMMIT = "386f81328c656f416d61a6d2ef662f05443e1b4a"
REGISTRY_COMMIT = "b8bcce436838b716522bb2c8b35438b47ac1830b"
FILES = {
    "utils/include/cmeta_zstd.h": "core/include/cmeta_zstd.h",
    "utils/src/cmeta_zstd.c": "core/src/cmeta_zstd.c",
    "utils/tests/test_cmeta_zstd.c": "core/tests/test_cmeta_zstd.c",
}


def tracked_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def verify_module(root: Path = ROOT) -> None:
    inventory = json.loads((root / "tools/migration/original-salts-files.json").read_text())
    if inventory["source"]["repo"] != "qigao/salts" or inventory["source"]["commit"] != SOURCE_COMMIT:
        raise ValueError("Zstd original source revision changed")
    records = {row["path"]: row for row in inventory["files"]}
    for original, destination in FILES.items():
        path = root / destination
        if not path.is_file():
            raise ValueError("Zstd original input missing: " + destination)
        data = tracked_bytes(path)
        if original.endswith(".h"):
            if data.count(b'#include "salts_api.h"') != 1:
                raise ValueError("Zstd native public linkage header changed")
            data = data.replace(b'#include "salts_api.h"', b"#include <platform.h>", 1)
        if len(data) != records[original]["size"] or git_blob(data) != records[original]["git_blob"]:
            raise ValueError("Zstd original bytes changed: " + destination)
    api = json.loads((root / "tools/migration/original-public-functions.json").read_text())
    row = next(row for row in api["headers"] if row["path"] == "utils/include/cmeta_zstd.h")
    if row["source_commit"] != SOURCE_COMMIT or row["unparsed_declarations"]:
        raise ValueError("Zstd public API source record changed")
    declared = set(re.findall(r"\b(cmeta_zstd_\w+)\s*\(",
                             tracked_bytes(root / FILES[row["path"]]).decode()))
    if len(row["explicit_functions"]) != 7 or declared != set(row["explicit_functions"]):
        raise ValueError("Zstd original seven-function public surface changed")


class OriginalZstdProvenance(unittest.TestCase):
    def test_all_original_sources_and_seven_public_functions(self) -> None:
        verify_module()

    def test_same_pinned_registry_and_explicit_feature(self) -> None:
        config = json.loads((ROOT / "vcpkg-configuration.json").read_text())
        selected = [row for row in config["registries"] if "zstd" in row.get("packages", [])]
        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0]["repository"], "https://github.com/qigao/vcpkg-cache")
        self.assertEqual(selected[0]["reference"], REGISTRY_COMMIT)
        self.assertEqual(selected[0]["baseline"], REGISTRY_COMMIT)
        manifest = json.loads((ROOT / "vcpkg.json").read_text())
        self.assertEqual(manifest["features"]["zstd"]["dependencies"], ["zstd"])

    def test_exact_provider_license_retained(self) -> None:
        # facebook/zstd@5c7b7bad26808e6b40ac3b3d0075466e27738a9d/LICENSE
        self.assertEqual(git_blob(tracked_bytes(ROOT / "core/licenses/zstd.LICENSE")),
                         "75800288cc243f164b9d130f125e2ffae28f8e39")

    def test_changed_source_and_missing_original_test_fail_closed(self) -> None:
        for relative in FILES.values():
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                for path in (*FILES.values(), "tools/migration/original-salts-files.json",
                             "tools/migration/original-public-functions.json"):
                    destination = root / path
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / path, destination)
                target = root / relative
                if relative.startswith("tests/"):
                    target.unlink()
                else:
                    target.write_bytes(target.read_bytes() + b"\n/* drift */\n")
                with self.assertRaises(ValueError):
                    verify_module(root)


if __name__ == "__main__":
    unittest.main()
