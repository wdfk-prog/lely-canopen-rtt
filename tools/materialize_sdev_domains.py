#!/usr/bin/env python3
"""Materialize fileless DOMAIN ParameterValue bytes in dcf2c static C output.

The bundled Windows dcf2c executable used by this project can emit a NULL
DOMAIN value for an inline DCF ParameterValue. The RT-Thread target cannot fall
back to UploadFile because LELY_NO_CO_OBJ_FILE=1, so this helper makes the
static C representation match the already-compacted DCF and fails closed when
the generated structure is not recognizable.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]\s*$")
KEY_VALUE_RE = re.compile(r"^\s*([^=]+?)\s*=\s*(.*?)\s*$")
CONCISE_SUB_RE = re.compile(r"^1F22sub([0-9A-Fa-f]{1,2})$", re.IGNORECASE)
C_OBJECT_RE = re.compile(r"^\s*\.idx\s*=\s*0x([0-9A-Fa-f]+),\s*$")
C_SUB_RE = re.compile(r"^\s*\.subidx\s*=\s*0x([0-9A-Fa-f]+),\s*$")


@dataclass(frozen=True)
class DomainValue:
    subidx: int
    data: bytes


def extract_1f22_domains(text: str) -> list[DomainValue]:
    lines = text.splitlines()
    starts: list[tuple[str, int]] = []
    for index, line in enumerate(lines):
        match = SECTION_RE.match(line)
        if match:
            starts.append((match.group(1).strip(), index))

    result: list[DomainValue] = []
    for position, (name, start) in enumerate(starts):
        match = CONCISE_SUB_RE.fullmatch(name)
        if not match:
            continue
        subidx = int(match.group(1), 16)
        if subidx == 0:
            continue
        end = starts[position + 1][1] if position + 1 < len(starts) else len(lines)
        values: dict[str, str] = {}
        for line in lines[start + 1 : end]:
            item = KEY_VALUE_RE.match(line)
            if item:
                values[item.group(1).strip().casefold()] = item.group(2).strip()
        if values.get("datatype", "").lower() != "0x000f":
            continue
        raw = values.get("parametervalue")
        if not raw:
            continue
        try:
            data = bytes.fromhex(raw)
        except ValueError as exc:
            raise ValueError(
                f"0x1F22:{subidx:02X} has invalid DOMAIN ParameterValue hex"
            ) from exc
        result.append(DomainValue(subidx=subidx, data=data))
    return result


def _find_object_range(lines: list[str], object_index: int) -> tuple[int, int]:
    start = -1
    for index, line in enumerate(lines):
        match = C_OBJECT_RE.match(line)
        if not match:
            continue
        current = int(match.group(1), 16)
        if start >= 0:
            return start, index
        if current == object_index:
            start = index
    if start >= 0:
        return start, len(lines)
    raise ValueError(f"generated C does not contain object 0x{object_index:04X}")


def _find_sub_range(
    lines: list[str], object_start: int, object_end: int, subidx: int
) -> tuple[int, int]:
    start = -1
    for index in range(object_start, object_end):
        match = C_SUB_RE.match(lines[index])
        if not match:
            continue
        current = int(match.group(1), 16)
        if start >= 0:
            return start, index
        if current == subidx:
            start = index
    if start >= 0:
        return start, object_end
    raise ValueError(f"generated C does not contain 0x1F22:{subidx:02X}")


def _domain_initializer(prefix: str, data: bytes, newline: str) -> list[str]:
    lines = [f"{prefix}.val = {{ .dom = CO_DOMAIN_C(co_unsigned8_t, {{{newline}"]
    for offset in range(0, len(data), 8):
        chunk = data[offset : offset + 8]
        encoded = ", ".join(f"0x{byte:02x}" for byte in chunk)
        suffix = "," if offset + len(chunk) < len(data) else ""
        lines.append(f"{prefix}\t{encoded}{suffix}{newline}")
    lines.append(f"{prefix}}}) }},{newline}")
    return lines


def _extract_initializer_bytes(lines: list[str], start: int, end: int) -> bytes | None:
    line = lines[start]
    if ".dom = NULL" in line:
        return None
    if "CO_DOMAIN_C" not in line:
        raise ValueError("unexpected DOMAIN initializer emitted by dcf2c")
    raw: list[int] = []
    for index in range(start + 1, end):
        for token in re.findall(r"0x([0-9A-Fa-f]{2})\b", lines[index]):
            raw.append(int(token, 16))
        if "}) }," in lines[index]:
            break
    return bytes(raw)


def materialize(c_text: str, domains: list[DomainValue]) -> tuple[str, list[int]]:
    newline = "\r\n" if "\r\n" in c_text else "\n"
    lines = c_text.splitlines(keepends=True)
    changed: list[int] = []

    for domain in domains:
        object_start, object_end = _find_object_range(lines, 0x1F22)
        sub_start, sub_end = _find_sub_range(
            lines, object_start, object_end, domain.subidx
        )

        val_start = next(
            (i for i in range(sub_start, sub_end) if ".val =" in lines[i]), None
        )
        if val_start is None:
            raise ValueError(f"0x1F22:{domain.subidx:02X} has no .val initializer")

        val_end = val_start + 1
        if "CO_DOMAIN_C" in lines[val_start]:
            while val_end < sub_end and "}) }," not in lines[val_end - 1]:
                val_end += 1
        current = _extract_initializer_bytes(lines, val_start, val_end)
        if current is not None and current != domain.data:
            raise ValueError(
                f"0x1F22:{domain.subidx:02X} C DOMAIN does not match compact DCF"
            )
        if current is None:
            prefix = lines[val_start][: len(lines[val_start]) - len(lines[val_start].lstrip())]
            lines[val_start:val_end] = _domain_initializer(prefix, domain.data, newline)
            changed.append(domain.subidx)

        object_start, object_end = _find_object_range(lines, 0x1F22)
        sub_start, sub_end = _find_sub_range(
            lines, object_start, object_end, domain.subidx
        )
        flags = next(
            (i for i in range(sub_start, sub_end) if ".flags = 0" in lines[i]), None
        )
        if flags is None:
            raise ValueError(f"0x1F22:{domain.subidx:02X} has no .flags initializer")
        if not any(
            "CO_OBJ_FLAGS_PARAMETER_VALUE" in lines[i]
            for i in range(flags, sub_end)
        ):
            prefix = lines[flags][: len(lines[flags]) - len(lines[flags].lstrip())]
            lines.insert(flags + 1, f"{prefix}\t| CO_OBJ_FLAGS_PARAMETER_VALUE{newline}")
            if domain.subidx not in changed:
                changed.append(domain.subidx)

    output = "".join(lines)
    if "CO_OBJ_FLAGS_UPLOAD_FILE" in output or "CO_OBJ_FLAGS_DOWNLOAD_FILE" in output:
        raise ValueError("generated C still contains file-backed object dictionary values")
    return output, changed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dcf", required=True, type=Path, help="compacted Master DCF")
    parser.add_argument("--c", required=True, type=Path, help="dcf2c generated C file")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        with args.dcf.open("r", encoding="utf-8-sig", newline="") as stream:
            dcf_text = stream.read()
        with args.c.open("r", encoding="utf-8", newline="") as stream:
            c_text = stream.read()
        domains = extract_1f22_domains(dcf_text)
        output, changed = materialize(c_text, domains)
        temp = args.c.with_name(args.c.name + ".tmp")
        with temp.open("w", encoding="utf-8", newline="") as stream:
            stream.write(output)
        temp.replace(args.c)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    for domain in domains:
        action = "materialized" if domain.subidx in changed else "verified"
        print(
            f"  0x1F22:{domain.subidx:02X}: {action} static DOMAIN "
            f"({len(domain.data)} bytes)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
