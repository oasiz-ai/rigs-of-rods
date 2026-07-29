#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the collisionless CityWorld bridge streetlight in native RoR."""

from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path
import re
import sys
from typing import Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = REPOSITORY_ROOT / "tools/run_cityworld_bridge_scene.py"


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


BASE = load_module("ror_cityworld_bridge_streetlight_base", BASE_PATH)

ASSET_MANIFEST = (
    "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/"
    "rorng_city_led_streetlight_bridge.asset.json"
)
COMPILE_REPORT = (
    "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/compiled/"
    "rorng_city_led_streetlight_bridge.compile.json"
)
FIXTURE_DIRECTORY = "tests/fixtures/cityworld_bridge_streetlight_runtime"
FIXTURE_FILES = (
    "LICENSE.md",
    "cityworld_bridge_streetlight_runtime.as",
    "cityworld_bridge_streetlight_runtime.terrn2",
    "cityworld_bridge_streetlight_runtime.tobj",
)
TERRAIN = "cityworld_bridge_streetlight_runtime.terrn2"
RUNTIME_PACK = "cityworld-next-bridge-streetlight-runtime.zip"

ASSET_ID = "rorng_city_led_streetlight_bridge"
EXPECTED_TOBJ = (
    "512, 0.08, 500, 0, 0, 0, rorng_city_led_streetlight_bridge"
    " - collisionless_bridge_fixture\n"
)
EXPECTED_ODEF = (
    "rorng_city_led_streetlight_bridge_lod0.mesh\n"
    "1, 1, 1\n"
    "standard\n"
    "\n"
    "pointlight 0, 7.12, -1.58, 0, -1, 0, 1, 0.72, 0.3, 24\n"
    "\n"
    "end\n"
)
EXPECTED_OUTPUTS = {
    "material-fallback": "rorng_city_led_streetlight_bridge.material",
    "render-lod0": "rorng_city_led_streetlight_bridge_lod0.mesh",
    "render-lod1": "rorng_city_led_streetlight_bridge_lod1.mesh",
    "render-lod2": "rorng_city_led_streetlight_bridge_lod2.mesh",
    "terrain-object": "rorng_city_led_streetlight_bridge.odef",
}
EXPECTED_MATERIALS = {
    "rorng_fixture_galvanized_steel",
    "rorng_fixture_led_lens_emissive",
    "rorng_fixture_lens_gasket",
    "rorng_fixture_powdercoat_graphite",
}
EXPECTED_RUNTIME_LIGHT = {
    "color_linear": [1.0, 0.72, 0.3],
    "id": "rorng_bridge_streetlight_warm",
    "position_ogre_y_up_m": [0.0, 7.12, -1.58],
    "range_m": 24.0,
    "type": "point",
}
FALLBACK_LIGHTING_MARKER = BASE.fallback_lighting_marker(
    (0.24, 0.24, 0.24)
)

SCRIPT_MARKERS = (
    "[RoR|CW1|BridgeStreetlightRuntime] START fixtures=1 "
    "collision_objects=0 point_lights=1",
    "[RoR|CW1|BridgeStreetlightRuntime] LOADED "
    "collision_subsystem=enabled",
    "[RoR|CW1|BridgeStreetlightRuntime] CAPTURE",
    "[RoR|CW1|BridgeStreetlightRuntime] PASS fixtures=1 "
    "collision_objects=0 point_lights=1",
)
ENGINE_MARKERS = (
    "Parsing script rorng_city_led_streetlight_bridge.material",
    "Mesh: Loading rorng_city_led_streetlight_bridge_lod0.mesh.",
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_led_streetlight_bridge.odef "
    "spotlights=0 point_lights=1 local_shadow_casters=0",
    "[RoR|TerrainObject|LocalLightBudget] "
    "discovered=1 active=1 budget=64",
    FALLBACK_LIGHTING_MARKER,
    "Pass 0 of 'rorng_fixture_galvanized_steel'",
    "Pass 0 of 'rorng_fixture_led_lens_emissive'",
    "Pass 0 of 'rorng_fixture_lens_gasket'",
    "Pass 0 of 'rorng_fixture_powdercoat_graphite'",
)
ENGINE_SINGLETON_MARKERS = ENGINE_MARKERS[:5]
FATAL_MARKERS = (
    "[RoR|CW1|BridgeStreetlightRuntime] FAIL",
    "[ODEF] Could not find rorng_city_led_streetlight_bridge",
    "Can't assign material to SubMesh of "
    "'rorng_city_led_streetlight_bridge",
    "Mesh: Loading rorng_city_led_streetlight_bridge_collision",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
SCOPED_COMPILER_ERROR_PATTERN = re.compile(
    r"Error: ScriptCompiler[^\r\n]*"
    r"(?:rorng_city_led_streetlight_bridge\.material|rorng_fixture_)"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW1\|BridgeStreetlightRuntime\] PASS "
    r"fixtures=1 collision_objects=0 point_lights=1 "
    r"frames=(?P<frames>[0-9]+) "
    r"physics_steps=(?P<steps>[0-9]+)"
)


