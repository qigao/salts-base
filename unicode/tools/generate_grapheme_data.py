#!/usr/bin/env python3
"""Generate Unicode 17 grapheme property interval tables.

Inputs are the Unicode 17.0.0 GraphemeBreakProperty.txt, emoji-data.txt and
DerivedCoreProperties.txt files. The generated header is deterministic and
contains no runtime allocation or mutable state.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

VERSION = "17.0"

GCB_VALUES = {
    "CR": "SALTS_UNICODE_GRAPHEME_CR",
    "LF": "SALTS_UNICODE_GRAPHEME_LF",
    "Control": "SALTS_UNICODE_GRAPHEME_CONTROL",
    "Extend": "SALTS_UNICODE_GRAPHEME_EXTEND",
    "ZWJ": "SALTS_UNICODE_GRAPHEME_ZWJ",
    "Regional_Indicator": "SALTS_UNICODE_GRAPHEME_REGIONAL_INDICATOR",
    "Prepend": "SALTS_UNICODE_GRAPHEME_PREPEND",
    "SpacingMark": "SALTS_UNICODE_GRAPHEME_SPACING_MARK",
    "L": "SALTS_UNICODE_GRAPHEME_L",
    "V": "SALTS_UNICODE_GRAPHEME_V",
    "T": "SALTS_UNICODE_GRAPHEME_T",
    "LV": "SALTS_UNICODE_GRAPHEME_LV",
    "LVT": "SALTS_UNICODE_GRAPHEME_LVT",
}
INCB_VALUES = {
    "Consonant": "SALTS_UNICODE_INCB_CONSONANT",
    "Extend": "SALTS_UNICODE_INCB_EXTEND",
    "Linker": "SALTS_UNICODE_INCB_LINKER",
}


def parse_range(text: str) -> tuple[int, int]:
    parts = text.strip().split("..")
    first = int(parts[0], 16)
    last = int(parts[1], 16) if len(parts) == 2 else first
    if first > last or last > 0x10FFFF:
        raise ValueError(f"invalid Unicode range: {text}")
    return first, last


def merge(entries: list[tuple[int, int, str]]) -> list[tuple[int, int, str]]:
    entries.sort()
    result: list[tuple[int, int, str]] = []
    for first, last, value in entries:
        if result and first <= result[-1][1]:
            raise ValueError(f"overlapping Unicode ranges at U+{first:04X}")
        if result and value == result[-1][2] and first == result[-1][1] + 1:
            prev = result[-1]
            result[-1] = (prev[0], last, value)
        else:
            result.append((first, last, value))
    return result


def read(path: Path, version_marker: str) -> str:
    data = path.read_text(encoding="utf-8")
    if version_marker not in data[:2048]:
        raise ValueError(f"{path}: expected Unicode version marker {version_marker!r}")
    return data


def records(text: str):
    for raw in text.splitlines():
        payload = raw.split("#", 1)[0].strip()
        if not payload or payload.startswith("@"):
            continue
        yield [part.strip() for part in payload.split(";")]


def parse_gcb(text: str):
    out = []
    for parts in records(text):
        if len(parts) < 2:
            raise ValueError(f"malformed GraphemeBreakProperty record: {parts}")
        prop = parts[1]
        if prop not in GCB_VALUES:
            raise ValueError(f"unknown Grapheme_Cluster_Break value: {prop}")
        first, last = parse_range(parts[0])
        out.append((first, last, GCB_VALUES[prop]))
    return merge(out)


def parse_ep(text: str):
    out = []
    for parts in records(text):
        if len(parts) < 2:
            raise ValueError(f"malformed emoji-data record: {parts}")
        if parts[1] != "Extended_Pictographic":
            continue
        first, last = parse_range(parts[0])
        out.append((first, last, "1"))
    if not out:
        raise ValueError("emoji-data contains no Extended_Pictographic values")
    return merge(out)


def parse_incb(text: str):
    out = []
    for parts in records(text):
        if len(parts) < 2 or parts[1] != "InCB":
            continue
        if len(parts) < 3 or parts[2] not in INCB_VALUES:
            raise ValueError(f"unknown/malformed Indic_Conjunct_Break record: {parts}")
        first, last = parse_range(parts[0])
        out.append((first, last, INCB_VALUES[parts[2]]))
    if not out:
        raise ValueError("DerivedCoreProperties contains no InCB values")
    return merge(out)


def git_blob_sha(text: str) -> str:
    data = text.encode("utf-8")
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def emit_array(name: str, entries: list[tuple[int, int, str]]) -> list[str]:
    lines = [f"static const salts_unicode_grapheme_range {name}[] = {{"]
    for first, last, value in entries:
        lines.append(f"  {{0x{first:06X}u, 0x{last:06X}u, {value}}},")
    lines.append("};")
    lines.append(f"#define {name.upper()}_COUNT (sizeof({name}) / sizeof({name}[0]))")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--grapheme-break", type=Path, required=True)
    parser.add_argument("--emoji-data", type=Path, required=True)
    parser.add_argument("--derived-core", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    gcb_text = read(args.grapheme_break, "GraphemeBreakProperty-17.0.0.txt")
    emoji_text = read(args.emoji_data, "Version: 17.0")
    dcp_text = read(args.derived_core, "DerivedCoreProperties-17.0.0.txt")

    gcb = parse_gcb(gcb_text)
    ep = parse_ep(emoji_text)
    incb = parse_incb(dcp_text)

    lines = [
        "/* Generated by unicode/tools/generate_grapheme_data.py. DO NOT EDIT. */",
        "/* Unicode 17.0.0 / UAX #29 revision 47. */",
        "/* Sources:",
        f" * GraphemeBreakProperty.txt git blob {git_blob_sha(gcb_text)}",
        f" * emoji-data.txt git blob {git_blob_sha(emoji_text)}",
        f" * DerivedCoreProperties.txt git blob {git_blob_sha(dcp_text)}",
        " * Normative algorithm: UAX #29 revision 47.",
        " */",
        "#ifndef SALTS_UNICODE_GRAPHEME_DATA_H",
        "#define SALTS_UNICODE_GRAPHEME_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "typedef struct salts_unicode_grapheme_range {",
        "  uint32_t first;",
        "  uint32_t last;",
        "  uint8_t value;",
        "} salts_unicode_grapheme_range;",
        "",
        "enum {",
        "  SALTS_UNICODE_INCB_NONE = 0,",
        "  SALTS_UNICODE_INCB_CONSONANT = 1,",
        "  SALTS_UNICODE_INCB_EXTEND = 2,",
        "  SALTS_UNICODE_INCB_LINKER = 3",
        "};",
        "",
    ]
    lines += emit_array("salts_unicode_gcb_ranges", gcb)
    lines.append("")
    lines += emit_array("salts_unicode_extended_pictographic_ranges", ep)
    lines.append("")
    lines += emit_array("salts_unicode_incb_ranges", incb)
    lines += ["", "#endif", ""]

    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
