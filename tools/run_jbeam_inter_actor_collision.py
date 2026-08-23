#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run authenticated product-path JBeam inter-actor collision evidence.

The tool performs no downloads. It packages the project-original structural
fixture, spawns two exact instances above the terrain, executes native
node-to-NORMALTYPE-cab contact, and compares canonical one-worker/eight-worker
state traces. This is bounded RoR execution evidence, not BeamNG.drive force
parity or broad third-party-mod compatibility.
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
import sys
from typing import Sequence

import run_calibrated_beam_soak as support
import run_jbeam_spawn_soak as package_support


PROFILE_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_inter_actor_collision/fixture-profile.json"
)
JBEAM_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_spawn_soak/"
    "vehicles/ror_jbeam_spawn/main.jbeam"
)
SCRIPT_RELATIVE = Path(
    "resources/scripts/example_jbeam_inter_actor_collision.as"
)
JBEAM_MEMBER = "vehicles/ror_jbeam_spawn/main.jbeam"
JBEAM_ARCHIVE = "RoRJBeamInterActorCollision.zip"
SCRIPT_MEMBER = "example_jbeam_inter_actor_collision.as"
VEHICLE = "ror_jbeam_spawn_fixture.jbeam"
TERRAIN = "simple2.terrn2"
SCENARIO_ID = 2026082106
EXPECTED_STEPS = 2000
EXPECTED_ACTORS = 2
EXPECTED_STATE_DIGEST_SCHEMA_VERSION = 3

