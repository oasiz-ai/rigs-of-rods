#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the connector-solved CityWorld curved bridge through RoR physics."""

from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path
import re
import sys
from typing import Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = REPOSITORY_ROOT / "tools/run_cityworld_bridge_scene.py"
SOLVER_PATH = REPOSITORY_ROOT / "tools/solve_cityworld_bridge_corridor.py"


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


BASE = load_module("ror_cityworld_bridge_scene_base", BASE_PATH)
SOLVER = load_module("ror_cityworld_bridge_corridor_solver", SOLVER_PATH)

CURVED_ASSET_MANIFEST = (
    "resources/nextgen/cityworld/bridge/curve_left_15deg/"
    "rorng_city_bridge_curve_left_15deg_20m.asset.json"
)
CURVED_COMPILE_REPORT = (
    "resources/nextgen/cityworld/bridge/curve_left_15deg/compiled/"
    "rorng_city_bridge_curve_left_15deg_20m.compile.json"
)
CURVED_FIXTURE_DIRECTORY = "tests/fixtures/cityworld_curved_bridge_runtime"
CURVED_FIXTURE_FILES = (
    "LICENSE.md",
    "cityworld_curved_bridge_runtime.as",
    "cityworld_curved_bridge_runtime.terrn2",
    "cityworld_curved_bridge_runtime.tobj",
)
CURVED_TERRAIN = "cityworld_curved_bridge_runtime.terrn2"
CURVED_RUNTIME_PACK = "cityworld-next-curved-bridge-runtime.zip"

SCRIPT_MARKERS = (
    "[RoR|CW2|CurveRuntime] START modules=3 seams=2 turn_degrees=45",
    "[RoR|CW2|CurveRuntime] ARMED actor=2026072803 heading=",
    "[RoR|CW2|CurveRuntime] ENTER",
    "[RoR|CW2|CurveRuntime] SEAM index=0",
    "[RoR|CW2|CurveRuntime] CAPTURE",
    "[RoR|CW2|CurveRuntime] SEAM index=1",
    "[RoR|CW2|CurveRuntime] EXIT",
    "[RoR|CW2|CurveRuntime] PASS modules=3 seams=2 turn_degrees=45",
)
ENGINE_MARKERS = (
    "Parsing script rorng_city_bridge_curve_left_15deg_20m.material",
    "Mesh: Loading rorng_city_bridge_curve_left_15deg_20m_lod0.mesh.",
    "Mesh: Loading rorng_city_bridge_curve_left_15deg_20m_collision_barrier_left.mesh.",
    "Mesh: Loading rorng_city_bridge_curve_left_15deg_20m_collision_barrier_right.mesh.",
    "Mesh: Loading rorng_city_bridge_curve_left_15deg_20m_collision_road.mesh.",
    "Pass 0 of 'rorng_city_concrete'",
    "Pass 0 of 'rorng_city_asphalt'",
    "Pass 0 of 'rorng_city_galvanized_steel'",
    "Pass 0 of 'rorng_city_dark_steel'",
    "Pass 0 of 'rorng_city_lane_white'",
    "Pass 0 of 'rorng_city_lane_yellow'",
    "Pass 0 of 'rorng_city_lamp_emissive'",
)
FATAL_MARKERS = (
    "[RoR|CW2|CurveRuntime] FAIL",
    "[ODEF] Could not find rorng_city_bridge",
    "Can't assign material to SubMesh of 'rorng_city_bridge",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|CurveRuntime\] PASS modules=3 seams=2 "
    r"turn_degrees=45 "
    r"distance_m=(?P<distance>-?[0-9.eE+]+) "
    r"min_y=(?P<min_y>-?[0-9.eE+]+) "
    r"max_y=(?P<max_y>-?[0-9.eE+]+) "
    r"path_error=(?P<path_error>-?[0-9.eE+]+) "
    r"exit_x=(?P<exit_x>-?[0-9.eE+]+) "
    r"exit_z=(?P<exit_z>-?[0-9.eE+]+) "
    r"speed=(?P<speed>-?[0-9.eE+]+) "
    r"physics_steps=(?P<steps>[0-9]+)"
)


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> dict[str, float | int]:
    if returncode != 0:
        if returncode < 0:
            raise BASE.BridgeSceneFailure(
                f"RoR curved bridge scene terminated by signal {-returncode}"
            )
        raise BASE.BridgeSceneFailure(
            f"RoR curved bridge scene exited with {returncode}"
        )
    for marker in SCRIPT_MARKERS:
        if marker not in script_log:
            raise BASE.BridgeSceneFailure(
                f"AngelScript log missed marker: {marker}"
            )
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise BASE.BridgeSceneFailure(
                f"engine log missed marker: {marker}"
            )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise BASE.BridgeSceneFailure(
                f"curved bridge runtime logged a fatal marker: {marker}"
            )

    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise BASE.BridgeSceneFailure(
            f"expected exactly one curved bridge PASS record, found {len(matches)}"
        )
    record = matches[0].groupdict()
    metrics: dict[str, float | int] = {
        "distance_m": float(record["distance"]),
        "exit_x": float(record["exit_x"]),
        "exit_z": float(record["exit_z"]),
        "max_path_error_m": float(record["path_error"]),
        "max_y": float(record["max_y"]),
        "min_y": float(record["min_y"]),
        "physics_steps": int(record["steps"]),
        "speed_mps": float(record["speed"]),
        "turn_degrees": 45.0,
    }
    floats = [value for value in metrics.values() if isinstance(value, float)]
    if not all(math.isfinite(value) for value in floats):
        raise BASE.BridgeSceneFailure(
            "curved bridge PASS metrics contain non-finite values"
        )
    if not 80.0 <= metrics["distance_m"] <= 105.0:
        raise BASE.BridgeSceneFailure(
            "curved bridge traversal distance is outside its gate"
        )
    if not -1.0 <= metrics["min_y"] <= metrics["max_y"] <= 5.0:
        raise BASE.BridgeSceneFailure(
            "curved bridge vertical envelope is invalid"
        )
    if metrics["max_y"] - metrics["min_y"] > 2.5:
        raise BASE.BridgeSceneFailure(
            "curved bridge vertical travel is excessive"
        )
    if not 0.0 <= metrics["max_path_error_m"] <= 2.25:
        raise BASE.BridgeSceneFailure(
            "curved bridge path error is excessive"
        )
    if not 530.0 <= metrics["exit_x"] <= 538.0:
        raise BASE.BridgeSceneFailure(
            "curved bridge exit X is outside its gate"
        )
    if not 533.0 <= metrics["exit_z"] <= 538.0:
        raise BASE.BridgeSceneFailure(
            "curved bridge exit Z is outside its gate"
        )
    if not 0.0 < metrics["speed_mps"] < 40.0:
        raise BASE.BridgeSceneFailure(
            "curved bridge exit speed is outside its gate"
        )
    if not 1 <= metrics["physics_steps"] <= 36000:
        raise BASE.BridgeSceneFailure(
            "curved bridge physics-step count is outside its gate"
        )
    return metrics


