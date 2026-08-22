#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the authenticated JBeam J2 product spawn and fixed-step soak.

The tool performs no downloads. It packages one project-original JBeam source
and a separately authenticated project script, runs the real cache/load/spawn
path for 120,000 fixed 0.5 ms steps, and compares one-worker and eight-worker
canonical state traces. This is structural/hydro/SUPPORT execution evidence, not a
BeamNG.drive behavior-parity or third-party-mod claim.
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
from typing import Mapping, Sequence
import zipfile

import run_calibrated_beam_soak as support


PROFILE_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_spawn_soak/fixture-profile.json"
)
JBEAM_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_spawn_soak/"
    "vehicles/ror_jbeam_spawn/main.jbeam"
)
SCRIPT_RELATIVE = Path("resources/scripts/example_jbeam_spawn_soak.as")
JBEAM_MEMBER = "vehicles/ror_jbeam_spawn/main.jbeam"
JBEAM_ARCHIVE = "RoRJBeamSpawnSoak.zip"
SCRIPT_MEMBER = "example_jbeam_spawn_soak.as"
VEHICLE = "ror_jbeam_spawn_fixture.jbeam"
TERRAIN = "simple2.terrn2"
SCENARIO_ID = 2026082105
EXPECTED_STEPS = 120000
EXPECTED_NODES = 6
EXPECTED_RUNTIME_BEAMS = 16
EXPECTED_CAB_TRIANGLES = 5
EXPECTED_COLLISION_CAB_TRIANGLES = 5
EXPECTED_CONTACTERS = 0
EXPECTED_GROUND_CONTACT_NODES = 6
EXPECTED_HYDROS = 1
EXPECTED_SUPPORT_BEAMS = 1
EXPECTED_NODE_MASS_KG = 20
EXPECTED_TOTAL_MASS_KG = 120
EXPECTED_COM_FROM_REFERENCE_SQUARED = 1.0 / 12.0
EXPECTED_STATE_DIGEST_SCHEMA_VERSION = 3
HYDRO_MINIMUM_RATIO_LOWER_BOUND = 0.98
HYDRO_MINIMUM_RATIO_UPPER_BOUND = 0.995
HYDRO_MAXIMUM_RATIO_LOWER_BOUND = 1.005
HYDRO_MAXIMUM_RATIO_UPPER_BOUND = 1.02

