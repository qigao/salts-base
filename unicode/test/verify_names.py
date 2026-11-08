"""Check a built Unicode library against every official name and alias (offline)."""
import argparse
import ctypes
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class View(ctypes.Structure):
    _fields_ = [("data", ctypes.c_char_p), ("len", ctypes.c_size_t)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--dll-directory", action="append", default=[])
    args = parser.parse_args()
    directories = [args.library.resolve().parent, *map(Path, args.dll_directory)]
    handles = [os.add_dll_directory(str(path.resolve())) for path in directories] if os.name == "nt" else []
    library = ctypes.CDLL(str(args.library.resolve()))
    lookup = library.salts_unicode_name_lookup
    lookup.argtypes = [View, ctypes.POINTER(ctypes.c_uint32)]
    lookup.restype = ctypes.c_int
    checked = 0
    for filename in ("DerivedName.txt", "NameAliases.txt"):
        for line in (ROOT / "data" / "17.0.0" / filename).read_text(encoding="utf-8").splitlines():
            line = line.partition("#")[0].strip()
            if not line:
                continue
            points, name, *_ = [field.strip() for field in line.split(";")]
            bounds = [int(point, 16) for point in points.split("..")]
            for scalar in range(bounds[0], bounds[-1] + 1):
                canonical = name.replace("*", f"{scalar:04X}")
                for candidate in (canonical, canonical.lower()):
                    strict_case = canonical.startswith(("HANGUL SYLLABLE ", "CJK UNIFIED IDEOGRAPH-"))
                    expected_ok = candidate == canonical or not strict_case
                    encoded = candidate.encode("ascii")
                    output = ctypes.c_uint32(0xFFFFFFFF)
                    status = lookup(View(encoded, len(encoded)), ctypes.byref(output))
                    if expected_ok:
                        assert status == 0 and output.value == scalar, (candidate, status, output.value, scalar)
                    else:
                        assert status == 2 and output.value == 0xFFFFFFFF, (candidate, status, output.value)
                    checked += 1
    print(f"Verified {checked} canonical/lowercase name and alias lookups against Unicode 17.0.0")
    for handle in handles:
        handle.close()


if __name__ == "__main__":
    main()
