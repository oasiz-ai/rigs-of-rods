#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the CityWorld bridge-to-gateway corridor through RoR."""

from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path
import re
import sys
from typing import Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TRANSITION_RUNNER_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_bridge_transition_scene.py"
)


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


TRANSITION = load_module(
    "ror_cityworld_transition_scene_for_gateway",
    TRANSITION_RUNNER_PATH,
)
BASE = TRANSITION.BASE
SOLVER = TRANSITION.SOLVER
CURVE = TRANSITION.CURVE

GATEWAY_ASSET_MANIFEST = (
    "resources/nextgen/cityworld/streetscape/gateway_block_40m/"
    "rorng_city_gateway_block_40m.asset.json"
)
GATEWAY_COMPILE_REPORT = (
    "resources/nextgen/cityworld/streetscape/gateway_block_40m/compiled/"
    "rorng_city_gateway_block_40m.compile.json"
)
FIXTURE_DIRECTORY = "tests/fixtures/cityworld_gateway_runtime"
FIXTURE_FILES = (
    "LICENSE.md",
    "cityworld_gateway_runtime.as",
    "cityworld_gateway_runtime.terrn2",
    "cityworld_gateway_runtime.tobj",
)
TERRAIN = "cityworld_gateway_runtime.terrn2"
RUNTIME_PACK = "cityworld-next-gateway-runtime.zip"