def require_mapping(value: object, label: str) -> Mapping[str, object]:
    if not isinstance(value, dict):
        raise BASE.BridgeSceneFailure(f"{label} is not an object")
    return value


def require_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise BASE.BridgeSceneFailure(f"{label} is not an array")
    return value


def read_text(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise BASE.BridgeSceneFailure(f"cannot read {label}: {path}") from error


def validate_fixture_contract(repository: Path) -> dict[str, object]:
    """Authenticate the visual, point-light, and zero-collision contract."""

    manifest_path = repository / ASSET_MANIFEST
    manifest = BASE.load_json(manifest_path)
    asset = require_mapping(manifest.get("asset"), "bridge streetlight asset")
    if (
        manifest.get("format") != "ror-cityworld-asset-v1"
        or asset.get("id") != ASSET_ID
        or asset.get("profile") != "static-visual-v1"
        or asset.get("version") != 1
    ):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight asset identity or profile drifted"
        )

    collision = require_mapping(
        manifest.get("collision"),
        "bridge streetlight collision contract",
    )
    if (
        collision.get("profile") != "collisionless-visual-v1"
        or collision.get("separate_from_render_mesh") is not True
        or require_list(
            collision.get("objects"),
            "bridge streetlight collision objects",
        )
    ):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight must have an explicit empty collision inventory"
        )

    compiled = require_mapping(
        manifest.get("compiled"),
        "bridge streetlight compiled contract",
    )
    raw_outputs = require_list(
        compiled.get("outputs"),
        "bridge streetlight compiled outputs",
    )
    observed_outputs: dict[str, str] = {}
    for raw_output in raw_outputs:
        output = require_mapping(
            raw_output,
            "bridge streetlight compiled output",
        )
        role = output.get("role")
        path = output.get("path")
        if (
            not isinstance(role, str)
            or role in observed_outputs
            or not isinstance(path, str)
        ):
            raise BASE.BridgeSceneFailure(
                "bridge streetlight compiled output inventory is ambiguous"
            )
        observed_outputs[role] = Path(path).name
    if observed_outputs != EXPECTED_OUTPUTS:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight compiled output inventory drifted"
        )
    if any("collision" in role.casefold() for role in observed_outputs):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight unexpectedly compiled a collision role"
        )

    raw_materials = require_list(
        manifest.get("materials"),
        "bridge streetlight materials",
    )
    observed_materials: set[str] = set()
    for raw_material in raw_materials:
        material = require_mapping(
            raw_material,
            "bridge streetlight material",
        )
        name = material.get("name")
        if not isinstance(name, str) or name in observed_materials:
            raise BASE.BridgeSceneFailure(
                "bridge streetlight material inventory is ambiguous"
            )
        observed_materials.add(name)
    if observed_materials != EXPECTED_MATERIALS:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight material inventory drifted"
        )

    runtime_lights = require_mapping(
        manifest.get("runtime_lights"),
        "bridge streetlight runtime lights",
    )
    if (
        runtime_lights.get("profile") != "ror-cityworld-local-lights-v1"
        or require_list(
            runtime_lights.get("lights"),
            "bridge streetlight runtime light entries",
        )
        != [
            {
                "color_linear": [1.0, 0.72, 0.3],
                "id": "rorng_bridge_streetlight_warm",
                "position_blender_z_up_m": [0.0, 1.58, 7.12],
                "range_m": 24.0,
                "type": "point",
            }
        ]
    ):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight authored point-light contract drifted"
        )

    compile_report = BASE.load_json(repository / COMPILE_REPORT)
    if compile_report.get("runtime_lights") != [EXPECTED_RUNTIME_LIGHT]:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight compiled point-light contract drifted"
        )

    odef_path = (
        repository
        / "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/"
        "compiled/rorng_city_led_streetlight_bridge.odef"
    )
    odef = read_text(odef_path, "bridge streetlight ODEF")
    if odef != EXPECTED_ODEF:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight ODEF drifted from its reviewed contract"
        )
    if (
        odef.count("\npointlight ") != 1
        or any(
            directive in odef.casefold()
            for directive in (
                "\nbeginbox",
                "\nbeginmesh",
                "\nfrictionconfig",
                "\nstdfriction",
            )
        )
    ):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight ODEF is not point-lit and collisionless"
        )

    tobj_path = (
        repository
        / FIXTURE_DIRECTORY
        / "cityworld_bridge_streetlight_runtime.tobj"
    )
    if read_text(tobj_path, "bridge streetlight placement") != EXPECTED_TOBJ:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight placement fixture drifted"
        )

    return {
        "asset_id": ASSET_ID,
        "asset_manifest": {
            "path": ASSET_MANIFEST,
            "sha256": BASE.sha256_file(manifest_path),
        },
        "collision": {
            "objects": 0,
            "profile": "collisionless-visual-v1",
        },
        "materials": sorted(EXPECTED_MATERIALS),
        "placement": {"x": 512.0, "y": 0.08, "z": 500.0},
        "runtime_light": EXPECTED_RUNTIME_LIGHT,
    }


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> dict[str, float | int]:
    if returncode != 0:
        if returncode < 0:
            raise BASE.BridgeSceneFailure(
                "RoR bridge streetlight scene terminated by signal "
                f"{-returncode}"
            )
        raise BASE.BridgeSceneFailure(
            f"RoR bridge streetlight scene exited with {returncode}"
        )

    for marker in SCRIPT_MARKERS:
        if script_log.count(marker) != 1:
            raise BASE.BridgeSceneFailure(
                "AngelScript log must contain exactly one marker: " + marker
            )
    for marker in ENGINE_MARKERS:
        count = engine_log.count(marker)
        if marker in ENGINE_SINGLETON_MARKERS:
            valid_count = count == 1
        else:
            valid_count = count in (2, 4)
        if not valid_count:
            raise BASE.BridgeSceneFailure(
                "engine log marker count is outside the bridge streetlight "
                "contract: " + marker
            )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise BASE.BridgeSceneFailure(
                "bridge streetlight runtime logged a fatal marker: " + marker
            )
    if SCOPED_COMPILER_ERROR_PATTERN.search(engine_log):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight material logged a ScriptCompiler error"
        )

    passes = list(PASS_PATTERN.finditer(script_log))
    if len(passes) != 1:
        raise BASE.BridgeSceneFailure(
            "expected exactly one bridge streetlight PASS record, "
            f"found {len(passes)}"
        )
    record = passes[0].groupdict()
    frames = int(record["frames"])
    physics_steps = int(record["steps"])
    metrics: dict[str, float | int] = {
        "collision_extent_m": 0.0,
        "collision_objects": 0,
        "distance_m": 0.0,
        "frames": frames,
        "physics_steps": physics_steps,
        "point_lights": 1,
    }
    floats = [
        value for value in metrics.values() if isinstance(value, float)
    ]
    if not all(math.isfinite(value) for value in floats):
        raise BASE.BridgeSceneFailure(
            "bridge streetlight metrics contain non-finite values"
        )
    if frames != 45:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight rendered an unexpected frame count"
        )
    if not 1 <= physics_steps <= 4096:
        raise BASE.BridgeSceneFailure(
            "bridge streetlight physics-step count is outside its gate"
        )
    return metrics