START_MARKER = (
    "[RoR|J2|SpawnSoak] START scenario=2026082105 "
    "vehicle=ror_jbeam_spawn_fixture.jbeam steps=120000 "
    "impact_translation_y=2 impact_velocity_y=-4"
)
ARM_MARKER = (
    "[RoR|J2|SpawnSoak] ARMED actors=1 nodes=6 beams=16 "
    "cab_triangles=5 collision_cab_triangles=5 contacters=0 "
    "ground_contact_nodes=6 hydros=1 support_beams=1 "
    "total_mass=120 translation_y=2 first_step=0 batch=100"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|J2\|SpawnSoak\] PASS actors=1 nodes=6 beams=16 "
    r"cab_triangles=5 collision_cab_triangles=5 contacters=0 "
    r"ground_contact_nodes=6 hydros=1 support_beams=1 "
    r"total_mass=120 steps=120000 "
    r"hydro_steps=120000 "
    r"support_steps=(?P<support_steps>[0-9]+) "
    r"support_compression_steps=(?P<support_compression>[0-9]+) "
    r"hydro_min_ratio=(?P<hydro_min_ratio>[-+0-9.eE]+) "
    r"hydro_max_ratio=(?P<hydro_max_ratio>[-+0-9.eE]+) "
    r"max_abs_position=(?P<position>[-+0-9.eE]+) "
    r"max_abs_velocity=(?P<velocity>[-+0-9.eE]+) "
    r"minimum_com_drop=(?P<drop>[-+0-9.eE]+) "
    r"peak_com_speed=(?P<peak>[-+0-9.eE]+) "
    r"broken_beams=(?P<broken>[0-9]+)"
)
FATAL_MARKERS = (
    "[RoR|J2|SpawnSoak] FAIL",
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


class SpawnSoakFailure(RuntimeError):
    """Fail-closed diagnostic for invalid source, execution, or evidence."""


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
            raise SpawnSoakFailure(f"{label} is missing or indirect: {path}")
    try:
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SpawnSoakFailure("fixture profile is not canonical JSON") from error
    if not isinstance(profile, dict) or set(profile) != {
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
    }:
        raise SpawnSoakFailure("fixture profile schema drifted")
    jbeam = jbeam_path.read_bytes()
    script = script_path.read_bytes()
    jbeam_record = profile.get("jbeamSource")
    script_record = profile.get("scenarioScript")
    expected = profile.get("expectedRuntime")
    if (
        profile.get("schema") != 7
        or profile.get("fixtureId")
        != "ror-jbeam-authenticated-spawn-soak-v7"
        or profile.get("authorship") != "original-clean-room"
        or profile.get("license") != "GPL-3.0-or-later"
        or profile.get("execution") != "authenticated-product-path"
        or profile.get("rootPart") != "ror_jbeam_spawn_fixture"
        or not isinstance(jbeam_record, dict)
        or jbeam_record
        != {
            "path": JBEAM_RELATIVE.relative_to(
                "tests/fixtures/beamng/jbeam_spawn_soak"
            ).as_posix(),
            "sha256": sha256_bytes(jbeam),
        }
        or not isinstance(script_record, dict)
        or script_record
        != {
            "path": SCRIPT_RELATIVE.as_posix(),
            "sha256": sha256_bytes(script),
        }
        or expected
        != {
            "cabTriangles": EXPECTED_CAB_TRIANGLES,
            "collisionCabTriangles": EXPECTED_COLLISION_CAB_TRIANGLES,
            "contacters": EXPECTED_CONTACTERS,
            "centerOfMassFromReferenceSquared":
                EXPECTED_COM_FROM_REFERENCE_SQUARED,
            "fixedSteps": EXPECTED_STEPS,
            "groundContactNodes": EXPECTED_GROUND_CONTACT_NODES,
            "impactTranslationY": 2,
            "impactVelocityY": -4,
            "hydroMaximumRatioGreaterThan":
                HYDRO_MAXIMUM_RATIO_LOWER_BOUND,
            "hydroMaximumRatioLessThan":
                HYDRO_MAXIMUM_RATIO_UPPER_BOUND,
            "hydroMinimumRatioGreaterThan":
                HYDRO_MINIMUM_RATIO_LOWER_BOUND,
            "hydroMinimumRatioLessThan":
                HYDRO_MINIMUM_RATIO_UPPER_BOUND,
            "jbeamHydros": EXPECTED_HYDROS,
            "jbeamSupportBeams": EXPECTED_SUPPORT_BEAMS,
            "nodeMassKg": EXPECTED_NODE_MASS_KG,
            "nodes": EXPECTED_NODES,
            "physicsStepDenominator": 2000,
            "physicsStepNumerator": 1,
            "runtimeBeams": EXPECTED_RUNTIME_BEAMS,
            "totalMassKg": EXPECTED_TOTAL_MASS_KG,
        }
    ):
        raise SpawnSoakFailure("fixture profile does not match source/runtime")
    prohibited = profile.get("prohibitedInputs")
    if prohibited != [
        "lua-execution",
        "ogre-script-execution",
        "network",
        "external-assets",
        "third-party-mod-data",
    ]:
        raise SpawnSoakFailure("fixture prohibited-input policy drifted")
    return profile, jbeam, script


def deterministic_zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.flag_bits = 0x800
    return info


def write_archive(path: Path, members: Mapping[str, bytes]) -> str:
    if not members or len(members) != len(set(members)):
        raise SpawnSoakFailure("archive member inventory is invalid")
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        strict_timestamps=True,
    ) as archive:
        for name in sorted(members):
            archive.writestr(deterministic_zip_info(name), members[name])
    with zipfile.ZipFile(path, "r") as archive:
        if archive.namelist() != sorted(members) or archive.testzip() is not None:
            raise SpawnSoakFailure("derived archive inventory is not exact")
        for name, payload in members.items():
            if archive.read(name) != payload:
                raise SpawnSoakFailure(f"derived archive member changed: {name}")
    return support.sha256_file(path)


