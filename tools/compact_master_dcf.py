#!/usr/bin/env python3
"""Compact dcfgen Master DCF arrays before dcf2c is used for an MCU target.

Lely dcfgen intentionally emits several CANopen Manager objects with
CompactSubObj=127/254. Lely's DCF loader expands every compact sub-object, and
co_dev_create_from_sdev() later allocates matching dynamic co_sub objects on the
target. This helper keeps the generated DCF semantics needed by the configured
network while reducing those expansion ranges for memory-constrained targets.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]\s*$")
KEY_VALUE_RE = re.compile(r"^(\s*)([^=]+?)(\s*=\s*)(.*?)(\r?\n)?$")
OBJECT_RE = re.compile(r"^[0-9A-Fa-f]{4}$")
VALUE_SECTION_RE = re.compile(r"^([0-9A-Fa-f]{4})Value$", re.IGNORECASE)
TPDO_RESERVED_SUB4_RE = re.compile(
    r"^([0-9A-Fa-f]{4})sub0?4$", re.IGNORECASE
)

# These compact arrays are indexed by CANopen node-ID. Keeping sub-indices up to
# the highest configured node preserves the access pattern used by NMT/boot
# services while avoiding dcfgen's unconditional 1..127 expansion for small
# networks such as the Master + Node1 example.
NODE_INDEXED_OBJECTS = {
    "1016",  # consumer heartbeat time
    "1028",  # EMCY consumer
    "1F25",  # configuration request
    "1F55",  # expected software identification
    "1F81",  # NMT slave assignment
    "1F82",  # request NMT
    "1F84",  # expected device type
    "1F85",  # expected vendor-ID
    "1F86",  # expected product code
    "1F87",  # expected revision number
    "1F88",  # expected serial number
    "1F8A",  # restore configuration
}


@dataclass
class Section:
    name: str
    start: int
    end: int


def parse_positive_int(text: str) -> int | None:
    value = text.strip()
    if re.fullmatch(r"[0-9]+", value):
        return int(value, 10)
    if re.fullmatch(r"0[xX][0-9A-Fa-f]+", value):
        return int(value, 16)
    return None


def collect_sections(lines: list[str]) -> list[Section]:
    starts: list[tuple[str, int]] = []
    for index, line in enumerate(lines):
        match = SECTION_RE.match(line.rstrip("\r\n"))
        if match:
            starts.append((match.group(1).strip(), index))

    sections: list[Section] = []
    for position, (name, start) in enumerate(starts):
        end = starts[position + 1][1] if position + 1 < len(starts) else len(lines)
        sections.append(Section(name=name, start=start, end=end))
    return sections


def section_values(lines: list[str], section: Section) -> dict[str, tuple[str, int]]:
    result: dict[str, tuple[str, int]] = {}
    for index in range(section.start + 1, section.end):
        match = KEY_VALUE_RE.match(lines[index])
        if not match:
            continue
        key = match.group(2).strip()
        value = match.group(4).strip()
        result[key.casefold()] = (value, index)
    return result


def numeric_value_keys(lines: list[str], section: Section) -> list[int]:
    keys: list[int] = []
    for index in range(section.start + 1, section.end):
        match = KEY_VALUE_RE.match(lines[index])
        if not match:
            continue
        key = match.group(2).strip()
        if key.casefold() == "nrofentries":
            continue
        parsed = parse_positive_int(key)
        if parsed is not None and parsed > 0:
            keys.append(parsed)
    return keys


def replace_value(line: str, new_value: int) -> str:
    match = KEY_VALUE_RE.match(line)
    if not match:
        raise ValueError(f"cannot rewrite DCF key/value line: {line!r}")
    newline = match.group(5) or ""
    return f"{match.group(1)}{match.group(2)}{match.group(3)}{new_value}{newline}"


def strip_reserved_tpdo_sub4(text: str) -> tuple[str, list[str]]:
    """Remove reserved TPDO communication sub-index 04h from a dcfgen DCF."""
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}
    removals: list[Section] = []
    removed_objects: list[str] = []

    for section in sections:
        match = TPDO_RESERVED_SUB4_RE.fullmatch(section.name)
        if not match:
            continue
        obj_index = int(match.group(1), 16)
        if obj_index < 0x1800 or obj_index > 0x19FF:
            continue

        parent = by_name.get(match.group(1).casefold())
        if parent is None:
            raise ValueError(
                f"TPDO communication sub-index without parent object: [{section.name}]"
            )
        values = section_values(lines, parent)
        subnumber = values.get("subnumber")
        if subnumber is None:
            raise ValueError(
                f"TPDO communication object 0x{obj_index:04X} has no SubNumber"
            )
        count = parse_positive_int(subnumber[0])
        if count is None or count <= 0:
            raise ValueError(
                f"invalid SubNumber for TPDO communication object 0x{obj_index:04X}"
            )
        lines[subnumber[1]] = replace_value(lines[subnumber[1]], count - 1)
        removals.append(section)
        removed_objects.append(f"{obj_index:04X}")

    for section in sorted(removals, key=lambda item: item.start, reverse=True):
        del lines[section.start:section.end]

    return "".join(lines), removed_objects


def estimate_subobjects(lines: list[str], sections: list[Section]) -> tuple[int, int]:
    objects = 0
    subobjects = 0
    for section in sections:
        if not OBJECT_RE.fullmatch(section.name):
            continue
        objects += 1
        values = section_values(lines, section)
        compact = values.get("compactsubobj")
        if compact:
            count = parse_positive_int(compact[0])
            if count is not None:
                subobjects += count + 1
                continue
        subnumber = values.get("subnumber")
        if subnumber:
            count = parse_positive_int(subnumber[0])
            if count is not None:
                subobjects += count
                continue
        # VAR-like objects have one sub-index 0.
        subobjects += 1
    return objects, subobjects


def compact_dcf(
    text: str,
    *,
    error_history_depth: int,
    max_subobjects: int,
) -> tuple[str, list[tuple[str, int, int]], list[str], int, int, int, int, int]:
    original_lines = text.splitlines(keepends=True)
    before_objects, before_subobjects = estimate_subobjects(
        original_lines, collect_sections(original_lines)
    )
    text, removed_reserved_tpdo = strip_reserved_tpdo_sub4(text)
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}

    node_assignment = by_name.get("1f81value")
    node_ids = numeric_value_keys(lines, node_assignment) if node_assignment else []
    max_node_id = max(node_ids, default=1)
    if max_node_id > 127:
        raise ValueError(f"invalid CANopen node-ID in [1F81Value]: {max_node_id}")

    explicit_value_keys: dict[str, list[int]] = {}
    for section in sections:
        match = VALUE_SECTION_RE.fullmatch(section.name)
        if not match:
            continue
        explicit_value_keys[match.group(1).upper()] = numeric_value_keys(lines, section)

    changes: list[tuple[str, int, int]] = []

    for section in sections:
        if not OBJECT_RE.fullmatch(section.name):
            continue
        obj_index = section.name.upper()
        values = section_values(lines, section)
        compact = values.get("compactsubobj")
        if not compact:
            continue

        original = parse_positive_int(compact[0])
        if original is None or original <= 0:
            continue

        target = original
        if obj_index == "1003":
            target = min(original, error_history_depth)
        elif obj_index in NODE_INDEXED_OBJECTS:
            target = min(original, max_node_id)
        else:
            keys = explicit_value_keys.get(obj_index, [])
            if keys:
                target = min(original, max(keys))

        target = max(1, target)
        if target < original:
            line_index = compact[1]
            lines[line_index] = replace_value(lines[line_index], target)
            changes.append((obj_index, original, target))

    after_objects, after_subobjects = estimate_subobjects(lines, collect_sections(lines))
    if max_subobjects > 0 and after_subobjects > max_subobjects:
        raise ValueError(
            "compacted Master DCF still expands to "
            f"{after_subobjects} sub-objects (limit {max_subobjects}); "
            "review the network Node-IDs/DCF policy or raise --max-subobjects explicitly"
        )

    return (
        "".join(lines),
        changes,
        removed_reserved_tpdo,
        max_node_id,
        before_objects,
        before_subobjects,
        after_objects,
        after_subobjects,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="dcfgen Master DCF")
    parser.add_argument("--output", required=True, type=Path, help="compacted DCF path")
    parser.add_argument(
        "--error-history-depth",
        type=int,
        default=8,
        help="maximum CompactSubObj depth kept for object 0x1003 (default: 8)",
    )
    parser.add_argument(
        "--max-subobjects",
        type=int,
        default=256,
        help="fail if estimated dynamic sub-object count still exceeds this value; 0 disables the guard",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.error_history_depth < 1 or args.error_history_depth > 254:
        print("error: --error-history-depth must be in range 1..254", file=sys.stderr)
        return 2
    if args.max_subobjects < 0:
        print("error: --max-subobjects must be >= 0", file=sys.stderr)
        return 2
    if not args.input.is_file():
        print(f"error: input DCF not found: {args.input}", file=sys.stderr)
        return 2

    try:
        with args.input.open("r", encoding="utf-8-sig", newline="") as stream:
            source = stream.read()
        result = compact_dcf(
            source,
            error_history_depth=args.error_history_depth,
            max_subobjects=args.max_subobjects,
        )
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    (
        output_text,
        changes,
        removed_reserved_tpdo,
        max_node_id,
        before_obj,
        before_sub,
        after_obj,
        after_sub,
    ) = result
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_name(args.output.name + ".tmp")
    try:
        with temp.open("w", encoding="utf-8", newline="") as stream:
            stream.write(output_text)
        temp.replace(args.output)
    except OSError as exc:
        try:
            temp.unlink(missing_ok=True)
        except OSError:
            pass
        print(f"error: unable to publish compacted DCF: {exc}", file=sys.stderr)
        return 1

    print(f"Master DCF nodes: highest configured remote node-ID = {max_node_id}")
    for index in removed_reserved_tpdo:
        print(f"  0x{index}: removed reserved TPDO communication sub-index 04h")
    for index, old, new in changes:
        print(f"  0x{index}: CompactSubObj {old} -> {new}")
    print(
        "Master DCF footprint estimate: "
        f"objects {before_obj}->{after_obj}, sub-objects {before_sub}->{after_sub}"
    )
    print(f"Compacted Master DCF: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
