#!/usr/bin/env python3
"""Restore compact-object DCF defaults in dcf2c-generated static SDEV C.

The bundled Windows dcf2c can copy a CompactSubObj ParameterValue into both
``.def`` and ``.val``. For a compact DCF object that also declares a parent
``DefaultValue``, this changes reset/default semantics. This helper restores the
numeric parent default in each generated compact sub-object while leaving the
current value untouched.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys

SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]\s*$")
KEY_VALUE_RE = re.compile(r"^\s*([^=]+?)\s*=\s*(.*?)\s*$")
OBJECT_INDEX_RE = re.compile(r"^\s*\.idx\s*=\s*0x([0-9A-Fa-f]{4}),\s*$")
SUBINDEX_RE = re.compile(r"^\s*\.subidx\s*=\s*0x([0-9A-Fa-f]{2}),\s*$")
TYPE_RE = re.compile(r"^\s*\.type\s*=\s*(CO_DEFTYPE_[A-Z0-9_]+),\s*$")
DEFAULT_RE = re.compile(
    r"^(\s*\.def\s*=\s*\{\s*\.([A-Za-z0-9_]+)\s*=\s*)(.*?)(\s*\},\s*)(\r?\n)?$"
)

TYPE_INFO = {
    0x0005: ("CO_DEFTYPE_UNSIGNED8", "u8", 2, ""),
    0x0006: ("CO_DEFTYPE_UNSIGNED16", "u16", 4, ""),
    0x0007: ("CO_DEFTYPE_UNSIGNED32", "u32", 8, "lu"),
}


@dataclass(frozen=True)
class CompactDefault:
    index: int
    count: int
    data_type: int
    value: int


def parse_int(text: str) -> int | None:
    value = text.strip()
    try:
        if re.fullmatch(r"0[xX][0-9A-Fa-f]+", value):
            return int(value, 16)
        if re.fullmatch(r"[0-9]+", value):
            return int(value, 10)
    except ValueError:
        return None
    return None


def parse_compact_defaults(text: str) -> list[CompactDefault]:
    sections: dict[str, dict[str, str]] = {}
    current: str | None = None
    for raw_line in text.splitlines():
        section_match = SECTION_RE.match(raw_line)
        if section_match:
            current = section_match.group(1).strip()
            sections[current.casefold()] = {}
            continue
        if current is None:
            continue
        key_value = KEY_VALUE_RE.match(raw_line)
        if key_value:
            sections[current.casefold()][key_value.group(1).strip().casefold()] = (
                key_value.group(2).strip()
            )

    result: list[CompactDefault] = []
    for name, values in sections.items():
        if not re.fullmatch(r"[0-9a-f]{4}", name):
            continue
        compact_text = values.get("compactsubobj")
        default_text = values.get("defaultvalue")
        data_type_text = values.get("datatype")
        if compact_text is None or default_text is None or data_type_text is None:
            continue

        count = parse_int(compact_text)
        default = parse_int(default_text)
        data_type = parse_int(data_type_text)
        if count is None or count <= 0:
            raise ValueError(f"object 0x{name.upper()} has invalid CompactSubObj")
        if default is None:
            raise ValueError(
                f"object 0x{name.upper()} has non-numeric DefaultValue={default_text!r}; "
                "cannot safely normalize generated defaults"
            )
        if data_type not in TYPE_INFO:
            raise ValueError(
                f"object 0x{name.upper()} uses unsupported compact DefaultValue "
                f"DataType={data_type_text}"
            )
        result.append(
            CompactDefault(
                index=int(name, 16), count=count, data_type=data_type, value=default
            )
        )
    return result


def format_default(data_type: int, value: int) -> str:
    _, _, width, suffix = TYPE_INFO[data_type]
    limit = (1 << (width * 4)) - 1
    if value < 0 or value > limit:
        raise ValueError(
            f"DefaultValue 0x{value:X} does not fit DataType 0x{data_type:04X}"
        )
    return f"0x{value:0{width}X}{suffix}"


def normalize_c_text(dcf_text: str, c_text: str) -> tuple[str, list[str]]:
    compact_defaults = parse_compact_defaults(dcf_text)
    if not compact_defaults:
        return c_text, []

    lines = c_text.splitlines(keepends=True)
    object_lines: dict[int, int] = {}
    for line_index, line in enumerate(lines):
        match = OBJECT_INDEX_RE.match(line.rstrip("\r\n"))
        if match:
            object_lines[int(match.group(1), 16)] = line_index

    ordered_objects = sorted((line, idx) for idx, line in object_lines.items())
    object_end: dict[int, int] = {}
    for position, (line, idx) in enumerate(ordered_objects):
        object_end[idx] = (
            ordered_objects[position + 1][0]
            if position + 1 < len(ordered_objects)
            else len(lines)
        )

    changes: list[str] = []
    for item in compact_defaults:
        start = object_lines.get(item.index)
        if start is None:
            raise ValueError(
                f"generated SDEV is missing compact DCF object 0x{item.index:04X}"
            )
        end = object_end[item.index]
        expected_type, expected_field, _, _ = TYPE_INFO[item.data_type]
        expected_value = format_default(item.data_type, item.value)

        sub_lines: dict[int, int] = {}
        for line_index in range(start, end):
            match = SUBINDEX_RE.match(lines[line_index].rstrip("\r\n"))
            if match:
                sub_lines[int(match.group(1), 16)] = line_index

        for subidx in range(1, item.count + 1):
            sub_start = sub_lines.get(subidx)
            if sub_start is None:
                raise ValueError(
                    f"generated SDEV object 0x{item.index:04X} is missing sub-index "
                    f"0x{subidx:02X}"
                )
            later = [line for idx, line in sub_lines.items() if idx > subidx]
            sub_end = min(later) if later else end

            actual_type = None
            default_line = None
            default_match = None
            for line_index in range(sub_start, sub_end):
                stripped = lines[line_index].rstrip("\r\n")
                type_match = TYPE_RE.match(stripped)
                if type_match:
                    actual_type = type_match.group(1)
                match = DEFAULT_RE.match(lines[line_index])
                if match:
                    default_line = line_index
                    default_match = match
                    break

            if actual_type != expected_type:
                raise ValueError(
                    f"generated SDEV object 0x{item.index:04X}:{subidx:02X} "
                    f"type {actual_type!r} does not match DCF {expected_type}"
                )
            if default_line is None or default_match is None:
                raise ValueError(
                    f"generated SDEV object 0x{item.index:04X}:{subidx:02X} "
                    "has no .def initializer"
                )
            if default_match.group(2) != expected_field:
                raise ValueError(
                    f"generated SDEV object 0x{item.index:04X}:{subidx:02X} uses "
                    f".{default_match.group(2)} default field, expected .{expected_field}"
                )

            current_value = default_match.group(3).strip()
            if current_value == expected_value:
                continue
            newline = default_match.group(5) or ""
            lines[default_line] = (
                f"{default_match.group(1)}{expected_value}{default_match.group(4)}{newline}"
            )
            changes.append(
                f"0x{item.index:04X}:{subidx:02X} {current_value} -> {expected_value}"
            )

    return "".join(lines), changes


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dcf", required=True, type=Path, help="compacted DCF input")
    parser.add_argument("--c", required=True, type=Path, help="generated co_sdev C file")
    parser.add_argument(
        "--check",
        action="store_true",
        help="report mismatches without rewriting the generated C file",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if not args.dcf.is_file() or not args.c.is_file():
        print("error: --dcf and --c must name existing files", file=sys.stderr)
        return 2

    try:
        dcf_text = args.dcf.read_bytes().decode("utf-8-sig")
        c_text = args.c.read_bytes().decode("ascii")
        normalized, changes = normalize_c_text(dcf_text, c_text)
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.check:
        if changes:
            for change in changes:
                print(f"mismatch: {change}", file=sys.stderr)
            return 1
        print("SDEV compact defaults: consistent")
        return 0

    if normalized != c_text:
        temp = args.c.with_name(args.c.name + ".tmp")
        try:
            temp.write_bytes(normalized.encode("ascii"))
            temp.replace(args.c)
        except OSError as exc:
            try:
                temp.unlink(missing_ok=True)
            except OSError:
                pass
            print(f"error: unable to publish normalized SDEV: {exc}", file=sys.stderr)
            return 1

    for change in changes:
        print(f"SDEV default normalized: {change}")
    if not changes:
        print("SDEV compact defaults: already consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
