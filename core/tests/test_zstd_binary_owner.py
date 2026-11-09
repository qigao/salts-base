"""Inspect the actual ELF owner: seven public adapters, one private archive."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--library", required=True, type=Path)
parser.add_argument("--nm", required=True)
parser.add_argument("--readelf", required=True)
arguments = parser.parse_args()
root = Path(__file__).resolve().parents[2]
api = json.loads((root / "tools/migration/original-public-functions.json").read_text())
expected = set(next(row for row in api["headers"]
                    if row["path"] == "utils/include/salts/zstd.h")["explicit_functions"])
symbols = subprocess.check_output(
    [arguments.nm, "-D", "--defined-only", str(arguments.library)], text=True)
names = {line.split()[-1].split("@", 1)[0] for line in symbols.splitlines() if line.split()}
if {name for name in names if name.startswith("cmeta_zstd_")} != expected:
    raise SystemExit("Core must export exactly the seven original Zstd adapters")
if any(name.startswith(("ZSTD", "ZDICT", "FSE_", "HUF_", "XXH")) for name in names):
    raise SystemExit("Private Zstd archive symbols escaped the Core owner")
dynamic = subprocess.check_output(
    [arguments.readelf, "-d", str(arguments.library)], text=True)
if "libzstd" in dynamic.lower():
    raise SystemExit("Core unexpectedly depends on a second shared Zstd provider")
print("PASS: seven original Core adapters; private static Zstd symbols and linkage")
