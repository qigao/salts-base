"""Retain all frozen Serial inputs and constrain production adaptation.

Private typed storage has executable ownership/failure tests. Presence of
source or replacement tests does not close the frozen global migration gate.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1]
ROOT = MODULE.parent
SOURCE_REPO = "qigao/salts-utils"
SOURCE_COMMIT = "5b0be65de673261fc37b29485b0364ca38cc6cd6"
ADAPTED = frozenset({
    "CMakeLists.txt", "examples/CMakeLists.txt", "serial_internal.h",
    "serial.c", "backend_posix.c", "backend_win32.c",
    "test/CMakeLists.txt", "test/test_serial_mock.c",
})
ORIGINAL_PATHS = frozenset({
    "backend_posix.c", "backend_win32.c", "CMakeLists.txt",
    "examples/await_events.c", "examples/CMakeLists.txt",
    "examples/handle_errors.c", "examples/list_ports.c", "examples/port_config.c",
    "examples/port_info.c", "examples/README", "examples/send_receive.c",
    "serial_internal.h", "serial.c", "serial.h",
    "test/CMakeLists.txt", "test/serial_test.h", "test/test_fixes.c",
    "test/test_serial_mock.c", "test/test_serial_public_abi.c",
    "test/test_serial.c",
})
CURRENT_RENAMES = {
    "serial.h": "include/serial.h",
    "serial_internal.h": "src/serial_internal.h",
    "serial.c": "src/serial.c",
    "backend_posix.c": "src/backend_posix.c",
    "backend_win32.c": "src/backend_win32.c",
    "test/serial_test.h": "test/serial_test.h",
}


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def restore_original_header_names(data: bytes) -> bytes:
    return (data
            .replace(b'"serial_internal.h"', b'"serial_internal.h"')
            .replace(b'"test/serial_test.h"', b'"test/serial_test.h"')
            .replace(b'"serial.h"', b'"serial.h"')
            .replace(b'"thread.h"', b'"salts/thread.h"')
            .replace(b'"clock.h"', b'"salts/clock.h"')
            .replace(b'`serial.h`', b'`serial.h`'))


def original_path(relative: str) -> Path:
    return Path("original") / (relative + ".original") if relative in ADAPTED else Path(relative)


def mechanical_native_source(original: str, relative: str) -> str:
    original = (original
                .replace('"serial_internal.h"', '"serial_internal.h"')
                .replace('"test/serial_test.h"', '"serial_test.h"')
                .replace('"salts/thread.h"', '"thread.h"')
                .replace('"salts/clock.h"', '"clock.h"'))
    if relative == "serial.c":
        result = original.replace('#include <core/platform.h>\n', "").replace('#include <cstl.h>\n', "")
        result = result.replace(
            "result = serial_result_from_stl(\n      serial_port_info_vec_t_init(&list->items));",
            "result = serial_port_info_vec_t_init(&list->items);")
        return result.replace(
            "result = serial_result_from_stl(\n      serial_port_info_vec_t_push(&list->items, storage));",
            "result = serial_port_info_vec_t_push(&list->items, storage);")
    result = original.replace(
        "stl_status status = serial_port_info_vec_t_push(vec, storage);",
        "serial_result_t status = serial_port_info_vec_t_push(vec, storage);")
    result = result.replace("if (status != STL_OK)", "if (status != SALTS_SERIAL_OK)")
    result = result.replace("return serial_result_from_stl(status);", "return status;")
    if relative == "backend_win32.c":
        result = result.replace("serial_port_info_vec_t vec;", "serial_port_info_vec_t vec = {0};")
        result = result.replace(
            "res = serial_result_from_stl(serial_port_info_vec_t_init(&vec));",
            "res = serial_port_info_vec_t_init(&vec);")
    return result


def verify_retention(module: Path, inventory: dict) -> None:
    if inventory["source"]["repo"] != SOURCE_REPO or inventory["source"]["commit"] != SOURCE_COMMIT:
        raise ValueError("Serial donor revision changed")
    rows = {row["path"].removeprefix("serial/"): row
            for row in inventory["files"] if row["path"].startswith("serial/")}
    if set(rows) != ORIGINAL_PATHS:
        raise ValueError("Serial original file coverage changed")
    for relative, row in rows.items():
        path = module / (original_path(relative) if relative in ADAPTED
                         else CURRENT_RENAMES.get(relative, relative))
        if not path.is_file():
            raise ValueError(f"Serial original file missing: {relative}")
        # Compare Git blob bytes independently of a Windows checkout's EOLs.
        data = path.read_bytes().replace(b"\r\n", b"\n")
        if relative not in ADAPTED:
            data = restore_original_header_names(data)
        if len(data) != row["size"] or git_blob(data) != row["git_blob"]:
            raise ValueError(f"Serial original bytes changed: {relative}")
    for relative in ("serial.c", "backend_posix.c", "backend_win32.c"):
        expected = mechanical_native_source((module / original_path(relative)).read_text(), relative)
        if (module / CURRENT_RENAMES.get(relative, relative)).read_text() != expected:
            raise ValueError(f"Serial non-mechanical behavior change: {relative}")


class SerialProvenance(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.inventory = json.loads((ROOT / "tools/migration/original-salts-utils-files.json").read_text())

    def test_all_twenty_original_files_and_native_adaptations(self) -> None:
        verify_retention(MODULE, self.inventory)

    def test_all_thirty_public_declarations_and_definitions_remain(self) -> None:
        inventory = json.loads((ROOT / "tools/migration/original-public-functions.json").read_text())
        matching = [row for row in inventory["headers"]
                    if row["source_repo"] == SOURCE_REPO and row["path"] == "serial/serial.h"]
        self.assertEqual(len(matching), 1)
        row = matching[0]
        self.assertEqual(row["source_commit"], SOURCE_COMMIT)
        self.assertEqual(row["unparsed_declarations"], [])
        expected = set(row["explicit_functions"])
        self.assertEqual(len(expected), 30)
        header = (MODULE / "include/serial.h").read_text()
        declared = set(re.findall(r"\b(serial_\w+)\s*\(", header))
        self.assertEqual(declared, expected)
        implementation = (MODULE / "src/serial.c").read_text()
        defined = set(re.findall(
            r"(?m)^(?:void|int|size_t|serial_result_t|const char \*|const serial_port_info_t \*)\s*(serial_\w+)\s*\(", implementation))
        self.assertTrue(expected <= defined)

    def test_original_string_ownership_operations_are_unchanged(self) -> None:
        original = (MODULE / original_path("serial_internal.h")).read_text()
        native = (MODULE / "src/serial_internal.h").read_text()
        begin = "static inline void serial_port_info_storage_refresh_view"
        old_operations = original.split(begin, 1)[1].split("static const type_traits", 1)[0]
        native_operations = native.split(begin, 1)[1].split("/* One module-private owned array", 1)[0]
        self.assertEqual(native_operations, old_operations)

    def test_all_original_injected_backend_scenarios_remain(self) -> None:
        relative = "test/test_serial_mock.c"
        original = (MODULE / original_path(relative)).read_text()
        native = (MODULE / relative).read_text()
        scenarios = re.findall(r'\bit\("([^"\n]+)"\)', original)
        self.assertEqual(len(scenarios), 7)  # Six portable plus Windows UTF-8.
        self.assertEqual(re.findall(r'\bit\("([^"\n]+)"\)', native), scenarios)
        self.assertNotIn("tinymock_function.h", native)
        self.assertNotIn("FunctionDeclResult", native)
        for obligation in ("read_probe.calls >= 1u", "write_probe.calls >= 1u",
                           "read_probe.first_handle == FAKE_HANDLE",
                           "write_probe.first_handle == FAKE_HANDLE",
                           "create_worker_before_return", "reject_thread_create",
                           "fail_fourth_storage_clone"):
            self.assertIn(obligation, native)

    def test_source_change_and_missing_original_test_fail_closed(self) -> None:
        for relative in ("include/serial.h", "src/backend_posix.c", "test/test_serial.c"):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temporary:
                candidate = Path(temporary) / "serial"
                shutil.copytree(MODULE, candidate, ignore=shutil.ignore_patterns("__pycache__"))
                target = candidate / relative
                if relative.endswith("test_serial.c"):
                    target.unlink()
                else:
                    target.write_bytes(target.read_bytes() + b"\n/* drift */\n")
                with self.assertRaises(ValueError):
                    verify_retention(candidate, self.inventory)


if __name__ == "__main__":
    unittest.main()
