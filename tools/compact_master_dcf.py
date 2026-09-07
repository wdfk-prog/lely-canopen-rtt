#!/usr/bin/env python3
"""Compact dcfgen Master DCF arrays before dcf2c is used for an MCU target.

Lely dcfgen intentionally emits several CANopen Manager objects with
CompactSubObj=127/254. Lely's DCF loader expands every compact sub-object, and
co_dev_create_from_sdev() later allocates matching dynamic co_sub objects on the
target. This helper keeps the generated DCF semantics needed by the configured
network while reducing those expansion ranges for memory-constrained targets.

For fileless MCU builds, it also materializes the dcfgen-generated 0x1F22
UploadFile references into inline DOMAIN ParameterValue bytes. This is required
when LELY_NO_CO_OBJ_FILE=1 because the target cannot open the concise DCF file.

dcfgen may additionally emit master.bin with writes that initialize the static
Master object dictionary (for example 0x1F87/0x1F88 identity expectations).
Those writes are materialized into the compact DCF as well, because this MCU
profile has no runtime master.bin loader.
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
CONCISE_DCF_SUB_RE = re.compile(r"^1F22sub([0-9A-Fa-f]{1,2})$", re.IGNORECASE)

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


@dataclass
class EmbeddedConciseDcf:
    subidx: int
    filename: str
    size: int
    entries: int


@dataclass(frozen=True)
class ConciseDcfEntry:
    index: int
    subidx: int
    data: bytes


def parse_concise_dcf(data: bytes, source: Path) -> list[ConciseDcfEntry]:
    """Parse the concise DCF layout consumed by co_csdo_dn_dcf_req()."""
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
        lines[existing[1]] = replace_key_value(lines[existing[1]], key, value)
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
    """Apply dcfgen master.bin writes to the static Master DCF."""
    if master_bin is None or not master_bin.is_file():
        return text, []

    entries = parse_concise_dcf(master_bin.read_bytes(), master_bin)
    materialized: list[str] = []
    for entry in entries:
        if entry.index == 0x1018 and entry.subidx == 0x04:
            text = _upsert_section_value(
                text,
                "1018sub4",
                "ParameterValue",
                _format_u32(entry.data, master_bin, entry.index, entry.subidx),
            )
        elif entry.index in {0x1F55, 0x1F87, 0x1F88} and 1 <= entry.subidx <= 127:
            text = _upsert_compact_value(
                text,
                entry.index,
                entry.subidx,
                _format_u32(entry.data, master_bin, entry.index, entry.subidx),
            )
        else:
            raise ValueError(
                f"unsupported master.bin entry for static materialization: "
                f"0x{entry.index:04X}:{entry.subidx:02X} ({len(entry.data)} bytes)"
            )
        materialized.append(f"0x{entry.index:04X}:{entry.subidx:02X}")
    return text, materialized


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


def replace_key_value(line: str, new_key: str | None, new_value: str) -> str:
    match = KEY_VALUE_RE.match(line)
    if not match:
        raise ValueError(f"cannot rewrite DCF key/value line: {line!r}")
    newline = match.group(5) or ""
    key = new_key if new_key is not None else match.group(2)
    return f"{match.group(1)}{key}{match.group(3)}{new_value}{newline}"


def replace_raw_value(line: str, new_value: str) -> str:
    return replace_key_value(line, None, new_value)


def replace_value(line: str, new_value: int) -> str:
    return replace_raw_value(line, str(new_value))


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


def validate_concise_dcf(data: bytes, source: Path) -> int:
    """Validate the binary layout consumed by co_csdo_dn_dcf_req()."""
    return len(parse_concise_dcf(data, source))


def materialize_and_compact_1f22(
    text: str, *, input_dir: Path, max_node_id: int
) -> tuple[str, list[EmbeddedConciseDcf], tuple[int, int] | None]:
    """Inline 0x1F22 UploadFile data and trim unused explicit node entries."""
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)
    by_name = {section.name.casefold(): section for section in sections}
    parent = by_name.get("1f22")
    if parent is None:
        return text, [], None

    parent_values = section_values(lines, parent)
    subnumber = parent_values.get("subnumber")
    if subnumber is None:
        raise ValueError("object 0x1F22 has no SubNumber")
    original_subnumber = parse_positive_int(subnumber[0])
    if original_subnumber is None or original_subnumber <= 0:
        raise ValueError("object 0x1F22 has an invalid SubNumber")

    target_subnumber = max_node_id + 1
    if original_subnumber < target_subnumber:
        raise ValueError(
            f"object 0x1F22 SubNumber={original_subnumber} does not cover node-ID {max_node_id}"
        )
    if target_subnumber < original_subnumber:
        lines[subnumber[1]] = replace_value(lines[subnumber[1]], target_subnumber)

    sub0 = by_name.get("1f22sub0")
    if sub0 is None:
        raise ValueError("object 0x1F22 is missing sub-index 0")
    sub0_values = section_values(lines, sub0)
    highest = sub0_values.get("defaultvalue")
    if highest is None:
        raise ValueError("object 0x1F22:00 has no DefaultValue")
    lines[highest[1]] = replace_value(lines[highest[1]], max_node_id)

    base_dir = input_dir.resolve()
    removals: list[Section] = []
    embedded: list[EmbeddedConciseDcf] = []

    for section in sections:
        match = CONCISE_DCF_SUB_RE.fullmatch(section.name)
        if not match:
            continue
        subidx = int(match.group(1), 16)
        if subidx > max_node_id:
            removals.append(section)
            continue
        if subidx == 0:
            continue

        values = section_values(lines, section)
        upload = values.get("uploadfile")
        if upload is None:
            continue
        if values.get("parametervalue") is not None:
            raise ValueError(
                f"object 0x1F22:{subidx:02X} contains both UploadFile and ParameterValue"
            )

        filename = upload[0].strip().strip('"')
        source = (base_dir / filename).resolve()
        try:
            source.relative_to(base_dir)
        except ValueError as exc:
            raise ValueError(
                f"object 0x1F22:{subidx:02X} UploadFile escapes the DCF staging directory: "
                f"{filename}"
            ) from exc
        if not source.is_file():
            raise ValueError(
                f"object 0x1F22:{subidx:02X} concise DCF file not found: {source}"
            )

        data = source.read_bytes()
        entries = validate_concise_dcf(data, source)
        lines[upload[1]] = replace_key_value(
            lines[upload[1]], "ParameterValue", data.hex().upper()
        )
        embedded.append(
            EmbeddedConciseDcf(
                subidx=subidx, filename=filename, size=len(data), entries=entries
            )
        )

    for section in sorted(removals, key=lambda item: item.start, reverse=True):
        del lines[section.start : section.end]

    output = "".join(lines)
    output_lines = output.splitlines(keepends=True)
    for section in collect_sections(output_lines):
        match = CONCISE_DCF_SUB_RE.fullmatch(section.name)
        if not match or int(match.group(1), 16) == 0:
            continue
        values = section_values(output_lines, section)
        if "uploadfile" in values:
            raise ValueError(
                f"object 0x1F22:{int(match.group(1), 16):02X} still contains UploadFile"
            )

    range_change = None
    if target_subnumber != original_subnumber:
        range_change = (original_subnumber - 1, max_node_id)
    return output, embedded, range_change


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
    input_dir: Path,
    master_bin: Path | None,
    error_history_depth: int,
    max_subobjects: int,
) -> tuple[
    str,
    list[tuple[str, int, int]],
    list[str],
    list[str],
    list[EmbeddedConciseDcf],
    tuple[int, int] | None,
    int,
    int,
    int,
    int,
    int,
]:
    text, materialized_master_bin = materialize_master_bin(text, master_bin)
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

    text, embedded_1f22, concise_range_change = materialize_and_compact_1f22(
        text, input_dir=input_dir, max_node_id=max_node_id
    )
    lines = text.splitlines(keepends=True)
    sections = collect_sections(lines)

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
        materialized_master_bin,
        embedded_1f22,
        concise_range_change,
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

    try:
        with args.input.open("r", encoding="utf-8-sig", newline="") as stream:
            source = stream.read()
        result = compact_dcf(
            source,
            input_dir=args.input.parent,
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
        embedded_1f22,
        concise_range_change,
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
    if concise_range_change is not None:
        old, new = concise_range_change
        print(f"  0x1F22: explicit Node-ID range 1..{old} -> 1..{new}")
    for item in embedded_1f22:
        print(
            f"  0x1F22:{item.subidx:02X}: embedded {item.filename} "
            f"({item.size} bytes, {item.entries} entries)"
        )
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
