#!/usr/bin/env python3
"""Compact dcfgen Master DCF arrays before dcf2c is used for an MCU target.

Lely dcfgen intentionally emits several CANopen Manager objects with
CompactSubObj=127/254. Lely's DCF loader expands every compact sub-object, and
co_dev_create_from_sdev() later allocates matching dynamic co_sub objects on the
target. This helper keeps the generated DCF semantics needed by the configured
network while reducing those expansion ranges for memory-constrained targets.

dcfgen may additionally emit master.bin with writes that initialize the static
Master object dictionary (for example 0x1F87/0x1F88 identity expectations).
Those writes are materialized into the compact DCF because this MCU profile has
no runtime master.bin loader. Slave concise-DCF files remain separate and are
not embedded implicitly by this helper.
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


@dataclass(frozen=True)
class ConciseDcfEntry:
    index: int
    subidx: int
    data: bytes


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


def configured_max_node_id(text: str) -> int:
    """Return the highest remote node-ID represented by [1F81Value]."""
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}
    node_assignment = by_name.get("1f81value")
    node_ids = numeric_value_keys(lines, node_assignment) if node_assignment else []
    max_node_id = max(node_ids, default=1)
    if max_node_id > 127:
        raise ValueError(f"invalid CANopen node-ID in [1F81Value]: {max_node_id}")
    return max_node_id


def replace_value(line: str, new_value: int) -> str:
    match = KEY_VALUE_RE.match(line)
    if not match:
        raise ValueError(f"cannot rewrite DCF key/value line: {line!r}")
    newline = match.group(5) or ""
    return f"{match.group(1)}{match.group(2)}{match.group(3)}{new_value}{newline}"


def parse_concise_dcf(data: bytes, source: Path) -> list[ConciseDcfEntry]:
    """Parse the concise DCF layout emitted by dcfgen for master.bin."""
    if len(data) < 4:
        raise ValueError(f"concise DCF is shorter than its 4-byte entry count: {source}")

    count = int.from_bytes(data[0:4], "little")
    entries: list[ConciseDcfEntry] = []
    offset = 4
    for entry in range(count):
        if len(data) - offset < 7:
            raise ValueError(
                f"concise DCF entry {entry + 1}/{count} has a truncated header: {source}"
            )
        index = int.from_bytes(data[offset : offset + 2], "little")
        subidx = data[offset + 2]
        size = int.from_bytes(data[offset + 3 : offset + 7], "little")
        offset += 7
        if len(data) - offset < size:
            raise ValueError(
                f"concise DCF entry {entry + 1}/{count} has {len(data) - offset} "
                f"data bytes but declares {size}: {source}"
            )
        entries.append(
            ConciseDcfEntry(
                index=index, subidx=subidx, data=data[offset : offset + size]
            )
        )
        offset += size

    if offset != len(data):
        raise ValueError(
            f"concise DCF has {len(data) - offset} trailing bytes after {count} entries: {source}"
        )
    return entries


def _newline_for(text: str) -> str:
    return "\r\n" if "\r\n" in text else "\n"


def _format_u32(data: bytes, source: Path, index: int, subidx: int) -> str:
    if len(data) != 4:
        raise ValueError(
            f"master.bin entry 0x{index:04X}:{subidx:02X} must be 4 bytes, "
            f"got {len(data)}: {source}"
        )
    return f"0x{int.from_bytes(data, 'little'):08X}"


def _require_unsigned32_target(
    text: str, section_name: str, index: int, subidx: int
) -> None:
    lines = text.splitlines(keepends=True)
    by_name = {section.name.casefold(): section for section in collect_sections(lines)}
    section = by_name.get(section_name.casefold())
    if section is None:
        raise ValueError(f"master.bin targets missing DCF section [{section_name}]")

    data_type = section_values(lines, section).get("datatype")
    parsed = parse_positive_int(data_type[0]) if data_type is not None else None
    if parsed == 0x0007:
        return

    actual = data_type[0] if data_type is not None else "<missing>"
    raise ValueError(
        f"master.bin target 0x{index:04X}:{subidx:02X} must use UNSIGNED32 "
        f"(DataType=0x0007), got {actual} in [{section_name}]"
    )


def _replace_key_value(line: str, new_value: str) -> str:
    match = KEY_VALUE_RE.match(line)
    if not match:
        raise ValueError(f"cannot rewrite DCF key/value line: {line!r}")
    newline = match.group(5) or ""
    return (
        f"{match.group(1)}{match.group(2)}{match.group(3)}"
        f"{new_value}{newline}"
    )


def _upsert_section_value(text: str, section_name: str, key: str, value: str) -> str:
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}
    section = by_name.get(section_name.casefold())
    if section is None:
        raise ValueError(f"master.bin targets missing DCF section [{section_name}]")
    values = section_values(lines, section)
    existing = values.get(key.casefold())
    if existing is not None:
        lines[existing[1]] = _replace_key_value(lines[existing[1]], value)
        return "".join(lines)

    newline = _newline_for(text)
    insert_at = section.end
    while insert_at > section.start + 1 and not lines[insert_at - 1].strip():
        insert_at -= 1
    lines.insert(insert_at, f"{key}={value}{newline}")
    return "".join(lines)


def _upsert_compact_value(text: str, index: int, subidx: int, value: str) -> str:
    section_name = f"{index:04X}Value"
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}
    section = by_name.get(section_name.casefold())
    newline = _newline_for(text)

    if section is None:
        parent = by_name.get(f"{index:04X}".casefold())
        if parent is None:
            raise ValueError(f"master.bin targets missing DCF object 0x{index:04X}")
        block = [
            f"[{section_name}]{newline}",
            f"NrOfEntries=1{newline}",
            f"{subidx}={value}{newline}",
            newline,
        ]
        lines[parent.end:parent.end] = block
        return "".join(lines)

    existing_values: dict[int, str] = {}
    for line_index in range(section.start + 1, section.end):
        match = KEY_VALUE_RE.match(lines[line_index])
        if not match:
            continue
        key = match.group(2).strip()
        if key.casefold() == "nrofentries":
            continue
        parsed = parse_positive_int(key)
        if parsed is not None and parsed > 0:
            existing_values[parsed] = match.group(4).strip()
    existing_values[subidx] = value

    block = [f"[{section_name}]{newline}", f"NrOfEntries={len(existing_values)}{newline}"]
    for key in sorted(existing_values):
        block.append(f"{key}={existing_values[key]}{newline}")
    block.append(newline)
    lines[section.start:section.end] = block
    return "".join(lines)


def materialize_master_bin(text: str, master_bin: Path | None) -> tuple[str, list[str]]:
    """Apply supported dcfgen master.bin writes to the static Master DCF."""
    if master_bin is None:
        return text, []

    entries = parse_concise_dcf(master_bin.read_bytes(), master_bin)
    max_node_id = configured_max_node_id(text)
    materialized: list[str] = []
    for entry in entries:
        if entry.index == 0x1018 and entry.subidx == 0x04:
            _require_unsigned32_target(text, "1018sub4", entry.index, entry.subidx)
            text = _upsert_section_value(
                text,
                "1018sub4",
                "ParameterValue",
                _format_u32(entry.data, master_bin, entry.index, entry.subidx),
            )
        elif entry.index in {0x1F55, 0x1F87, 0x1F88} and 1 <= entry.subidx <= 127:
            _require_unsigned32_target(
                text, f"{entry.index:04X}", entry.index, entry.subidx
            )
            if entry.subidx > max_node_id:
                raise ValueError(
                    "master.bin node-indexed entry exceeds the compact Master network range: "
                    f"0x{entry.index:04X}:{entry.subidx:02X}; highest configured "
                    f"remote node-ID is {max_node_id}"
                )
            text = _upsert_compact_value(
                text,
                entry.index,
                entry.subidx,
                _format_u32(entry.data, master_bin, entry.index, entry.subidx),
            )
        else:
            raise ValueError(
                "unsupported master.bin entry for static materialization: "
                f"0x{entry.index:04X}:{entry.subidx:02X} ({len(entry.data)} bytes)"
            )
        materialized.append(f"0x{entry.index:04X}:{entry.subidx:02X}")
    return text, materialized


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


def normalize_master_object_contracts(text: str) -> str:
    """Normalize known dcfgen Master template defects and reject unknown drift."""
    lines = text.splitlines(keepends=True)
    by_name = {section.name.casefold(): section for section in collect_sections(lines)}
    config_request = by_name.get("1f25")
    if config_request is None:
        return text

    values = section_values(lines, config_request)
    data_type = values.get("datatype")
    parsed = parse_positive_int(data_type[0]) if data_type is not None else None
    if parsed == 0x0007:
        return text
    if parsed == 0x0005 and data_type is not None:
        # dcf-tools 2.4.2 emits UNSIGNED8 here, but Lely's 0x1F25 handler
        # consumes the 32-bit CANopen "conf" signature (0x666E6F63).
        lines[data_type[1]] = _replace_key_value(lines[data_type[1]], "0x0007")
        return "".join(lines)

    actual = data_type[0] if data_type is not None else "<missing>"
    raise ValueError(
        "CANopen object 0x1F25 Configuration request must use "
        f"UNSIGNED32 (DataType=0x0007), got {actual}"
    )


def validate_fileless_object_values(text: str) -> None:
    """Reject file-backed OD values before publishing an MCU compact DCF."""
    lines = text.splitlines(keepends=True)
    for section in collect_sections(lines):
        values = section_values(lines, section)
        for key in ("uploadfile", "downloadfile"):
            if key in values:
                raise ValueError(
                    f"[{section.name}] contains {key}; CompactMaster targets "
                    "LELY_NO_CO_OBJ_FILE=1 and cannot publish file-backed OD "
                    "values. Use tools/gen_cfg_dcf.py for manual application "
                    "concise DCF data or provide an inline/static OD value."
                )


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
    master_bin: Path | None = None,
    error_history_depth: int,
    max_subobjects: int,
) -> tuple[str, list[tuple[str, int, int]], list[str], list[str], int, int, int, int, int]:
    text = normalize_master_object_contracts(text)
    text, materialized_master_bin = materialize_master_bin(text, master_bin)
    original_lines = text.splitlines(keepends=True)
    before_objects, before_subobjects = estimate_subobjects(
        original_lines, collect_sections(original_lines)
    )
    text, removed_reserved_tpdo = strip_reserved_tpdo_sub4(text)
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    max_node_id = configured_max_node_id(text)

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

    output_text = "".join(lines)
    validate_fileless_object_values(output_text)
    lines = output_text.splitlines(keepends=True)
    after_objects, after_subobjects = estimate_subobjects(lines, collect_sections(lines))
    if max_subobjects > 0 and after_subobjects > max_subobjects:
        raise ValueError(
            "compacted Master DCF still expands to "
            f"{after_subobjects} sub-objects (limit {max_subobjects}); "
            "review the network Node-IDs/DCF policy or raise --max-subobjects explicitly"
        )

    return (
        output_text,
        changes,
        removed_reserved_tpdo,
        materialized_master_bin,
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
        "--master-bin",
        type=Path,
        default=None,
        help="optional dcfgen master.bin to materialize into the static Master DCF",
    )
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
    if args.master_bin is not None and not args.master_bin.is_file():
        print(f"error: master.bin not found: {args.master_bin}", file=sys.stderr)
        return 2

    try:
        with args.input.open("r", encoding="utf-8-sig", newline="") as stream:
            source = stream.read()
        result = compact_dcf(
            source,
            master_bin=args.master_bin,
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
        materialized_master_bin,
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
    for target in materialized_master_bin:
        print(f"  {target}: materialized from master.bin")
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
