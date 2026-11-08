#!/usr/bin/env python3
"""Generate Unicode 17 line-break property and auxiliary interval tables."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

LB_VALUES = {
    name: f"SALTS_UNICODE_LINE_BREAK_{name}"
    for name in (
        "OP CL CP QU GL NS EX SY IS PR PO NU AL HL ID IN HY BA BB B2 ZW CM "
        "WJ H2 H3 JL JV JT RI EB EM ZWJ AK AP AS VF VI HH CB AI BK CJ CR LF "
        "NL SA SG SP XX"
    ).split()
}


def parse_range(text: str) -> tuple[int, int]:
    parts = text.strip().split("..")
    first = int(parts[0], 16)
    last = int(parts[1], 16) if len(parts) == 2 else first
    if first > last or last > 0x10FFFF:
        raise ValueError(f"invalid Unicode range: {text}")
    return first, last


def merge_plain(entries: list[tuple[int, int]]) -> list[tuple[int, int]]:
    entries.sort()
    out: list[tuple[int, int]] = []
    for first, last in entries:
        if out and first <= out[-1][1] + 1:
            if last > out[-1][1]:
                out[-1] = (out[-1][0], last)
        else:
            out.append((first, last))
    return out


def merge_values(entries: list[tuple[int, int, str]]) -> list[tuple[int, int, str]]:
    entries.sort()
    out: list[tuple[int, int, str]] = []
    for first, last, value in entries:
        if out and first <= out[-1][1]:
            raise ValueError(f"overlapping Unicode ranges at U+{first:04X}")
        if out and value == out[-1][2] and first == out[-1][1] + 1:
            out[-1] = (out[-1][0], last, value)
        else:
            out.append((first, last, value))
    return out


def read(path: Path, marker: str) -> str:
    text = path.read_text(encoding="utf-8")
    if marker not in text[:4096]:
        raise ValueError(f"{path}: expected marker {marker!r}")
    return text


def records(text: str):
    for raw in text.splitlines():
        payload = raw.split("#", 1)[0].strip()
        if not payload or payload.startswith("@"):
            continue
        yield [part.strip() for part in payload.split(";")]


def parse_line_break(text: str):
    out = []
    for parts in records(text):
        if len(parts) < 2 or parts[1] not in LB_VALUES:
            raise ValueError(f"unknown/malformed Line_Break record: {parts}")
        first, last = parse_range(parts[0])
        out.append((first, last, LB_VALUES[parts[1]]))
    return merge_values(out)


def parse_east_asian(text: str):
    out = []
    for parts in records(text):
        if len(parts) >= 2 and parts[1] in {"F", "W", "H"}:
            out.append(parse_range(parts[0]))
    return merge_plain(out)


def parse_emoji(text: str):
    out = []
    for parts in records(text):
        if len(parts) >= 2 and parts[1] == "Extended_Pictographic":
            out.append(parse_range(parts[0]))
    return merge_plain(out)


def parse_unicode_data(text: str):
    assigned = []
    pi = []
    pf = []
    mn_mc = []
    pending = None
    for raw in text.splitlines():
        if not raw:
            continue
        fields = raw.split(";")
        cp = int(fields[0], 16)
        name = fields[1]
        gc = fields[2]
        if name.endswith(", First>"):
            pending = (cp, gc)
            continue
        if name.endswith(", Last>"):
            if pending is None or pending[1] != gc:
                raise ValueError("malformed UnicodeData First/Last range")
            first = pending[0]
            pending = None
            span = (first, cp)
        else:
            span = (cp, cp)
        assigned.append(span)
        if gc == "Pi":
            pi.append(span)
        if gc == "Pf":
            pf.append(span)
        if gc in {"Mn", "Mc"}:
            mn_mc.append(span)
    if pending is not None:
        raise ValueError("unterminated UnicodeData range")
    return (
        merge_plain(assigned),
        merge_plain(pi),
        merge_plain(pf),
        merge_plain(mn_mc),
    )


def subtract_ranges(source, removed):
    out = []
    j = 0
    for first, last in source:
        cursor = first
        while j < len(removed) and removed[j][1] < cursor:
            j += 1
        k = j
        while k < len(removed) and removed[k][0] <= last:
            rfirst, rlast = removed[k]
            if rfirst > cursor:
                out.append((cursor, min(last, rfirst - 1)))
            if rlast >= cursor:
                cursor = rlast + 1
            if cursor > last:
                break
            k += 1
        if cursor <= last:
            out.append((cursor, last))
    return out


def git_blob_sha(text: str) -> str:
    data = text.encode("utf-8")
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def emit_bool_array(name, entries):
    lines = [f"static const salts_unicode_line_break_bool_range {name}[] = {{"]
    for first, last in entries:
        lines.append(f"  {{0x{first:06X}u, 0x{last:06X}u}},")
    lines += [
        "};",
        f"#define {name.upper()}_COUNT (sizeof({name}) / sizeof({name}[0]))",
    ]
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--line-break", type=Path, required=True)
    parser.add_argument("--unicode-data", type=Path, required=True)
    parser.add_argument("--east-asian-width", type=Path, required=True)
    parser.add_argument("--emoji-data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    lb_text = read(args.line_break, "LineBreak-17.0.0.txt")
    ud_text = read(args.unicode_data, "LATIN CAPITAL LETTER A")
    eaw_text = read(args.east_asian_width, "EastAsianWidth-17.0.0.txt")
    emoji_text = read(args.emoji_data, "Version: 17.0")

    lb = parse_line_break(lb_text)
    east_asian = parse_east_asian(eaw_text)
    extended_pictographic = parse_emoji(emoji_text)
    assigned, pi, pf, mn_mc = parse_unicode_data(ud_text)
    potential_emoji = subtract_ranges(extended_pictographic, assigned)

    lines = [
        "/* Generated by unicode/tools/generate_line_break_data.py. DO NOT EDIT. */",
        "/* Unicode 17.0.0 / UAX #14 revision 55. */",
        "/* Sources:",
        f" * LineBreak.txt git blob {git_blob_sha(lb_text)}",
        f" * UnicodeData.txt git blob {git_blob_sha(ud_text)}",
        f" * EastAsianWidth.txt git blob {git_blob_sha(eaw_text)}",
        f" * emoji-data.txt git blob {git_blob_sha(emoji_text)}",
        " * Normative algorithm: UAX #14 revision 55.",
        " */",
        "#ifndef SALTS_UNICODE_LINE_BREAK_DATA_H",
        "#define SALTS_UNICODE_LINE_BREAK_DATA_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "typedef struct salts_unicode_line_break_range {",
        "  uint32_t first;",
        "  uint32_t last;",
        "  uint8_t value;",
        "} salts_unicode_line_break_range;",
        "",
        "typedef struct salts_unicode_line_break_bool_range {",
        "  uint32_t first;",
        "  uint32_t last;",
        "} salts_unicode_line_break_bool_range;",
        "",
        "static const salts_unicode_line_break_range salts_unicode_line_break_ranges[] = {",
    ]
    for first, last, value in lb:
        lines.append(f"  {{0x{first:06X}u, 0x{last:06X}u, {value}}},")
    lines += [
        "};",
        "#define SALTS_UNICODE_LINE_BREAK_RANGES_COUNT \\",
        "  (sizeof(salts_unicode_line_break_ranges) / sizeof(salts_unicode_line_break_ranges[0]))",
        "",
    ]
    for name, entries in (
        ("salts_unicode_line_break_pi_ranges", pi),
        ("salts_unicode_line_break_pf_ranges", pf),
        ("salts_unicode_line_break_mn_mc_ranges", mn_mc),
        ("salts_unicode_line_break_east_asian_ranges", east_asian),
        ("salts_unicode_line_break_potential_emoji_ranges", potential_emoji),
    ):
        lines += emit_bool_array(name, entries)
        lines.append("")
    lines += ["#endif", ""]
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
