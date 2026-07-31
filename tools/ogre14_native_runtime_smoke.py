#!/usr/bin/env python3
"""Fail-closed ten-frame renderer smoke for relocated OGRE 14 builds."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import ntpath
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


SCRIPT_NAME = "example_ci_bundle_smoke.as"
SCRIPT_REQUIRED_MARKERS = (
    "[RoR|CI|BundleSmoke] START",
    "[RoR|CI|BundleSmoke] PASS frames=10",
)
COMMON_ENGINE_REQUIRED_MARKERS = (
    "[RoR|Startup|Paths]",
    "[RoR|Startup|Rendering] Starting renderer '",
    "[RoR|Startup|Rendering] Creating render window with settings:",
    "RenderSystem::_createRenderWindow",
    "[RoR|Shutdown] Leaving the main loop after the shutdown message",
    "*** Terminating OIS ***",
    "[RoR|Shutdown] Window-bound runtime integrations released",
    "*-*-* OGRE Shutdown",
)
SHUTDOWN_ENGINE_REQUIRED_MARKERS = (
    "[RoR|Shutdown] Leaving the main loop after the shutdown message",
    "*** Terminating OIS ***",
    "[RoR|Shutdown] Window-bound runtime integrations released",
    "*-*-* OGRE Shutdown",
)
LINUX_ENGINE_REQUIRED_MARKERS = (
    "Installing plugin: GL 3+ RenderSystem",
    "OpenGL 3+ Rendering Subsystem created.",
    "OpenGL 3+ Renderer Started",
    "RenderSystem Name: OpenGL 3+ Rendering Subsystem",
)
WINDOWS_ENGINE_REQUIRED_PATTERNS = (
    r"Installing plugin: [^\r\n]*D3D11[^\r\n]*RenderSystem",
    r"Starting renderer '[^']*(?:Direct3D11|D3D11)[^']*'",
    r"RenderSystem Name: [^\r\n]*(?:Direct3D11|D3D11)",
)
LINUX_LLVMPIPE_PATTERN = (
    r"(?i)(?:GL_RENDERER\s*=|Device Name:)[^\r\n]*\bllvmpipe\b"
)
FATAL_DIAGNOSTIC = re.compile(
    r"RenderingAPIException|OGRE EXCEPTION|"
    r"An exception[^\r\n]*occur+ed|"
    r"Segmentation fault|Access violation|Unhandled exception|"
    r"D3D11 device (?:lost|removed)|"
    r"No render system plugin available",
    re.IGNORECASE,
)


class SmokeFailure(RuntimeError):
    """The native renderer did not satisfy the complete smoke contract."""


@dataclass(frozen=True)
class PlatformContract:
    executable: str
    user_directory_parts: tuple[str, ...]
    engine_markers: tuple[str, ...]
    engine_patterns: tuple[str, ...] = ()


PLATFORM_CONTRACTS = {
    "linux-x86_64": PlatformContract(
        executable="RunRoR",
        user_directory_parts=(".rigsofrods",),
        engine_markers=LINUX_ENGINE_REQUIRED_MARKERS,
        engine_patterns=(LINUX_LLVMPIPE_PATTERN,),
    ),
    "windows-x86_64": PlatformContract(
        executable="RoR.exe",
        user_directory_parts=("My Games", "Rigs of Rods"),
        engine_markers=(),
        engine_patterns=WINDOWS_ENGINE_REQUIRED_PATTERNS,
    ),
}


def is_within(root: Path, candidate: Path) -> bool:
    """Return whether candidate is root or a descendant."""

    root = root.resolve(strict=True)
    candidate = candidate.resolve(strict=True)
    return candidate == root or root in candidate.parents


def require_absolute_directory(
    path: Path,
    *,
    label: str,
    create: bool,
) -> Path:
    """Resolve one caller path without silently accepting relative inputs."""

    if not path.is_absolute():
        raise SmokeFailure(f"{label} must be absolute: {path}")
    if create:
        path.mkdir(parents=True, exist_ok=True)
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise SmokeFailure(f"{label} is unavailable: {path}: {error}") from error
    if not resolved.is_dir():
        raise SmokeFailure(f"{label} is not a directory: {resolved}")
    return resolved


def log_identity(path: Path) -> tuple[int, int, int, int] | None:
    """Return enough metadata to distinguish a refreshed log."""

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
    *,
    label: str,
) -> str:
    """Read a UTF-8 log only if the just-finished process refreshed it."""

    identity = log_identity(path)
    if identity is None:
        raise SmokeFailure(f"{label} was not created: {path}")
    if identity == previous_identity:
        raise SmokeFailure(f"{label} was not refreshed: {path}")
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise SmokeFailure(f"{label} cannot be read: {path}: {error}") from error


def runtime_environment(
    platform: str,
    log_root: Path,
    base_environment: dict[str, str] | None = None,
) -> dict[str, str]:
    """Build an isolated deterministic runtime environment."""

    environment = dict(
        os.environ if base_environment is None else base_environment
    )
    stripped_variables = (
        "CMAKE_PREFIX_PATH",
        "CONAN_HOME",
        "DYLD_FALLBACK_LIBRARY_PATH",
        "DYLD_LIBRARY_PATH",
        "LD_AUDIT",
        "LD_DEBUG",
        "LD_DEBUG_OUTPUT",
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
        "MESA_LOADER_DRIVER_OVERRIDE",
        "OGRE_PLUGIN_DIR",
        "ROR_D0_SCENE_HOME",
    )
    stripped_keys = {variable.casefold() for variable in stripped_variables}
    for variable in tuple(environment):
        if variable.casefold() in stripped_keys:
            environment.pop(variable)
    environment["LANG"] = "C"
    environment["LC_ALL"] = "C"
    environment["ROR_D0_SCENE_HOME"] = str(log_root)
    if platform == "linux-x86_64":
        environment["GALLIUM_DRIVER"] = "llvmpipe"
        environment["LIBGL_ALWAYS_SOFTWARE"] = "1"
    else:
        environment.pop("GALLIUM_DRIVER", None)
        environment.pop("LIBGL_ALWAYS_SOFTWARE", None)
        system_root = next(
            (
                value
                for preferred_name in ("systemroot", "windir")
                for name, value in environment.items()
                if name.casefold() == preferred_name
            ),
            None,
        )
        if system_root is None or not ntpath.isabs(system_root):
            raise SmokeFailure(
                "Windows runtime smoke requires an absolute SystemRoot"
            )
        for variable in tuple(environment):
            if variable.casefold() == "path":
                environment.pop(variable)
        environment["PATH"] = ";".join(
            (
                ntpath.join(system_root, "System32"),
                system_root,
            )
        )
    return environment


def run_runtime(
    command: Sequence[str],
    *,
    cwd: Path,
    environment: dict[str, str],
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    """Run the real renderer loop with bounded stdout capture."""

    try:
        return subprocess.run(
            list(command),
            check=False,
            cwd=cwd,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        if isinstance(output, str):
            output = output.encode("utf-8", errors="replace")
        raise SmokeFailure(
            f"native renderer did not finish within {timeout}s; "
            f"partial output={output.decode('utf-8', errors='replace')!r}"
        ) from error
    except OSError as error:
        raise SmokeFailure(f"native renderer could not start: {error}") from error


def validate_runtime_evidence(
    platform: str,
    *,
    returncode: int,
    runtime_output: str,
    engine_log: str,
    script_log: str,
    expected_user_directory: Path,
) -> None:
    """Require renderer startup, ten frames, isolation, and clean shutdown."""

    if returncode != 0:
        if returncode < 0:
            raise SmokeFailure(
                f"native renderer terminated by signal {-returncode}"
            )
        raise SmokeFailure(f"native renderer exited with {returncode}")

    contract = PLATFORM_CONTRACTS[platform]
    for marker in (
        *COMMON_ENGINE_REQUIRED_MARKERS,
        *contract.engine_markers,
    ):
        if marker not in engine_log:
            raise SmokeFailure(f"engine log missed runtime marker: {marker}")
    for pattern in contract.engine_patterns:
        if re.search(pattern, engine_log) is None:
            raise SmokeFailure(
                f"engine log missed runtime pattern: {pattern}"
            )

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

    marker_positions: list[int] = []
    for marker in SCRIPT_REQUIRED_MARKERS:
        count = script_log.count(marker)
        if count != 1:
            raise SmokeFailure(
                f"script log contains {count} copies of marker: {marker}"
            )
        marker_positions.append(script_log.index(marker))
    if marker_positions != sorted(marker_positions):
        raise SmokeFailure("script START/PASS markers are out of order")

    normalized_engine_log = engine_log.replace("\\", "/").casefold()
    normalized_user_directory = (
        str(expected_user_directory).replace("\\", "/").casefold()
    )
    if normalized_user_directory not in normalized_engine_log:
        raise SmokeFailure(
            "engine log does not prove the isolated user directory: "
            f"{expected_user_directory}"
        )

    combined = "\n".join((runtime_output, engine_log, script_log))
    fatal = FATAL_DIAGNOSTIC.search(combined)
    if fatal is not None:
        raise SmokeFailure(
            f"fatal renderer/runtime diagnostic was logged: {fatal.group(0)}"
        )


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def smoke(
    root: Path,
    platform: str,
    log_root: Path,
    *,
    timeout: int,
) -> dict[str, object]:
    """Run one native ten-frame smoke and return its evidence report."""

    root = require_absolute_directory(root, label="runtime root", create=False)
    log_root = require_absolute_directory(
        log_root,
        label="runtime log root",
        create=True,
    )
    if is_within(root, log_root) or is_within(log_root, root):
        raise SmokeFailure(
            "runtime root and isolated log root must not contain each other"
        )
    contract = PLATFORM_CONTRACTS[platform]
    executable = root / contract.executable
    if not executable.is_file():
        raise SmokeFailure(f"native runtime executable is absent: {executable}")
    if platform == "linux-x86_64" and not os.access(executable, os.X_OK):
        raise SmokeFailure(f"Linux runtime launcher is not executable: {executable}")

    outside_cwd = log_root / "outside-package-cwd"
    outside_cwd.mkdir(exist_ok=True)
    if is_within(root, outside_cwd):
        raise SmokeFailure("native renderer cwd is inside the package")
    user_directory = log_root.joinpath(*contract.user_directory_parts)
    engine_log_path = user_directory / "logs" / "RoR.log"
    script_log_path = user_directory / "logs" / "Angelscript.log"
    engine_identity = log_identity(engine_log_path)
    script_identity = log_identity(script_log_path)

    environment = runtime_environment(platform, log_root)
    command = (str(executable), "-runscript", SCRIPT_NAME)
    result = run_runtime(
        command,
        cwd=outside_cwd,
        environment=environment,
        timeout=timeout,
    )
    runtime_output = (result.stdout or b"").decode(
        "utf-8",
        errors="replace",
    )
    runtime_output_path = log_root / "runtime.stdout.log"
    runtime_output_path.write_text(runtime_output, encoding="utf-8")
    if result.returncode != 0:
        validate_runtime_evidence(
            platform,
            returncode=result.returncode,
            runtime_output=runtime_output,
            engine_log="",
            script_log="",
            expected_user_directory=user_directory,
        )
    engine_log = require_fresh_log(
        engine_log_path,
        engine_identity,
        label="RoR engine log",
    )
    script_log = require_fresh_log(
        script_log_path,
        script_identity,
        label="AngelScript log",
    )
    validate_runtime_evidence(
        platform,
        returncode=result.returncode,
        runtime_output=runtime_output,
        engine_log=engine_log,
        script_log=script_log,
        expected_user_directory=user_directory,
    )
    report = {
        "command": [contract.executable, "-runscript", SCRIPT_NAME],
        "engine_log": str(engine_log_path),
        "engine_log_sha256": sha256_text(engine_log),
        "frames": 10,
        "log_root": str(log_root),
        "outside_package_cwd": str(outside_cwd),
        "platform": platform,
        "renderer": (
            "OpenGL 3+ Rendering Subsystem (llvmpipe)"
            if platform == "linux-x86_64"
            else "Direct3D11 Rendering Subsystem"
        ),
        "runtime_output": str(runtime_output_path),
        "script": SCRIPT_NAME,
        "script_log": str(script_log_path),
        "script_log_sha256": sha256_text(script_log),
    }
    report_path = log_root / "runtime-smoke-report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--platform",
        choices=sorted(PLATFORM_CONTRACTS),
        required=True,
    )
    parser.add_argument("--log-root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    arguments = parser.parse_args(argv)
    if arguments.timeout <= 0:
        parser.error("--timeout must be positive")
    return arguments


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = smoke(
            arguments.root,
            arguments.platform,
            arguments.log_root,
            timeout=arguments.timeout,
        )
    except (OSError, SmokeFailure) as error:
        print(f"OGRE 14 native runtime smoke failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
