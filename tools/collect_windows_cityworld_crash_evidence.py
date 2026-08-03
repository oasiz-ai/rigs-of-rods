#!/usr/bin/env python3
"""Collect fail-closed Windows CityWorld crash and symbol evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from typing import Sequence


FORMAT = "ror-windows-cityworld-crash-evidence-v1"
ACCESS_VIOLATION = 0xC0000005
MODES = ("async", "sync")


class EvidenceFailure(RuntimeError):
    """Raised when required Windows crash evidence is incomplete."""


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
    termination = document.get("termination")
    if not isinstance(termination, dict):
        raise EvidenceFailure(
            "runtime process diagnostic has no termination object"
        )
    return document


def exit_code_record(
    diagnostic: dict[str, object] | None,
) -> dict[str, object] | None:
    if diagnostic is None:
        return None
    termination = diagnostic.get("termination")
    if not isinstance(termination, dict):
        raise EvidenceFailure(
            "runtime process diagnostic has no termination object"
        )
    raw = termination.get("returncode")
    if not isinstance(raw, int) or isinstance(raw, bool):
        raise EvidenceFailure("native return code is not an integer")
    unsigned = raw & 0xFFFFFFFF
    signed = unsigned if unsigned < 0x80000000 else unsigned - 0x100000000
    record: dict[str, object] = {
        "hex": f"0x{unsigned:08X}",
        "kind": termination.get("kind"),
        "raw": raw,
        "signed": signed,
        "unsigned": unsigned,
    }
    meaning = termination.get("meaning")
    if isinstance(meaning, str):
        record["meaning"] = meaning
    return record


def dump_files(dump_dir: Path) -> tuple[Path, ...]:
    if not dump_dir.exists():
        return ()
    if not dump_dir.is_dir() or dump_dir.is_symlink():
        raise EvidenceFailure(
            f"WER dump destination is not a directory: {dump_dir}"
        )
    return tuple(
        sorted(
            (
                path.resolve()
                for path in dump_dir.glob("*.dmp")
                if path.is_file()
                and not path.is_symlink()
                and path.stat().st_size > 0
            ),
            key=lambda path: path.name.casefold(),
        )
    )


def await_required_dumps(
    dump_dir: Path,
    required: bool,
    poll_attempts: int,
    poll_interval_ms: int,
) -> tuple[Path, ...]:
    attempts = poll_attempts if required else 1
    for attempt in range(attempts):
        dumps = dump_files(dump_dir)
        if dumps or not required:
            return dumps
        if attempt + 1 < attempts:
            time.sleep(poll_interval_ms / 1000.0)
    return ()


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
    dump_dir: Path,
    executable: Path,
    pdb: Path,
    output: Path,
    poll_attempts: int,
    poll_interval_ms: int,
) -> dict[str, object]:
    if mode not in MODES:
        raise EvidenceFailure(f"unsupported physics mode: {mode}")
    if poll_attempts <= 0:
        raise EvidenceFailure("poll attempts must be positive")
    if poll_interval_ms < 0 or poll_interval_ms > 1000:
        raise EvidenceFailure("poll interval must be between 0 and 1000 ms")

    executable = require_regular_file(executable, "RoR executable")
    pdb = require_regular_file(pdb, "RoR program database")
    diagnostic = load_process_diagnostic(process_diagnostic)
    native_exit = exit_code_record(diagnostic)
    dump_required = (
        native_exit is not None
        and native_exit["unsigned"] == ACCESS_VIOLATION
    )
    dumps = await_required_dumps(
        dump_dir.resolve(),
        dump_required,
        poll_attempts,
        poll_interval_ms,
    )
    status = (
        "captured"
        if dump_required and dumps
        else "required_missing"
        if dump_required
        else "not_required"
    )
    dump_records = [
        file_record(
            path,
            f"dumps/{mode}/{path.name}",
        )
        for path in dumps
    ]
    document: dict[str, object] = {
        "binaries": {
            "executable": file_record(executable, "runtime/RoR.exe"),
            "pdb": file_record(pdb, "symbols/RoR.pdb"),
        },
        "driver_exit_code": driver_exit_code,
        "dump_capture": {
            "required": dump_required,
            "status": status,
        },
        "dumps": dump_records,
        "format": FORMAT,
        "mode": mode,
        "native_exit_code": native_exit,
        "process_diagnostic": (
            str(process_diagnostic.resolve())
            if diagnostic is not None
            else None
        ),
    }
    write_manifest(output.resolve(), document)
    if dump_required and not dumps:
        raise EvidenceFailure(
            "WER evidence error: native access violation 0xC0000005 "
            f"produced no nonempty .dmp in {dump_dir}"
        )
    return document


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=MODES, required=True)
    parser.add_argument("--driver-exit-code", type=int, required=True)
    parser.add_argument("--process-diagnostic", type=Path, required=True)
    parser.add_argument("--dump-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--pdb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--poll-attempts", type=int, default=40)
    parser.add_argument("--poll-interval-ms", type=int, default=250)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        document = collect(
            mode=args.mode,
            driver_exit_code=args.driver_exit_code,
            process_diagnostic=args.process_diagnostic,
            dump_dir=args.dump_dir,
            executable=args.executable,
            pdb=args.pdb,
            output=args.output,
            poll_attempts=args.poll_attempts,
            poll_interval_ms=args.poll_interval_ms,
        )
    except EvidenceFailure as error:
        print(f"Windows CityWorld crash evidence failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(document, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
