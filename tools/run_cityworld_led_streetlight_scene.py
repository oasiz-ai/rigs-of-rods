#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render and collide with the compiled CityWorld LED streetlight in RoR."""

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


BASE = load_module("ror_cityworld_led_streetlight_scene_base", BASE_PATH)

ASSET_MANIFEST = (
    "resources/nextgen/cityworld/fixtures/led_streetlight/"
    "rorng_city_led_streetlight.asset.json"
)
COMPILE_REPORT = (
    "resources/nextgen/cityworld/fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight.compile.json"
)
FIXTURE_DIRECTORY = "tests/fixtures/cityworld_led_streetlight_runtime"
FIXTURE_FILES = (
    "LICENSE.md",
    "cityworld_led_streetlight_runtime.as",
    "cityworld_led_streetlight_runtime.terrn2",
    "cityworld_led_streetlight_runtime.tobj",
)
TERRAIN = "cityworld_led_streetlight_runtime.terrn2"
RUNTIME_PACK = "cityworld-next-led-streetlight-runtime.zip"

ASSET_ID = "rorng_city_led_streetlight"
COLLISION_NAME = "rorng_city_led_streetlight_collision_fixture"
SCENE_SPAWN_Z = 466.0
EXPECTED_TOBJ = (
    "512, 0.08, 500, 0, 0, 0, rorng_city_led_streetlight"
    " - led_streetlight_collision_target\n"
)
EXPECTED_OUTPUTS = {
    "material-fallback": "rorng_city_led_streetlight.material",
    "terrain-object": "rorng_city_led_streetlight.odef",
    "collision-fixture": (
        "rorng_city_led_streetlight_collision_fixture.mesh"
    ),
    "render-lod0": "rorng_city_led_streetlight_lod0.mesh",
    "render-lod1": "rorng_city_led_streetlight_lod1.mesh",
    "render-lod2": "rorng_city_led_streetlight_lod2.mesh",
}
EXPECTED_MATERIALS = {
    "rorng_fixture_collision_debug",
    "rorng_fixture_galvanized_steel",
    "rorng_fixture_led_lens_emissive",
    "rorng_fixture_lens_gasket",
    "rorng_fixture_powdercoat_graphite",
    "rorng_fixture_precast_concrete",
}