SCRIPT_MARKERS = (
    "[RoR|CW2|GatewayRuntime] START modules=5 seams=4 turn_degrees=45",
    "[RoR|CW2|GatewayRuntime] ARMED actor=2026072805 heading=",
    "[RoR|CW2|GatewayRuntime] ENTER",
    "[RoR|CW2|GatewayRuntime] SEAM index=0",
    "[RoR|CW2|GatewayRuntime] SEAM index=1",
    "[RoR|CW2|GatewayRuntime] SEAM index=2",
    "[RoR|CW2|GatewayRuntime] SEAM index=3",
    "[RoR|CW2|GatewayRuntime] CAPTURE",
    "[RoR|CW2|GatewayRuntime] EXIT",
    "[RoR|CW2|GatewayRuntime] PASS modules=5 seams=4 turn_degrees=45",
)
LOCAL_LIGHTING_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_gateway_block_40m.odef spotlights=0 point_lights=8 "
    "local_shadow_casters=0"
)
LIGHTING_MARKERS = (
    BASE.FALLBACK_LIGHTING_MARKER,
    LOCAL_LIGHTING_MARKER,
)
ENGINE_MARKERS = (
    *TRANSITION.ENGINE_MARKERS,
    *LIGHTING_MARKERS,
    "Parsing script rorng_city_gateway_block_40m.material",
    "Mesh: Loading rorng_city_gateway_block_40m_lod0.mesh.",
    "Mesh: Loading rorng_city_gateway_block_40m_collision_barrier_left.mesh.",
    "Mesh: Loading rorng_city_gateway_block_40m_collision_barrier_right.mesh.",
    "Mesh: Loading rorng_city_gateway_block_40m_collision_road.mesh.",
    "Pass 0 of 'rorng_gateway_architectural_concrete'",
    "Pass 0 of 'rorng_gateway_asphalt'",
    "Pass 0 of 'rorng_gateway_tree_bark'",
    "Pass 0 of 'rorng_gateway_brick'",
    "Pass 0 of 'rorng_gateway_lamp_emissive'",
    "Pass 0 of 'rorng_gateway_glass_blue'",
    "Pass 0 of 'rorng_gateway_lane_white'",
    "Pass 0 of 'rorng_gateway_lane_yellow'",
    "Pass 0 of 'rorng_gateway_leaf_dark'",
    "Pass 0 of 'rorng_gateway_leaf_light'",
    "Pass 0 of 'rorng_gateway_powdercoat_metal'",
    "Pass 0 of 'rorng_gateway_sidewalk'",
    "Pass 0 of 'rorng_gateway_stone'",
)
FATAL_MARKERS = (
    "[RoR|CW2|GatewayRuntime] FAIL",
    "[ODEF] Could not find rorng_city",
    "Can't assign material to SubMesh of 'rorng_city",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|GatewayRuntime\] PASS modules=5 seams=4 "
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
PERFORMANCE_PATTERN = re.compile(
    r"\[RoR\|CW2\|GatewayRuntime\] PERF "
    r"samples=(?P<samples>[0-9]+) "
    r"mean_ms=(?P<mean>-?[0-9.eE+]+) "
    r"p95_ms=(?P<p95>-?[0-9.eE+]+) "
    r"max_ms=(?P<maximum>-?[0-9.eE+]+)"
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
                f"RoR gateway scene terminated by signal {-returncode}"
            )
        raise BASE.BridgeSceneFailure(
            f"RoR gateway scene exited with {returncode}"
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
    for marker in LIGHTING_MARKERS:
        if engine_log.count(marker) != 1:
            raise BASE.BridgeSceneFailure(
                "gateway lighting marker must appear exactly once: " + marker
            )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise BASE.BridgeSceneFailure(
                f"gateway runtime logged a fatal marker: {marker}"
            )

    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise BASE.BridgeSceneFailure(
            f"expected exactly one gateway PASS record, found {len(matches)}"
        )
    record = matches[0].groupdict()
    performance_matches = list(PERFORMANCE_PATTERN.finditer(script_log))
    if len(performance_matches) != 1:
        raise BASE.BridgeSceneFailure(
            "expected exactly one gateway PERF record, found "
            f"{len(performance_matches)}"
        )
    performance = performance_matches[0].groupdict()
    metrics: dict[str, float | int] = {
        "distance_m": float(record["distance"]),
        "exit_x": float(record["exit_x"]),
        "exit_z": float(record["exit_z"]),
        "frame_max_ms": float(performance["maximum"]),
        "frame_mean_ms": float(performance["mean"]),
        "frame_p95_ms": float(performance["p95"]),
        "frame_samples": int(performance["samples"]),
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
            "gateway PASS metrics contain non-finite values"
        )
    if not 125.0 <= metrics["distance_m"] <= 150.0:
        raise BASE.BridgeSceneFailure(
            "gateway traversal distance is outside its gate"
        )
    if not -1.0 <= metrics["min_y"] <= metrics["max_y"] <= 5.0:
        raise BASE.BridgeSceneFailure(
            "gateway vertical envelope is invalid"
        )
    if metrics["max_y"] - metrics["min_y"] > 2.5:
        raise BASE.BridgeSceneFailure(
            "gateway vertical travel is excessive"
        )
    if not 0.0 <= metrics["max_path_error_m"] <= 2.5:
        raise BASE.BridgeSceneFailure(
            "gateway path error is excessive"
        )
    if not 563.0 <= metrics["exit_x"] <= 572.0:
        raise BASE.BridgeSceneFailure(
            "gateway exit X is outside its gate"
        )
    if not 565.0 <= metrics["exit_z"] <= 575.0:
        raise BASE.BridgeSceneFailure(
            "gateway exit Z is outside its gate"
        )
    if not 0.0 < metrics["speed_mps"] < 40.0:
        raise BASE.BridgeSceneFailure(
            "gateway exit speed is outside its gate"
        )
    if not 1 <= metrics["physics_steps"] <= 48000:
        raise BASE.BridgeSceneFailure(
            "gateway physics-step count is outside its gate"
        )
    if not 500 <= metrics["frame_samples"] <= 4096:
        raise BASE.BridgeSceneFailure(
            "gateway performance sample count is outside its gate"
        )
    if not (
        0.0 < metrics["frame_mean_ms"] <=
        metrics["frame_p95_ms"] <=
        metrics["frame_max_ms"] < 1000.0
    ):
        raise BASE.BridgeSceneFailure(
            "gateway frame-time telemetry is invalid"
        )
    return metrics


def verify_corridor_fixture(repository: Path) -> dict[str, object]:
    profiles = (
        *(
            SOLVER.load_asset_profile(
                repository,
                CURVE.CURVED_ASSET_MANIFEST,
            )
            for _ in range(3)
        ),
        SOLVER.load_asset_profile(
            repository,
            TRANSITION.TRANSITION_ASSET_MANIFEST,
        ),
        SOLVER.load_asset_profile(
            repository,
            GATEWAY_ASSET_MANIFEST,
        ),
    )
    placements = SOLVER.solve_corridor(
        profiles,
        entry_x=512.0,
        entry_z=482.0,
        heading_degrees=0.0,
    )
    expected = SOLVER.tobj_text(placements, surface_y=0.08)
    fixture_path = (
        repository / FIXTURE_DIRECTORY / "cityworld_gateway_runtime.tobj"
    )
    try:
        actual = fixture_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise BASE.BridgeSceneFailure(
            f"cannot read gateway corridor fixture: {fixture_path}"
        ) from error
    if actual != expected:
        raise BASE.BridgeSceneFailure(
            "gateway corridor fixture is stale relative to connector solver"
        )
    corridor = SOLVER.report(profiles, placements, surface_y=0.08)
    if any(
        seam["position_gap_m"] != 0.0
        or seam["heading_error_degrees"] != 0.0
        for seam in corridor["seams"]
    ):
        raise BASE.BridgeSceneFailure(
            "gateway corridor connector continuity is not exact"
        )
    return corridor


def configure_base(repository: Path, corridor: dict[str, object]) -> None:
    BASE.ASSET_MANIFEST = CURVE.CURVED_ASSET_MANIFEST
    BASE.COMPILE_REPORT = CURVE.CURVED_COMPILE_REPORT
    BASE.ADDITIONAL_ASSET_PACKAGES = (
        (
            TRANSITION.TRANSITION_ASSET_MANIFEST,
            TRANSITION.TRANSITION_COMPILE_REPORT,
        ),
        (GATEWAY_ASSET_MANIFEST, GATEWAY_COMPILE_REPORT),
    )
    BASE.FIXTURE_DIRECTORY = FIXTURE_DIRECTORY
    BASE.FIXTURE_FILES = FIXTURE_FILES
    BASE.TERRAIN = TERRAIN
    BASE.RUNTIME_PACK = RUNTIME_PACK
    BASE.SCRIPT_MARKERS = SCRIPT_MARKERS
    BASE.ENGINE_MARKERS = ENGINE_MARKERS
    BASE.FATAL_MARKERS = FATAL_MARKERS
    BASE.PASS_PATTERN = PASS_PATTERN
    BASE.REPORT_FORMAT = "ror-cityworld-gateway-runtime-report-v2"
    BASE.RGB_ARTIFACT_NAME = "cityworld_gateway_rgb.png"
    BASE.SUCCESS_PREFIX = "CityWorld gateway runtime gate passed"
    BASE.DEVIATION_METRIC_KEY = "max_path_error_m"
    BASE.DEVIATION_LABEL = "path_error"
    BASE.RUNNER_PATHS = (
        "tools/run_cityworld_bridge_scene.py",
        "tools/run_cityworld_curved_bridge_scene.py",
        "tools/run_cityworld_bridge_transition_scene.py",
        "tools/run_cityworld_gateway_scene.py",
        "tools/solve_cityworld_bridge_corridor.py",
    )
    BASE.EXTRA_REPORT_FIELDS = {
        "additional_cityworld_compile_reports": [
            {
                "path": path,
                "sha256": BASE.sha256_file(repository / path),
            }
            for path in (
                TRANSITION.TRANSITION_COMPILE_REPORT,
                GATEWAY_COMPILE_REPORT,
            )
        ],
        "corridor": corridor,
    }
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
    configure_base(repository, corridor)
    return BASE.main(arguments)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BASE.BridgeSceneFailure, SOLVER.CorridorFailure) as error:
        print(f"CityWorld gateway scene gate failed: {error}", file=sys.stderr)
        raise SystemExit(1)
