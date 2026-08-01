#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capability-aware smoke test for a packaged macOS Rigs of Rods app.

The packaged executable is always exercised through two pre-render command
paths.  A full renderer/script smoke is additionally required when the host can
create the same accelerated OpenGL 3.2 Core context that OGRE GL3Plus requests.
Hosts without that optional capability are recognized only through the
dedicated native probe's explicit exit contract; application crashes are never
treated as a capability skip.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


GL3_UNAVAILABLE_EXIT = 78
GL3_AVAILABLE_MARKER = "ROR_MACOS_GL3_CAPABILITY=available"
GL3_UNAVAILABLE_MARKER = "ROR_MACOS_GL3_CAPABILITY=unavailable"
GL3_ERROR_MARKER = "ROR_MACOS_GL3_CAPABILITY=error"

PRE_RENDER_PATH_MARKER = "[RoR|Startup|Paths]"
VERSION_MARKER = "Version Information: Rigs of Rods"
HELP_MARKER = "Command Line Arguments: --help (this)"

ENGINE_REQUIRED_MARKERS = (
    "[RoR|Startup|Rendering] Creating SDL host",
    "OpenGL 3+ Renderer Started",
    "SoundManager: OpenAL renderer is: OpenAL Soft",
    "[RoR|Shutdown] Physics and graphics worker pools released",
    "*-*-* OGRE Shutdown",
)
SHUTDOWN_ENGINE_REQUIRED_MARKERS = (
    "[RoR|Shutdown] Leaving the main loop after the shutdown message",
    "[RoR|Shutdown] Physics and graphics worker pools released",
    "*** Terminating OIS ***",
    "[RoR|Shutdown] Window-bound runtime integrations released",
    "[RoR|Shutdown] Environment map renderer resources released",
    "*-*-* OGRE Shutdown",
)
SCRIPT_REQUIRED_MARKERS = (
    "[RoR|CI|BundleSmoke] START",
    "[RoR|CI|BundleSmoke] PASS frames=10",
)
FATAL_DIAGNOSTIC = re.compile(
    r"RenderingAPIException|OGRE EXCEPTION|An exception.*occur+ed|"
    r"EXC_BAD_ACCESS|Segmentation fault"
)


class SmokeFailure(RuntimeError):
    """An expected fail-closed smoke-test diagnostic."""


def decode_output(payload: bytes | str | None) -> str:
    if payload is None:
        return ""
    if isinstance(payload, str):
        return payload
    return payload.decode("utf-8", errors="replace")


def classify_gl3_capability(returncode: int, output: str) -> bool:
    """Return True when GL3 is available and False for an explicit skip."""

    lines = output.splitlines()
    available_lines = [
        line for line in lines if line.startswith(GL3_AVAILABLE_MARKER)
    ]
    unavailable_lines = [
        line for line in lines if line.startswith(GL3_UNAVAILABLE_MARKER)
    ]
    error_lines = [
        line for line in lines if line.startswith(GL3_ERROR_MARKER)
    ]

    if returncode == 0:
        if (
            len(available_lines) != 1
            or unavailable_lines
            or error_lines
        ):
            raise SmokeFailure(
                "GL3 probe succeeded without one unambiguous available marker"
            )
        return True

    if returncode == GL3_UNAVAILABLE_EXIT:
        if (
            len(unavailable_lines) != 1
            or available_lines
            or error_lines
        ):
            raise SmokeFailure(
                "GL3 probe reported unavailable without one unambiguous "
                "unavailable marker"
            )
        return False

    raise SmokeFailure(f"GL3 probe failed unexpectedly with exit {returncode}")


def validate_pre_render(
    label: str,
    returncode: int,
    output: str,
    command_marker: str,
) -> None:
    if returncode != 0:
        if returncode < 0:
            raise SmokeFailure(
                f"packaged {label} pre-render check terminated by signal "
                f"{-returncode}"
            )
        raise SmokeFailure(
            f"packaged {label} pre-render check exited with {returncode}"
        )

    for marker in (PRE_RENDER_PATH_MARKER, command_marker):
        if marker not in output:
            raise SmokeFailure(
                f"packaged {label} pre-render check missed marker: {marker}"
            )