def configure_base(
    repository: Path,
    fixture_contract: dict[str, object],
) -> None:
    BASE.ASSET_MANIFEST = ASSET_MANIFEST
    BASE.COMPILE_REPORT = COMPILE_REPORT
    BASE.ADDITIONAL_ASSET_PACKAGES = ()
    BASE.FIXTURE_DIRECTORY = FIXTURE_DIRECTORY
    BASE.FIXTURE_FILES = FIXTURE_FILES
    BASE.TERRAIN = TERRAIN
    BASE.RUNTIME_PACK = RUNTIME_PACK
    BASE.SCRIPT_MARKERS = SCRIPT_MARKERS
    BASE.ENGINE_MARKERS = ENGINE_MARKERS
    BASE.FATAL_MARKERS = FATAL_MARKERS
    BASE.PASS_PATTERN = PASS_PATTERN
    BASE.REPORT_FORMAT = "ror-cityworld-bridge-streetlight-runtime-report-v1"
    BASE.RGB_ARTIFACT_NAME = "cityworld_bridge_streetlight_rgb.png"
    BASE.SUCCESS_PREFIX = "CityWorld bridge streetlight runtime gate passed"
    BASE.DEVIATION_METRIC_KEY = "collision_extent_m"
    BASE.DEVIATION_LABEL = "collision_extent"
    BASE.RUNNER_PATHS = (
        "tools/run_cityworld_bridge_scene.py",
        "tools/run_cityworld_bridge_streetlight_scene.py",
    )
    BASE.EXTRA_REPORT_FIELDS = {
        "fixture_contract": fixture_contract,
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
    fixture_contract = validate_fixture_contract(repository)
    configure_base(repository, fixture_contract)
    return BASE.main(arguments)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BASE.BridgeSceneFailure as error:
        print(
            f"CityWorld bridge streetlight scene gate failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