def verify_runtime_terrain(runtime_content: Path) -> None:
    archive_path = runtime_content / "simple2-terrain.zip"
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            names = archive.namelist()
            if (
                len(names) != len(set(names))
                or TERRAIN not in names
                or not archive.read(TERRAIN)
            ):
                raise SpawnSoakFailure("runtime terrain inventory is invalid")
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise SpawnSoakFailure(
            f"runtime terrain archive is invalid: {archive_path}"
        ) from error


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-checkcache",
            "-map",
            TERRAIN,
            "-truck",
            VEHICLE,
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
) -> dict[str, float]:
    if returncode != 0:
        raise SpawnSoakFailure(f"RoR spawn soak exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise SpawnSoakFailure(f"AngelScript log missed marker: {marker}")
    engine_markers = (
        "[RoR|ModCache|JBeam] Mounted exact archive",
        f"archive_sha256={archive_sha256}",
        "roots=1",
        "[RoR|Determinism] Recording state trace",
        "scenario=2026082105",
        "limit=120000",
        "with 120000 fixed-step records (trace step limit reached)",
    )
    if require_scan_receipt:
        engine_markers += (
            "[RoR|ModCache|JBeam] Added exact root "
            "'ror_jbeam_spawn_fixture'",
            "nodes=6, beams=15, hydros=1",
        )
    for marker in engine_markers:
        if marker not in engine_log:
            raise SpawnSoakFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise SpawnSoakFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise SpawnSoakFailure(
            f"expected one spawn-soak PASS receipt, found {len(matches)}"
        )
    position = float(matches[0].group("position"))
    velocity = float(matches[0].group("velocity"))
    minimum_drop = float(matches[0].group("drop"))
    peak_speed = float(matches[0].group("peak"))
    broken_beams = int(matches[0].group("broken"))
    support_steps = int(matches[0].group("support_steps"))
    support_compression = int(
        matches[0].group("support_compression")
    )
    hydro_min_ratio = float(matches[0].group("hydro_min_ratio"))
    hydro_max_ratio = float(matches[0].group("hydro_max_ratio"))
    if not (0.0 < position <= 1.0e7) or not (0.0 <= velocity <= 1.0e7):
        raise SpawnSoakFailure("spawn-soak telemetry is outside finite bounds")
    if (
        not (1.0 < minimum_drop <= 100.0)
        or not (4.0 <= peak_speed <= 1.0e7)
        or broken_beams != 0
        or support_steps != EXPECTED_STEPS
        or not (0 < support_compression <= support_steps)
        or not (
            HYDRO_MINIMUM_RATIO_LOWER_BOUND
            < hydro_min_ratio
            < HYDRO_MINIMUM_RATIO_UPPER_BOUND
        )
        or not (
            HYDRO_MAXIMUM_RATIO_LOWER_BOUND
            < hydro_max_ratio
            < HYDRO_MAXIMUM_RATIO_UPPER_BOUND
        )
    ):
        raise SpawnSoakFailure("terrain-impact telemetry is outside bounds")
    return {
        "broken_beams": broken_beams,
        "max_abs_position": position,
        "max_abs_velocity": velocity,
        "minimum_com_drop": minimum_drop,
        "peak_com_speed": peak_speed,
        "support_accepted_steps": support_steps,
        "support_compression_steps": support_compression,
        "hydro_min_ratio": hydro_min_ratio,
        "hydro_max_ratio": hydro_max_ratio,
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
        raise SpawnSoakFailure("trace comparator emitted invalid JSON") from error
    if (
        completed.returncode != 0
        or not isinstance(payload, dict)
        or payload.get("format") != "ror-d0-state-trace-comparison-v2"
        or payload.get("status") != "match"
        or payload.get("steps_compared") != EXPECTED_STEPS
    ):
        raise SpawnSoakFailure(f"JBeam state traces diverged: {output}")
    for side_name, workers in (
        ("left", left_workers),
        ("right", right_workers),
    ):
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
            raise SpawnSoakFailure(f"trace missed {side_name} metadata")
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise SpawnSoakFailure(
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
        raise SpawnSoakFailure("trace inspector emitted invalid JSON") from error
    metadata = payload.get("metadata") if isinstance(payload, dict) else None
    final_step = payload.get("final_step") if isinstance(payload, dict) else None
    if (
        completed.returncode != 0
        or not isinstance(payload, dict)
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
        or final_step.get("actor_count") != 1
        or not isinstance(final_step.get("contact_count"), int)
        or not isinstance(final_step.get("state_digest"), str)
        or re.fullmatch(r"[0-9a-f]{64}", final_step["state_digest"])
        is None
    ):
        raise SpawnSoakFailure(f"JBeam state trace inspection failed: {output}")
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
        raise SpawnSoakFailure("executable and trace tool must exist")
    if artifact_dir.exists():
        raise SpawnSoakFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    profile, jbeam, script = read_profile(repository)
    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise SpawnSoakFailure(f"runtime content is missing: {runtime_content}")
    verify_runtime_terrain(runtime_content)

    isolated_home = artifact_dir / "work" / "jbeam-spawn-soak-home"
    layout = support.runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    jbeam_archive_sha256 = write_archive(
        layout["mods"] / JBEAM_ARCHIVE,
        {JBEAM_MEMBER: jbeam},
    )
    scripts_directory = layout["user"] / "scripts"
    scripts_directory.mkdir(parents=True, exist_ok=True)
    runtime_script = scripts_directory / SCRIPT_MEMBER
    runtime_script.write_bytes(script)
    if support.sha256_file(runtime_script) != sha256_bytes(script):
        raise SpawnSoakFailure("trusted runtime script staging changed bytes")

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
            support.write_runtime_config(
                layout["config"], workers, not cache_initialized
            )
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
            engine_log = support.read_required(
                layout["logs"] / "RoR.log", "RoR log"
            )
            script_log = support.read_required(
                layout["logs"] / "Angelscript.log", "AngelScript log"
            )
            telemetry = validate_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
                jbeam_archive_sha256,
                not cache_initialized,
            )
            cache_initialized = True
            runtime_trace = support.find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostics / f"{label}.stdout").write_text(
                stdout, encoding="utf-8"
            )
            (diagnostics / f"{label}.RoR.log").write_text(
                engine_log, encoding="utf-8"
            )
            (diagnostics / f"{label}.Angelscript.log").write_text(
                script_log, encoding="utf-8"
            )
            compare_traces(
                trace_tool,
                trace_path,
                trace_path,
                workers,
                workers,
                args.timeout,
            )
            inspection = inspect_trace(
                trace_tool, trace_path, workers, args.timeout
            )
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
                        "first_divergent_step": comparison[
                            "first_divergent_step"
                        ],
                        "format": comparison["format"],
                        "left_metadata": comparison["left"]["metadata"],
                        "left_trace": str(baseline.relative_to(artifact_dir)),
                        "right_metadata": comparison["right"]["metadata"],
                        "right_trace": str(
                            trace_path.relative_to(artifact_dir)
                        ),
                        "status": comparison["status"],
                        "steps_compared": comparison["steps_compared"],
                    }
                )
            trace_sha256 = support.sha256_file(trace_path)
            results.append(
                {
                    "run": run_index,
                    "final_contact_count": inspection["final_step"][
                        "contact_count"
                    ],
                    "final_state_digest": inspection["final_step"][
                        "state_digest"
                    ],
                    "telemetry": telemetry,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_bytes": inspection["bytes_read"],
                    "trace_sha256": trace_sha256,
                    "workers": workers,
                }
            )
            print(
                "J2 authenticated spawn soak matched: "
                f"workers={workers} run={run_index}/{args.runs} "
                f"sha256={trace_sha256}"
            )

    report = {
        "documentation_profile": profile["documentationProfile"],
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_id": profile["fixtureId"],
        "format": "ror-j2-authenticated-jbeam-spawn-soak-v7",
        "jbeam_archive_sha256": jbeam_archive_sha256,
        "jbeam_source_sha256": sha256_bytes(jbeam),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "script_sha256": sha256_bytes(script),
        "state_comparisons": state_comparisons,
        "steps": EXPECTED_STEPS,
        "runtime_topology": {
            "cab_triangles": EXPECTED_CAB_TRIANGLES,
            "collision_cab_triangles":
                EXPECTED_COLLISION_CAB_TRIANGLES,
            "center_of_mass_from_reference_squared":
                EXPECTED_COM_FROM_REFERENCE_SQUARED,
            "contacters": EXPECTED_CONTACTERS,
            "ground_contact_nodes": EXPECTED_GROUND_CONTACT_NODES,
            "jbeam_hydros": EXPECTED_HYDROS,
            "jbeam_support_beams": EXPECTED_SUPPORT_BEAMS,
            "node_mass_kg": EXPECTED_NODE_MASS_KG,
            "nodes": EXPECTED_NODES,
            "runtime_beams": EXPECTED_RUNTIME_BEAMS,
            "total_mass_kg": EXPECTED_TOTAL_MASS_KG,
        },
        "workers": list(args.workers),
        "scope": "clean-room-structural-hydro-support-normaltype-product-path-not-behavior-parity",
    }
    temporary = artifact_dir / "report.json.tmp"
    final = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, final)
    print(f"J2 authenticated JBeam spawn soak passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SpawnSoakFailure as error:
        print(f"JBeam spawn soak failed: {error}", file=sys.stderr)
        raise SystemExit(1)
