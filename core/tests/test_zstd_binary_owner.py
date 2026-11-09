"""Inspect the actual ELF owner: seven public adapters, one private archive."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--library", required=True, type=Path)
parser.add_argument("--nm", required=True)
parser.add_argument("--readelf", required=True)
arguments = parser.parse_args()
expected = {
    "zstd_compress_bound", "zstd_compress", "zstd_decompress",
    "zstd_frame_content_size", "zstd_default_level", "zstd_min_level",
    "zstd_max_level",
}
symbols = subprocess.check_output(
    [arguments.nm, "-D", "--defined-only", str(arguments.library)], text=True)
names = {line.split()[-1].split("@", 1)[0] for line in symbols.splitlines() if line.split()}
if {name for name in names if name.startswith("zstd_")} != expected:
    raise SystemExit("Core must export exactly the seven public Zstd adapters")
if any(name.startswith(("ZSTD", "ZDICT", "FSE_", "HUF_", "XXH")) for name in names):
    raise SystemExit("Private Zstd archive symbols escaped the Core owner")
dynamic = subprocess.check_output(
    [arguments.readelf, "-d", str(arguments.library)], text=True)
if "libzstd" in dynamic.lower():
    raise SystemExit("Core unexpectedly depends on a second shared Zstd provider")
print("PASS: seven public Core adapters; private static Zstd symbols and linkage")