START_MARKER = (
    "[RoR|J2|InterActorCollision] START scenario=2026082106 "
    "vehicle=ror_jbeam_spawn_fixture.jbeam actors=2 steps=2000 "
    "initial_vertical_gap=0.01 closing_speed=1"
)
ARM_MARKER = (
    "[RoR|J2|InterActorCollision] ARMED actors=2 nodes=12 beams=32 "
    "cab_triangles=10 collision_cabs=10 contacters=0 hydros=2 "
    "initial_vertical_gap=0.01 closing_speed=1 first_step=0 batch=10"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|J2\|InterActorCollision\] PASS actors=2 nodes=12 "
    r"beams=32 cab_triangles=10 collision_cabs=10 contacters=0 "
    r"hydros=2 steps=2000 "
    r"maximum_relative_velocity_change=(?P<relative>[-+0-9.eE]+) "
    r"maximum_vertical_separation=(?P<separation>[-+0-9.eE]+) "
    r"broken_beams=(?P<broken>[0-9]+)"
)
FATAL_MARKERS = (
    "[RoR|J2|InterActorCollision] FAIL",
    "[RoR|JBeam] Rejected actor spawn",
    "[RoR|ModCache|JBeam] Refused",
    "[RoR|ModCache|JBeam] Rejected",
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


class CollisionGateFailure(RuntimeError):
    """Fail-closed diagnostic for invalid input, execution, or evidence."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def read_profile(repository: Path) -> tuple[dict[str, object], bytes, bytes]:
    profile_path = repository / PROFILE_RELATIVE
    jbeam_path = repository / JBEAM_RELATIVE
    script_path = repository / SCRIPT_RELATIVE
    for path, label in (
        (profile_path, "fixture profile"),
        (jbeam_path, "JBeam source"),
        (script_path, "scenario script"),
    ):
        if not path.is_file() or path.is_symlink():
            raise CollisionGateFailure(f"{label} is missing or indirect: {path}")
    try:
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CollisionGateFailure("fixture profile is not canonical JSON") from error
    try:
        jbeam = support.canonical_lf_text(jbeam_path.read_bytes(), "JBeam source")
        script = support.canonical_lf_text(script_path.read_bytes(), "scenario script")
    except support.SoakFailure as error:
        raise CollisionGateFailure(str(error)) from error
    expected_keys = {
        "authorship",
        "documentationProfile",
        "execution",
        "expectedRuntime",
        "fixtureId",
        "jbeamSource",
        "license",
        "prohibitedInputs",
        "rootPart",
        "scenarioScript",
        "schema",
    }
    if not isinstance(profile, dict) or set(profile) != expected_keys:
        raise CollisionGateFailure("fixture profile schema drifted")
    if (
        profile.get("schema") != 1
        or profile.get("fixtureId")
        != "ror-jbeam-authenticated-inter-actor-collision-v1"
        or profile.get("authorship") != "original-clean-room"
        or profile.get("license") != "GPL-3.0-or-later"
        or profile.get("execution") != "authenticated-product-path"
        or profile.get("rootPart") != "ror_jbeam_spawn_fixture"
        or profile.get("jbeamSource")
        != {
            "path": JBEAM_RELATIVE.as_posix(),
            "sha256": sha256_bytes(jbeam),
        }
        or profile.get("scenarioScript")
        != {
            "path": SCRIPT_RELATIVE.as_posix(),
            "sha256": sha256_bytes(script),
        }
        or profile.get("expectedRuntime")
        != {
            "actors": 2,
            "cabTrianglesPerActor": 5,
            "collisionCabsPerActor": 5,
            "contactersPerActor": 0,
            "fixedSteps": EXPECTED_STEPS,
            "initialVerticalGap": 0.01,
            "initialVerticalRelativeSpeed": 1,
            "jbeamHydrosPerActor": 1,
            "nodesPerActor": 6,
            "physicsStepDenominator": 2000,
            "physicsStepNumerator": 1,
            "runtimeBeamsPerActor": 16,
        }
        or profile.get("prohibitedInputs")
        != [
            "lua-execution",
            "ogre-script-execution",
            "network",
            "external-assets",
            "third-party-mod-data",
        ]
    ):
        raise CollisionGateFailure("fixture profile does not match exact sources")
    return profile, jbeam, script


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-checkcache",
            "-map",
            TERRAIN,
            "-enter",
            "-runscript",
            SCRIPT_MEMBER,
        )
    )
    return tuple(command)


def validate_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    archive_sha256: str,
    require_scan_receipt: bool,
) -> dict[str, float | int]:
    if returncode != 0:
        raise CollisionGateFailure(f"RoR collision gate exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise CollisionGateFailure(f"AngelScript log missed marker: {marker}")
    engine_markers = (
        "[RoR|ModCache|JBeam] Mounted exact archive",
        f"archive_sha256={archive_sha256}",
        "roots=1",
        "[RoR|Determinism] Recording state trace",
        f"scenario={SCENARIO_ID}",
        f"limit={EXPECTED_STEPS}",
        f"with {EXPECTED_STEPS} fixed-step records (trace step limit reached)",
    )
    if require_scan_receipt:
        engine_markers += (
            "[RoR|ModCache|JBeam] Added exact root "
            "'ror_jbeam_spawn_fixture'",
            "nodes=6, beams=15, hydros=1",
        )
    for marker in engine_markers:
        if marker not in engine_log:
            raise CollisionGateFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise CollisionGateFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise CollisionGateFailure(
            f"expected one collision PASS receipt, found {len(matches)}"
        )
    relative = float(matches[0].group("relative"))
    separation = float(matches[0].group("separation"))
    broken = int(matches[0].group("broken"))
    if not (0.1 < relative <= 1.0e7):
        raise CollisionGateFailure("relative-velocity response is outside bounds")
    if not (0.03 < separation <= 1.0e7) or broken != 0:
        raise CollisionGateFailure("collision separation/breakage is outside bounds")
    return {
        "broken_beams": broken,
        "maximum_relative_velocity_change": relative,
        "maximum_vertical_separation": separation,
    }


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
        raise CollisionGateFailure("trace comparator emitted invalid JSON") from error
    if (
        completed.returncode != 0
        or not isinstance(payload, dict)
        or payload.get("format") != "ror-d0-state-trace-comparison-v2"
        or payload.get("status") != "match"
        or payload.get("steps_compared") != EXPECTED_STEPS
    ):
        raise CollisionGateFailure(f"collision traces diverged: {output}")
    for side_name, workers in (("left", left_workers), ("right", right_workers)):
        side = payload.get(side_name)
        metadata = side.get("metadata") if isinstance(side, dict) else None
        expected = {
            "first_physics_step": 0,
            "physics_step_denominator": 2000,
            "physics_step_numerator": 1,
            "scenario_id": SCENARIO_ID,
            "worker_count": workers,
        }
        if not isinstance(metadata, dict):
            raise CollisionGateFailure(f"trace missed {side_name} metadata")
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise CollisionGateFailure(
                    f"{side_name} trace {key}={metadata.get(key)!r}; "
                    f"expected {value!r}"
                )
    return payload


def inspect_trace(
    trace_tool: Path,
    trace: Path,
    workers: int,
    timeout: int,
) -> dict[str, object]:
    completed = support.run_command(
        (str(trace_tool), "--inspect", str(trace)), timeout
    )
    output = support.decode_output(completed.stdout)
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise CollisionGateFailure("trace inspector emitted invalid JSON") from error
    metadata = payload.get("metadata") if isinstance(payload, dict) else None
    final_step = payload.get("final_step") if isinstance(payload, dict) else None
    summary = payload.get("contact_summary") if isinstance(payload, dict) else None
    if (
        completed.returncode != 0
        or payload.get("format") != "ror-d0-state-trace-inspection-v2"
        or payload.get("status") != "valid"
        or payload.get("step_count") != EXPECTED_STEPS
        or payload.get("has_final_step") is not True
        or not isinstance(metadata, dict)
        or metadata.get("state_digest_schema_version")
        != EXPECTED_STATE_DIGEST_SCHEMA_VERSION
        or metadata.get("worker_count") != workers
        or metadata.get("scenario_id") != SCENARIO_ID
        or metadata.get("first_physics_step") != 0
        or metadata.get("physics_step_numerator") != 1
        or metadata.get("physics_step_denominator") != 2000
        or not isinstance(final_step, dict)
        or final_step.get("physics_step") != EXPECTED_STEPS - 1
        or final_step.get("actor_count") != EXPECTED_ACTORS
        or not isinstance(final_step.get("state_digest"), str)
        or re.fullmatch(r"[0-9a-f]{64}", final_step["state_digest"]) is None
        or not isinstance(summary, dict)
        or not isinstance(summary.get("total_contact_count"), int)
        or summary["total_contact_count"] <= 0
        or not isinstance(summary.get("contact_step_count"), int)
        or summary["contact_step_count"] <= 0
        or summary["contact_step_count"] > EXPECTED_STEPS
        or not isinstance(summary.get("maximum_contact_count"), int)
        or summary["maximum_contact_count"] <= 0
        or summary.get("first_contact_physics_step") != 0
        or not isinstance(summary.get("last_contact_physics_step"), int)
        or summary["last_contact_physics_step"] >= EXPECTED_STEPS
    ):
        raise CollisionGateFailure(f"collision trace inspection failed: {output}")
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
    parser.add_argument("--timeout", type=int, default=300)
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
        raise CollisionGateFailure("executable and trace tool must exist")
    if artifact_dir.exists():
        raise CollisionGateFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    profile, jbeam, script = read_profile(repository)
    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise CollisionGateFailure(f"runtime content is missing: {runtime_content}")
    package_support.verify_runtime_terrain(runtime_content)

    isolated_home = artifact_dir / "work" / "jbeam-collision-home"
    layout = support.runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    archive_sha256 = package_support.write_archive(
        layout["mods"] / JBEAM_ARCHIVE,
        {JBEAM_MEMBER: jbeam},
    )
    scripts = layout["user"] / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    runtime_script = scripts / SCRIPT_MEMBER
    runtime_script.write_bytes(script)
    if support.sha256_file(runtime_script) != sha256_bytes(script):
        raise CollisionGateFailure("trusted runtime script staging changed bytes")

    traces = artifact_dir / "traces"
    diagnostics = artifact_dir / "diagnostics"
    traces.mkdir()
    diagnostics.mkdir()
    baseline: Path | None = None
    baseline_workers = 0
    cache_initialized = False
    results: list[dict[str, object]] = []
    state_comparisons: list[dict[str, object]] = []

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

            environment = os.environ.copy()
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
            stdout = support.decode_output(completed.stdout)
            engine_log = support.read_required(layout["logs"] / "RoR.log", "RoR log")
            script_log = support.read_required(
                layout["logs"] / "Angelscript.log", "AngelScript log"
            )
            telemetry = validate_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
                archive_sha256,
                not cache_initialized,
            )
            cache_initialized = True
            runtime_trace = support.find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostics / f"{label}.stdout").write_text(stdout, encoding="utf-8")
            (diagnostics / f"{label}.RoR.log").write_text(engine_log, encoding="utf-8")
            (diagnostics / f"{label}.Angelscript.log").write_text(
                script_log, encoding="utf-8"
            )
            compare_traces(trace_tool, trace_path, trace_path, workers, workers, args.timeout)
            inspection = inspect_trace(trace_tool, trace_path, workers, args.timeout)
            if baseline is None:
                baseline = trace_path
                baseline_workers = workers
            else:
                comparison = compare_traces(
                    trace_tool,
                    baseline,
                    trace_path,
                    baseline_workers,
                    workers,
                    args.timeout,
                )
                state_comparisons.append(
                    {
                        "difference": comparison["difference"],
                        "first_divergent_step": comparison["first_divergent_step"],
                        "format": comparison["format"],
                        "left_metadata": comparison["left"]["metadata"],
                        "left_trace": str(baseline.relative_to(artifact_dir)),
                        "right_metadata": comparison["right"]["metadata"],
                        "right_trace": str(trace_path.relative_to(artifact_dir)),
                        "status": comparison["status"],
                        "steps_compared": comparison["steps_compared"],
                    }
                )
            trace_sha256 = support.sha256_file(trace_path)
            results.append(
                {
                    "contact_summary": inspection["contact_summary"],
                    "final_state_digest": inspection["final_step"]["state_digest"],
                    "run": run_index,
                    "telemetry": telemetry,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_bytes": inspection["bytes_read"],
                    "trace_sha256": trace_sha256,
                    "workers": workers,
                }
            )
            print(
                "J2 inter-actor collision matched: "
                f"workers={workers} run={run_index}/{args.runs} "
                f"contacts={inspection['contact_summary']['total_contact_count']} "
                f"sha256={trace_sha256}"
            )

    report = {
        "documentation_profile": profile["documentationProfile"],
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_id": profile["fixtureId"],
        "format": "ror-j2-authenticated-inter-actor-collision-v1",
        "jbeam_archive_sha256": archive_sha256,
        "jbeam_source_sha256": sha256_bytes(jbeam),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "scope": "clean-room-normaltype-native-inter-actor-contact-not-force-parity",
        "script_sha256": sha256_bytes(script),
        "state_comparisons": state_comparisons,
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
    print(f"J2 authenticated inter-actor collision passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CollisionGateFailure as error:
        print(f"JBeam inter-actor collision failed: {error}", file=sys.stderr)
        raise SystemExit(1)