def verify_corridor_fixture(repository: Path) -> dict[str, object]:
    profiles = tuple(
        SOLVER.load_asset_profile(repository, CURVED_ASSET_MANIFEST)
        for _ in range(3)
    )
    placements = SOLVER.solve_corridor(
        profiles,
        entry_x=512.0,
        entry_z=482.0,
        heading_degrees=0.0,
    )
    expected = SOLVER.tobj_text(placements, surface_y=0.08)
    fixture_path = (
        repository
        / CURVED_FIXTURE_DIRECTORY
        / "cityworld_curved_bridge_runtime.tobj"
    )
    try:
        actual = fixture_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise BASE.BridgeSceneFailure(
            f"cannot read curved corridor fixture: {fixture_path}"
        ) from error
    if actual != expected:
        raise BASE.BridgeSceneFailure(
            "curved corridor fixture is stale relative to connector solver"
        )
    corridor = SOLVER.report(profiles, placements, surface_y=0.08)
    seams = corridor["seams"]
    if any(
        seam["position_gap_m"] != 0.0
        or seam["heading_error_degrees"] != 0.0
        for seam in seams
    ):
        raise BASE.BridgeSceneFailure(
            "curved corridor connector continuity is not exact"
        )
    return corridor


def configure_base(corridor: dict[str, object]) -> None:
    BASE.ASSET_MANIFEST = CURVED_ASSET_MANIFEST
    BASE.COMPILE_REPORT = CURVED_COMPILE_REPORT
    BASE.FIXTURE_DIRECTORY = CURVED_FIXTURE_DIRECTORY
    BASE.FIXTURE_FILES = CURVED_FIXTURE_FILES
    BASE.TERRAIN = CURVED_TERRAIN
    BASE.RUNTIME_PACK = CURVED_RUNTIME_PACK
    BASE.SCRIPT_MARKERS = SCRIPT_MARKERS
    BASE.ENGINE_MARKERS = ENGINE_MARKERS
    BASE.FATAL_MARKERS = FATAL_MARKERS
    BASE.PASS_PATTERN = PASS_PATTERN
    BASE.REPORT_FORMAT = "ror-cityworld-curved-bridge-runtime-report-v1"
    BASE.RGB_ARTIFACT_NAME = "cityworld_curved_bridge_rgb.png"
    BASE.SUCCESS_PREFIX = "CityWorld curved bridge runtime gate passed"
    BASE.DEVIATION_METRIC_KEY = "max_path_error_m"
    BASE.DEVIATION_LABEL = "path_error"
    BASE.RUNNER_PATHS = (
        "tools/run_cityworld_bridge_scene.py",
        "tools/run_cityworld_curved_bridge_scene.py",
        "tools/solve_cityworld_bridge_corridor.py",
    )
    BASE.EXTRA_REPORT_FIELDS = {"corridor": corridor}
    BASE.validate_runtime_logs = validate_runtime_logs


def repository_from_args(argv: Sequence[str]) -> Path:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument(
        "--repository",
        type=Path,
        default=REPOSITORY_ROOT,
    )
    args, _ = parser.parse_known_args(argv)
    return args.repository.resolve()


def main(argv: Sequence[str] | None = None) -> int:
    arguments = tuple(sys.argv[1:] if argv is None else argv)
    repository = repository_from_args(arguments)
    corridor = verify_corridor_fixture(repository)
    configure_base(corridor)
    return BASE.main(arguments)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BASE.BridgeSceneFailure, SOLVER.CorridorFailure) as error:
        print(
            f"CityWorld curved bridge scene gate failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
