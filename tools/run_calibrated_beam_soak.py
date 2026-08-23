#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the authenticated P1 calibrated-beam starter-content soak.

The tool performs no downloads. It derives one numerical soak fixture from a
pinned DAF source file, places only that derived truck in an isolated user mod,
runs 120,000 exact solver steps, and compares one-worker and eight-worker state
traces through the canonical trace comparator.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Mapping, Sequence
import zipfile


CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"
SOURCE_RELATIVE = Path("dafsemi/b6b0UID-semi.truck")
SOURCE_SHA256 = "88343bed2edaf0cbabadb307bd2e8251f26a18840a0fbc2ca111c74ccdaf7b6c"
FIXTURE_MEMBER = "P1CalibratedDAF.truck"
FIXTURE_SHA256 = "67c752107d7bd23224e1818d4f3e4920fe146ce7cabfd96005f630918263eb12"
FIXTURE_SIZE = 11701
SCENARIO_ID = 2026081302
SCENARIO_SCRIPT = "example_calibrated_beam_soak.as"
TERRAIN = "simple2.terrn2"
EXPECTED_STEPS = 120000
EXPECTED_CALIBRATED_BEAMS = 15

START_MARKER = (
    "[RoR|P1|CalibratedBeamSoak] START scenario=2026081302 "
    "vehicle=P1CalibratedDAF.truck steps=120000"
)
ARM_MARKER = (
    "[RoR|P1|CalibratedBeamSoak] ARMED actors=1 nodes=176 "
    "calibrated_beams=15 first_step=0 batch=100"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|P1\|CalibratedBeamSoak\] PASS actors=1 nodes=176 "
    r"calibrated_beams=15 steps=120000 active_history=(?P<history>\d+) "
    r"max_abs_strain=(?P<strain>[-+0-9.eE]+) "
    r"max_plastic_strain=(?P<plastic>[-+0-9.eE]+) "
    r"max_damage=(?P<damage>[-+0-9.eE]+)"
)
ENGINE_MARKERS = (
    "[RoR|Determinism] Recording state trace",
    "scenario=2026081302",
    "limit=120000",
    "with 120000 fixed-step records (trace step limit reached)",
)
FATAL_MARKERS = (
    "[RoR|P1|CalibratedBeamSoak] FAIL",
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


class SoakFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid soak or artifact."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def decode_output(payload: bytes | str | None) -> str:
    if payload is None:
        return ""
    if isinstance(payload, str):
        return payload
    return payload.decode("utf-8", errors="replace")


def run_command(
    command: Sequence[str],
    timeout: int,
    *,
    cwd: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    launch = list(command)
    if not launch:
        raise SoakFailure("cannot run an empty command")
    # A shebang is not an executable format on Windows.  Qualification uses
    # the native ror_state_trace binary, while unit tests and local diagnostic
    # probes may intentionally provide a Python trace-tool double.
    if Path(launch[0]).suffix.casefold() == ".py":
        launch.insert(0, sys.executable)
    try:
        return subprocess.run(
            launch,
            cwd=None if cwd is None else str(cwd),
            env=None if environment is None else dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise SoakFailure(
            f"command exceeded {timeout} seconds: {' '.join(command)}"
        ) from error


def git_output(repository: Path, arguments: Sequence[str]) -> str:
    result = run_command(("git", "-C", str(repository), *arguments), 30)
    output = decode_output(result.stdout)
    if result.returncode != 0:
        raise SoakFailure(
            f"git {' '.join(arguments)} failed in {repository}: {output}"
        )
    return output.strip()


def derive_fixture_payload(source: bytes) -> bytes:
    try:
        text = source.decode("utf-8")
    except UnicodeDecodeError as error:
        raise SoakFailure("DAF source is not canonical UTF-8") from error

    replacements = (
        ("Daf Semi truck\n", "P1 calibrated-beam DAF numerical soak fixture\n"),
        ("fileinfo b6b0UID,  160,  1\n", "fileinfo p1calibUID,  160,  1\n"),
        (
            "guid a87c50a0-e11b-48b6-9cfe-f2de121e7d40\n",
            "guid 2d2efab8-1d6b-4b25-81f4-2687f6cb1302\n",
        ),
        (
            ";main chassis structural\n0,1,i\n",
            ";main chassis structural\n"
            "; Numerical integration fixture only; not physical DAF calibration.\n"
            "set_calibrated_beam_material 1, on, 0.1, 100000000, "
            "1000000000, 0, 1, 1000000000000\n"
            "0,1,i\n",
        ),
        (
            "27,29,i\n;\n0,2,i\n",
            "27,29,i\nset_calibrated_beam_material 1, off\n;\n0,2,i\n",
        ),
    )
    for old, new in replacements:
        if text.count(old) != 1:
            raise SoakFailure(f"DAF source structure drifted at {old!r}")
        text = text.replace(old, new, 1)

    return text.encode("utf-8")


def generate_fixture(source: bytes) -> bytes:
    if sha256_bytes(source) != SOURCE_SHA256:
        raise SoakFailure("DAF source SHA-256 does not match the pinned fixture")
    fixture = derive_fixture_payload(source)
    if len(fixture) != FIXTURE_SIZE or sha256_bytes(fixture) != FIXTURE_SHA256:
        raise SoakFailure("derived calibrated-beam fixture is not canonical")
    if fixture.count(b"set_calibrated_beam_material 1, on,") != 1:
        raise SoakFailure("derived fixture has an invalid material enable count")
    if fixture.count(b"set_calibrated_beam_material 1, off") != 1:
        raise SoakFailure("derived fixture has an invalid material disable count")
    return fixture


def verify_source(repository: Path) -> tuple[Path, bytes]:
    content = repository / "content"
    if git_output(content, ("rev-parse", "HEAD")) != CONTENT_COMMIT:
        raise SoakFailure("content checkout is not the pinned DAF revision")
    relative = SOURCE_RELATIVE.as_posix()
    if git_output(content, ("ls-files", "--error-unmatch", relative)) != relative:
        raise SoakFailure("pinned DAF source is not tracked")
    dirty = git_output(
        content,
        ("status", "--porcelain", "--untracked-files=no", "--", relative),
    )
    if dirty:
        raise SoakFailure("pinned DAF source has local modifications")
    source = content / SOURCE_RELATIVE
    if not source.is_file() or source.is_symlink():
        raise SoakFailure(f"pinned DAF source is missing: {source}")
    payload = source.read_bytes()
    return content, payload


def verify_runtime_source(runtime_content: Path, source: bytes) -> None:
    archive_path = runtime_content / "dafsemi.zip"
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise SoakFailure("runtime DAF archive has duplicate entries")
            payload = archive.read(SOURCE_RELATIVE.name)
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise SoakFailure(
            f"runtime DAF archive is invalid: {archive_path}"
        ) from error
    if payload != source:
        raise SoakFailure("runtime DAF source differs from pinned content")

    terrain_archive = runtime_content / "simple2-terrain.zip"
    try:
        with zipfile.ZipFile(terrain_archive, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or TERRAIN not in names:
                raise SoakFailure("runtime simple2 archive inventory is invalid")
            if not archive.read(TERRAIN):
                raise SoakFailure("runtime simple2 terrain entry is empty")
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise SoakFailure(
            f"runtime simple2 archive is invalid: {terrain_archive}"
        ) from error


def deterministic_zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.flag_bits = 0x800
    return info


def write_fixture_archive(path: Path, fixture: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        strict_timestamps=True,
    ) as archive:
        archive.writestr(deterministic_zip_info(FIXTURE_MEMBER), fixture)
    with zipfile.ZipFile(path, "r") as archive:
        if archive.namelist() != [FIXTURE_MEMBER]:
            raise SoakFailure("derived fixture archive inventory is not exact")
        if archive.testzip() is not None or archive.read(FIXTURE_MEMBER) != fixture:
            raise SoakFailure("derived fixture archive failed round-trip validation")
    return sha256_file(path)


def runtime_layout(root: Path, target_platform: str) -> dict[str, Path]:
    if target_platform == "darwin":
        # The soak launches a non-bundle development executable. RoR's
        # authenticated ROR_D0_SCENE_HOME override therefore retains the
        # development layout rather than the application-bundle Library
        # layout.
        user = root / "RigsOfRods"
        logs = user / "logs"
    elif target_platform == "win32":
        user = root / "My Games" / "Rigs of Rods"
        logs = user / "logs"
    elif target_platform == "linux":
        user = root / ".rigsofrods"
        logs = user / "logs"
    else:
        raise SoakFailure(f"unsupported runtime platform: {target_platform}")
    return {
        "config": user / "config",
        "logs": logs,
        "mods": user / "mods",
        "user": user,
    }


def write_runtime_config(config: Path, workers: int, force_cache: bool) -> None:
    config.mkdir(parents=True, exist_ok=True)
    (config / "RoR.cfg").write_text(
        "\n".join(
            (
                "; Generated by tools/run_calibrated_beam_soak.py",
                "app_config_long_names=false",
                f"app_num_workers={workers}",
                "app_async_physics=true",
                "app_disable_online_api=true",
                "app_force_cache_update=" + str(force_cache).lower(),
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
    (config / "ogre.cfg").write_text(
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


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-map",
            TERRAIN,
            "-truck",
            FIXTURE_MEMBER,
            "-enter",
            "-runscript",
            SCENARIO_SCRIPT,
        )
    )
    return tuple(command)


def validate_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> dict[str, float | int]:
    if returncode != 0:
        raise SoakFailure(f"RoR soak exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise SoakFailure(f"AngelScript log missed marker: {marker}")
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise SoakFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise SoakFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise SoakFailure(f"expected one soak PASS receipt, found {len(matches)}")
    match = matches[0]
    telemetry: dict[str, float | int] = {
        "active_history": int(match.group("history")),
        "max_abs_strain": float(match.group("strain")),
        "max_plastic_strain": float(match.group("plastic")),
        "max_damage": float(match.group("damage")),
    }
    if not 1 <= telemetry["active_history"] <= EXPECTED_CALIBRATED_BEAMS:
        raise SoakFailure("active calibrated history count is outside 1..15")
    if not 0.0 < telemetry["max_abs_strain"] <= 0.5:
        raise SoakFailure("reported calibrated strain is outside (0, 0.5]")
    if not 0.0 <= telemetry["max_plastic_strain"] <= 0.5:
        raise SoakFailure("reported plastic strain is outside [0, 0.5]")
    if not 0.0 <= telemetry["max_damage"] <= 1.0:
        raise SoakFailure("reported damage is outside [0, 1]")
    return telemetry


def find_single_trace(logs: Path) -> Path:
    traces = sorted(logs.glob("*.rortrace"))
    if len(traces) != 1 or traces[0].stat().st_size == 0:
        raise SoakFailure(f"expected exactly one nonempty trace in {logs}")
    return traces[0]


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
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise SoakFailure("trace comparator emitted invalid JSON") from error
    if result.returncode != 0 or not isinstance(payload, dict):
        raise SoakFailure(f"trace comparison failed: {output}")
    if payload.get("format") != "ror-d0-state-trace-comparison-v2":
        raise SoakFailure("trace comparator format is unsupported")
    if payload.get("status") != "match":
        raise SoakFailure("calibrated-beam traces diverged")
    if payload.get("steps_compared") != EXPECTED_STEPS:
        raise SoakFailure("trace comparison did not cover 120,000 steps")
    for side_name, expected_workers in (
        ("left", left_workers),
        ("right", right_workers),
    ):
        side = payload.get(side_name)
        metadata = side.get("metadata") if isinstance(side, dict) else None
        expected = {
            "worker_count": expected_workers,
            "scenario_id": SCENARIO_ID,
            "first_physics_step": 0,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
        }
        if not isinstance(metadata, dict):
            raise SoakFailure(f"trace comparison missed {side_name} metadata")
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise SoakFailure(
                    f"{side_name} trace metadata {key}={metadata.get(key)!r}; "
                    f"expected {value!r}"
                )
    return payload


def read_required(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError as error:
        raise SoakFailure(f"{label} was not created: {path}") from error


def infer_runtime_content(executable: Path) -> Path:
    candidates = (
        executable.resolve().parent / "content",
        executable.resolve().parent.parent / "Resources" / "content",
    )
    for candidate in candidates:
        if candidate.is_dir():
            return candidate.resolve()
    raise SoakFailure("could not infer runtime content; supply --runtime-content")


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
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--workers", type=int, nargs="+", default=(1, 8))
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args(argv)
    if args.runs <= 0 or args.timeout <= 0:
        parser.error("--runs and --timeout must be positive")
    if not args.workers or any(value <= 0 for value in args.workers):
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
    if not executable.is_file() or not trace_tool.is_file():
        raise SoakFailure("executable and trace tool must be existing files")
    if artifact_dir.exists():
        raise SoakFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    _, source = verify_source(repository)
    fixture = generate_fixture(source)
    runtime_content = (
        infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise SoakFailure(f"runtime content is missing: {runtime_content}")
    verify_runtime_source(runtime_content, source)

    isolated_home = artifact_dir / "work" / "p1-soak-home"
    layout = runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    fixture_archive = layout["mods"] / "P1CalibratedDAF.zip"
    fixture_archive_sha = write_fixture_archive(fixture_archive, fixture)

    traces = artifact_dir / "traces"
    diagnostics = artifact_dir / "diagnostics"
    traces.mkdir()
    diagnostics.mkdir()
    baseline: Path | None = None
    baseline_workers = 0
    cache_initialized = False
    results: list[dict[str, object]] = []

    for workers in args.workers:
        for run_index in range(1, args.runs + 1):
            write_runtime_config(layout["config"], workers, not cache_initialized)
            for trace in layout["logs"].glob("*.rortrace"):
                trace.unlink()
            for name in ("RoR.log", "Angelscript.log"):
                try:
                    (layout["logs"] / name).unlink()
                except FileNotFoundError:
                    pass

            environment = os.environ.copy()
            environment.pop("SNAP_USER_COMMON", None)
            environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
            environment["ALSOFT_DRIVERS"] = "null"
            environment["ALSOFT_LOGLEVEL"] = "0"
            completed = run_command(
                build_command(executable),
                args.timeout,
                cwd=executable.parent,
                environment=environment,
            )
            cache_initialized = True
            stdout = decode_output(completed.stdout)
            engine_log = read_required(layout["logs"] / "RoR.log", "RoR log")
            script_log = read_required(
                layout["logs"] / "Angelscript.log",
                "AngelScript log",
            )
            telemetry = validate_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
            )
            runtime_trace = find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostics / f"{label}.stdout").write_text(stdout, encoding="utf-8")
            (diagnostics / f"{label}.RoR.log").write_text(
                engine_log,
                encoding="utf-8",
            )
            (diagnostics / f"{label}.Angelscript.log").write_text(
                script_log,
                encoding="utf-8",
            )
            compare_traces(
                trace_tool,
                trace_path,
                trace_path,
                workers,
                workers,
                args.timeout,
            )
            if baseline is None:
                baseline = trace_path
                baseline_workers = workers
            else:
                compare_traces(
                    trace_tool,
                    baseline,
                    trace_path,
                    baseline_workers,
                    workers,
                    args.timeout,
                )
            trace_sha = sha256_file(trace_path)
            results.append(
                {
                    "run": run_index,
                    "telemetry": telemetry,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_sha256": trace_sha,
                    "workers": workers,
                }
            )
            print(
                f"P1 calibrated-beam soak matched: workers={workers} "
                f"run={run_index}/{args.runs} sha256={trace_sha}"
            )

    report = {
        "content_commit": CONTENT_COMMIT,
        "executable": str(executable),
        "executable_sha256": sha256_file(executable),
        "fixture_archive_sha256": fixture_archive_sha,
        "fixture_member": FIXTURE_MEMBER,
        "fixture_sha256": FIXTURE_SHA256,
        "format": "ror-p1-calibrated-beam-runtime-soak-v1",
        "machine": platform.machine(),
        "material_claim": "numerical-integration-fixture-not-physical-calibration",
        "platform": platform.platform(),
        "repository_commit": git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "source_sha256": SOURCE_SHA256,
        "steps": EXPECTED_STEPS,
        "workers": list(args.workers),
    }
    temporary = artifact_dir / "report.json.tmp"
    final = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, final)
    print(f"P1 calibrated-beam runtime soak passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SoakFailure as error:
        print(f"calibrated-beam soak failed: {error}", file=sys.stderr)
        raise SystemExit(1)
