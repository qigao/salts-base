#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

ORIGINAL = {
    "utils/include/platform.h": "a60fb0c660b9700d697b3462c17b8c26f96e10c5",
    "utils/src/platform.c": "472a0ca73ff5b0183f335e1c2b10298c53c2e3c3",
    "utils/include/cmeta_thread.h": "a72d39fd9125cd0180371b53b85d38d2702eeed9",
    "utils/src/cmeta_thread.c": "508c6ef29d730f61219083bab9cfcfef3e7a85d0",
    "utils/tests/test_platform.c": "0bb8954d6b5e78f0df160a8b7919fe684c6efc3a",
    "utils/tests/test_native_timer.c": "283fa3ee44f7c45322aaf3992cd670f80387ffab",
    "utils/tests/test_platform_header_cpp.cpp": "3220cbadbb738350b6ea8cbd5cc8b77513f92cf0",
    "utils/tests/test_platform_build_state_contract.c": "e40095744908e5df43f9a4916275de3d79b7ad12",
}

ORIGINAL_COPIES = {
    "utils/include/platform.h": "platform/original/include/platform.h.original",
    "utils/src/platform.c": "platform/original/src/platform.c.original",
    "utils/include/cmeta_thread.h": "platform/original/include/cmeta_thread.h.original",
    "utils/src/cmeta_thread.c": "platform/original/src/cmeta_thread.c.original",
    "utils/tests/test_platform.c": "platform/original/tests/test_platform.c.original",
    "utils/tests/test_native_timer.c": "platform/original/tests/test_native_timer.c.original",
    "utils/tests/test_platform_header_cpp.cpp":
        "platform/original/tests/test_platform_header_cpp.cpp.original",
    "utils/tests/test_platform_build_state_contract.c":
        "platform/original/tests/test_platform_build_state_contract.c.original",
}

PLATFORM_API = [
    "cmeta_gettimeofday",
    "cmeta_gmtime",
    "cmeta_localtime",
    "cmeta_mktime",
    "cmeta_platform_arch",
    "cmeta_platform_cpu_info",
    "cmeta_platform_hostname",
    "cmeta_platform_load_average",
    "cmeta_platform_memory_info",
    "cmeta_platform_network_interfaces",
    "cmeta_platform_os_name",
    "cmeta_platform_os_version",
    "cmeta_platform_username",
    "cmeta_secure_random",
    "cmeta_strftime_local",
    "cmeta_strftime_utc",
    "cmeta_timegm",
    "cmeta_timer_create",
    "cmeta_timer_destroy",
    "cmeta_timer_get_data",
    "cmeta_timer_get_repeat",
    "cmeta_timer_set_data",
    "cmeta_timer_start",
    "cmeta_timer_stop",
]

THREAD_API = [
    "cmeta_getpid",
    "cmeta_sync_is_single_threaded",
    "cmeta_sync_set_single_threaded",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def git_blob(data: bytes) -> str:
    return hashlib.sha1(
        b"blob " + str(len(data)).encode("ascii") + b"\0" + data
    ).hexdigest()


def declared_names(text: str) -> set[str]:
    return set(re.findall(r"\b(cmeta_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*;", text))


def defined_names(text: str) -> set[str]:
    return set(re.findall(
        r"(?m)^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s+|\*\s*)+"
        r"(cmeta_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*\{",
        text))


def main() -> int:
    frozen = json.loads(
        (ROOT / "tools/migration/original-salts-files.json").read_text())
    rows = {row["path"]: row for row in frozen["files"]}
    for path, blob in ORIGINAL.items():
        row = rows.get(path)
        require(row is not None, f"frozen Platform donor path vanished: {path}")
        require(row["git_blob"] == blob,
                f"frozen Platform donor blob changed: {path}")
        copy = ROOT / ORIGINAL_COPIES[path]
        require(copy.is_file(), f"frozen Platform donor copy missing: {path}")
        require(git_blob(copy.read_bytes()) == blob,
                f"frozen Platform donor copy changed: {path}")

    public = json.loads(
        (ROOT / "tools/migration/original-public-functions.json").read_text())
    headers = {row["path"]: row for row in public["headers"]}
    require(headers["utils/include/platform.h"]["explicit_functions"] ==
            PLATFORM_API,
            "frozen 24-function Platform public inventory changed")
    require(headers["utils/include/cmeta_thread.h"]["explicit_functions"] ==
            THREAD_API,
            "frozen 3-function cmeta_thread public inventory changed")

    header = (ROOT / "platform/include/platform.h").read_text()
    thread_header = (ROOT / "platform/include/thread.h").read_text()
    source = (ROOT / "platform/src/platform.c").read_text()
    thread_source = (ROOT / "platform/src/thread_policy.c").read_text()
    cmake = (ROOT / "platform/CMakeLists.txt").read_text()
    tests = (ROOT / "platform/tests/test_platform.c").read_text()
    tests_cmake = (ROOT / "platform/tests/CMakeLists.txt").read_text()

    require(set(PLATFORM_API).issubset(declared_names(header)),
            "restored Platform public declarations are incomplete")
    require(set(PLATFORM_API).issubset(defined_names(source)),
            "restored Platform native definitions are incomplete")
    require(set(THREAD_API).issubset(declared_names(thread_header)),
            "restored cmeta_thread declarations are incomplete")
    require(set(THREAD_API).issubset(defined_names(thread_source)),
            "restored cmeta_thread definitions are incomplete")

    require("src/platform.c" in cmake and "src/thread_policy.c" in cmake,
            "restored Platform sources are not compiled by Salts::Platform")
    require("iphlpapi" in cmake and "ws2_32" in cmake,
            "Windows Platform system dependencies are incomplete")
    require("SALTS_C_API" not in header,
            "restored Platform header reintroduced DLL decoration")
    for token in ("SALTS_WIN32", "_SSIZE_T_DEFINED", "UNUSED", "likely", "unlikely"):
        require(token in header, "restored Platform public macro missing: " + token)
    require("#include <clock.h>" in header and
            "#include <random.h>" in header,
            "restored Platform umbrella clock/random includes are incomplete")
    require("TLOG_" not in source and '#include "tlog.h"' not in source,
            "Platform reintroduced reverse TLog/Core dependency")
    require('#include "cmeta_' not in source and '#include "platform.h"' not in source,
            "Platform reintroduced old CMeta/umbrella dependency")

    # Preserve the original timer behavioral obligations rather than merely
    # checking that the public names link.
    for needle in (
        "repeating_timer_cb",
        "oneshot_timer_cb",
        "destroy_race_timer_cb",
        "for (int i = 0; i < 256; ++i)",
        "cmeta_timer_start(timer, repeating_timer_cb, 100u, 100u)",
    ):
        require(needle in tests, "original native timer test obligation missing: " + needle)

    require("saltscore.original_platform_cpp17" in tests_cmake,
            "native C++17 Platform contract is not registered")
    print("PASS: frozen Platform/thread donor identities and 24+3 native owner surface retained")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
