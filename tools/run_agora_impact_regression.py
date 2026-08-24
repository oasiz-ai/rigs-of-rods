#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the authenticated P1 full-vehicle Agora impact regression.

The runner performs no downloads. It derives one numerical material fixture
from the pinned Agora source, applies an exact 12 m/s downward velocity while
paused, records every 0.5 ms physics step, samples scalar impact telemetry at
100-step render boundaries, and requires one-worker/eight-worker state traces
and telemetry to agree.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePath
import platform
import re
import shutil
import sys
from typing import Mapping, Sequence
import zipfile

TOOLS_DIRECTORY = Path(__file__).resolve().parent
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))
import run_calibrated_beam_soak as support


CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"
SOURCE_RELATIVE = Path("agora/95bbUID-agoral.truck")
SOURCE_SHA256 = "09730ea7988b1f3c19ad88e78e0645d32d2ca84c004357fd2437cd97b124ea38"
FIXTURE_MEMBER = "P1CalibratedAgoraImpact.truck"
FIXTURE_SHA256 = "f3f2b5672864fbe9fac7da657aad708b708d4511479ce103730a49856ebc41f2"
FIXTURE_SIZE = 18919
SCENARIO_ID = 2026082001
SCENARIO_SCRIPT = "example_calibrated_agora_impact.as"
TERRAIN = "simple2.terrn2"
EXPECTED_STEPS = 6000
FIXED_STEPS_PER_FRAME = 100
EXPECTED_NODES = 297
EXPECTED_CALIBRATED_BEAMS = 675

START_MARKER = (
    "[RoR|P1|AgoraImpact] START scenario=2026082001 "
    "vehicle=P1CalibratedAgoraImpact.truck steps=6000 velocity=0,-12,0"
)
ARM_MARKER = (
    "[RoR|P1|AgoraImpact] ARMED actors=1 nodes=297 "
    "calibrated_beams=675 first_step=0 batch=100 "
    "trace_interval_steps=1 telemetry_interval_steps=100"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|P1\|AgoraImpact\] PASS actors=1 nodes=297 "
    r"calibrated_beams=675 steps=6000 "
    r"sampled_peak_deceleration=(?P<sampled_peak>[-+0-9.eE]+) "
    r"initial_energy=(?P<initial>[-+0-9.eE]+) "
    r"final_energy=(?P<final>[-+0-9.eE]+) "
    r"absorbed_energy=(?P<absorbed>[-+0-9.eE]+) "
    r"permanent_rms=(?P<rms>[-+0-9.eE]+) "
    r"permanent_max=(?P<maximum>[-+0-9.eE]+) "
    r"broken_beams=(?P<broken>\d+) fractures=(?P<fractures>\d+) "
    r"disabled=(?P<disabled>\d+) "
    r"final_com_speed=(?P<final_speed>[-+0-9.eE]+)"
)
ENGINE_MARKERS = (
    "[RoR|Determinism] Recording state trace",
    "scenario=2026082001",
    "limit=6000",
    "with 6000 fixed-step records (trace step limit reached)",
)
FATAL_MARKERS = support.FATAL_MARKERS + (
    "[RoR|P1|AgoraImpact] FAIL",
)


class ImpactFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid Agora impact artifact."""


def canonical_text(payload: bytes, label: str) -> bytes:
    try:
        return support.canonical_lf_text(payload, label)
    except support.SoakFailure as error:
        raise ImpactFailure(str(error)) from error


def portable_artifact_path(path: PurePath, artifact_dir: PurePath) -> str:
    """Return one platform-independent path for signed JSON evidence."""
    return path.relative_to(artifact_dir).as_posix()


def derive_fixture_payload(source: bytes) -> bytes:
    try:
        text = source.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ImpactFailure("Agora source is not canonical UTF-8") from error

    replacements = (
        (
            "Bus RVI Agora L\n",
            "P1 calibrated Agora fixed-speed impact regression\n",
        ),
        (
            "fileinfo 95bbUID,  107,  1\n",
            "fileinfo p1impactUID,  107,  1\n",
        ),
        (
            "guid 0e943cc8-d9a5-4d24-89fc-907423a0ddfb\n",
            "guid 7d2ca7c2-40c9-4d4b-96a6-a7a1797ab198\n",
        ),
        (
            "beams\n;chassis front structural\n",
            "beams\n"
            "; Numerical impact regression only; not physical Agora "
            "calibration.\n"
            "set_calibrated_beam_material 1, on, 0.0001, 10000000000, "
            "250000000, 100000000, 0.02, 5000000\n"
            ";chassis front structural\n",
        ),
        (
            "\n\nhydros\n",
            "\nset_calibrated_beam_material 1, off\n\nhydros\n",
        ),
    )
    for old, new in replacements:
        if text.count(old) != 1:
            raise ImpactFailure(f"Agora source structure drifted at {old!r}")
        text = text.replace(old, new, 1)
    return text.encode("utf-8")


def generate_fixture(source: bytes) -> bytes:
    source = canonical_text(source, "Agora source")
    if support.sha256_bytes(source) != SOURCE_SHA256:
        raise ImpactFailure("Agora source SHA-256 does not match the pin")
    fixture = derive_fixture_payload(source)
    if len(fixture) != FIXTURE_SIZE or support.sha256_bytes(fixture) != FIXTURE_SHA256:
        raise ImpactFailure("derived Agora impact fixture is not canonical")
    if fixture.count(b"set_calibrated_beam_material 1, on,") != 1:
        raise ImpactFailure("derived fixture has an invalid material enable count")
    if fixture.count(b"set_calibrated_beam_material 1, off") != 1:
        raise ImpactFailure("derived fixture has an invalid material disable count")
    return fixture


def verify_source(repository: Path) -> bytes:
    content = repository / "content"
    if support.git_output(content, ("rev-parse", "HEAD")) != CONTENT_COMMIT:
        raise ImpactFailure("content checkout is not the pinned Agora revision")
    relative = SOURCE_RELATIVE.as_posix()
    if support.git_output(content, ("ls-files", "--error-unmatch", relative)) != relative:
        raise ImpactFailure("pinned Agora source is not tracked")
    if support.git_output(
        content,
        ("status", "--porcelain", "--untracked-files=no", "--", relative),
    ):
        raise ImpactFailure("pinned Agora source has local modifications")
    source = content / SOURCE_RELATIVE
    if not source.is_file() or source.is_symlink():
        raise ImpactFailure(f"pinned Agora source is missing: {source}")
    return canonical_text(source.read_bytes(), "Agora source")


def verify_runtime_source(runtime_content: Path, source: bytes) -> None:
    try:
        with zipfile.ZipFile(runtime_content / "agora.zip", "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise ImpactFailure("runtime Agora archive has duplicate entries")
            payload = archive.read(SOURCE_RELATIVE.name)
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise ImpactFailure("runtime Agora archive is invalid") from error
    if canonical_text(payload, "runtime Agora source") != source:
        raise ImpactFailure("runtime Agora source differs from pinned content")


def verify_terrain(runtime_content: Path) -> None:
    try:
        with zipfile.ZipFile(runtime_content / "simple2-terrain.zip", "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or TERRAIN not in names:
                raise ImpactFailure("runtime simple2 archive inventory is invalid")
            if not archive.read(TERRAIN):
                raise ImpactFailure("runtime simple2 terrain entry is empty")
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise ImpactFailure("runtime simple2 archive is invalid") from error


def write_fixture_archive(path: Path, fixture: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        strict_timestamps=True,
    ) as archive:
        archive.writestr(support.deterministic_zip_info(FIXTURE_MEMBER), fixture)
    with zipfile.ZipFile(path, "r") as archive:
        if archive.namelist() != [FIXTURE_MEMBER]:
            raise ImpactFailure("derived fixture archive inventory is not exact")
        if archive.testzip() is not None or archive.read(FIXTURE_MEMBER) != fixture:
            raise ImpactFailure("derived fixture archive failed round-trip validation")
    return support.sha256_file(path)


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
        raise ImpactFailure(f"RoR impact run exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise ImpactFailure(f"AngelScript log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise ImpactFailure(f"runtime logged a fatal marker: {marker}")
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise ImpactFailure(f"engine log missed marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise ImpactFailure(f"expected one impact PASS receipt, found {len(matches)}")
    match = matches[0]
    telemetry: dict[str, float | int] = {
        "sampled_peak_deceleration": float(match.group("sampled_peak")),
        "initial_energy": float(match.group("initial")),
        "final_energy": float(match.group("final")),
        "absorbed_energy": float(match.group("absorbed")),
        "permanent_rms": float(match.group("rms")),
        "permanent_max": float(match.group("maximum")),
        "broken_beams": int(match.group("broken")),
        "fractures": int(match.group("fractures")),
        "disabled": int(match.group("disabled")),
        "final_com_speed": float(match.group("final_speed")),
    }
    if not 0.0 < telemetry["sampled_peak_deceleration"] <= 1.0e6:
        raise ImpactFailure("sampled peak deceleration is outside (0, 1e6]")
    if not 0.0 < telemetry["absorbed_energy"] <= 1.0e8:
        raise ImpactFailure("absorbed energy is outside (0, 1e8]")
    if not 0.0 < telemetry["permanent_rms"] <= 20.0:
        raise ImpactFailure("permanent RMS deformation is outside (0, 20]")
    if not 0.0 < telemetry["permanent_max"] <= 40.0:
        raise ImpactFailure("maximum permanent deformation is outside (0, 40]")
    if not 1 <= telemetry["fractures"] <= EXPECTED_CALIBRATED_BEAMS:
        raise ImpactFailure("fracture count is outside 1..675")
    if not telemetry["fractures"] <= telemetry["disabled"] <= EXPECTED_CALIBRATED_BEAMS:
        raise ImpactFailure("disabled calibrated beam count is inconsistent")
    if not telemetry["broken_beams"] >= telemetry["fractures"]:
        raise ImpactFailure("broken beam count is below calibrated fractures")
    if not 0.0 <= telemetry["final_com_speed"] < 12.0:
        raise ImpactFailure("final center-of-mass speed did not decrease")
    energy_balance = telemetry["initial_energy"] - telemetry["final_energy"]
    tolerance = max(1.0e-3, abs(telemetry["absorbed_energy"]) * 1.0e-6)
    if abs(energy_balance - telemetry["absorbed_energy"]) > tolerance:
        raise ImpactFailure("impact energy receipt is internally inconsistent")
    return telemetry


def compare_traces(
    trace_tool: Path,
    left: Path,
    right: Path,
    left_workers: int,
    right_workers: int,
    timeout: int,
) -> dict[str, object]:
    completed = support.run_command(
        (
            str(trace_tool),
            "--allow-worker-count-difference",
            str(left),
            str(right),
        ),
        timeout,
    )
    output = support.decode_output(completed.stdout)
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise ImpactFailure("trace comparator emitted invalid JSON") from error
    if completed.returncode != 0 or not isinstance(payload, dict):
        raise ImpactFailure(f"trace comparison failed: {output}")
    if payload.get("format") != "ror-d0-state-trace-comparison-v2":
        raise ImpactFailure("trace comparator format is unsupported")
    if payload.get("status") != "match":
        raise ImpactFailure("Agora impact traces diverged")
    if payload.get("steps_compared") != EXPECTED_STEPS:
        raise ImpactFailure("trace comparison did not cover 6,000 steps")
    for side_name, workers in (("left", left_workers), ("right", right_workers)):
        side = payload.get(side_name)
        metadata = side.get("metadata") if isinstance(side, dict) else None
        expected = {
            "worker_count": workers,
            "scenario_id": SCENARIO_ID,
            "first_physics_step": 0,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
        }
        if not isinstance(metadata, dict):
            raise ImpactFailure(f"trace comparison missed {side_name} metadata")
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise ImpactFailure(
                    f"{side_name} trace metadata {key}={metadata.get(key)!r}; "
                    f"expected {value!r}"
                )
    return payload


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
        raise ImpactFailure("executable and trace tool must be existing files")
    if artifact_dir.exists():
        raise ImpactFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    source = verify_source(repository)
    fixture = generate_fixture(source)
    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise ImpactFailure(f"runtime content is missing: {runtime_content}")
    verify_runtime_source(runtime_content, source)
    verify_terrain(runtime_content)

    isolated_home = artifact_dir / "work" / "p1-agora-impact-home"
    layout = support.runtime_layout(isolated_home, sys.platform, executable)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    fixture_archive = layout["mods"] / "P1CalibratedAgoraImpact.zip"
    fixture_archive_sha = write_fixture_archive(fixture_archive, fixture)

    traces = artifact_dir / "traces"
    diagnostics = artifact_dir / "diagnostics"
    traces.mkdir()
    diagnostics.mkdir()
    baseline_trace: Path | None = None
    baseline_workers = 0
    baseline_telemetry: dict[str, float | int] | None = None
    cache_initialized = False
    results: list[dict[str, object]] = []
    cross_worker_trace_comparisons: list[dict[str, object]] = []

    for workers in args.workers:
        for run_index in range(1, args.runs + 1):
            support.write_runtime_config(layout["config"], workers, not cache_initialized)
            for trace in layout["logs"].glob("*.rortrace"):
                trace.unlink()
            for name in ("RoR.log", "Angelscript.log"):
                try:
                    (layout["logs"] / name).unlink()
                except FileNotFoundError:
                    pass

            environment: Mapping[str, str] = os.environ.copy()
            environment.pop("SNAP_USER_COMMON", None)
            environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
            environment["ALSOFT_DRIVERS"] = "null"
            environment["ALSOFT_LOGLEVEL"] = "0"
            completed = support.run_command(
                build_command(executable),
                args.timeout,
                cwd=executable.parent,
                environment=environment,
            )
            cache_initialized = True
            stdout = support.decode_output(completed.stdout)
            engine_log = support.read_required(layout["logs"] / "RoR.log", "RoR log")
            script_log = support.read_required(
                layout["logs"] / "Angelscript.log",
                "AngelScript log",
            )
            telemetry = validate_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
            )
            runtime_trace = support.find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostics / f"{label}.stdout").write_text(stdout, encoding="utf-8")
            (diagnostics / f"{label}.RoR.log").write_text(engine_log, encoding="utf-8")
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
            if baseline_trace is None:
                baseline_trace = trace_path
                baseline_workers = workers
                baseline_telemetry = telemetry
            else:
                comparison = compare_traces(
                    trace_tool,
                    baseline_trace,
                    trace_path,
                    baseline_workers,
                    workers,
                    args.timeout,
                )
                cross_worker_trace_comparisons.append(
                    {
                        "left_trace": portable_artifact_path(
                            baseline_trace,
                            artifact_dir,
                        ),
                        "left_workers": baseline_workers,
                        "right_trace": portable_artifact_path(
                            trace_path,
                            artifact_dir,
                        ),
                        "right_workers": workers,
                        "status": comparison["status"],
                        "steps_compared": comparison["steps_compared"],
                    }
                )
                if telemetry != baseline_telemetry:
                    raise ImpactFailure("impact telemetry diverged across runs")
            trace_sha = support.sha256_file(trace_path)
            results.append(
                {
                    "run": run_index,
                    "telemetry": telemetry,
                    "trace": portable_artifact_path(trace_path, artifact_dir),
                    "trace_sha256": trace_sha,
                    "workers": workers,
                }
            )
            print(
                f"P1 Agora impact matched: workers={workers} "
                f"run={run_index}/{args.runs} sha256={trace_sha}"
            )

    report = {
        "content_commit": CONTENT_COMMIT,
        "cross_worker_trace_comparisons": cross_worker_trace_comparisons,
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_archive_sha256": fixture_archive_sha,
        "fixture_member": FIXTURE_MEMBER,
        "fixture_sha256": FIXTURE_SHA256,
        "format": "ror-p1-agora-impact-regression-v2",
        "initial_velocity_meters_per_second": [0.0, -12.0, 0.0],
        "machine": platform.machine(),
        "material_claim": "numerical-impact-fixture-not-physical-calibration",
        "platform": platform.platform(),
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "source_sha256": SOURCE_SHA256,
        "steps": EXPECTED_STEPS,
        "state_trace_interval_steps": 1,
        "telemetry_sample_interval_steps": FIXED_STEPS_PER_FRAME,
        "workers": list(args.workers),
    }
    temporary = artifact_dir / "report.json.tmp"
    final = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, final)
    print(f"P1 Agora impact regression passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ImpactFailure as error:
        print(f"Agora impact regression failed: {error}", file=sys.stderr)
        raise SystemExit(1)
