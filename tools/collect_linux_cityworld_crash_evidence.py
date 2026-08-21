#!/usr/bin/env python3
"""Collect fail-closed Linux CityWorld core and backtrace evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from typing import Sequence


FORMAT = "ror-linux-cityworld-crash-evidence-v1"
MODES = ("async", "sync")


class EvidenceFailure(RuntimeError):
    """Raised when required Linux crash evidence is incomplete."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_regular_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise EvidenceFailure(f"{label} is not a regular file: {path}")
    resolved = path.resolve()
    if resolved.stat().st_size <= 0:
        raise EvidenceFailure(f"{label} is empty: {path}")
    return resolved


def load_process_diagnostic(path: Path) -> dict[str, object] | None:
    if not path.exists():
        return None
    source = require_regular_file(path, "runtime process diagnostic")
    try:
        document = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise EvidenceFailure(
            f"runtime process diagnostic is invalid: {path}"
        ) from error
    if not isinstance(document, dict):
        raise EvidenceFailure("runtime process diagnostic is not an object")
    if document.get("target_platform") != "linux":
        raise EvidenceFailure("runtime process diagnostic is not for Linux")
    termination = document.get("termination")
    if not isinstance(termination, dict):
        raise EvidenceFailure(
            "runtime process diagnostic has no termination object"
        )
    return document


def termination_record(
    diagnostic: dict[str, object] | None,
) -> dict[str, object] | None:
    if diagnostic is None:
        return None
    termination = diagnostic.get("termination")
    if not isinstance(termination, dict):
        raise EvidenceFailure(
            "runtime process diagnostic has no termination object"
        )
    returncode = termination.get("returncode")
    if not isinstance(returncode, int) or isinstance(returncode, bool):
        raise EvidenceFailure("native return code is not an integer")
    record: dict[str, object] = {
        "kind": termination.get("kind"),
        "returncode": returncode,
    }
    signal = termination.get("signal")
    if returncode < 0:
        if signal != -returncode:
            raise EvidenceFailure(
                "native signal does not match the signed return code"
            )
        record["signal"] = signal
    elif signal is not None:
        raise EvidenceFailure("non-signal exit unexpectedly names a signal")
    return record


def core_files(core_dir: Path) -> tuple[Path, ...]:
    if not core_dir.exists():
        return ()
    if not core_dir.is_dir() or core_dir.is_symlink():
        raise EvidenceFailure(
            f"Linux core destination is not a directory: {core_dir}"
        )
    return tuple(
        sorted(
            (
                path.resolve()
                for path in core_dir.glob("core.*")
                if path.is_file()
                and not path.is_symlink()
                and path.stat().st_size > 0
            ),
            key=lambda path: path.name,
        )
    )


def file_record(path: Path, artifact_path: str) -> dict[str, object]:
    return {
        "artifact": artifact_path,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def write_manifest(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(document, indent=2, ensure_ascii=True, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def collect(
    *,
    mode: str,
    driver_exit_code: int,
    process_diagnostic: Path,
    core_dir: Path,
    executable: Path,
    backtrace: Path,
    output: Path,
) -> dict[str, object]:
    if mode not in MODES:
        raise EvidenceFailure(f"unsupported physics mode: {mode}")
    executable = require_regular_file(executable, "RoR executable")
    diagnostic = load_process_diagnostic(process_diagnostic)
    native_exit = termination_record(diagnostic)
    if native_exit is None and driver_exit_code == 0:
        raise EvidenceFailure(
            "successful driver exit has no native process diagnostic"
        )
    if native_exit is not None:
        native_succeeded = native_exit["returncode"] == 0
        driver_succeeded = driver_exit_code == 0
        if native_succeeded != driver_succeeded:
            raise EvidenceFailure(
                "driver and native process success states disagree"
            )
    crash_required = (
        native_exit is not None
        and native_exit["returncode"] < 0
    )
    cores = core_files(core_dir.resolve())
    resolved_backtrace: Path | None = None
    if backtrace.exists():
        resolved_backtrace = require_regular_file(
            backtrace, "Linux debugger backtrace"
        )

    core_status = (
        "captured"
        if crash_required and len(cores) == 1
        else "required_missing"
        if crash_required and not cores
        else "ambiguous"
        if crash_required
        else "unexpected"
        if cores
        else "not_required"
    )
    backtrace_status = (
        "captured"
        if crash_required and resolved_backtrace is not None
        else "required_missing"
        if crash_required
        else "unexpected"
        if resolved_backtrace is not None
        else "not_required"
    )
    document: dict[str, object] = {
        "backtrace": (
            file_record(resolved_backtrace, f"backtraces/{mode}.txt")
            if resolved_backtrace is not None
            else None
        ),
        "backtrace_capture": {
            "required": crash_required,
            "status": backtrace_status,
        },
        "binaries": {
            "executable": file_record(
                executable, f"runtime/{executable.name}"
            ),
        },
        "core_capture": {
            "required": crash_required,
            "status": core_status,
        },
        "cores": [
            file_record(path, f"cores/{mode}/{path.name}")
            for path in cores
        ],
        "driver_exit_code": driver_exit_code,
        "format": FORMAT,
        "mode": mode,
        "native_exit": native_exit,
        "process_diagnostic": (
            str(process_diagnostic.resolve())
            if diagnostic is not None
            else None
        ),
    }
    write_manifest(output.resolve(), document)
    if crash_required and len(cores) != 1:
        raise EvidenceFailure(
            "Linux crash evidence requires exactly one nonempty core, "
            f"found {len(cores)} in {core_dir}"
        )
    if crash_required and resolved_backtrace is None:
        raise EvidenceFailure(
            "Linux crash evidence requires a nonempty debugger backtrace"
        )
    if not crash_required and (cores or resolved_backtrace is not None):
        raise EvidenceFailure(
            "successful or preflight run produced unexpected crash evidence"
        )
    return document


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=MODES, required=True)
    parser.add_argument("--driver-exit-code", type=int, required=True)
    parser.add_argument("--process-diagnostic", type=Path, required=True)
    parser.add_argument("--core-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--backtrace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        document = collect(
            mode=args.mode,
            driver_exit_code=args.driver_exit_code,
            process_diagnostic=args.process_diagnostic,
            core_dir=args.core_dir,
            executable=args.executable,
            backtrace=args.backtrace,
            output=args.output,
        )
    except EvidenceFailure as error:
        print(f"Linux CityWorld crash evidence failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(document, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