SCRIPT_MARKERS = (
    "[RoR|CW1|StreetlightRuntime] START fixtures=1 collision_triangles=44",
    "[RoR|CW1|StreetlightRuntime] ARMED actor=2026072901 nodes=",
    "[RoR|CW1|StreetlightRuntime] CONTACT step=",
    "[RoR|CW1|StreetlightRuntime] CAPTURE",
    "[RoR|CW1|StreetlightRuntime] PASS fixtures=1 collision_triangles=44",
)
ENGINE_MARKERS = (
    "Parsing script rorng_city_led_streetlight.material",
    "Mesh: Loading rorng_city_led_streetlight_lod0.mesh.",
    "Mesh: Loading rorng_city_led_streetlight_collision_fixture.mesh.",
    "Pass 0 of 'rorng_fixture_precast_concrete'",
    "Pass 0 of 'rorng_fixture_galvanized_steel'",
    "Pass 0 of 'rorng_fixture_powdercoat_graphite'",
    "Pass 0 of 'rorng_fixture_lens_gasket'",
    "Pass 0 of 'rorng_fixture_led_lens_emissive'",
)
ENGINE_SINGLETON_MARKERS = ENGINE_MARKERS[:3]
FATAL_MARKERS = (
    "[RoR|CW1|StreetlightRuntime] FAIL",
    "[ODEF] Could not find rorng_city_led_streetlight",
    "Can't assign material to SubMesh of 'rorng_city_led_streetlight",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
SCOPED_COMPILER_ERROR_PATTERN = re.compile(
    r"Error: ScriptCompiler[^\r\n]*"
    r"(?:rorng_city_led_streetlight\.material|rorng_fixture_)"
)
CONTACT_PATTERN = re.compile(
    r"\[RoR\|CW1\|StreetlightRuntime\] CONTACT "
    r"step=(?P<step>[0-9]+) "
    r"clearance=(?P<clearance>-?[0-9.eE+]+) "
    r"approach_speed=(?P<approach>-?[0-9.eE+]+) "
    r"actor_z=(?P<actor_z>-?[0-9.eE+]+)"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW1\|StreetlightRuntime\] PASS "
    r"fixtures=1 collision_triangles=44 "
    r"approach_speed=(?P<approach>-?[0-9.eE+]+) "
    r"post_contact_speed=(?P<post>-?[0-9.eE+]+) "
    r"min_clearance=(?P<clearance>-?[0-9.eE+]+) "
    r"contact_travel=(?P<travel>-?[0-9.eE+]+) "
    r"max_z=(?P<max_z>-?[0-9.eE+]+) "
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


def validate_fixture_contract(repository: Path) -> dict[str, object]:
    """Authenticate the checked render, collision, and placement contract."""

    manifest_path = repository / ASSET_MANIFEST
    manifest = BASE.load_json(manifest_path)
    asset = require_mapping(manifest.get("asset"), "streetlight asset")
    if (
        manifest.get("format") != "ror-cityworld-asset-v1"
        or asset.get("id") != ASSET_ID
        or asset.get("profile") != "static-fixture-v1"
        or asset.get("version") != 1
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight asset identity or profile drifted"
        )

    collision = require_mapping(
        manifest.get("collision"),
        "streetlight collision contract",
    )
    objects = require_list(
        collision.get("objects"),
        "streetlight collision objects",
    )
    if (
        collision.get("profile") != "single-watertight-proxy-v1"
        or collision.get("separate_from_render_mesh") is not True
        or len(objects) != 1
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight collision proxy contract drifted"
        )
    collision_object = require_mapping(
        objects[0],
        "streetlight collision object",
    )
    topology = require_mapping(
        collision_object.get("topology"),
        "streetlight collision topology",
    )
    if (
        collision_object.get("name") != COLLISION_NAME
        or collision_object.get("role") != "collision-fixture"
        or collision_object.get("triangles") != 44
        or topology.get("connected_components") != 1
        or topology.get("intersecting_faces") != 0
        or topology.get("outward_winding") is not True
        or topology.get("watertight") is not True
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight collision mesh is not the reviewed proxy"
        )

    compiled = require_mapping(
        manifest.get("compiled"),
        "streetlight compiled contract",
    )
    raw_outputs = require_list(
        compiled.get("outputs"),
        "streetlight compiled outputs",
    )
    observed_outputs: dict[str, str] = {}
    for raw_output in raw_outputs:
        output = require_mapping(
            raw_output,
            "streetlight compiled output",
        )
        role = output.get("role")
        path = output.get("path")
        if (
            not isinstance(role, str)
            or role in observed_outputs
            or not isinstance(path, str)
        ):
            raise BASE.BridgeSceneFailure(
                "streetlight compiled output inventory is ambiguous"
            )
        observed_outputs[role] = Path(path).name
    if observed_outputs != EXPECTED_OUTPUTS:
        raise BASE.BridgeSceneFailure(
            "streetlight compiled output inventory drifted"
        )

    raw_materials = require_list(
        manifest.get("materials"),
        "streetlight materials",
    )
    observed_materials: set[str] = set()
    for raw_material in raw_materials:
        material = require_mapping(
            raw_material,
            "streetlight material",
        )
        name = material.get("name")
        if not isinstance(name, str) or name in observed_materials:
            raise BASE.BridgeSceneFailure(
                "streetlight material inventory is ambiguous"
            )
        observed_materials.add(name)
    if observed_materials != EXPECTED_MATERIALS:
        raise BASE.BridgeSceneFailure(
            "streetlight material inventory drifted"
        )

    fixture_root = repository / FIXTURE_DIRECTORY
    tobj_path = fixture_root / "cityworld_led_streetlight_runtime.tobj"
    try:
        tobj = tobj_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise BASE.BridgeSceneFailure(
            f"cannot read streetlight placement fixture: {tobj_path}"
        ) from error
    if tobj != EXPECTED_TOBJ:
        raise BASE.BridgeSceneFailure(
            "streetlight placement fixture drifted"
        )

    return {
        "asset_id": ASSET_ID,
        "asset_manifest": {
            "path": ASSET_MANIFEST,
            "sha256": BASE.sha256_file(manifest_path),
        },
        "collision": {
            "name": COLLISION_NAME,
            "profile": "single-watertight-proxy-v1",
            "triangles": 44,
            "watertight": True,
        },
        "materials": sorted(EXPECTED_MATERIALS),
        "placement": {
            "x": 512.0,
            "y": 0.08,
            "z": 500.0,
        },
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
                f"RoR streetlight scene terminated by signal {-returncode}"
            )
        raise BASE.BridgeSceneFailure(
            f"RoR streetlight scene exited with {returncode}"
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
                "engine log marker count is outside its contract: " + marker
            )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise BASE.BridgeSceneFailure(
                f"streetlight runtime logged a fatal marker: {marker}"
            )
    if SCOPED_COMPILER_ERROR_PATTERN.search(engine_log):
        raise BASE.BridgeSceneFailure(
            "streetlight material logged a ScriptCompiler error"
        )

    contacts = list(CONTACT_PATTERN.finditer(script_log))
    passes = list(PASS_PATTERN.finditer(script_log))
    if len(contacts) != 1:
        raise BASE.BridgeSceneFailure(
            "expected exactly one streetlight CONTACT record, "
            f"found {len(contacts)}"
        )
    if len(passes) != 1:
        raise BASE.BridgeSceneFailure(
            "expected exactly one streetlight PASS record, "
            f"found {len(passes)}"
        )

    contact = contacts[0].groupdict()
    record = passes[0].groupdict()
    metrics: dict[str, float | int] = {
        "approach_speed_mps": float(record["approach"]),
        "contact_actor_z_m": float(contact["actor_z"]),
        "contact_clearance_m": float(contact["clearance"]),
        "contact_step": int(contact["step"]),
        "contact_travel_m": float(record["travel"]),
        "distance_m": float(contact["actor_z"]) - SCENE_SPAWN_Z,
        "max_z_m": float(record["max_z"]),
        "min_clearance_m": float(record["clearance"]),
        "physics_steps": int(record["steps"]),
        "post_contact_speed_mps": float(record["post"]),
    }
    floats = [
        value for value in metrics.values() if isinstance(value, float)
    ]
    if not all(math.isfinite(value) for value in floats):
        raise BASE.BridgeSceneFailure(
            "streetlight collision metrics contain non-finite values"
        )
    if not 2.0 <= metrics["approach_speed_mps"] <= 20.0:
        raise BASE.BridgeSceneFailure(
            "streetlight approach speed is outside its gate"
        )
    if (
        abs(
            float(contact["approach"])
            - metrics["approach_speed_mps"]
        )
        > 0.05
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight CONTACT and PASS speeds disagree"
        )
    if not -0.15 <= metrics["contact_clearance_m"] <= 0.25:
        raise BASE.BridgeSceneFailure(
            "streetlight contact clearance is outside its gate"
        )
    if not -0.15 <= metrics["min_clearance_m"] <= 0.25:
        raise BASE.BridgeSceneFailure(
            "streetlight minimum clearance is outside its gate"
        )
    if metrics["min_clearance_m"] > metrics["contact_clearance_m"] + 0.01:
        raise BASE.BridgeSceneFailure(
            "streetlight clearance telemetry is inconsistent"
        )
    if not 0.0 <= metrics["contact_travel_m"] <= 3.5:
        raise BASE.BridgeSceneFailure(
            "streetlight post-contact travel is outside its gate"
        )
    if not 485.0 <= metrics["contact_actor_z_m"] <= 505.0:
        raise BASE.BridgeSceneFailure(
            "streetlight contact position is outside its gate"
        )
    if not 19.0 <= metrics["distance_m"] <= 39.0:
        raise BASE.BridgeSceneFailure(
            "streetlight approach distance is outside its gate"
        )
    if not (
        metrics["contact_actor_z_m"]
        <= metrics["max_z_m"]
        <= metrics["contact_actor_z_m"] + 3.5
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight maximum position is inconsistent"
        )
    if not (
        0.0
        <= metrics["post_contact_speed_mps"]
        <= 0.75 * metrics["approach_speed_mps"]
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight collision did not produce the required speed loss"
        )
    if not (
        1
        <= metrics["contact_step"]
        < metrics["physics_steps"]
        <= 24000
    ):
        raise BASE.BridgeSceneFailure(
            "streetlight physics-step lineage is outside its gate"
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
    BASE.REPORT_FORMAT = "ror-cityworld-led-streetlight-runtime-report-v1"
    BASE.RGB_ARTIFACT_NAME = "cityworld_led_streetlight_rgb.png"
    BASE.SUCCESS_PREFIX = "CityWorld LED streetlight runtime gate passed"
    BASE.DEVIATION_METRIC_KEY = "min_clearance_m"
    BASE.DEVIATION_LABEL = "clearance"
    BASE.RUNNER_PATHS = (
        "tools/run_cityworld_bridge_scene.py",
        "tools/run_cityworld_led_streetlight_scene.py",
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
            f"CityWorld LED streetlight scene gate failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
