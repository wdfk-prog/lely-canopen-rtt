#!/usr/bin/env python3
"""Build the bounded Lely CANopen RT-Thread CI profile matrix."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SPEC = Path(__file__).with_name("config_matrix.json")
DEFINE_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
PROFILE_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
WORK_DIR_NAME_RE = re.compile(r"^_ci(?:$|[-_][A-Za-z0-9._-]+)$")
KCONFIG_SYMBOL_RE = re.compile(r"^(?:menu)?config\s+(PKG_LELY_[A-Z0-9_]+)\s*$", re.MULTILINE)
PACKAGE_DEFINE_RE = re.compile(r"^#define\s+(PKG_LELY_[A-Z0-9_]+|PKG_USING_LELY)\b")


class BuildError(RuntimeError):
    """Expected CI preparation or build failure."""


def load_spec(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)
    if not isinstance(data, dict):
        raise BuildError("matrix spec root must be an object")
    return data


def profile_defines(spec: dict[str, Any], profile: dict[str, Any]) -> dict[str, Any]:
    defines: dict[str, Any] = {"PKG_USING_LELY": None}
    feature_sets = spec.get("feature_sets", {})
    for set_name in profile.get("use", []):
        try:
            selected = feature_sets[set_name]
        except KeyError as exc:
            raise BuildError(f"unknown feature set {set_name!r}") from exc
        defines.update(selected)
    defines.update(profile.get("define", {}))
    for symbol in profile.get("undefine", []):
        defines.pop(symbol, None)
    return defines


def validate_spec(spec: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    profiles = spec.get("profiles")
    feature_sets = spec.get("feature_sets")
    package_name = spec.get("package_name")
    if not isinstance(profiles, list) or not profiles:
        errors.append("profiles must be a non-empty list")
        return errors
    if not isinstance(feature_sets, dict):
        errors.append("feature_sets must be an object")
        return errors
    if not isinstance(package_name, str) or not PROFILE_NAME_RE.fullmatch(package_name):
        errors.append("package_name must use lowercase letters, digits and hyphens")

    kconfig_text = (REPO_ROOT / "Kconfig").read_text(encoding="utf-8")
    kconfig_symbols = set(KCONFIG_SYMBOL_RE.findall(kconfig_text))

    seen: set[str] = set()
    for profile in profiles:
        name = profile.get("name")
        if not isinstance(name, str) or not name:
            errors.append("every profile needs a non-empty name")
            continue
        if not PROFILE_NAME_RE.fullmatch(name):
            errors.append(f"{name}: profile name must use lowercase letters, digits and hyphens")
        if name in seen:
            errors.append(f"duplicate profile name: {name}")
        seen.add(name)
        try:
            defines = profile_defines(spec, profile)
        except BuildError as exc:
            errors.append(f"{name}: {exc}")
            continue

        for symbol, value in defines.items():
            if not DEFINE_RE.fullmatch(symbol):
                errors.append(f"{name}: invalid macro name {symbol!r}")
            if symbol.startswith("PKG_LELY_") and symbol not in kconfig_symbols:
                errors.append(f"{name}: package macro is not declared by Kconfig: {symbol}")
            if isinstance(value, str) and ("\n" in value or "\r" in value):
                errors.append(f"{name}: multiline macro value is not allowed: {symbol}")
            if value is not None and not isinstance(value, (int, str)):
                errors.append(f"{name}: unsupported macro value type for {symbol}")

        for constraint in spec.get("constraints", []):
            symbol = constraint["symbol"]
            if symbol not in defines:
                continue
            missing = [required for required in constraint.get("requires", []) if required not in defines]
            if missing:
                errors.append(f"{name}: {symbol} requires {', '.join(missing)}")

        for choice in spec.get("choices", []):
            trigger = choice["when"]
            choices = choice["exactly_one"]
            selected = [symbol for symbol in choices if symbol in defines]
            if trigger in defines and len(selected) != 1:
                errors.append(f"{name}: {trigger} requires exactly one of {', '.join(choices)}")
            if trigger not in defines and selected:
                errors.append(f"{name}: {', '.join(selected)} set while {trigger} is disabled")

        for field in ("expect_sources", "forbid_sources"):
            values = profile.get(field, [])
            if not isinstance(values, list) or any(not isinstance(value, str) for value in values):
                errors.append(f"{name}: {field} must be a list of paths")

    return errors


def validate_work_dir(work_dir: Path) -> Path:
    resolved = work_dir.resolve()
    try:
        relative = resolved.relative_to(REPO_ROOT)
    except ValueError as exc:
        raise BuildError("work directory must stay inside the repository root") from exc
    if not relative.parts:
        raise BuildError("work directory must not be the repository root")
    if not WORK_DIR_NAME_RE.fullmatch(resolved.name):
        raise BuildError("work directory basename must be _ci or start with _ci-/_ci_")
    return resolved


def run_logged(command: list[str], cwd: Path, log_path: Path, env: dict[str, str] | None = None) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as log:
        line = "+ " + " ".join(command)
        print(line)
        log.write(line + "\n")
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for output in process.stdout:
            sys.stdout.write(output)
            log.write(output)
        return_code = process.wait()
    if return_code:
        raise BuildError(f"command failed with exit code {return_code}: {' '.join(command)}")


def git_clone(repository: str, ref: str, destination: Path, log_path: Path) -> str:
    run_logged(
        ["git", "clone", "--depth", "1", "--branch", ref, repository, str(destination)],
        cwd=destination.parent,
        log_path=log_path,
    )
    result = subprocess.run(
        ["git", "-C", str(destination), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def ensure_vendor_tree() -> None:
    required = [
        REPO_ROOT / "upstream" / "include" / "lely" / "co" / "co.h",
        REPO_ROOT / "upstream" / "src" / "co" / "dev.c",
    ]
    missing = [path.relative_to(REPO_ROOT).as_posix() for path in required if not path.is_file()]
    if missing:
        raise BuildError("required vendored Lely sources are missing: " + ", ".join(missing))


def enable_hal_can(bsp_dir: Path, stm32_series: str) -> None:
    hal_conf = bsp_dir / "board" / "CubeMX_Config" / "Inc" / f"{stm32_series}xx_hal_conf.h"
    if not hal_conf.is_file():
        raise BuildError(f"STM32 HAL config not found: {hal_conf}")
    text = hal_conf.read_text(encoding="utf-8", errors="ignore")
    if re.search(r"^\s*#define\s+HAL_CAN_MODULE_ENABLED\b", text, re.MULTILINE):
        return
    anchor = re.search(r"^\s*#define\s+HAL_MODULE_ENABLED\b.*$", text, re.MULTILINE)
    if anchor:
        insert_at = anchor.end()
        text = text[:insert_at] + "\n#define HAL_CAN_MODULE_ENABLED" + text[insert_at:]
    else:
        text = "#define HAL_CAN_MODULE_ENABLED\n" + text
    hal_conf.write_text(text, encoding="utf-8")


def prepare_base(spec: dict[str, Any], work_dir: Path) -> tuple[Path, dict[str, str]]:
    prepare_log = work_dir / "logs" / "prepare.log"
    prepared = work_dir / "prepared"
    shutil.rmtree(prepared, ignore_errors=True)
    prepared.mkdir(parents=True)

    rt = spec["rtthread"]
    rtthread_dir = prepared / "rt-thread"
    env_dir = prepared / "env"
    rtthread_sha = git_clone(rt["repository"], rt["ref"], rtthread_dir, prepare_log)
    env_sha = git_clone(rt["env_repository"], rt["env_ref"], env_dir, prepare_log)

    bsp_dir = rtthread_dir / rt["bsp"]
    if not (bsp_dir / "SConstruct").is_file():
        raise BuildError(f"BSP SConstruct not found: {bsp_dir / 'SConstruct'}")

    build_env = os.environ.copy()
    build_env["RTT_ROOT"] = str(rtthread_dir)
    build_env["RTT_ENV"] = str(env_dir)
    build_env.setdefault("RTT_CC", "gcc")
    build_env.setdefault("RTT_EXEC_PATH", "/opt/gcc-arm-none-eabi/bin")
    build_env["PATH"] = os.pathsep.join(
        [
            str(Path.home() / ".local" / "bin"),
            str(env_dir),
            build_env["RTT_EXEC_PATH"],
            build_env.get("PATH", ""),
        ]
    )

    compiler = Path(build_env["RTT_EXEC_PATH"]) / "arm-none-eabi-gcc"
    if not compiler.is_file():
        raise BuildError(f"arm-none-eabi-gcc not found: {compiler}")

    run_logged(["scons", "--pyconfig-silent"], cwd=bsp_dir, log_path=prepare_log, env=build_env)
    run_logged(
        [sys.executable, str(env_dir / "env.py"), "package", "--upgrade"],
        cwd=bsp_dir,
        log_path=prepare_log,
        env=build_env,
    )
    run_logged(
        [sys.executable, str(env_dir / "env.py"), "package", "--update"],
        cwd=bsp_dir,
        log_path=prepare_log,
        env=build_env,
    )

    hal_drivers = rtthread_dir / "bsp" / "stm32" / "libraries" / "HAL_Drivers" / "drivers"
    if not (hal_drivers / "drv_can.c").is_file():
        raise BuildError(f"STM32 CAN driver source not found: {hal_drivers / 'drv_can.c'}")
    enable_hal_can(bsp_dir, rt["stm32_series"])

    archive = work_dir / "rt-thread-base.tar"
    if archive.exists():
        archive.unlink()
    run_logged(
        ["tar", "--exclude=.git", "-cf", str(archive), "rt-thread"],
        cwd=prepared,
        log_path=prepare_log,
        env=build_env,
    )

    revisions = {
        "rtthread_ref": rt["ref"],
        "rtthread_sha": rtthread_sha,
        "env_ref": rt["env_ref"],
        "env_sha": env_sha,
        "bsp": rt["bsp"],
    }
    (work_dir / "source-revisions.json").write_text(
        json.dumps(revisions, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return archive, revisions


def stage_package(package_name: str, bsp_dir: Path) -> Path:
    destination = bsp_dir / "packages" / package_name
    shutil.rmtree(destination, ignore_errors=True)

    def ignore(path: str, names: list[str]) -> set[str]:
        ignored = {
            name
            for name in names
            if name in {".git", "__pycache__"} or WORK_DIR_NAME_RE.fullmatch(name)
        }
        if Path(path) == REPO_ROOT:
            ignored.update({".github", "docs"})
        return ignored

    shutil.copytree(REPO_ROOT, destination, ignore=ignore)
    return destination


def patch_sconstruct(bsp_dir: Path, package_name: str) -> None:
    sconstruct = bsp_dir / "SConstruct"
    marker = f"objs += SConscript('packages/{package_name}/SConscript')"
    text = sconstruct.read_text(encoding="utf-8")
    if marker in text:
        return
    text += "\n# CI-only local package integration for Lely CANopen compile checks.\n"
    text += marker + "\n"
    sconstruct.write_text(text, encoding="utf-8")


def write_define(stream: Any, symbol: str, value: Any) -> None:
    if value is None:
        stream.write(f"#define {symbol}\n")
    else:
        stream.write(f"#define {symbol} {value}\n")


def apply_profile(spec: dict[str, Any], profile: dict[str, Any], bsp_dir: Path) -> dict[str, Any]:
    rtconfig = bsp_dir / "rtconfig.h"
    lines = rtconfig.read_text(encoding="utf-8").splitlines(keepends=True)
    filtered = [line for line in lines if not PACKAGE_DEFINE_RE.match(line)]
    rtconfig.write_text("".join(filtered), encoding="utf-8")

    defines = profile_defines(spec, profile)
    with rtconfig.open("a", encoding="utf-8") as stream:
        stream.write(f"\n/* CI-only Lely profile: {profile['name']} */\n")
        for symbol in sorted(defines):
            write_define(stream, symbol, defines[symbol])
    return defines


def find_object(build_root: Path, source_path: str) -> Path | None:
    object_suffix = source_path[:-2] + ".o" if source_path.endswith(".c") else source_path + ".o"
    object_suffix = object_suffix.replace("\\", "/")
    for candidate in build_root.rglob("*.o"):
        normalized = candidate.as_posix()
        if normalized.endswith(object_suffix):
            return candidate
    return None


def verify_objects(bsp_dir: Path, profile: dict[str, Any]) -> dict[str, list[str]]:
    build_root = bsp_dir / "build"
    missing: list[str] = []
    unexpected: list[str] = []
    for source in profile.get("expect_sources", []):
        if find_object(build_root, source) is None:
            missing.append(source)
    for source in profile.get("forbid_sources", []):
        if find_object(build_root, source) is not None:
            unexpected.append(source)
    if missing or unexpected:
        details = []
        if missing:
            details.append("missing expected objects: " + ", ".join(missing))
        if unexpected:
            details.append("forbidden objects present: " + ", ".join(unexpected))
        raise BuildError("; ".join(details))
    return {"missing": missing, "unexpected": unexpected}


def verify_outputs(bsp_dir: Path) -> dict[str, str]:
    elf_candidates = [bsp_dir / "rt-thread.elf", bsp_dir / "rtthread.elf"]
    elf = next((path for path in elf_candidates if path.is_file()), None)
    binary = bsp_dir / "rtthread.bin"
    if elf is None:
        raise BuildError(f"no RT-Thread ELF output found under {bsp_dir}")
    if not binary.is_file():
        raise BuildError(f"RT-Thread binary output not found: {binary}")
    can_object = next((path for path in (bsp_dir / "build").rglob("*drv_can*.o")), None)
    if can_object is None:
        raise BuildError("STM32 CAN driver object was not built")
    return {
        "elf": str(elf),
        "bin": str(binary),
        "can_object": str(can_object),
    }


def build_profile(
    spec: dict[str, Any],
    profile: dict[str, Any],
    archive: Path,
    work_dir: Path,
) -> dict[str, Any]:
    name = profile["name"]
    case_dir = work_dir / "cases" / name
    log_path = work_dir / "logs" / f"{name}.log"
    shutil.rmtree(case_dir, ignore_errors=True)
    case_dir.mkdir(parents=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("", encoding="utf-8")

    run_logged(["tar", "-xf", str(archive), "-C", str(case_dir)], cwd=REPO_ROOT, log_path=log_path)
    rtthread_dir = case_dir / "rt-thread"
    bsp_dir = rtthread_dir / spec["rtthread"]["bsp"]
    package_dir = stage_package(spec["package_name"], bsp_dir)
    patch_sconstruct(bsp_dir, spec["package_name"])
    defines = apply_profile(spec, profile, bsp_dir)

    build_env = os.environ.copy()
    build_env["RTT_ROOT"] = str(rtthread_dir)
    build_env.setdefault("RTT_CC", "gcc")
    build_env.setdefault("RTT_EXEC_PATH", "/opt/gcc-arm-none-eabi/bin")
    build_env["PATH"] = os.pathsep.join(
        [
            str(Path.home() / ".local" / "bin"),
            build_env["RTT_EXEC_PATH"],
            build_env.get("PATH", ""),
        ]
    )

    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"PROFILE={name}\n")
        log.write(f"PACKAGE_DIR={package_dir}\n")
        for symbol in sorted(defines):
            log.write(f"DEFINE {symbol}={defines[symbol]!r}\n")

    jobs = str(max(1, min(os.cpu_count() or 1, 8)))
    run_logged(["scons", f"-j{jobs}"], cwd=bsp_dir, log_path=log_path, env=build_env)
    outputs = verify_outputs(bsp_dir)
    verify_objects(bsp_dir, profile)
    run_logged(
        [str(Path(build_env["RTT_EXEC_PATH"]) / "arm-none-eabi-size"), outputs["elf"]],
        cwd=bsp_dir,
        log_path=log_path,
        env=build_env,
    )
    return {
        "profile": name,
        "status": "PASS",
        "defines": sorted(defines),
        "outputs": outputs,
    }


def prepare(spec: dict[str, Any], work_dir: Path) -> int:
    errors = validate_spec(spec)
    if errors:
        for error in errors:
            print(f"MATRIX_SPEC_ERROR: {error}", file=sys.stderr)
        return 2

    try:
        work_dir = validate_work_dir(work_dir)
    except BuildError as exc:
        print(f"LELY_MATRIX_WORKDIR_ERROR: {exc}", file=sys.stderr)
        return 2

    shutil.rmtree(work_dir, ignore_errors=True)
    (work_dir / "logs").mkdir(parents=True)

    try:
        ensure_vendor_tree()
        archive, revisions = prepare_base(spec, work_dir)
    except Exception as exc:
        summary = {"status": "PREPARE_FAIL", "error": str(exc)}
        (work_dir / "prepare-summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (work_dir / "logs" / "prepare-failure.log").write_text(str(exc) + "\n", encoding="utf-8")
        print(f"LELY_MATRIX_PREPARE_FAIL: {exc}", file=sys.stderr)
        return 3

    summary = {
        "status": "PASS",
        "profiles": len(spec["profiles"]),
        "revisions": revisions,
        "archive": archive.name,
    }
    (work_dir / "prepare-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"LELY_MATRIX_PREPARE_PASS profiles={len(spec['profiles'])}")
    return 0


def run_profile(spec: dict[str, Any], profile_name: str, archive: Path, work_dir: Path) -> int:
    errors = validate_spec(spec)
    if errors:
        for error in errors:
            print(f"MATRIX_SPEC_ERROR: {error}", file=sys.stderr)
        return 2

    try:
        work_dir = validate_work_dir(work_dir)
    except BuildError as exc:
        print(f"LELY_MATRIX_WORKDIR_ERROR: {exc}", file=sys.stderr)
        return 2

    profile = next((item for item in spec["profiles"] if item["name"] == profile_name), None)
    if profile is None:
        print(f"LELY_MATRIX_PROFILE_ERROR: unknown profile {profile_name!r}", file=sys.stderr)
        return 2

    archive = archive.resolve()
    if not archive.is_file():
        print(f"LELY_MATRIX_ARCHIVE_ERROR: base archive not found: {archive}", file=sys.stderr)
        return 2

    shutil.rmtree(work_dir, ignore_errors=True)
    (work_dir / "logs").mkdir(parents=True)

    print(f"=== LELY MATRIX PROFILE: {profile_name} ===")
    try:
        ensure_vendor_tree()
        result = build_profile(spec, profile, archive, work_dir)
    except Exception as exc:
        result = {"profile": profile_name, "status": "FAIL", "error": str(exc)}
        (work_dir / "profile-summary.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (work_dir / "logs" / f"{profile_name}-failure.log").write_text(str(exc) + "\n", encoding="utf-8")
        print(f"LELY_STM32F4_BUILD_FAIL profile={profile_name}: {exc}", file=sys.stderr)
        return 1

    (work_dir / "profile-summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"LELY_STM32F4_BUILD_PASS profile={profile_name}")
    return 0


def run_all(spec: dict[str, Any], work_dir: Path) -> int:
    errors = validate_spec(spec)
    if errors:
        for error in errors:
            print(f"MATRIX_SPEC_ERROR: {error}", file=sys.stderr)
        return 2

    try:
        work_dir = validate_work_dir(work_dir)
    except BuildError as exc:
        print(f"LELY_MATRIX_WORKDIR_ERROR: {exc}", file=sys.stderr)
        return 2

    shutil.rmtree(work_dir, ignore_errors=True)
    (work_dir / "logs").mkdir(parents=True)

    results: list[dict[str, Any]] = []
    failures: list[str] = []
    try:
        ensure_vendor_tree()
        archive, revisions = prepare_base(spec, work_dir)
    except Exception as exc:
        summary = {"status": "PREPARE_FAIL", "error": str(exc), "results": []}
        (work_dir / "matrix-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        (work_dir / "logs" / "prepare-failure.log").write_text(str(exc) + "\n", encoding="utf-8")
        print(f"LELY_MATRIX_PREPARE_FAIL: {exc}", file=sys.stderr)
        return 3

    for profile in spec["profiles"]:
        name = profile["name"]
        print(f"=== LELY MATRIX PROFILE: {name} ===")
        try:
            result = build_profile(spec, profile, archive, work_dir)
            results.append(result)
            print(f"LELY_STM32F4_BUILD_PASS profile={name}")
        except Exception as exc:
            failures.append(name)
            results.append({"profile": name, "status": "FAIL", "error": str(exc)})
            print(f"LELY_STM32F4_BUILD_FAIL profile={name}: {exc}", file=sys.stderr)

    summary = {
        "status": "PASS" if not failures else "FAIL",
        "revisions": revisions,
        "profiles": len(spec["profiles"]),
        "failures": failures,
        "results": results,
    }
    (work_dir / "matrix-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if failures:
        print("LELY_STM32F4_MATRIX_FAIL profiles=" + ",".join(failures), file=sys.stderr)
        return 1
    print(f"LELY_STM32F4_MATRIX_PASS profiles={len(spec['profiles'])}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--work-dir", type=Path, default=REPO_ROOT / "_ci")
    parser.add_argument("--base-archive", type=Path)
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--list-profiles", action="store_true")
    parser.add_argument("--list-profiles-json", action="store_true")
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--run-profile")
    parser.add_argument("--run-all", action="store_true")
    args = parser.parse_args()

    spec = load_spec(args.spec.resolve())
    errors = validate_spec(spec)
    if args.validate:
        if errors:
            for error in errors:
                print(f"MATRIX_SPEC_ERROR: {error}", file=sys.stderr)
            return 1
        print(f"LELY_MATRIX_SPEC_PASS profiles={len(spec['profiles'])}")
    if args.list_profiles:
        for profile in spec["profiles"]:
            print(profile["name"])
    if args.list_profiles_json:
        print(json.dumps([profile["name"] for profile in spec["profiles"]], separators=(",", ":")))
    if args.prepare:
        return prepare(spec, args.work_dir.resolve())
    if args.run_profile:
        if args.base_archive is None:
            parser.error("--run-profile requires --base-archive")
        return run_profile(spec, args.run_profile, args.base_archive, args.work_dir.resolve())
    if args.run_all:
        return run_all(spec, args.work_dir.resolve())
    if not (
        args.validate
        or args.list_profiles
        or args.list_profiles_json
        or args.prepare
        or args.run_profile
        or args.run_all
    ):
        parser.error(
            "one of --validate, --list-profiles, --list-profiles-json, --prepare, "
            "--run-profile or --run-all is required"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
