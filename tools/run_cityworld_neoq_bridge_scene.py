#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Accept the complete CityWorld Neo-to-NeoQ2.0 bridge in native RoR.

This standard-library-only gate performs no downloads and never touches the
developer's normal RoR profile. It authenticates CityWorld and the derived
overlay, independently rebuilds that overlay, then runs two isolated scenes:

* six UI-free fixed-camera RGB captures of both seams, the deck, and supports;
* a collision-enabled packaged-DAF traversal across both city-road seams.

Artifacts are published atomically only after both native runs pass.
"""

from __future__ import annotations

import argparse
from collections import Counter
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import sys
import tempfile
from typing import Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
CORRIDOR_RUNNER_PATH = REPOSITORY_ROOT / "tools/run_cityworld_corridor_scene.py"


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


corridor = load_module(
    "ror_cityworld_neoq_bridge_scene_corridor",
    CORRIDOR_RUNNER_PATH,
)
base = corridor.base


CITYWORLD_NAME = corridor.CITYWORLD_NAME
OVERLAY_NAME = corridor.OVERLAY_NAME
OVERLAY_TERRAIN = corridor.OVERLAY_TERRAIN
VEHICLE_ARCHIVE = corridor.VEHICLE_ARCHIVE
REPORT_FORMAT = "ror-cityworld-neoq-bridge-runtime-report-v1"
STATIC_FIXTURE_PATH = (
    "tests/fixtures/cityworld_neoq_bridge_runtime/"
    "cityworld_neoq_bridge_runtime.as"
)
DRIVE_FIXTURE_PATH = (
    "tests/fixtures/cityworld_neoq_bridge_drive_runtime/"
    "cityworld_neoq_bridge_drive_runtime.as"
)
STATIC_SCRIPT_NAME = "cityworld_neoq_bridge_runtime.as"
DRIVE_SCRIPT_NAME = "cityworld_neoq_bridge_drive_runtime.as"
STATIC_RGB_NAMES = tuple(
    f"cityworld_neoq_bridge_static_{index:02d}.png"
    for index in range(6)
)
SIDE_PIER_STYLE = "ror-native-procedural-side-pier-pair-v1"
SIDE_PIER_SUMMARY_PATTERN = corridor.SIDE_PIER_SUMMARY_PATTERN
SIDE_PIER_SKIP_PREFIX = corridor.SIDE_PIER_SKIP_PREFIX
LIGHT_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_led_streetlight_bridge.odef "
    "spotlights=0 point_lights=1 local_shadow_casters=0"
)
EXPECTED_LIGHTS = 49
GROUNDING_PATTERN = re.compile(
    r"\[RoR\|CityWorld\|NeoQ20Grounding\] Applied "
    r"placements=35 renames=3 "
    r"(?:(?:road_replacements=1 )?)"
    r"telepoints=1 tree_replacements=18 transactionally before object "
    r"instantiation \(tobj_sha256="
    + re.escape(corridor.NEOQ_TREE_SOURCE_TOBJ_SHA256)
    + r"\)"
)
DEPENDENCY_PATTERN = corridor.DEPENDENCY_PATTERN
STATIC_MARKERS = (
    "[RoR|CW2|NeoBridgeRuntime] START cameras=6 "
    "route_m=3076.132100441 width_m=24 supports=74 lights=33",
    *(
        f"[RoR|CW2|NeoBridgeRuntime] CAPTURE index={index}"
        for index in range(6)
    ),
)
STATIC_PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|NeoBridgeRuntime\] PASS cameras=6 "
    r"frames=(?P<frames>[0-9]+) "
    r"physics_steps=(?P<steps>[0-9]+) "
    r"route_m=3076\.132100441 supports=74 lights=33"
)
DRIVE_MARKERS = (
    "[RoR|CW2|NeoBridgeDrive] START route_m=3076.132100441 "
    "waypoints=80 vehicle=b6b0UID-semi.truck batch=40 "
    "source_overlap_m=0 destination_overlap_m=0 "
    "collisions=on self_collisions=on",
    "[RoR|CW2|NeoBridgeDrive] ARMED actor=2026072902",
    "[RoR|CW2|NeoBridgeDrive] SEAM name=source",
    "[RoR|CW2|NeoBridgeDrive] MIDPOINT",
    "[RoR|CW2|NeoBridgeDrive] SEAM name=destination",
)
DRIVE_ARMED_PATTERN = re.compile(
    r"\[RoR\|CW2\|NeoBridgeDrive\] ARMED actor=2026072902 "
    r"heading=(?P<heading>-?[0-9.eE+]+) "
    r"station=(?P<station>-?[0-9.eE+]+) "
    r"cross_track=(?P<cross>-?[0-9.eE+]+) "
    r"height=(?P<height>-?[0-9.eE+]+)"
)
DRIVE_SOURCE_SEAM_PATTERN = re.compile(
    r"\[RoR\|CW2\|NeoBridgeDrive\] SEAM name=source "
    r"station=(?P<station>-?[0-9.eE+]+) "
    r"x=(?P<x>-?[0-9.eE+]+) z=(?P<z>-?[0-9.eE+]+)"
)
DRIVE_DESTINATION_SEAM_PATTERN = re.compile(
    r"\[RoR\|CW2\|NeoBridgeDrive\] SEAM name=destination "
    r"station=(?P<station>-?[0-9.eE+]+) "
    r"x=(?P<x>-?[0-9.eE+]+) z=(?P<z>-?[0-9.eE+]+)"
)
DRIVE_PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|NeoBridgeDrive\] PASS seams=2 "
    r"route_m=3076\.132100441 "
    r"station_m=(?P<station>-?[0-9.eE+]+) "
    r"destination_x_m=(?P<destination_x>-?[0-9.eE+]+) "
    r"destination_local_z_m=(?P<destination_local_z>-?[0-9.eE+]+) "
    r"distance_m=(?P<distance>-?[0-9.eE+]+) "
    r"path_error_m=(?P<path>-?[0-9.eE+]+) "
    r"vertical_error_m=(?P<vertical>-?[0-9.eE+]+) "
    r"regression_m=(?P<regression>-?[0-9.eE+]+) "
    r"speed_mps=(?P<speed>-?[0-9.eE+]+) "
    r"physics_steps=(?P<steps>[0-9]+)"
)
FATAL_MARKERS = (
    "[RoR|CW2|NeoBridgeRuntime] FAIL",
    "[RoR|CW2|NeoBridgeDrive] FAIL",
    "Could not load script 'cityworld_neoq_bridge",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "GL_INVALID_",
)


class NeoBridgeSceneFailure(RuntimeError):
    """Fail-closed native acceptance failure for the complete Neo bridge."""


def sha256_file(path: Path) -> str:
    try:
        return corridor.sha256_file(path)
    except corridor.CorridorSceneFailure as error:
        raise NeoBridgeSceneFailure(str(error)) from error


def ordered_markers(text: str, markers: Sequence[str], label: str) -> None:
    previous = -1
    for marker in markers:
        offset = text.find(marker)
        if offset <= previous:
            raise NeoBridgeSceneFailure(
                f"{label} marker is missing or out of order: {marker}"
            )
        previous = offset


def validate_fixture(
    path: Path,
    *,
    drive: bool,
) -> dict[str, object]:
    if not path.is_file() or path.is_symlink():
        raise NeoBridgeSceneFailure(f"fixture is missing: {path}")
    if not 1 <= path.stat().st_size <= 1024 * 1024:
        raise NeoBridgeSceneFailure(f"fixture size is invalid: {path}")
    text = path.read_text(encoding="utf-8")
    shared = (
        'console.cVarSet("ui_hide_gui", "true");',
        "route_m=3076.132100441",
    )
    required = (
        shared
        + (
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "40");',
            'console.cVarSet("sim_no_collisions", "false");',
            'console.cVarSet("sim_no_self_collisions", "false");',
            "source_overlap_m=0 destination_overlap_m=0",
            "collisions=on self_collisions=on",
            "PASS_DESTINATION_X_M = 6877.0f",
            "DESTINATION_LANE_SAFE_MIN_LOCAL_Z_M = 1.95f",
            "DESTINATION_LANE_SAFE_MAX_LOCAL_Z_M = 6.30f",
        )
        if drive
        else shared
        + (
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");',
            "const uint CAPTURE_COUNT = 6;",
            "const uint PASS_FRAME = 265;",
            "MSG_APP_SCREENSHOT_REQUESTED",
        )
    )
    for marker in required:
        if marker not in text:
            raise NeoBridgeSceneFailure(
                f"fixture contract marker drifted: {marker}"
            )
    return {
        "path": path.relative_to(REPOSITORY_ROOT).as_posix(),
        "sha256": sha256_file(path),
        "size": path.stat().st_size,
    }


def expected_side_pier_counts(
    overlay_report: Mapping[str, object],
) -> tuple[int, ...]:
    try:
        corridors = overlay_report["corridors"]
    except KeyError as error:
        raise NeoBridgeSceneFailure("overlay corridors are missing") from error
    if not isinstance(corridors, dict):
        raise NeoBridgeSceneFailure("overlay corridors are not an object")
    counts = []
    for name in sorted(corridors):
        value = corridors[name]
        if not isinstance(value, dict):
            raise NeoBridgeSceneFailure(f"overlay corridor is invalid: {name}")
        supports = value.get("supports")
        if not isinstance(supports, dict):
            raise NeoBridgeSceneFailure(
                f"overlay corridor support contract is invalid: {name}"
            )
        if supports.get("style") != SIDE_PIER_STYLE:
            continue
        requested = supports.get("requested_count")
        if (
            isinstance(requested, bool)
            or not isinstance(requested, int)
            or requested <= 0
        ):
            raise NeoBridgeSceneFailure(
                f"overlay side-pier count is invalid: {name}"
            )
        counts.append(requested)
    if 74 not in counts:
        raise NeoBridgeSceneFailure(
            "Neo intercity bridge side-pier contract is missing"
        )
    return tuple(sorted(counts))


def validate_side_piers(
    engine_log: str,
    expected_counts: Sequence[int],
) -> list[dict[str, int]]:
    if SIDE_PIER_SKIP_PREFIX in engine_log:
        raise NeoBridgeSceneFailure(
            "procedural side-pier construction logged a skip"
        )
    actual = [
        (
            int(match.group("requested")),
            int(match.group("built")),
            int(match.group("skipped")),
        )
        for match in SIDE_PIER_SUMMARY_PATTERN.finditer(engine_log)
    ]
    expected = [(count, count, 0) for count in expected_counts]
    if Counter(actual) != Counter(expected):
        raise NeoBridgeSceneFailure(
            "procedural side-pier runtime multiset drifted: "
            f"expected={sorted(expected)} actual={sorted(actual)}"
        )
    return [
        {
            "requested": requested,
            "built": built,
            "skipped": skipped,
        }
        for requested, built, skipped in sorted(actual)
    ]


def validate_common_runtime(
    *,
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    expected_side_piers: Sequence[int],
    drive: bool,
) -> dict[str, object]:
    if returncode != 0:
        raise NeoBridgeSceneFailure(
            f"RoR Neo bridge scene exited with {returncode}"
        )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise NeoBridgeSceneFailure(
                f"runtime logged fatal marker: {marker}"
            )
    if "Error =" in script_log:
        raise NeoBridgeSceneFailure(
            "AngelScript compiler emitted an error"
        )
    engine_markers = (
        corridor.CITYWORLD_FALLBACK_LIGHTING_MARKER,
        "===== TERRAIN LOADING DONE CityWorldNextLocalOverlay.terrn2",
    )
    for marker in engine_markers:
        if engine_log.count(marker) != 1:
            raise NeoBridgeSceneFailure(
                f"engine marker must appear exactly once: {marker}"
            )
    grounding = list(GROUNDING_PATTERN.finditer(engine_log))
    if len(grounding) != 1:
        raise NeoBridgeSceneFailure(
            "NeoQ2.0 native grounding marker is missing or repeated"
        )
    dependencies = list(DEPENDENCY_PATTERN.finditer(engine_log))
    if len(dependencies) != 1:
        raise NeoBridgeSceneFailure(
            "CityWorld terrain dependency was not mounted exactly once"
        )
    dependency_path = dependencies[0].group("path").replace("\\", "/")
    if not dependency_path.endswith("/mods/" + CITYWORLD_NAME):
        raise NeoBridgeSceneFailure(
            "terrain dependency mounted an unexpected CityWorld path"
        )
    if engine_log.count(LIGHT_MARKER) != EXPECTED_LIGHTS:
        raise NeoBridgeSceneFailure(
            "native bridge light instance count drifted"
        )
    if drive and engine_log.count(
        "===== LOADING VEHICLE: b6b0UID-semi.truck"
    ) != 1:
        raise NeoBridgeSceneFailure(
            "packaged DAF was not loaded exactly once"
        )
    return {
        "renderer": base.parse_renderer_identity(engine_log, sys.platform),
        "shadows": base.validate_pssm_log(engine_log, "pssm", 2),
        "side_piers": validate_side_piers(
            engine_log,
            expected_side_piers,
        ),
    }


def validate_static_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    expected_side_piers: Sequence[int],
) -> dict[str, object]:
    common = validate_common_runtime(
        returncode=returncode,
        stdout=stdout,
        engine_log=engine_log,
        script_log=script_log,
        expected_side_piers=expected_side_piers,
        drive=False,
    )
    ordered_markers(script_log, STATIC_MARKERS, "static AngelScript")
    passes = list(STATIC_PASS_PATTERN.finditer(script_log))
    if len(passes) != 1:
        raise NeoBridgeSceneFailure(
            "static scene did not emit exactly one PASS"
        )
    if passes[0].start() <= script_log.find(STATIC_MARKERS[-1]):
        raise NeoBridgeSceneFailure(
            "static PASS preceded the final RGB capture"
        )
    frames = int(passes[0].group("frames"))
    steps = int(passes[0].group("steps"))
    if frames != 265 or not 1000 <= steps <= 5000:
        raise NeoBridgeSceneFailure(
            "static deterministic frame/physics count drifted"
        )
    return {
        **common,
        "captures": 6,
        "frames": frames,
        "physics_steps": steps,
    }


def _single_float_match(
    pattern: re.Pattern[str],
    script_log: str,
    label: str,
) -> dict[str, float]:
    matches = list(pattern.finditer(script_log))
    if len(matches) != 1:
        raise NeoBridgeSceneFailure(
            f"drive scene did not emit exactly one {label}"
        )
    result = {
        key: float(value)
        for key, value in matches[0].groupdict().items()
    }
    if not all(math.isfinite(value) for value in result.values()):
        raise NeoBridgeSceneFailure(f"{label} metrics are non-finite")
    return result


def validate_drive_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    expected_side_piers: Sequence[int],
) -> dict[str, object]:
    common = validate_common_runtime(
        returncode=returncode,
        stdout=stdout,
        engine_log=engine_log,
        script_log=script_log,
        expected_side_piers=expected_side_piers,
        drive=True,
    )
    ordered_markers(script_log, DRIVE_MARKERS, "drive AngelScript")
    armed = _single_float_match(
        DRIVE_ARMED_PATTERN,
        script_log,
        "ARMED",
    )
    source = _single_float_match(
        DRIVE_SOURCE_SEAM_PATTERN,
        script_log,
        "source SEAM",
    )
    destination = _single_float_match(
        DRIVE_DESTINATION_SEAM_PATTERN,
        script_log,
        "destination SEAM",
    )
    passes = list(DRIVE_PASS_PATTERN.finditer(script_log))
    if len(passes) != 1:
        raise NeoBridgeSceneFailure(
            "drive scene did not emit exactly one PASS"
        )
    if passes[0].start() <= script_log.find(DRIVE_MARKERS[-1]):
        raise NeoBridgeSceneFailure(
            "drive PASS preceded the destination seam"
        )
    pass_record = passes[0].groupdict()
    metrics: dict[str, float | int | object] = {
        "armed_cross_track_m": armed["cross"],
        "armed_height_m": armed["height"],
        "armed_station_m": armed["station"],
        "destination_local_z_m": float(
            pass_record["destination_local_z"]
        ),
        "destination_station_m": destination["station"],
        "destination_x_m": float(pass_record["destination_x"]),
        "distance_m": float(pass_record["distance"]),
        "path_error_m": float(pass_record["path"]),
        "physics_steps": int(pass_record["steps"]),
        "regression_m": float(pass_record["regression"]),
        "route_station_m": float(pass_record["station"]),
        "source_station_m": source["station"],
        "source_x_m": source["x"],
        "speed_mps": float(pass_record["speed"]),
        "vertical_error_m": float(pass_record["vertical"]),
    }
    float_metrics = (
        value for value in metrics.values() if isinstance(value, float)
    )
    if not all(math.isfinite(value) for value in float_metrics):
        raise NeoBridgeSceneFailure("drive PASS metrics are non-finite")
    if not 0.0 <= metrics["armed_station_m"] <= 4.0:
        raise NeoBridgeSceneFailure(
            "DAF was not armed at the NeoQueretaro source road"
        )
    if not -3.0 <= metrics["armed_cross_track_m"] <= 3.0:
        raise NeoBridgeSceneFailure("DAF spawn cross-track is invalid")
    if not 0.5 <= metrics["armed_height_m"] <= 4.0:
        raise NeoBridgeSceneFailure("DAF spawn height is invalid")
    if (
        metrics["source_x_m"] < 3790.970703
        or not 0.0 <= metrics["source_station_m"] <= 10.0
    ):
        raise NeoBridgeSceneFailure(
            "DAF did not cross the flush NeoQueretaro seam"
        )
    if (
        metrics["destination_station_m"] < 3075.0
        or metrics["route_station_m"] < 3075.0
        or metrics["destination_x_m"] < 6877.0
    ):
        raise NeoBridgeSceneFailure(
            "DAF did not cross the flush NeoQ2.0 seam and destination road"
        )
    if not 1.95 <= metrics["destination_local_z_m"] <= 6.30:
        raise NeoBridgeSceneFailure(
            "DAF left the authenticated NeoQ2.0 live-lane footprint"
        )
    if not 3050.0 <= metrics["distance_m"] <= 3400.0:
        raise NeoBridgeSceneFailure(
            "physical Neo bridge traversal distance is invalid"
        )
    if not 0.0 <= metrics["path_error_m"] <= 2.5:
        raise NeoBridgeSceneFailure("Neo bridge path error is excessive")
    if not 0.0 <= metrics["vertical_error_m"] <= 4.0:
        raise NeoBridgeSceneFailure("Neo bridge vertical error is excessive")
    if not 0.0 <= metrics["regression_m"] <= 8.0:
        raise NeoBridgeSceneFailure("Neo bridge progress regressed")
    if not 0.0 < metrics["speed_mps"] <= 30.0:
        raise NeoBridgeSceneFailure("Neo bridge exit speed is invalid")
    if not 100000 <= metrics["physics_steps"] <= 650000:
        raise NeoBridgeSceneFailure(
            "Neo bridge deterministic physics-step count is invalid"
        )
    return {**common, **metrics}


def build_command(executable: Path, script_name: str) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-map",
            OVERLAY_TERRAIN,
            "-runscript",
            script_name,
        )
    )
    return tuple(command)


def stage_runtime(
    isolated_home: Path,
    *,
    script_path: Path,
    script_record: Mapping[str, object],
    cityworld_archive: Path,
    cityworld_record: Mapping[str, object],
    overlay_archive: Path,
    overlay_record: Mapping[str, object],
    vehicle_archive: Path,
    vehicle_record: Mapping[str, object],
) -> tuple[dict[str, Path], tuple[Path, ...]]:
    layout = base.runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods", "screenshots", "user"):
        layout[key].mkdir(parents=True, exist_ok=True)
    scripts = layout["user"] / "scripts"
    scripts.mkdir()
    staged = (
        (
            script_path,
            scripts / script_path.name,
            script_record["sha256"],
            "runtime script",
        ),
        (
            cityworld_archive,
            layout["mods"] / CITYWORLD_NAME,
            cityworld_record["sha256"],
            "CityWorld archive",
        ),
        (
            overlay_archive,
            layout["mods"] / OVERLAY_NAME,
            overlay_record["sha256"],
            "overlay archive",
        ),
        (
            vehicle_archive,
            layout["mods"] / VEHICLE_ARCHIVE,
            vehicle_record["archive_sha256"],
            "packaged DAF archive",
        ),
    )
    for source, destination, expected_sha, label in staged:
        shutil.copyfile(source, destination)
        if sha256_file(destination) != expected_sha:
            raise NeoBridgeSceneFailure(
                f"staged {label} differs from validated input"
            )
    config_paths = base.write_runtime_config(
        layout["config"],
        shadow_mode="pssm",
        shadow_quality=2,
        target_platform=sys.platform,
    )
    return layout, config_paths


def collect_diagnostics(
    *,
    artifact_staging: Path,
    run_name: str,
    stdout: str,
    engine_log: str,
    script_log: str,
    config_paths: Sequence[Path],
) -> dict[str, object]:
    directory = artifact_staging / run_name / "diagnostics"
    directory.mkdir(parents=True)
    artifacts = {
        "stdout": directory / "runtime.stdout",
        "engine_log": directory / "RoR.log",
        "script_log": directory / "Angelscript.log",
    }
    artifacts["stdout"].write_text(stdout, encoding="utf-8")
    artifacts["engine_log"].write_text(engine_log, encoding="utf-8")
    artifacts["script_log"].write_text(script_log, encoding="utf-8")
    records: dict[str, object] = {}
    for label, path in artifacts.items():
        records[label] = {
            "artifact": path.relative_to(artifact_staging).as_posix(),
            "sha256": sha256_file(path),
            "size": path.stat().st_size,
        }
    configs = {}
    for path in config_paths:
        destination = directory / path.name
        shutil.copy2(path, destination)
        configs[path.name] = {
            "artifact": destination.relative_to(artifact_staging).as_posix(),
            "sha256": sha256_file(destination),
            "size": destination.stat().st_size,
        }
    records["configs"] = configs
    return records


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--cityworld-archive", required=True, type=Path)
    parser.add_argument("--overlay-archive", required=True, type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--rebuild-timeout", type=int, default=240)
    parser.add_argument("--static-timeout", type=int, default=240)
    parser.add_argument("--drive-timeout", type=int, default=900)
    args = parser.parse_args(argv)
    for field in (
        "rebuild_timeout",
        "static_timeout",
        "drive_timeout",
    ):
        if getattr(args, field) <= 0:
            parser.error("--" + field.replace("_", "-") + " must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    base.renderer_contract(sys.platform)
    repository = args.repository.resolve()
    if repository != REPOSITORY_ROOT.resolve():
        raise NeoBridgeSceneFailure(
            "--repository must be the checkout containing this runner"
        )
    executable = args.executable.resolve()
    cityworld_archive = args.cityworld_archive.resolve()
    overlay_archive = args.overlay_archive.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file() or executable.is_symlink():
        raise NeoBridgeSceneFailure(
            f"executable is missing or is a symlink: {executable}"
        )
    if artifact_dir.exists():
        raise NeoBridgeSceneFailure(
            f"artifact directory already exists: {artifact_dir}"
        )

    cityworld_record = corridor.validate_cityworld_archive(
        cityworld_archive
    )
    overlay_report, overlay_record = corridor.validate_overlay_archive(
        overlay_archive,
        repository,
    )
    overlay_rebuild = corridor.verify_overlay_rebuild(
        cityworld_archive,
        overlay_archive,
        overlay_report,
        repository,
        args.rebuild_timeout,
    )
    expected_side_piers = expected_side_pier_counts(overlay_report)
    static_path = repository / STATIC_FIXTURE_PATH
    drive_path = repository / DRIVE_FIXTURE_PATH
    static_fixture = validate_fixture(static_path, drive=False)
    drive_fixture = validate_fixture(drive_path, drive=True)

    runtime_content = (
        base.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise NeoBridgeSceneFailure(
            "runtime content directory is missing"
        )
    vehicle_record = corridor.verify_vehicle_archive(runtime_content)
    vehicle_archive = Path(vehicle_record["archive"])

    artifact_dir.parent.mkdir(parents=True, exist_ok=True)
    artifact_staging = Path(
        tempfile.mkdtemp(
            prefix=f".{artifact_dir.name}.partial-",
            dir=artifact_dir.parent,
        )
    )
    published = False
    try:
        run_records: dict[str, object] = {}
        for run_name, script_path, script_record, timeout in (
            (
                "static",
                static_path,
                static_fixture,
                args.static_timeout,
            ),
            (
                "drive",
                drive_path,
                drive_fixture,
                args.drive_timeout,
            ),
        ):
            with tempfile.TemporaryDirectory(
                prefix=f"ror-cityworld-neoq-{run_name}-"
            ) as temporary:
                isolated_home = Path(temporary)
                layout, config_paths = stage_runtime(
                    isolated_home,
                    script_path=script_path,
                    script_record=script_record,
                    cityworld_archive=cityworld_archive,
                    cityworld_record=cityworld_record,
                    overlay_archive=overlay_archive,
                    overlay_record=overlay_record,
                    vehicle_archive=vehicle_archive,
                    vehicle_record=vehicle_record,
                )
                environment = os.environ.copy()
                environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
                environment["ALSOFT_DRIVERS"] = "null"
                environment["ALSOFT_LOGLEVEL"] = "0"
                command = build_command(executable, script_path.name)
                completed = base.run_command(
                    command,
                    timeout,
                    cwd=executable.parent,
                    environment=environment,
                )
                stdout = base.decode_output(completed.stdout)
                engine_log = base.read_required(
                    layout["logs"] / "RoR.log",
                    "RoR engine log",
                )
                script_log = base.read_required(
                    layout["logs"] / "Angelscript.log",
                    "AngelScript log",
                )
                if run_name == "static":
                    metrics = validate_static_logs(
                        completed.returncode,
                        stdout,
                        engine_log,
                        script_log,
                        expected_side_piers,
                    )
                else:
                    metrics = validate_drive_logs(
                        completed.returncode,
                        stdout,
                        engine_log,
                        script_log,
                        expected_side_piers,
                    )
                diagnostics = collect_diagnostics(
                    artifact_staging=artifact_staging,
                    run_name=run_name,
                    stdout=stdout,
                    engine_log=engine_log,
                    script_log=script_log,
                    config_paths=config_paths,
                )
                rgb_records = []
                if run_name == "static":
                    screenshots = sorted(
                        layout["screenshots"].glob("*.png")
                    )
                    if len(screenshots) != len(STATIC_RGB_NAMES):
                        raise NeoBridgeSceneFailure(
                            "expected exactly six static RGB screenshots, "
                            f"found {len(screenshots)}"
                        )
                    rgb_directory = (
                        artifact_staging / "static" / "rgb"
                    )
                    rgb_directory.mkdir()
                    for source, name in zip(
                        screenshots,
                        STATIC_RGB_NAMES,
                    ):
                        image = base.validate_rgb_png(source)
                        destination = rgb_directory / name
                        shutil.copy2(source, destination)
                        rgb_records.append(
                            {
                                **image,
                                "artifact":
                                    destination.relative_to(
                                        artifact_staging
                                    ).as_posix(),
                            }
                        )
                elif list(layout["screenshots"].glob("*.png")):
                    raise NeoBridgeSceneFailure(
                        "drive gate unexpectedly emitted an RGB screenshot"
                    )
                run_records[run_name] = {
                    "command": list(command),
                    "diagnostics": diagnostics,
                    "fixture": script_record,
                    "metrics": metrics,
                    "rgb": rgb_records,
                }

        report = {
            "acceptance": {
                "collision_enabled": True,
                "destination_city_road_surface_connection_verified": True,
                "native_vehicle_traversal_verified": True,
                "source_city_road_surface_connection_verified": True,
                "static_ui_free_rgb_views": 6,
                "status": "passed",
                "support_completion_verified": True,
            },
            "archives": {
                "cityworld": cityworld_record,
                "overlay": overlay_record,
                "vehicle": vehicle_record,
            },
            "executable": {
                "path": str(executable),
                "sha256": sha256_file(executable),
                "size": executable.stat().st_size,
            },
            "expected_side_pier_counts": list(expected_side_piers),
            "format": REPORT_FORMAT,
            "machine": platform.machine(),
            "overlay_rebuild": overlay_rebuild,
            "platform": platform.platform(),
            "repository_commit": base.git_output(
                repository,
                ("rev-parse", "HEAD"),
            ),
            "runs": run_records,
        }
        report_path = (
            artifact_staging
            / "cityworld_neoq_bridge_runtime.report.json"
        )
        report_path.write_text(
            json.dumps(
                report,
                ensure_ascii=True,
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        corridor.publish_artifact_directory(
            artifact_staging,
            artifact_dir,
        )
        published = True
    finally:
        if not published and artifact_staging.exists():
            shutil.rmtree(artifact_staging)

    print(
        "CityWorld Neo bridge native acceptance passed: "
        f"{artifact_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