def validate_runtime_smoke(
    returncode: int,
    runtime_stdout: str,
    engine_log: str,
    script_log: str,
) -> None:
    if returncode != 0:
        if returncode < 0:
            raise SmokeFailure(
                f"RoR runtime smoke terminated by signal {-returncode}"
            )
        raise SmokeFailure(f"RoR runtime smoke exited with {returncode}")

    for marker in ENGINE_REQUIRED_MARKERS:
        if marker not in engine_log:
            raise SmokeFailure(f"engine log missed runtime marker: {marker}")
    shutdown_positions: list[int] = []
    for marker in SHUTDOWN_ENGINE_REQUIRED_MARKERS:
        count = engine_log.count(marker)
        if count != 1:
            raise SmokeFailure(
                f"engine log contains {count} copies of shutdown marker: "
                f"{marker}"
            )
        shutdown_positions.append(engine_log.index(marker))
    if shutdown_positions != sorted(shutdown_positions):
        raise SmokeFailure("engine shutdown markers are out of order")
    for marker in SCRIPT_REQUIRED_MARKERS:
        if marker not in script_log:
            raise SmokeFailure(f"script log missed runtime marker: {marker}")

    combined = "\n".join((runtime_stdout, engine_log, script_log))
    match = FATAL_DIAGNOSTIC.search(combined)
    if match is not None:
        raise SmokeFailure(
            f"fatal renderer/runtime diagnostic was logged: {match.group(0)}"
        )


def run_command(command: Sequence[str], timeout: int) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            list(command),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        rendered = " ".join(command)
        raise SmokeFailure(
            f"command did not finish within {timeout} seconds: {rendered}"
        ) from exc


def read_required_log(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError as exc:
        raise SmokeFailure(f"{label} was not created: {path}") from exc


def remove_if_present(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def log_identity(path: Path) -> tuple[int, int, int, int] | None:
    try:
        metadata = path.stat()
    except FileNotFoundError:
        return None
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_size,
        metadata.st_mtime_ns,
    )


def require_fresh_log(
    path: Path,
    previous_identity: tuple[int, int, int, int] | None,
    label: str,
) -> str:
    current_identity = log_identity(path)
    if current_identity is None:
        raise SmokeFailure(f"{label} was not created: {path}")
    if current_identity == previous_identity:
        raise SmokeFailure(f"{label} was not refreshed by the runtime smoke")
    return read_required_log(path, label)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--runtime-stdout", required=True, type=Path)
    parser.add_argument("--engine-log", required=True, type=Path)
    parser.add_argument("--script-log", required=True, type=Path)
    parser.add_argument("--pre-render-log", required=True, type=Path)
    parser.add_argument("--probe-log", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    for path in (
        args.runtime_stdout,
        args.pre_render_log,
        args.probe_log,
    ):
        path.parent.mkdir(parents=True, exist_ok=True)
        remove_if_present(path)

    pre_render_sections: list[str] = []
    for label, option, marker in (
        ("version", "-version", VERSION_MARKER),
        ("help", "--help", HELP_MARKER),
    ):
        result = run_command((str(args.executable), option), args.timeout)
        output = decode_output(result.stdout)
        pre_render_sections.append(
            f"===== {label} exit={result.returncode} =====\n{output}"
        )
        args.pre_render_log.write_text(
            "\n".join(pre_render_sections), encoding="utf-8"
        )
        validate_pre_render(label, result.returncode, output, marker)

    probe_result = run_command((str(args.probe),), args.timeout)
    probe_output = decode_output(probe_result.stdout)
    args.probe_log.write_text(probe_output, encoding="utf-8")
    print(probe_output, end="" if probe_output.endswith("\n") else "\n")
    gl3_available = classify_gl3_capability(
        probe_result.returncode, probe_output
    )
    if not gl3_available:
        print(
            "::warning title=macOS GL3 runtime smoke skipped::"
            "This runner cannot create OGRE's accelerated OpenGL 3.2 Core "
            "context. Packaged version/help startup checks passed; the full "
            "renderer smoke remains mandatory on capable hosts."
        )
        return 0

    # Pre-render invocations must never satisfy the renderer assertions through
    # stale logs left by another process or previous workflow attempt.
    remove_if_present(args.runtime_stdout)
    engine_log_before = log_identity(args.engine_log)
    script_log_before = log_identity(args.script_log)

    runtime_result = run_command(
        (
            str(args.executable),
            "-runscript",
            "example_ci_bundle_smoke.as",
        ),
        args.timeout,
    )
    runtime_stdout = decode_output(runtime_result.stdout)
    args.runtime_stdout.write_text(runtime_stdout, encoding="utf-8")
    if runtime_result.returncode != 0:
        validate_runtime_smoke(runtime_result.returncode, runtime_stdout, "", "")
    engine_log = require_fresh_log(
        args.engine_log, engine_log_before, "RoR engine log"
    )
    script_log = require_fresh_log(
        args.script_log, script_log_before, "AngelScript log"
    )
    validate_runtime_smoke(
        runtime_result.returncode,
        runtime_stdout,
        engine_log,
        script_log,
    )
    print("macOS packaged renderer smoke passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SmokeFailure as exc:
        print(f"macOS bundle smoke failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
