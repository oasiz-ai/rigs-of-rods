#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run and compare the pinned D0 two-truck deterministic runtime scene.

The tool performs no downloads. It accepts an already built RoR executable,
verifies the repository and runtime copies of the pinned fixture content,
launches isolated exact-step runs, rejects renderer API diagnostics, validates
each completed state trace, and compares every run with the first one through
the canonical ror_state_trace tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
from typing import Iterable, Mapping, Sequence
import zipfile


CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"
SCENARIO_ID = 2026072801
SCENARIO_SCRIPT = "example_deterministic_two_truck_trace.as"
TERRAIN = "simple2.terrn2"
EXPECTED_STEPS = 1000
FIXTURE_PREFIXES = ("dafsemi/", "simple2-terrain/")

SCRIPT_MARKERS = (
    "[RoR|D0|TwoTruck] START",
    "[RoR|D0|TwoTruck] ARMED actors=2 nodes=352 "
    "samples_per_step=1056 first_step=0 batch=10",
    "[RoR|D0|TwoTruck] PASS actors=2 nodes=352 steps=1000",
)
ENGINE_MARKERS = (
    "[RoR|Determinism] Recording state trace",
    "scenario=2026072801",
    "limit=1000",
    "with 1000 fixed-step records (trace step limit reached)",
)
FATAL_MARKERS = (
    "[RoR|D0|TwoTruck] FAIL",
    "State trace snapshot failed",
    "State trace append failed",
    "could not be finished",
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)


class SceneFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid scene run or artifact."""


def decode_output(payload: bytes | str | None) -> str:
    if payload is None:
        return ""
    if isinstance(payload, str):
        return payload
    return payload.decode("utf-8", errors="replace")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_command(
    command: Sequence[str],
    timeout: int,
    *,
    cwd: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            list(command),
            cwd=None if cwd is None else str(cwd),
            env=None if environment is None else dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SceneFailure(
            f"command exceeded {timeout} seconds: {' '.join(command)}"
        ) from exc


def git_output(repository: Path, arguments: Sequence[str]) -> str:
    result = run_command(
        ("git", "-C", str(repository), *arguments),
        timeout=30,
    )
    output = decode_output(result.stdout)
    if result.returncode != 0:
        raise SceneFailure(
            f"git {' '.join(arguments)} failed in {repository}: {output}"
        )
    return output.strip()


def verify_repository_content(
    repository_root: Path,
) -> tuple[Path, tuple[str, ...]]:
    content_root = repository_root / "content"
    commit = git_output(content_root, ("rev-parse", "HEAD"))
    if commit != CONTENT_COMMIT:
        raise SceneFailure(
            f"content commit drift: expected {CONTENT_COMMIT}, got {commit}"
        )

    listing = git_output(content_root, ("ls-files", "-z"))
    tracked = tuple(
        sorted(
            path
            for path in listing.split("\0")
            if path.startswith(FIXTURE_PREFIXES)
        )
    )
    if not tracked:
        raise SceneFailure("pinned D0 fixture inventory is empty")
    for required in (
        "dafsemi/b6b0UID-semi.truck",
        "simple2-terrain/simple2.terrn2",
    ):
        if required not in tracked:
            raise SceneFailure(f"pinned fixture is missing {required}")
    return content_root, tracked


def verify_runtime_fixture_files(
    source_content: Path,
    runtime_content: Path,
    tracked_paths: Iterable[str],
) -> None:
    tracked = tuple(tracked_paths)
    archive_members: dict[str, set[str]] = {}
    for relative in tracked:
        prefix, separator, member = relative.partition("/")
        if not separator or not prefix or not member:
            raise SceneFailure(f"invalid pinned fixture path: {relative}")
        archive_members.setdefault(prefix, set()).add(member)

    archives: dict[str, zipfile.ZipFile] = {}
    try:
        for prefix, expected_members in archive_members.items():
            archive_path = runtime_content / f"{prefix}.zip"
            if archive_path.is_file():
                try:
                    archive = zipfile.ZipFile(archive_path, "r")
                    actual_members = {
                        name
                        for name in archive.namelist()
                        if name and not name.endswith("/")
                    }
                except (OSError, zipfile.BadZipFile) as exc:
                    raise SceneFailure(
                        f"runtime fixture archive is invalid: {archive_path}"
                    ) from exc
                if actual_members != expected_members:
                    archive.close()
                    missing = sorted(expected_members - actual_members)
                    unexpected = sorted(actual_members - expected_members)
                    raise SceneFailure(
                        f"runtime fixture archive inventory differs for "
                        f"{prefix}: missing={missing}, unexpected={unexpected}"
                    )
                archives[prefix] = archive

        for relative in tracked:
            source = source_content / relative
            runtime = runtime_content / relative
            if not source.is_file():
                raise SceneFailure(f"tracked fixture is not a file: {source}")

            if runtime.is_file():
                runtime_size = runtime.stat().st_size
                runtime_digest = sha256_file(runtime)
            else:
                prefix, _, member = relative.partition("/")
                archive = archives.get(prefix)
                if archive is None:
                    raise SceneFailure(
                        f"runtime fixture file or archive is missing: "
                        f"{runtime}"
                    )
                try:
                    payload = archive.read(member)
                except KeyError as exc:
                    raise SceneFailure(
                        f"runtime fixture archive entry is missing: {relative}"
                    ) from exc
                runtime_size = len(payload)
                runtime_digest = hashlib.sha256(payload).hexdigest()

            if source.stat().st_size != runtime_size:
                raise SceneFailure(
                    f"runtime fixture size differs from pinned source: "
                    f"{relative}"
                )
            if sha256_file(source) != runtime_digest:
                raise SceneFailure(
                    f"runtime fixture hash differs from pinned source: "
                    f"{relative}"
                )
    finally:
        for archive in archives.values():
            archive.close()


def write_runtime_config(
    isolated_home: Path,
    worker_count: int,
    force_cache_update: bool,
) -> Path:
    config_dir = (
        isolated_home
        / "Library"
        / "Application Support"
        / "Rigs of Rods"
        / "config"
    )
    config_dir.mkdir(parents=True, exist_ok=True)
    config_path = config_dir / "RoR.cfg"
    config_path.write_text(
        "\n".join(
            (
                "; Generated by tools/run_deterministic_scene.py",
                "app_config_long_names=false",
                "app_num_workers=" + str(worker_count),
                "app_async_physics=true",
                "app_disable_online_api=true",
                "app_force_cache_update="
                + ("true" if force_cache_update else "false"),
                "audio_master_volume=0",
                "gfx_fps_limit=0",
                "gfx_shadow_type=0",
                "gfx_sky_mode=0",
                "gfx_water_mode=0",
                "",
            )
        ),
        encoding="utf-8",
    )
    (config_dir / "ogre.cfg").write_text(
        "\n".join(
            (
                "Render System=OpenGL 3+ Rendering Subsystem",
                "",
                "[OpenGL 3+ Rendering Subsystem]",
                "Colour Depth=32",
                "Content Scaling Factor=1",
                "Debug Layer=Off",
                "Display Frequency=N/A",
                "FSAA= 0",
                "Full Screen=No",
                "Reversed Z-Buffer=No",
                "Separate Shader Objects=Yes",
                "VSync=No",
                "VSync Interval=1",
                "Video Mode=1280 x 720",
                "sRGB Gamma Conversion=No",
                "",
            )
        ),
        encoding="utf-8",
    )
    return config_path


def build_scene_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        # AppKit otherwise presents a crash-recovery modal after an earlier
        # failed run and silently blocks an unattended determinism gate.
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-map",
            TERRAIN,
            "-runscript",
            SCENARIO_SCRIPT,
        )
    )
    return tuple(command)


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> None:
    if returncode != 0:
        if returncode < 0:
            raise SceneFailure(
                f"RoR scene run terminated by signal {-returncode}"
            )
        raise SceneFailure(f"RoR scene run exited with {returncode}")

    for marker in SCRIPT_MARKERS:
        if marker not in script_log:
            raise SceneFailure(f"AngelScript log missed marker: {marker}")
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise SceneFailure(f"engine log missed marker: {marker}")

    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise SceneFailure(f"runtime logged a fatal marker: {marker}")


def read_required(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError as exc:
        raise SceneFailure(f"{label} was not created: {path}") from exc


def find_single_trace(log_directory: Path) -> Path:
    traces = sorted(log_directory.glob("*.rortrace"))
    if len(traces) != 1:
        raise SceneFailure(
            f"expected exactly one state trace in {log_directory}, "
            f"found {len(traces)}"
        )
    if traces[0].stat().st_size == 0:
        raise SceneFailure(f"state trace is empty: {traces[0]}")
    return traces[0]


def parse_trace_comparison(output: str) -> dict[str, object]:
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as exc:
        raise SceneFailure(
            f"state-trace tool emitted invalid JSON: {exc}"
        ) from exc
    if not isinstance(payload, dict):
        raise SceneFailure("state-trace comparison is not a JSON object")
    return payload


def validate_trace_comparison(
    payload: Mapping[str, object],
    expected_left_workers: int,
    expected_right_workers: int,
) -> None:
    if payload.get("format") != "ror-d0-state-trace-comparison-v2":
        raise SceneFailure("state-trace comparison format is unsupported")
    if payload.get("status") != "match":
        raise SceneFailure(
            "state trace diverged: "
            + json.dumps(payload, sort_keys=True, separators=(",", ":"))
        )
    if payload.get("steps_compared") != EXPECTED_STEPS:
        raise SceneFailure(
            f"trace has {payload.get('steps_compared')} compared steps; "
            f"expected {EXPECTED_STEPS}"
        )

    for side_name, expected_workers in (
        ("left", expected_left_workers),
        ("right", expected_right_workers),
    ):
        side = payload.get(side_name)
        if not isinstance(side, dict):
            raise SceneFailure(f"trace comparison missed {side_name} side")
        metadata = side.get("metadata")
        if not isinstance(metadata, dict):
            raise SceneFailure(
                f"trace comparison missed {side_name} metadata"
            )
        expected = {
            "worker_count": expected_workers,
            "scenario_id": SCENARIO_ID,
            "first_physics_step": 0,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
        }
        for field, value in expected.items():
            if metadata.get(field) != value:
                raise SceneFailure(
                    f"{side_name} trace metadata {field}="
                    f"{metadata.get(field)!r}; expected {value!r}"
                )


def compare_traces(
    trace_tool: Path,
    left: Path,
    right: Path,
    left_workers: int,
    right_workers: int,
    timeout: int,
) -> dict[str, object]:
    result = run_command(
        (
            str(trace_tool),
            "--allow-worker-count-difference",
            str(left),
            str(right),
        ),
        timeout,
    )
    output = decode_output(result.stdout)
    payload = parse_trace_comparison(output)
    if result.returncode != 0:
        raise SceneFailure(
            f"state-trace comparison exited with {result.returncode}: "
            f"{output}"
        )
    validate_trace_comparison(payload, left_workers, right_workers)
    return payload


def prepare_run_logs(log_directory: Path) -> tuple[Path, Path]:
    log_directory.mkdir(parents=True, exist_ok=True)
    for trace in log_directory.glob("*.rortrace"):
        trace.unlink()
    engine_log = log_directory / "RoR.log"
    script_log = log_directory / "Angelscript.log"
    for path in (engine_log, script_log):
        try:
            path.unlink()
        except FileNotFoundError:
            pass
    return engine_log, script_log


def infer_runtime_content(executable: Path) -> Path:
    candidate = executable.resolve().parent / "content"
    if not candidate.is_dir():
        raise SceneFailure(
            "could not infer runtime content beside the executable; "
            "supply --runtime-content"
        )
    return candidate.resolve()


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--trace-tool", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=30)
    parser.add_argument(
        "--workers",
        type=int,
        nargs="+",
        default=(1, 8),
    )
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.runs <= 0:
        parser.error("--runs must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not args.workers or any(worker <= 0 for worker in args.workers):
        parser.error("--workers must contain positive integers")
    if len(set(args.workers)) != len(args.workers):
        parser.error("--workers must not contain duplicates")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    executable = args.executable.resolve()
    trace_tool = args.trace_tool.resolve()
    repository = args.repository.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file():
        raise SceneFailure(f"RoR executable does not exist: {executable}")
    if not trace_tool.is_file():
        raise SceneFailure(f"state-trace tool does not exist: {trace_tool}")
    if artifact_dir.exists():
        raise SceneFailure(
            f"artifact directory already exists; choose a fresh path: "
            f"{artifact_dir}"
        )
    artifact_dir.mkdir(parents=True)

    source_content, tracked_fixture = verify_repository_content(repository)
    runtime_content = (
        infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise SceneFailure(
            f"runtime content directory does not exist: {runtime_content}"
        )
    verify_runtime_fixture_files(
        source_content,
        runtime_content,
        tracked_fixture,
    )

    repository_commit = git_output(repository, ("rev-parse", "HEAD"))
    isolated_home = artifact_dir / "work" / "d0-scene-home"
    log_directory = (
        isolated_home / "Library" / "Logs" / "Rigs of Rods"
    )
    trace_directory = artifact_dir / "traces"
    diagnostic_directory = artifact_dir / "diagnostics"
    trace_directory.mkdir(parents=True)
    diagnostic_directory.mkdir(parents=True)

    baseline_trace: Path | None = None
    baseline_workers = 0
    results: list[dict[str, object]] = []
    cache_initialized = False

    for worker_count in args.workers:
        for run_index in range(1, args.runs + 1):
            write_runtime_config(
                isolated_home,
                worker_count,
                not cache_initialized,
            )
            engine_log_path, script_log_path = prepare_run_logs(
                log_directory
            )
            environment = os.environ.copy()
            environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
            environment["ALSOFT_DRIVERS"] = "null"
            environment["ALSOFT_LOGLEVEL"] = "0"

            command = build_scene_command(executable)
            completed = run_command(
                command,
                args.timeout,
                cwd=executable.parent,
                environment=environment,
            )
            cache_initialized = True
            stdout = decode_output(completed.stdout)
            engine_log = read_required(engine_log_path, "RoR engine log")
            script_log = read_required(
                script_log_path,
                "AngelScript log",
            )
            validate_runtime_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
            )

            runtime_trace = find_single_trace(log_directory)
            label = f"worker-{worker_count:02d}-run-{run_index:02d}"
            trace_path = trace_directory / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostic_directory / f"{label}.stdout").write_text(
                stdout,
                encoding="utf-8",
            )
            (diagnostic_directory / f"{label}.RoR.log").write_text(
                engine_log,
                encoding="utf-8",
            )
            (
                diagnostic_directory / f"{label}.Angelscript.log"
            ).write_text(script_log, encoding="utf-8")

            compare_traces(
                trace_tool,
                trace_path,
                trace_path,
                worker_count,
                worker_count,
                args.timeout,
            )
            if baseline_trace is None:
                baseline_trace = trace_path
                baseline_workers = worker_count
            else:
                compare_traces(
                    trace_tool,
                    baseline_trace,
                    trace_path,
                    baseline_workers,
                    worker_count,
                    args.timeout,
                )

            trace_sha = sha256_file(trace_path)
            results.append(
                {
                    "run": run_index,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_sha256": trace_sha,
                    "workers": worker_count,
                }
            )
            print(
                f"D0 scene matched: workers={worker_count} "
                f"run={run_index}/{args.runs} sha256={trace_sha}"
            )

    report = {
        "content_commit": CONTENT_COMMIT,
        "executable": str(executable),
        "executable_sha256": sha256_file(executable),
        "format": "ror-d0-runtime-scene-report-v1",
        "machine": platform.machine(),
        "platform": platform.platform(),
        "repository_commit": repository_commit,
        "results": results,
        "runs_per_worker": args.runs,
        "runtime_content": str(runtime_content),
        "scenario_id": SCENARIO_ID,
        "steps": EXPECTED_STEPS,
        "workers": list(args.workers),
    }
    report_path = artifact_dir / "report.json"
    temporary_report = artifact_dir / "report.json.tmp"
    temporary_report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary_report, report_path)
    print(f"D0 runtime scene gate passed: {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SceneFailure as exc:
        print(f"deterministic scene gate failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
