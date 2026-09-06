#!/usr/bin/env python3
"""Resolve dcfgen slave DCF/EDS inputs against the YAML file directory.

Lely dcfgen opens each top-level node's ``dcf`` value directly. That means a
relative value is otherwise interpreted against the process working directory,
not against the YAML file location. This helper creates a staging-only YAML
copy with those paths made absolute so generation is independent of the caller
or launcher working directory.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

try:
    import yaml
except ImportError as exc:  # pragma: no cover - exercised on misconfigured hosts.
    raise SystemExit(
        "PyYAML is required in the Python environment that provides dcfgen."
    ) from exc


def _resolve_path(value: object, base_dir: Path) -> Path:
    raw = os.path.expanduser(os.path.expandvars(str(value)))
    path = Path(raw)
    if not path.is_absolute():
        path = base_dir / path
    return path.resolve()


def resolve_yaml(input_path: Path, output_path: Path, base_dir: Path) -> int:
    with input_path.open("r", encoding="utf-8") as stream:
        config = yaml.safe_load(stream)

    if not isinstance(config, dict):
        raise ValueError("dcfgen YAML root must be a mapping")

    resolved = 0
    for name, node in config.items():
        # Match dcfgen's own top-level filtering before touching node inputs.
        if name == "master" or name == "options" or str(name).startswith("."):
            continue
        if not isinstance(node, dict) or "dcf" not in node:
            continue

        path = _resolve_path(node["dcf"], base_dir)
        if not path.is_file():
            raise FileNotFoundError(
                f"{name}: DCF/EDS input not found after YAML-relative resolution: {path}"
            )

        node["dcf"] = str(path)
        resolved += 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as stream:
        yaml.safe_dump(
            config,
            stream,
            sort_keys=False,
            default_flow_style=False,
            allow_unicode=True,
        )

    return resolved


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--base-dir", required=True, type=Path)
    args = parser.parse_args()

    try:
        count = resolve_yaml(
            args.input.resolve(), args.output.resolve(), args.base_dir.resolve()
        )
    except (OSError, ValueError, yaml.YAMLError) as exc:
        print(f"dcfgen YAML path resolution failed: {exc}", file=sys.stderr)
        return 1

    print(f"Resolved dcfgen DCF/EDS inputs: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
