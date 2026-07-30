#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Accept overlay v5 and its seamless Penguinville route with the packaged DAF.

CityWorld is third-party content and is intentionally absent from this
repository. This gate authenticates the original and locally derived overlay,
rebuilds the overlay byte-for-byte, stages both in an ephemeral RoR home, and
requires two physical DAF traversals plus four distinct UI-free RGB seam views.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import sys
import tempfile
from typing import Mapping, Sequence
import zipfile
import zlib


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


base = load_module("ror_cityworld_corridor_scene_base", BASE_PATH)


CITYWORLD_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)
CITYWORLD_NAME = "CityWorld.zip"
OVERLAY_NAME = "CityWorldNextLocalOverlay.zip"
OVERLAY_REPORT_MEMBER = "cityworld_next_local_overlay.report.json"
OVERLAY_PLACEMENT_MEMBER = "cityworld_next_local_overlay.tobj"
OVERLAY_REPORT_FORMAT = "ror-cityworld-local-overlay-v5"
NEOQ_LIGHT_CANDIDATE_MEMBER = (
    "cityworld_next_neoq_core_lights.candidates.json"
)
NEOQ_LIGHT_CANDIDATE_FORMAT = (
    "ror-cityworld-neoq-core-light-candidates-v1"
)
NEOQ_TREE_REPLACEMENT_MEMBER = (
    "cityworld_next_neoq_tree_replacements.v1.json"
)
NEOQ_TREE_REPLACEMENT_FORMAT = (
    "ror-cityworld-neoq-tree-replacements-v1"
)
NEOQ_TREE_NATIVE_PLAN = (
    "source/main/resources/tobj_fileformat/"
    "CityWorldNeoQTreePlan.inc"
)
NEOQ_TREE_FAMILY_MANIFEST = (
    "content-source/cityworld_next/vegetation/"
    "rorng_city_neoq_tree_family.v1.json"
)
NEOQ_TREE_SOURCE_TOBJ_SHA256 = (
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48"
)
NEOQ_TREE_COUNT = 18
NEOQ_TREE_VARIANTS = [
    "rorng_city_neoq_tree_columnar",
    "rorng_city_neoq_tree_round",
    "rorng_city_neoq_tree_windswept",
]
NEOQ_LIGHT_POLICY_ID = "ror-cityworld-local-light-budget-v1"
NEOQ_LIGHT_RADIUS_M = 400.0
NEOQ_LIGHT_RANGE_M = 24.0
NEOQ_LIGHT_TELEPOINT = "NeoQueretaro Spawn"
NEOQ_LIGHT_TELEPOINT_POSITION_M = [2425.0, 0.300000001, 1013.0]
NEOQ_CANDIDATE_FAMILY_COUNTS = {
    "luminariaLQr": 42,
    "luminariaQr": 25,
    "luminariaYQr": 0,
}
NEOQ_MAP_FAMILY_COUNTS = {
    "luminariaLQr": 528,
    "luminariaQr": 239,
    "luminariaYQr": 12,
}
NEOQ_LIGHT_ADAPTERS = {
    "luminariaLQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_l_lightonly",
        "local_light_position_m": [-2.75, 0.0, 9.7],
    },
    "luminariaQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_dual_lightonly",
        "local_light_position_m": [0.0, 0.0, 9.7],
    },
    "luminariaYQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_triple_lightonly",
        "local_light_position_m": [0.0, 0.0, 9.7],
    },
}
NEOQ_SOURCE_POLE_DEFINITIONS = [
    {
        "available": True,
        "bytes": 77,
        "collision_geometry": True,
        "definition": "luminariaLQr.odef",
        "family": "luminariaLQr",
        "lod": False,
        "point_light_directives": 0,
        "sha256":
            "d14535992b54e9b49255a257808df79dea7baaaf986dcbb613e3976cc730bfb7",
        "spot_light_directives": 0,
    },
    {
        "available": True,
        "bytes": 74,
        "collision_geometry": True,
        "definition": "luminariaQr.odef",
        "family": "luminariaQr",
        "lod": False,
        "point_light_directives": 0,
        "sha256":
            "bf79b0aee0321a69a79fafed67a00b2d788e8caf0c5aba0bf487338281728f13",
        "spot_light_directives": 0,
    },
    {
        "available": True,
        "bytes": 77,
        "collision_geometry": True,
        "definition": "luminariaYQr.odef",
        "family": "luminariaYQr",
        "lod": False,
        "point_light_directives": 0,
        "sha256":
            "b6d01408ea3002c447d21f55911567e1b6c7d1dc24ca573a321322f559891958",
        "spot_light_directives": 0,
    },
]
NEOQ_ACTIVATION_CONTRACT = {
    "blockers": [
        "renderer-local-light-budget-policy-unavailable",
        "neoq-fixed-camera-runtime-visual-gate-unavailable",
    ],
    "contracts": {
        "zero_local_shadow": {
            "required_local_shadow_casters": 0,
            "runtime_marker_field": "local_shadow_casters=0",
            "satisfied": True,
            "satisfied_by":
                "TerrainObjectManager local-light creation policy",
        },
    },
    "enabled": False,
    "fail_closed": True,
    "runtime_adapter_definitions_emitted": 0,
    "runtime_candidate_placements_emitted": 0,
    "runtime_point_lights_emitted": 0,
    "status": "blocked",
}
OVERLAY_TERRAIN = "CityWorldNextLocalOverlay"
FIXTURE_PATH = (
    "tests/fixtures/cityworld_corridor_runtime/"
    "cityworld_corridor_runtime.as"
)
SCRIPT_NAME = "cityworld_corridor_runtime.as"
VEHICLE_ARCHIVE = "dafsemi.zip"
VEHICLE_ENTRY = "b6b0UID-semi.truck"
VEHICLE_ENTRY_SHA256 = (
    "88343bed2edaf0cbabadb307bd2e8251f26a18840a0fbc2ca111c74ccdaf7b6c"
)
REPORT_FORMAT = "ror-cityworld-corridor-runtime-report-v2"
RGB_CAPTURE_IDS = (
    "penguinville_low_forward_approach",
    "penguinville_oblique_forward_approach",
    "neoq_low_forward_approach",
    "neoq_oblique_forward_approach",
)
MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_CANDIDATE_BYTES = 1024 * 1024
MAX_TREE_REPLACEMENT_BYTES = 1024 * 1024
MAX_ARCHIVE_MEMBERS = 64
MAX_OVERLAY_MEMBER_BYTES = 64 * 1024 * 1024
MAX_OVERLAY_TOTAL_BYTES = 128 * 1024 * 1024
EXPECTED_WAYPOINTS = 55
EXPECTED_CORRIDOR_LIGHTS = 15
EXPECTED_NEOQ_BRIDGE_LIGHTS = 33
EXPECTED_LIGHTS = EXPECTED_CORRIDOR_LIGHTS + EXPECTED_NEOQ_BRIDGE_LIGHTS
EXPECTED_ROUTE_LENGTH_M = 1038.350024882
MAX_PHYSICS_STEPS = 480000
CITYWORLD_FALLBACK_LIGHTING_MARKER = base.fallback_lighting_marker(
    (0.93, 0.86, 0.76)
)
EXPECTED_UNPLACED_ASSETS = [
    "rorng_city_gateway_block_40m",
    "rorng_city_bridge_transition_12m",
    "rorng_city_bridge_curve_left_15deg_20m",
    "rorng_city_bridge_span_20m",
]
EXPECTED_VISUAL_PURPOSE = (
    "authenticated Penguinville curb-bearing T-junction replacement plus a "
    "crowned-to-flat Blender road transition inheriting the procedural road2 "
    "surface and marking atlas, open procedural collision endcaps, paired "
    "outboard bridge piers, and route-safe Blender lighting; "
    "a second raised bridge leaves NeoQueretaro from an authenticated overlap "
    "and merges flush at NeoQ2.0 without covering its median or live lanes, "
    "with continuous collision, paired outboard terrain-reaching side piers, "
    "and bounded LED fixtures; "
    "all 18 authenticated legacy NeoQueretaro trees are replaced in place by "
    "the rights-cleared three-variant family with per-instance "
    "visual/collision scale wrappers; deterministic NeoQueretaro pole-light "
    "candidates remain disabled pending the bounded renderer light budget and "
    "fixed-camera visual gate; bridge modules remain validated candidates for "
    "deck and abutment replacement"
)
EXPECTED_TREE_ASSET_IDS = [
    "rorng_city_neoq_tree_round",
    "rorng_city_neoq_tree_columnar",
    "rorng_city_neoq_tree_windswept",
]
REQUIRED_OVERLAY_TOOLS = frozenset(
    (
        NEOQ_TREE_NATIVE_PLAN,
        "tools/audit_cityworld_visuals.py",
        "tools/build_cityworld_local_overlay.py",
        "tools/cityworld_neoq_intercity_bridge.py",
        "tools/cityworld_penguin_neoq_corridor.py",
        "tools/compile_cityworld_asset.py",
        "tools/solve_cityworld_bridge_corridor.py",
        "tools/validate_cityworld_asset.py",
        "tools/validate_cityworld_tree_family.py",
    )
)

SCRIPT_MARKERS = (
    "[RoR|CW2|CorridorRuntime] START route_m=1038.350024882",
    "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901",
    "[RoR|CW2|CorridorRuntime] CAPTURE "
    "id=penguinville_low_forward_approach",
    "[RoR|CW2|CorridorRuntime] CAPTURE "
    "id=penguinville_oblique_forward_approach",
    "[RoR|CW2|CorridorRuntime] FORWARD_SOURCE_SEAM "
    "direction=penguinville_to_neoq target_station=0",
    "[RoR|CW2|CorridorRuntime] FORWARD_MIDPOINT "
    "direction=penguinville_to_neoq",
    "[RoR|CW2|CorridorRuntime] CAPTURE id=neoq_low_forward_approach",
    "[RoR|CW2|CorridorRuntime] CAPTURE id=neoq_oblique_forward_approach",
    "[RoR|CW2|CorridorRuntime] FORWARD_DESTINATION_SEAM "
    "direction=penguinville_to_neoq target_station=1038.350024882",
    "[RoR|CW2|CorridorRuntime] REVERSE_ARMED actor=2026072902 "
    "direction=neoq_to_penguinville",
    "[RoR|CW2|CorridorRuntime] REVERSE_DESTINATION_SEAM "
    "direction=neoq_to_penguinville target_station=1038.350024882",
    "[RoR|CW2|CorridorRuntime] REVERSE_MIDPOINT "
    "direction=neoq_to_penguinville",
    "[RoR|CW2|CorridorRuntime] REVERSE_SOURCE_SEAM "
    "direction=neoq_to_penguinville target_station=0",
    "[RoR|CW2|CorridorRuntime] PASS seams=4 screenshots=4 traversals=2 "
    "route_m=1038.350024882",
)
ENGINE_MARKERS = (
    CITYWORLD_FALLBACK_LIGHTING_MARKER,
    "[RoR|CityWorld|NeoQ20Grounding] Applied placements=35 renames=3 "
    "telepoints=1 tree_replacements=18 road_replacements=1 "
    "transactionally before object instantiation "
    "(tobj_sha256=" + NEOQ_TREE_SOURCE_TOBJ_SHA256 + ")",
    "[RoR|ProceduralRoad|SidePiers] requested=46 built=46 skipped=0",
    "===== TERRAIN LOADING DONE CityWorldNextLocalOverlay.terrn2",
    "===== LOADING VEHICLE: b6b0UID-semi.truck",
)
FATAL_MARKERS = (
    "[RoR|CW2|CorridorRuntime] FAIL",
    "Could not load script 'cityworld_corridor_runtime.as",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "GL_INVALID_",
    "[RoR|ProceduralRoad|SidePiers] skip reason=",
)
ARMED_PATTERN = re.compile(
    r"\[RoR\|CW2\|CorridorRuntime\] ARMED actor=2026072901 "
    r"direction=penguinville_to_neoq "
    r"heading=(?P<heading>-?[0-9.eE+]+) "
    r"station=(?P<station>-?[0-9.eE+]+) "
    r"cross_track=(?P<cross>-?[0-9.eE+]+) "
    r"height=(?P<height>-?[0-9.eE+]+)"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|CorridorRuntime\] PASS seams=4 screenshots=4 traversals=2 "
    r"route_m=1038\.350024882 "
    r"distance_m=(?P<distance>-?[0-9.eE+]+) "
    r"forward_distance_m=(?P<forward>-?[0-9.eE+]+) "
    r"reverse_distance_m=(?P<reverse>-?[0-9.eE+]+) "
    r"path_error_m=(?P<path>-?[0-9.eE+]+) "
    r"vertical_error_m=(?P<vertical>-?[0-9.eE+]+) "
    r"regression_m=(?P<regression>-?[0-9.eE+]+) "
    r"speed_mps=(?P<speed>-?[0-9.eE+]+) "
    r"physics_steps=(?P<steps>[0-9]+)"
)
DEPENDENCY_PATTERN = re.compile(
    r"\[RoR\|TerrainDependency\] Mounted "
    r"'(?P<path>[^'\r\n]+)' into "
    r"'\{bundle USER:/mods/CityWorldNextLocalOverlay\.zip\}'"
)
VECTOR_PATTERN = re.compile(
    r"vector3\(\s*"
    r"(?P<x>-?[0-9.]+)f,\s*"
    r"(?P<y>-?[0-9.]+)f,\s*"
    r"(?P<z>-?[0-9.]+)f\s*\)"
)
FLOAT_PATTERN = re.compile(r"(?<![A-Za-z0-9_])-?[0-9]+(?:\.[0-9]+)?f")
TREE_PLAN_FLOAT = r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?"
TREE_PLAN_PATTERN = re.compile(
    rf"^CITYWORLD_NEOQ_TREE_REPLACEMENT\("
    rf"([0-9]+)U, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf'"([a-z0-9_]+)", ({TREE_PLAN_FLOAT})f, '
    rf'"([a-z0-9_]+)", ({TREE_PLAN_FLOAT})f\)$'
)


class CorridorSceneFailure(RuntimeError):
    """Fail-closed acceptance failure for the private-content runtime gate."""


class DuplicateKeyError(ValueError):
    """An overlay report repeated a JSON object key."""


def reject_duplicate_keys(
    pairs: list[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise CorridorSceneFailure(f"required regular file is missing: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_staged_file(path: Path, expected_sha: object, label: str) -> None:
    if not isinstance(expected_sha, str) or sha256_file(path) != expected_sha:
        raise CorridorSceneFailure(
            f"staged {label} differs from validated input"
        )


def publish_artifact_directory(staging: Path, destination: Path) -> None:
    if destination.exists():
        raise CorridorSceneFailure(
            f"artifact directory appeared before publication: {destination}"
        )
    os.replace(staging, destination)


def finite_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CorridorSceneFailure(f"{label} is not numeric")
    result = float(value)
    if not math.isfinite(result):
        raise CorridorSceneFailure(f"{label} is not finite")
    return result


def exact_int(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CorridorSceneFailure(f"{label} is not an integer")
    return value


def exact_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise CorridorSceneFailure(f"{label} is not an object")
    return value


def exact_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise CorridorSceneFailure(f"{label} is not an array")
    return value


def require_exact_json(
    actual: object,
    expected: object,
    label: str,
) -> None:
    if type(actual) is not type(expected):
        raise CorridorSceneFailure(f"{label} has the wrong JSON type")
    if isinstance(expected, dict):
        if set(actual) != set(expected):
            raise CorridorSceneFailure(f"{label} fields drifted")
        for key, expected_value in expected.items():
            require_exact_json(
                actual[key],
                expected_value,
                f"{label}.{key}",
            )
        return
    if isinstance(expected, list):
        if len(actual) != len(expected):
            raise CorridorSceneFailure(f"{label} length drifted")
        for index, (actual_value, expected_value) in enumerate(
            zip(actual, expected)
        ):
            require_exact_json(
                actual_value,
                expected_value,
                f"{label}[{index}]",
            )
        return
    if actual != expected:
        raise CorridorSceneFailure(f"{label} value drifted")


def read_neoq_tree_native_plan(
    repository: Path,
) -> list[dict[str, object]]:
    repository = repository.resolve()
    unresolved_path = repository / NEOQ_TREE_NATIVE_PLAN
    if unresolved_path.is_symlink():
        raise CorridorSceneFailure("NeoQ tree native plan is a symlink")
    path = unresolved_path.resolve()
    try:
        path.relative_to(repository)
    except ValueError as error:
        raise CorridorSceneFailure("NeoQ tree plan escapes repository") from error
    if not path.is_file() or path.is_symlink():
        raise CorridorSceneFailure("NeoQ tree native plan is missing")
    if not 1 <= path.stat().st_size <= 64 * 1024:
        raise CorridorSceneFailure("NeoQ tree native plan size is invalid")
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        raise CorridorSceneFailure(
            f"cannot read NeoQ tree native plan: {error}"
        ) from error

    result: list[dict[str, object]] = []
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        match = TREE_PLAN_PATTERN.fullmatch(stripped)
        if match is None:
            raise CorridorSceneFailure("NeoQ tree native plan syntax drifted")
        groups = match.groups()
        ordinal = len(result)
        source_line = int(groups[0])
        object_definition = groups[9]
        if (
            source_line != 9 + ordinal
            or object_definition
            != f"rorng_city_neoq_tree_instance_{ordinal:02d}"
        ):
            raise CorridorSceneFailure("NeoQ tree native plan ordering drifted")
        result.append(
            {
                "object_definition": object_definition,
                "position_m": [float(value) for value in groups[1:4]],
                "rotation_degrees": [float(value) for value in groups[4:7]],
                "scale": float(groups[8]),
                "source_line": source_line,
                "variant": groups[7],
                "yaw_degrees": float(groups[10]),
            }
        )
    if (
        len(result) != NEOQ_TREE_COUNT
        or sorted({item["variant"] for item in result})
        != NEOQ_TREE_VARIANTS
    ):
        raise CorridorSceneFailure("NeoQ tree native plan inventory drifted")
    return result


def validate_neoq_tree_replacements(
    manifest: object,
    repository: Path,
    package_payloads: Mapping[str, bytes],
    package_records: Mapping[str, Mapping[str, object]],
) -> dict[str, object]:
    repository = repository.resolve()
    tree_manifest = exact_dict(manifest, "NeoQ tree replacement manifest")
    if set(tree_manifest) != {
        "activation",
        "family",
        "format",
        "replacements",
        "source",
        "summary",
    }:
        raise CorridorSceneFailure(
            "NeoQ tree replacement manifest fields drifted"
        )
    require_exact_json(
        tree_manifest.get("format"),
        NEOQ_TREE_REPLACEMENT_FORMAT,
        "NeoQ tree replacement format",
    )
    require_exact_json(
        tree_manifest.get("activation"),
        {
            "duplicate_placements_emitted": 0,
            "fail_closed": True,
            "mode": "native-authenticated-in-place-replacement-v1",
            "requires_exact_archive_dependency": True,
            "requires_exact_tobj_sha256": True,
            "runtime_resource_preflight": "all-18-scale-wrapper-odefs",
        },
        "NeoQ tree activation contract",
    )
    require_exact_json(
        tree_manifest.get("source"),
        {
            "legacy_object": "arbol1Qr",
            "placement_count": NEOQ_TREE_COUNT,
            "source_lines": [9, 26],
            "tobj": "CityWorld.tobj",
            "tobj_sha256": NEOQ_TREE_SOURCE_TOBJ_SHA256,
        },
        "NeoQ tree source contract",
    )
    require_exact_json(
        tree_manifest.get("summary"),
        {
            "collision_scale_matches_visual_scale": True,
            "positions_preserved": NEOQ_TREE_COUNT,
            "replacement_count": NEOQ_TREE_COUNT,
            "unique_scale_wrappers": NEOQ_TREE_COUNT,
            "variants": NEOQ_TREE_VARIANTS,
        },
        "NeoQ tree replacement summary",
    )

    native_plan = read_neoq_tree_native_plan(repository)
    unresolved_family_path = repository / NEOQ_TREE_FAMILY_MANIFEST
    if unresolved_family_path.is_symlink():
        raise CorridorSceneFailure("NeoQ tree family manifest is a symlink")
    family_path = unresolved_family_path.resolve()
    try:
        family_path.relative_to(repository)
    except ValueError as error:
        raise CorridorSceneFailure(
            "NeoQ tree family manifest escapes repository"
        ) from error
    if (
        not family_path.is_file()
        or family_path.is_symlink()
        or family_path.stat().st_size > 1024 * 1024
    ):
        raise CorridorSceneFailure("NeoQ tree family manifest is missing")
    family = exact_dict(tree_manifest.get("family"), "NeoQ tree family")
    require_exact_json(
        family,
        {
            "asset": {
                "author": "Oasiz AI and Rigs of Rods contributors",
                "id": "rorng_city_neoq_tree_family",
                "license": "GPL-3.0-or-later",
                "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
                "version": 1,
            },
            "family_manifest": {
                "path": NEOQ_TREE_FAMILY_MANIFEST,
                "sha256": sha256_file(family_path),
            },
            "native_plan": {
                "path": NEOQ_TREE_NATIVE_PLAN,
                "sha256": sha256_file(repository / NEOQ_TREE_NATIVE_PLAN),
            },
            "selector": {
                "algorithm": "sha256-little-endian-modulo-v1",
                "namespace": "cityworld:neoqueretaro:arbol1qr:v1",
            },
            "validation": {
                "format": "ror-cityworld-tree-family-validation-v1",
                "summary": {
                    "assets": 3,
                    "compiled_outputs": 18,
                    "errors": 0,
                    "placements": NEOQ_TREE_COUNT,
                    "silhouettes": 3,
                    "valid": True,
                },
            },
        },
        "NeoQ tree family contract",
    )

    replacements = exact_list(
        tree_manifest.get("replacements"),
        "NeoQ tree replacements",
    )
    if len(replacements) != NEOQ_TREE_COUNT:
        raise CorridorSceneFailure("NeoQ tree replacement count drifted")
    wrapper_names: set[str] = set()
    for ordinal, (value, expected) in enumerate(
        zip(replacements, native_plan)
    ):
        replacement = exact_dict(value, f"NeoQ tree replacement {ordinal}")
        if set(replacement) != {
            "legacy_object",
            "object_definition",
            "ordinal",
            "position_m",
            "position_preserved",
            "rotation_degrees",
            "scale",
            "source_line",
            "source_rotation_degrees",
            "variant",
            "wrapper",
        }:
            raise CorridorSceneFailure(
                f"NeoQ tree replacement {ordinal} fields drifted"
            )
        if (
            exact_int(replacement.get("ordinal"), "tree ordinal") != ordinal
            or exact_int(
                replacement.get("source_line"),
                "tree source line",
            )
            != expected["source_line"]
            or replacement.get("legacy_object") != "arbol1Qr"
            or replacement.get("object_definition")
            != expected["object_definition"]
            or replacement.get("variant") != expected["variant"]
            or replacement.get("position_preserved") is not True
        ):
            raise CorridorSceneFailure(
                f"NeoQ tree replacement {ordinal} identity drifted"
            )
        vectors = (
            (
                replacement.get("position_m"),
                expected["position_m"],
                "position",
            ),
            (
                replacement.get("source_rotation_degrees"),
                expected["rotation_degrees"],
                "source rotation",
            ),
            (
                replacement.get("rotation_degrees"),
                [0.0, expected["yaw_degrees"], 0.0],
                "replacement rotation",
            ),
        )
        for actual_value, expected_value, label in vectors:
            actual = exact_list(actual_value, f"tree {ordinal} {label}")
            if (
                len(actual) != 3
                or any(
                    abs(finite_number(item, label) - target) > 1.0e-9
                    for item, target in zip(actual, expected_value)
                )
            ):
                raise CorridorSceneFailure(
                    f"NeoQ tree replacement {ordinal} {label} drifted"
                )
        scale = finite_number(replacement.get("scale"), "tree scale")
        if abs(scale - expected["scale"]) > 1.0e-9:
            raise CorridorSceneFailure(
                f"NeoQ tree replacement {ordinal} scale drifted"
            )
        wrapper = exact_dict(
            replacement.get("wrapper"),
            f"tree {ordinal} wrapper",
        )
        wrapper_name = (
            f"rorng_city_neoq_tree_instance_{ordinal:02d}.odef"
        )
        record = package_records.get(wrapper_name)
        payload = package_payloads.get(wrapper_name)
        if (
            wrapper_name in wrapper_names
            or record is None
            or payload is None
            or record.get("role") != "terrain-object-scale-wrapper"
        ):
            raise CorridorSceneFailure(
                f"NeoQ tree replacement {ordinal} wrapper inventory drifted"
            )
        wrapper_names.add(wrapper_name)
        require_exact_json(
            wrapper,
            {
                "path": wrapper_name,
                "sha256": sha256_bytes(payload),
                "size": len(payload),
            },
            f"NeoQ tree replacement {ordinal} wrapper",
        )
        try:
            wrapper_lines = payload.decode("utf-8").splitlines()
        except UnicodeDecodeError as error:
            raise CorridorSceneFailure(
                f"NeoQ tree wrapper {ordinal} is not UTF-8"
            ) from error
        if (
            len(wrapper_lines) != 10
            or wrapper_lines[0] != expected["variant"] + "_lod0.mesh"
            or wrapper_lines[2:] != [
                "standard",
                "",
                "beginmesh",
                "mesh "
                + expected["variant"]
                + "_collision_fixture.mesh",
                "stdfriction concrete",
                "endmesh",
                "",
                "end",
            ]
        ):
            raise CorridorSceneFailure(
                f"NeoQ tree wrapper {ordinal} structure drifted"
            )
        try:
            wrapper_scale = [
                float(component.strip())
                for component in wrapper_lines[1].split(",")
            ]
        except ValueError as error:
            raise CorridorSceneFailure(
                f"NeoQ tree wrapper {ordinal} scale is invalid"
            ) from error
        if (
            len(wrapper_scale) != 3
            or any(
                abs(component - scale) > 1.0e-9
                for component in wrapper_scale
            )
        ):
            raise CorridorSceneFailure(
                f"NeoQ tree wrapper {ordinal} visual/collision scale drifted"
            )

    material_payload = package_payloads.get(
        "cityworld_next_local_overlay.material"
    )
    if material_payload is None:
        raise CorridorSceneFailure("overlay merged material is missing")
    try:
        material_text = material_payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CorridorSceneFailure("overlay merged material is not UTF-8") from error
    for variant in NEOQ_TREE_VARIANTS:
        required_resources = {
            variant + ".odef": "terrain-object",
            variant + "_collision_fixture.mesh": "collision-fixture",
            variant + "_lod0.mesh": "render-lod0",
            variant + "_lod1.mesh": "render-lod1",
            variant + "_lod2.mesh": "render-lod2",
        }
        for name, role in required_resources.items():
            if (
                name not in package_payloads
                or package_records.get(name, {}).get("role") != role
            ):
                raise CorridorSceneFailure(
                    f"NeoQ tree runtime resource is missing: {name}"
                )
        for suffix in ("bark", "foliage_dark", "foliage_light"):
            material_name = variant + "_" + suffix
            if re.search(
                r"(?m)^material[ \t]+"
                + re.escape(material_name)
                + r"(?:[ \t]*$|[ \t]+)",
                material_text,
            ) is None:
                raise CorridorSceneFailure(
                    f"NeoQ tree material is missing: {material_name}"
                )
    return tree_manifest


def validate_neoq_light_candidates(manifest: object) -> dict[str, object]:
    candidate_manifest = exact_dict(
        manifest,
        "NeoQueretaro light candidate manifest",
    )
    expected_keys = {
        "activation",
        "candidate_family_counts",
        "candidate_poles",
        "candidate_runtime_point_lights",
        "candidates",
        "format",
        "policy_contract",
        "scope",
        "visual_geometry",
    }
    if set(candidate_manifest) != expected_keys:
        raise CorridorSceneFailure(
            "NeoQueretaro light candidate manifest fields drifted"
        )
    require_exact_json(
        candidate_manifest.get("format"),
        NEOQ_LIGHT_CANDIDATE_FORMAT,
        "NeoQueretaro light candidate format",
    )
    require_exact_json(
        candidate_manifest.get("activation"),
        NEOQ_ACTIVATION_CONTRACT,
        "NeoQueretaro light activation",
    )
    require_exact_json(
        candidate_manifest.get("candidate_family_counts"),
        NEOQ_CANDIDATE_FAMILY_COUNTS,
        "NeoQueretaro candidate family counts",
    )
    require_exact_json(
        candidate_manifest.get("candidate_poles"),
        67,
        "NeoQueretaro candidate pole count",
    )
    require_exact_json(
        candidate_manifest.get("candidate_runtime_point_lights"),
        67,
        "NeoQueretaro candidate light count",
    )
    require_exact_json(
        candidate_manifest.get("policy_contract"),
        {
            "hard_max_range_m": NEOQ_LIGHT_RANGE_M,
            "maximum_candidate_lights": 67,
            "policy_id": NEOQ_LIGHT_POLICY_ID,
            "required_local_shadow_casters": 0,
            "sampling_strategy":
                "one-bounded-representative-light-per-existing-pole",
        },
        "NeoQueretaro light policy",
    )
    require_exact_json(
        candidate_manifest.get("scope"),
        {
            "map_family_counts": NEOQ_MAP_FAMILY_COUNTS,
            "radius_m": NEOQ_LIGHT_RADIUS_M,
            "source_telepoint": NEOQ_LIGHT_TELEPOINT,
            "source_telepoint_position_m":
                NEOQ_LIGHT_TELEPOINT_POSITION_M,
        },
        "NeoQueretaro light scope",
    )
    require_exact_json(
        candidate_manifest.get("visual_geometry"),
        {
            "duplicate_pole_geometry_emitted": False,
            "existing_cityworld_poles_reused": True,
            "future_adapter_mesh_header": "none",
        },
        "NeoQueretaro light visual geometry",
    )

    candidates = exact_list(
        candidate_manifest.get("candidates"),
        "NeoQueretaro light candidates",
    )
    if len(candidates) != 67:
        raise CorridorSceneFailure(
            "NeoQueretaro light candidate record count drifted"
        )
    family_counts = {
        family: 0 for family in NEOQ_CANDIDATE_FAMILY_COUNTS
    }
    previous_line = 0
    candidate_ids: set[str] = set()
    for index, value in enumerate(candidates):
        candidate = exact_dict(value, f"light candidate {index}")
        if set(candidate) != {"adapter", "candidate_id", "light", "source"}:
            raise CorridorSceneFailure(
                f"light candidate {index} fields drifted"
            )
        source = exact_dict(
            candidate.get("source"),
            f"light candidate {index} source",
        )
        if set(source) != {
            "distance_from_telepoint_m",
            "line",
            "object",
            "position_m",
            "rotation_degrees",
        }:
            raise CorridorSceneFailure(
                f"light candidate {index} source fields drifted"
            )
        line = exact_int(
            source.get("line"),
            f"light candidate {index} source line",
        )
        if line <= previous_line:
            raise CorridorSceneFailure(
                "NeoQueretaro light source lines are not increasing"
            )
        previous_line = line
        candidate_id = candidate.get("candidate_id")
        if (
            type(candidate_id) is not str
            or candidate_id != f"neoq-core-light-line-{line:06d}"
            or candidate_id in candidate_ids
        ):
            raise CorridorSceneFailure(
                f"light candidate {index} identifier drifted"
            )
        candidate_ids.add(candidate_id)
        family = source.get("object")
        if type(family) is not str or family not in family_counts:
            raise CorridorSceneFailure(
                f"light candidate {index} family drifted"
            )
        family_counts[family] += 1
        distance = finite_number(
            source.get("distance_from_telepoint_m"),
            f"light candidate {index} distance",
        )
        if distance < 0.0 or distance > NEOQ_LIGHT_RADIUS_M:
            raise CorridorSceneFailure(
                f"light candidate {index} lies outside the scope"
            )
        for vector_name in ("position_m", "rotation_degrees"):
            vector = exact_list(
                source.get(vector_name),
                f"light candidate {index} {vector_name}",
            )
            if len(vector) != 3:
                raise CorridorSceneFailure(
                    f"light candidate {index} {vector_name} is not 3D"
                )
            for component in vector:
                finite_number(
                    component,
                    f"light candidate {index} {vector_name} component",
                )
        position = exact_list(
            source.get("position_m"),
            f"light candidate {index} position",
        )
        calculated_distance = math.hypot(
            finite_number(position[0], "candidate position x")
            - NEOQ_LIGHT_TELEPOINT_POSITION_M[0],
            finite_number(position[2], "candidate position z")
            - NEOQ_LIGHT_TELEPOINT_POSITION_M[2],
        )
        if abs(round(calculated_distance, 9) - distance) > 1.0e-9:
            raise CorridorSceneFailure(
                f"light candidate {index} distance is inconsistent"
            )
        adapter = NEOQ_LIGHT_ADAPTERS[family]
        require_exact_json(
            candidate.get("adapter"),
            {
                "coordinate_system": "legacy-odef-local-z-up",
                "future_object_definition":
                    adapter["future_object_definition"],
                "light_only_mesh_header": "none",
                "local_light_position_m":
                    adapter["local_light_position_m"],
                "runtime_definition_emitted": False,
            },
            f"light candidate {index} adapter",
        )
        require_exact_json(
            candidate.get("light"),
            {
                "color_rgb": [1.0, 0.72, 0.3],
                "hard_max_range_m": NEOQ_LIGHT_RANGE_M,
                "representative_lights": 1,
                "shadow_casting_requested": False,
                "type": "point",
            },
            f"light candidate {index} light",
        )
    require_exact_json(
        family_counts,
        NEOQ_CANDIDATE_FAMILY_COUNTS,
        "derived NeoQueretaro candidate family counts",
    )
    return candidate_manifest


def validate_cityworld_archive(path: Path) -> dict[str, object]:
    digest = sha256_file(path)
    if digest != CITYWORLD_SHA256:
        raise CorridorSceneFailure(
            "CityWorld archive is not the authenticated compatibility source"
        )
    try:
        with zipfile.ZipFile(path, "r") as archive:
            names = archive.namelist()
            if (
                not names
                or len(names) != len(set(names))
                or archive.testzip() is not None
            ):
                raise CorridorSceneFailure("CityWorld ZIP integrity failed")
            for required in ("CityWorld.terrn2", "CityWorld.otc", "CityWorld.tobj"):
                if required not in names:
                    raise CorridorSceneFailure(
                        f"CityWorld archive missed {required}"
                    )
    except (OSError, zipfile.BadZipFile) as error:
        raise CorridorSceneFailure(f"cannot read CityWorld archive: {error}") from error
    return {
        "name": CITYWORLD_NAME,
        "sha256": digest,
        "size": path.stat().st_size,
    }


def validate_overlay_archive(
    path: Path,
    repository: Path,
) -> tuple[dict[str, object], dict[str, object]]:
    repository = repository.resolve()
    overlay_sha = sha256_file(path)
    candidate_record: dict[str, object] | None = None
    candidate_manifest: dict[str, object] | None = None
    tree_record: dict[str, object] | None = None
    tree_manifest: dict[str, object] | None = None
    package_payloads: dict[str, bytes] = {}
    package_records: dict[str, dict[str, object]] = {}
    try:
        with zipfile.ZipFile(path, "r") as archive:
            infos = archive.infolist()
            names = [info.filename for info in infos]
            if (
                not infos
                or len(infos) > MAX_ARCHIVE_MEMBERS
                or len(names) != len(set(names))
            ):
                raise CorridorSceneFailure("overlay ZIP integrity failed")
            total_size = 0
            if OVERLAY_REPORT_MEMBER not in names:
                raise CorridorSceneFailure("overlay report is missing")
            for info in infos:
                pure = PurePosixPath(info.filename)
                if (
                    info.is_dir()
                    or pure.name != info.filename
                    or pure.is_absolute()
                    or "\\" in info.filename
                    or any(part in ("", ".", "..") for part in pure.parts)
                ):
                    raise CorridorSceneFailure(
                        f"overlay has an unsafe member: {info.filename}"
                    )
                if not 1 <= info.file_size <= MAX_OVERLAY_MEMBER_BYTES:
                    raise CorridorSceneFailure(
                        f"overlay member size is invalid: {info.filename}"
                    )
                total_size += info.file_size
                if total_size > MAX_OVERLAY_TOTAL_BYTES:
                    raise CorridorSceneFailure("overlay expands beyond byte limit")
            if archive.testzip() is not None:
                raise CorridorSceneFailure("overlay ZIP CRC validation failed")
            report_info = archive.getinfo(OVERLAY_REPORT_MEMBER)
            if not 1 <= report_info.file_size <= MAX_REPORT_BYTES:
                raise CorridorSceneFailure("overlay report size is invalid")
            report_payload = archive.read(report_info)
            try:
                report = json.loads(
                    report_payload.decode("utf-8"),
                    object_pairs_hook=reject_duplicate_keys,
                )
            except (
                DuplicateKeyError,
                RecursionError,
                UnicodeDecodeError,
                json.JSONDecodeError,
            ) as error:
                raise CorridorSceneFailure(
                    f"overlay report is invalid JSON: {error}"
                ) from error
            report = exact_dict(report, "overlay report")
            if report.get("format") != OVERLAY_REPORT_FORMAT:
                raise CorridorSceneFailure("overlay report format is unsupported")

            source = exact_dict(report.get("source"), "overlay source")
            source_archive = exact_dict(source.get("archive"), "source archive")
            if source_archive.get("sha256") != CITYWORLD_SHA256:
                raise CorridorSceneFailure("overlay source archive hash drifted")
            references = exact_dict(
                source.get("references"),
                "overlay source references",
            )
            if references.get("resource_bundle_dependency") != (
                "CityWorld.zip:CityWorld.terrn2:" + CITYWORLD_SHA256
            ):
                raise CorridorSceneFailure(
                    "overlay resource dependency identity drifted"
                )
            rights = exact_dict(report.get("rights"), "overlay rights")
            required_rights = {
                "local_only": True,
                "redistribution_allowed": False,
                "shipping_allowed": False,
                "source_archive_copied": False,
                "source_geometry_copied": False,
                "source_objects_copied": False,
                "source_placement_payload_copied": False,
                "source_placement_records_derived": True,
                "source_placements_copied": False,
                "derived_source_placement_record_count": 90,
                "source_textures_copied": False,
            }
            require_exact_json(
                rights,
                required_rights,
                "overlay rights contract",
            )

            package = exact_dict(report.get("package"), "overlay package")
            package_files = exact_list(package.get("files"), "package files")
            expected_names = {OVERLAY_REPORT_MEMBER}
            for index, value in enumerate(package_files):
                record = exact_dict(value, f"package file {index}")
                if set(record) != {"path", "role", "sha256", "size"}:
                    raise CorridorSceneFailure(
                        "package file record fields drifted"
                    )
                name = record.get("path")
                if not isinstance(name, str) or PurePosixPath(name).name != name:
                    raise CorridorSceneFailure("package file path is unsafe")
                if name in expected_names or name not in names:
                    raise CorridorSceneFailure("package inventory is inconsistent")
                payload = archive.read(name)
                package_payloads[name] = payload
                package_records[name] = record
                if (
                    record.get("sha256") != sha256_bytes(payload)
                    or exact_int(record.get("size"), "package file size")
                    != len(payload)
                ):
                    raise CorridorSceneFailure(
                        f"overlay payload differs from report: {name}"
                    )
                if name == NEOQ_LIGHT_CANDIDATE_MEMBER:
                    if (
                        candidate_record is not None
                        or record.get("role")
                        != "disabled-light-candidate-manifest"
                    ):
                        raise CorridorSceneFailure(
                            "NeoQueretaro candidate inventory drifted"
                        )
                    candidate_record = record
                if name == NEOQ_TREE_REPLACEMENT_MEMBER:
                    if (
                        tree_record is not None
                        or record.get("role")
                        != "authenticated-in-place-tree-replacement-plan"
                    ):
                        raise CorridorSceneFailure(
                            "NeoQ tree replacement inventory drifted"
                        )
                    tree_record = record
                expected_names.add(name)
            if (
                exact_int(package.get("entries"), "package entries")
                != len(infos)
                or expected_names != set(names)
            ):
                raise CorridorSceneFailure("overlay member inventory drifted")
            if candidate_record is None:
                raise CorridorSceneFailure(
                    "NeoQueretaro candidate manifest is missing"
                )
            candidate_info = archive.getinfo(NEOQ_LIGHT_CANDIDATE_MEMBER)
            if not 1 <= candidate_info.file_size <= MAX_CANDIDATE_BYTES:
                raise CorridorSceneFailure(
                    "NeoQueretaro candidate manifest size is invalid"
                )
            candidate_payload = archive.read(NEOQ_LIGHT_CANDIDATE_MEMBER)
            try:
                decoded_candidate_manifest = json.loads(
                    candidate_payload.decode("utf-8"),
                    object_pairs_hook=reject_duplicate_keys,
                )
            except (
                DuplicateKeyError,
                RecursionError,
                UnicodeDecodeError,
                json.JSONDecodeError,
            ) as error:
                raise CorridorSceneFailure(
                    "NeoQueretaro candidate manifest is invalid JSON: "
                    f"{error}"
                ) from error
            candidate_manifest = validate_neoq_light_candidates(
                decoded_candidate_manifest
            )
            if tree_record is None:
                raise CorridorSceneFailure(
                    "NeoQ tree replacement manifest is missing"
                )
            tree_info = archive.getinfo(NEOQ_TREE_REPLACEMENT_MEMBER)
            if not 1 <= tree_info.file_size <= MAX_TREE_REPLACEMENT_BYTES:
                raise CorridorSceneFailure(
                    "NeoQ tree replacement manifest size is invalid"
                )
            tree_payload = archive.read(NEOQ_TREE_REPLACEMENT_MEMBER)
            try:
                decoded_tree_manifest = json.loads(
                    tree_payload.decode("utf-8"),
                    object_pairs_hook=reject_duplicate_keys,
                )
            except (
                DuplicateKeyError,
                RecursionError,
                UnicodeDecodeError,
                json.JSONDecodeError,
            ) as error:
                raise CorridorSceneFailure(
                    f"NeoQ tree replacement manifest is invalid JSON: {error}"
                ) from error
            tree_manifest = validate_neoq_tree_replacements(
                decoded_tree_manifest,
                repository,
                package_payloads,
                package_records,
            )
            if references.get("overlay_placements") != (
                OVERLAY_PLACEMENT_MEMBER
            ):
                raise CorridorSceneFailure(
                    "overlay placement reference drifted"
                )
            if references.get("tree_replacement_manifest") != (
                NEOQ_TREE_REPLACEMENT_MEMBER
            ):
                raise CorridorSceneFailure(
                    "NeoQ tree replacement reference drifted"
                )
            placement_payload = archive.read(OVERLAY_PLACEMENT_MEMBER)
            try:
                placement_text = placement_payload.decode("utf-8")
            except UnicodeDecodeError as error:
                raise CorridorSceneFailure(
                    "overlay placement is not UTF-8"
                ) from error
            if "rorng_city_neoq_luminaria" in placement_text:
                raise CorridorSceneFailure(
                    "NeoQueretaro candidate lights were activated"
                )
            if (
                "arbol1Qr" in placement_text
                or "rorng_city_neoq_tree_instance_" in placement_text
            ):
                raise CorridorSceneFailure(
                    "NeoQ trees were duplicated in overlay placements"
                )
            if placement_text.count("collision_endcaps_enabled false") != 1:
                raise CorridorSceneFailure(
                    "procedural collision endcaps are not disabled exactly once"
                )
            if ", bridge\n" in placement_text:
                raise CorridorSceneFailure(
                    "legacy center-pillar bridge token is present"
                )
            if (
                placement_text.count(", bridge_side_pillars\n") != 46
                or placement_text.count(", bridge_no_pillars\n") != 2
                or placement_text.count(
                    "rorng_city_penguin_road_seam_12m - "
                    "cityworld_next_penguin_road_seam_12m"
                )
                != 1
            ):
                raise CorridorSceneFailure(
                    "side-pier road or Penguinville transition placement drifted"
                )
            for adapter in NEOQ_LIGHT_ADAPTERS.values():
                definition = adapter["future_object_definition"] + ".odef"
                if definition in names:
                    raise CorridorSceneFailure(
                        "NeoQueretaro candidate adapter was emitted"
                    )

            tools = exact_list(report.get("tools"), "overlay tools")
            tool_paths: set[str] = set()
            for index, value in enumerate(tools):
                record = exact_dict(value, f"overlay tool {index}")
                relative = record.get("path")
                if not isinstance(relative, str) or "\\" in relative:
                    raise CorridorSceneFailure("overlay tool path is invalid")
                pure = PurePosixPath(relative)
                if pure.is_absolute() or ".." in pure.parts:
                    raise CorridorSceneFailure("overlay tool path is unsafe")
                if relative in tool_paths:
                    raise CorridorSceneFailure("overlay tool path is duplicated")
                tool_paths.add(relative)
                candidate = (repository / relative).resolve()
                try:
                    candidate.relative_to(repository)
                except ValueError as error:
                    raise CorridorSceneFailure(
                        "overlay tool escapes repository"
                    ) from error
                if record.get("sha256") != sha256_file(candidate):
                    raise CorridorSceneFailure(
                        f"overlay was not built by current tool: {relative}"
                    )
            if not REQUIRED_OVERLAY_TOOLS.issubset(tool_paths):
                raise CorridorSceneFailure(
                    "overlay omitted a required build tool identity"
                )
    except (
        OSError,
        KeyError,
        RuntimeError,
        zipfile.BadZipFile,
        zlib.error,
    ) as error:
        raise CorridorSceneFailure(f"cannot read overlay archive: {error}") from error

    if (
        candidate_record is None
        or candidate_manifest is None
        or tree_record is None
        or tree_manifest is None
    ):
        raise CorridorSceneFailure(
            "NeoQueretaro visual validation did not complete"
        )
    lighting = exact_dict(report.get("city_lighting"), "overlay city lighting")
    if set(lighting) != {"neoq_core"}:
        raise CorridorSceneFailure("overlay city-lighting fields drifted")
    neoq_lighting = exact_dict(
        lighting.get("neoq_core"),
        "NeoQueretaro lighting report",
    )
    require_exact_json(
        neoq_lighting,
        {
            "activation": candidate_manifest["activation"],
            "candidate_family_counts":
                candidate_manifest["candidate_family_counts"],
            "candidate_manifest": candidate_record,
            "candidate_poles": 67,
            "candidate_runtime_point_lights": 67,
            "policy_contract": candidate_manifest["policy_contract"],
            "scope": candidate_manifest["scope"],
            "source_pole_definitions": NEOQ_SOURCE_POLE_DEFINITIONS,
            "visual_geometry": candidate_manifest["visual_geometry"],
        },
        "NeoQueretaro lighting report",
    )
    city_visuals = exact_dict(
        report.get("city_visuals"),
        "overlay city visuals",
    )
    if set(city_visuals) != {"neoq_trees"}:
        raise CorridorSceneFailure("overlay city-visual fields drifted")
    require_exact_json(
        city_visuals.get("neoq_trees"),
        {
            "activation": tree_manifest["activation"],
            "family": tree_manifest["family"],
            "replacement_manifest": tree_record,
            "source": tree_manifest["source"],
            "summary": tree_manifest["summary"],
        },
        "NeoQ tree visual report",
    )

    corridor = exact_dict(report.get("corridor"), "overlay corridor")
    if corridor.get("format") != "ror-cityworld-intercity-corridor-v4":
        raise CorridorSceneFailure("corridor format is unsupported")
    waypoints = exact_list(corridor.get("waypoints"), "corridor waypoints")
    if len(waypoints) != EXPECTED_WAYPOINTS:
        raise CorridorSceneFailure("corridor waypoint count drifted")
    previous_station = -1.0
    for index, value in enumerate(waypoints):
        waypoint = exact_dict(value, f"waypoint {index}")
        if exact_int(waypoint.get("index"), "waypoint index") != index:
            raise CorridorSceneFailure("corridor waypoint index drifted")
        station = finite_number(waypoint.get("station_m"), "waypoint station")
        position = exact_list(waypoint.get("position_m"), "waypoint position")
        if len(position) != 3:
            raise CorridorSceneFailure("waypoint position is not 3D")
        for component in position:
            finite_number(component, "waypoint component")
        if station <= previous_station:
            raise CorridorSceneFailure("corridor stations are not increasing")
        previous_station = station
    endpoint_contracts = (
        (waypoints[0], (522.0, 0.100001, 370.023095), "source"),
        (
            waypoints[-1],
            (1380.966797, 0.1, 936.098389),
            "destination",
        ),
    )
    for value, expected, label in endpoint_contracts:
        waypoint = exact_dict(value, f"{label} waypoint")
        position = exact_list(
            waypoint.get("position_m"),
            f"{label} waypoint position",
        )
        if any(
            abs(finite_number(actual, label) - target) > 1.0e-6
            for actual, target in zip(position, expected)
        ):
            raise CorridorSceneFailure(f"{label} road seam drifted")
    covered = finite_number(
        corridor.get("covered_centerline_length_m"),
        "covered centerline length",
    )
    if abs(covered - EXPECTED_ROUTE_LENGTH_M) > 1.0e-6:
        raise CorridorSceneFailure("corridor length drifted")
    if abs(previous_station - covered) > 1.0e-6:
        raise CorridorSceneFailure("last station differs from corridor length")
    connection = exact_dict(corridor.get("connection"), "corridor connection")
    for key in (
        "source_position_gap_m",
        "source_heading_error_degrees",
        "destination_position_gap_m",
        "destination_heading_error_degrees",
    ):
        if abs(finite_number(connection.get(key), key)) > 1.0e-9:
            raise CorridorSceneFailure(f"corridor connection is open: {key}")
    source = exact_dict(corridor.get("source"), "corridor source")
    collision_handoff = exact_dict(
        source.get("collision_handoff"),
        "corridor source collision handoff",
    )
    if (
        source.get("connection_position_m")
        != [522.0, 0.100001, 370.023095]
        or collision_handoff.get("authorities_per_station") != 1
        or collision_handoff.get("legacy_curb_collision_retained") is not False
        or collision_handoff.get("replacement_mode")
        != "native-authenticated-in-place-object-definition-swap"
        or collision_handoff.get("transition_asset_id")
        != "rorng_city_penguin_road_seam_12m"
    ):
        raise CorridorSceneFailure("Penguinville collision handoff drifted")

    seams = exact_dict(corridor.get("seams"), "corridor seams")
    if seams.get("format") != "ror-cityworld-penguin-neoq-seams-v1":
        raise CorridorSceneFailure("corridor seam format drifted")
    endcaps = exact_dict(
        seams.get("collision_endcaps"),
        "corridor collision endcaps",
    )
    require_exact_json(
        endcaps,
        {
            "destination_exposed_vertical_face_m": 0.0,
            "directive": "collision_endcaps_enabled false",
            "maximum_exposed_vertical_face_m": 1e-06,
            "source_exposed_vertical_face_m": 0.0,
            "start_and_finish_transverse_collision_faces_emitted": False,
        },
        "corridor collision endcaps",
    )
    seam_source = exact_dict(seams.get("source"), "source seam")
    transition = exact_dict(
        seam_source.get("transition"),
        "source seam transition",
    )
    if (
        transition.get("asset_id") != "rorng_city_penguin_road_seam_12m"
        or transition.get("placement_position_m")
        != [516.0, 0.100001, 370.023095]
        or transition.get("end_position_m")
        != [522.0, 0.100001, 370.023095]
        or finite_number(
            transition.get("transition_to_procedural_gap_m"),
            "transition procedural gap",
        )
        != 0.0
    ):
        raise CorridorSceneFailure("Penguinville transition seam drifted")
    seam_destination = exact_dict(
        seams.get("destination"),
        "destination seam",
    )
    for label, seam in (
        ("source", seam_source),
        ("destination", seam_destination),
    ):
        for key in (
            "heading_error_degrees",
            "position_gap_m",
            "road_width_gap_m",
            "surface_overlap_m",
        ):
            if finite_number(seam.get(key), f"{label} seam {key}") != 0.0:
                raise CorridorSceneFailure(f"{label} seam is not flush")
    seam_supports = exact_dict(
        seams.get("supports"),
        "seam support contract",
    )
    require_exact_json(
        seam_supports,
        {
            "legacy_ground_road_envelopes_intersected": 0,
            "road_type_token": "bridge_side_pillars",
            "support_layout": "paired-outboard",
            "swept_bridge_carriageway_intrusion_m": 0.0,
        },
        "seam support contract",
    )

    fixtures = exact_dict(corridor.get("fixtures"), "corridor fixtures")
    if (
        exact_int(fixtures.get("instance_count"), "fixture count")
        != EXPECTED_CORRIDOR_LIGHTS
        or exact_int(
            fixtures.get("runtime_point_lights_per_instance"),
            "fixture light count",
        )
        != 1
        or fixtures.get("collision_authority")
        != "native-procedural-road-v4-open-seams"
    ):
        raise CorridorSceneFailure("corridor lighting contract drifted")
    profile = exact_dict(corridor.get("profile"), "corridor profile")
    if (
        abs(
            finite_number(profile.get("source_width_m"), "source width")
            - 9.75017
        )
        > 1.0e-9
        or abs(
            finite_number(
                profile.get("destination_width_m"),
                "destination width",
            )
            - 10.0
        )
        > 1.0e-9
        or finite_number(
            profile.get("sampled_maximum_grade"),
            "corridor grade",
        )
        > 0.075 + 1.0e-9
    ):
        raise CorridorSceneFailure("corridor drive profile drifted")
    supports = exact_dict(corridor.get("supports"), "corridor supports")
    if (
        supports.get("enabled") is not True
        or exact_int(supports.get("requested_count"), "support count") != 46
        or exact_int(
            supports.get("expected_built_count"),
            "built support count",
        )
        != 46
        or exact_int(
            supports.get("expected_skipped_count"),
            "skipped support count",
        )
        != 0
        or supports.get("road_type_token") != "bridge_side_pillars"
        or supports.get("paired_outboard") is not True
        or exact_int(
            supports.get("centerline_pillars_requested"),
            "centerline pillar count",
        )
        != 0
        or supports.get("terrain_contact_resolved_at_runtime") is not True
    ):
        raise CorridorSceneFailure("corridor support contract drifted")

    corridors = exact_dict(report.get("corridors"), "overlay corridors")
    if set(corridors) != {"neoq_to_neoq20", "penguinville_to_neoq"}:
        raise CorridorSceneFailure("overlay corridor inventory drifted")
    if corridors.get("penguinville_to_neoq") != corridor:
        raise CorridorSceneFailure(
            "legacy corridor alias differs from corridor inventory"
        )
    neoq_bridge = exact_dict(
        corridors.get("neoq_to_neoq20"),
        "Neo intercity bridge",
    )
    if (
        neoq_bridge.get("format")
        != "ror-cityworld-neoq-intercity-bridge-v2"
    ):
        raise CorridorSceneFailure("Neo intercity bridge format drifted")
    bridge_waypoints = exact_list(
        neoq_bridge.get("waypoints"),
        "Neo intercity bridge waypoints",
    )
    if len(bridge_waypoints) != 81:
        raise CorridorSceneFailure("Neo intercity bridge waypoint count drifted")
    bridge_endpoints = (
        (
            bridge_waypoints[0],
            (3780.970703, 0.1, 3993.104004),
            "source overlap",
        ),
        (
            bridge_waypoints[1],
            (3790.970703, 0.1, 3993.104004),
            "source seam",
        ),
        (
            bridge_waypoints[-1],
            (6867.0, 0.2, 4018.0),
            "destination seam",
        ),
    )
    for value, expected, label in bridge_endpoints:
        waypoint = exact_dict(value, f"Neo bridge {label}")
        position = exact_list(
            waypoint.get("position_m"),
            f"Neo bridge {label} position",
        )
        if len(position) != 3 or any(
            abs(finite_number(actual, label) - target) > 1.0e-6
            for actual, target in zip(position, expected)
        ):
            raise CorridorSceneFailure(f"Neo bridge {label} drifted")
    bridge_length = finite_number(
        neoq_bridge.get("covered_centerline_length_m"),
        "Neo bridge length",
    )
    if abs(bridge_length - 3086.132100441) > 1.0e-6:
        raise CorridorSceneFailure("Neo intercity bridge length drifted")
    bridge_connection = exact_dict(
        neoq_bridge.get("connection"),
        "Neo bridge connection",
    )
    for key in (
        "source_position_gap_m",
        "source_heading_error_degrees",
        "destination_position_gap_m",
        "destination_heading_error_degrees",
        "destination_generated_overlap_m",
        "destination_grade_discontinuity",
        "destination_route_vs_decoded_surface_step_m",
        "destination_vertical_step_m",
        "destination_width_edge_error_m",
        "source_route_vs_decoded_surface_step_m",
    ):
        if abs(finite_number(bridge_connection.get(key), key)) > 1.0e-9:
            raise CorridorSceneFailure(f"Neo bridge connection is open: {key}")
    bridge_collision = exact_dict(
        neoq_bridge.get("collision"),
        "Neo bridge collision",
    )
    if (
        bridge_collision.get("authority")
        != "native-procedural-road-v2-side-piers"
        or bridge_collision.get("continuous") is not True
        or bridge_collision.get("single_surface") is not True
        or bridge_collision.get("endcap_collision_enabled") is not False
        or exact_int(
            bridge_collision.get("endcap_collision_triangle_count"),
            "Neo bridge endcap collision triangles",
        )
        != 0
        or finite_number(
            bridge_collision.get("endpoint_wheel_path_intrusion_m"),
            "Neo bridge endpoint wheel-path intrusion",
        )
        != 0.0
    ):
        raise CorridorSceneFailure("Neo bridge collision contract drifted")
    bridge_profile = exact_dict(
        neoq_bridge.get("profile"),
        "Neo bridge profile",
    )
    if (
        bridge_profile.get("curb_free_approaches") is not True
        or abs(
            finite_number(bridge_profile.get("width_m"), "Neo bridge width")
            - 24.0
        )
        > 1.0e-9
        or abs(
            finite_number(
                bridge_profile.get("destination_merge_width_m"),
                "Neo bridge destination merge width",
            )
            - 15.1
        )
        > 1.0e-9
        or finite_number(
            bridge_profile.get("approach_border_height_m"),
            "Neo bridge approach border",
        )
        != 0.0
        or finite_number(
            bridge_profile.get("sampled_maximum_grade"),
            "Neo bridge grade",
        )
        > 0.075 + 1.0e-9
    ):
        raise CorridorSceneFailure("Neo bridge drive profile drifted")
    bridge_supports = exact_dict(
        neoq_bridge.get("supports"),
        "Neo bridge supports",
    )
    if (
        bridge_supports.get("enabled") is not True
        or exact_int(
            bridge_supports.get("requested_count"),
            "Neo bridge support count",
        )
        != 74
        or exact_int(
            bridge_supports.get("column_pair_count"),
            "Neo bridge side-pier pair count",
        )
        != 74
        or exact_int(
            bridge_supports.get("aabb_count"),
            "Neo bridge support collision AABB count",
        )
        != 222
        or bridge_supports.get("aabb_vs_swept_roadway_prism")
        != "all-disjoint"
        or abs(
            finite_number(
                bridge_supports.get(
                    "minimum_lateral_clearance_from_deck_edge_m"
                ),
                "Neo bridge side-pier lateral clearance",
            )
            - 2.5
        )
        > 1.0e-9
        or finite_number(
            bridge_supports.get(
                "minimum_vertical_clearance_below_collision_slab_m"
            ),
            "Neo bridge hammerhead vertical clearance",
        )
        <= 0.0
        or finite_number(
            bridge_supports.get("maximum_station_spacing_m"),
            "Neo bridge support spacing",
        )
        > 40.0 + 1.0e-9
        or bridge_supports.get("terrain_contact_resolved_at_runtime")
        is not True
    ):
        raise CorridorSceneFailure("Neo bridge support contract drifted")
    bridge_fixtures = exact_dict(
        neoq_bridge.get("fixtures"),
        "Neo bridge fixtures",
    )
    if (
        exact_int(
            bridge_fixtures.get("instance_count"),
            "Neo bridge fixture count",
        )
        != EXPECTED_NEOQ_BRIDGE_LIGHTS
        or exact_int(
            bridge_fixtures.get("runtime_point_lights_per_instance"),
            "Neo bridge point lights",
        )
        != 1
        or bridge_fixtures.get("collision_authority")
        != "native-procedural-road-v2-side-piers"
    ):
        raise CorridorSceneFailure("Neo bridge lighting contract drifted")
    bridge_authentication = exact_dict(
        neoq_bridge.get("authentication"),
        "Neo bridge authentication",
    )
    if (
        bridge_authentication.get("format")
        != "ror-cityworld-neoq-bridge-authentication-v1"
        or len(
            exact_list(
                bridge_authentication.get("members"),
                "Neo bridge authenticated members",
            )
        )
        != 6
        or exact_int(
            exact_dict(
                bridge_authentication.get("source"),
                "Neo bridge authenticated source",
            ).get("line_number"),
            "Neo bridge source line",
        )
        != 366
        or exact_int(
            exact_dict(
                bridge_authentication.get("destination"),
                "Neo bridge authenticated destination",
            ).get("line_number"),
            "Neo bridge destination line",
        )
        != 1230
        or exact_dict(
            bridge_authentication.get("open_gap"),
            "Neo bridge open-gap audit",
        ).get("verified")
        is not True
    ):
        raise CorridorSceneFailure("Neo bridge authentication drifted")
    destination_contract = exact_dict(
        neoq_bridge.get("destination"),
        "Neo bridge destination contract",
    )
    if (
        destination_contract.get("existing_lanes_preserved") is not True
        or finite_number(
            destination_contract.get("generated_overlap_length_m"),
            "Neo bridge generated destination overlap",
        )
        != 0.0
        or destination_contract.get("open_carriageways_local_z_m")
        != [[-7.55, -0.7], [0.7, 7.55]]
        or destination_contract.get("median_local_z_m") != [-0.7, 0.7]
        or exact_dict(
            destination_contract.get("elevation_authority"),
            "Neo bridge destination elevation authority",
        ).get("runtime_origin_plus_local_surface_y_m")
        != 0.2
    ):
        raise CorridorSceneFailure("Neo bridge destination merge drifted")
    obstacle_contract = exact_dict(
        neoq_bridge.get("obstacle_avoidance"),
        "Neo bridge obstacle avoidance",
    )
    ground_clearance = exact_dict(
        obstacle_contract.get("ground_level_support_clearance"),
        "Neo bridge ground support clearance",
    )
    if (
        obstacle_contract.get("destination_existing_lane_collision_preserved")
        is not True
        or finite_number(
            obstacle_contract.get("destination_generated_overlap_m"),
            "Neo bridge obstacle destination overlap",
        )
        != 0.0
        or ground_clearance.get("clearance")
        != "all-column-aabbs-inside-empty-corridor"
        or exact_int(
            ground_clearance.get("column_aabb_count"),
            "Neo bridge ground-cleared column count",
        )
        != 148
        or ground_clearance.get("native_underside_visual_gate_required")
        is not True
    ):
        raise CorridorSceneFailure("Neo bridge ground clearance drifted")

    usage = exact_dict(
        report.get("visual_asset_usage"),
        "overlay visual asset usage",
    )
    require_exact_json(
        usage,
        {
            "corridor_placement_mode":
                "native-procedural-v5-two-corridor-open-seams-side-piers-with-"
                "blender-transition-v2",
            "disabled_light_candidate_manifest":
                NEOQ_LIGHT_CANDIDATE_MEMBER,
            "neoq_core_runtime_light_activation": "blocked-fail-closed",
            "packaged_asset_ids": [
                "rorng_city_penguin_road_seam_12m",
                "rorng_city_led_streetlight_bridge",
                *EXPECTED_TREE_ASSET_IDS,
            ],
            "placed_asset_ids": [
                "rorng_city_penguin_road_seam_12m",
                "rorng_city_led_streetlight_bridge",
                *EXPECTED_TREE_ASSET_IDS,
            ],
            "purpose": EXPECTED_VISUAL_PURPOSE,
            "unplaced_asset_ids": EXPECTED_UNPLACED_ASSETS,
            "validated_asset_ids":
                EXPECTED_UNPLACED_ASSETS
                + [
                    "rorng_city_led_streetlight_bridge",
                    "rorng_city_penguin_road_seam_12m",
                    *EXPECTED_TREE_ASSET_IDS,
                ],
        },
        "overlay visual asset usage",
    )
    return report, {
        "name": OVERLAY_NAME,
        "report_sha256": sha256_bytes(report_payload),
        "sha256": overlay_sha,
        "size": path.stat().st_size,
    }


def validate_script_route(
    script_path: Path,
    overlay_report: Mapping[str, object],
) -> dict[str, object]:
    try:
        text = script_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise CorridorSceneFailure(f"cannot read corridor script: {error}") from error
    path_match = re.search(
        r"array<vector3>\s+gPath\s*=\s*\{(?P<body>.*?)\};",
        text,
        flags=re.DOTALL,
    )
    station_match = re.search(
        r"array<float>\s+gStation\s*=\s*\{(?P<body>.*?)\};",
        text,
        flags=re.DOTALL,
    )
    if path_match is None or station_match is None:
        raise CorridorSceneFailure("corridor script path arrays are missing")
    vectors = [
        tuple(float(match.group(key)) for key in ("x", "y", "z"))
        for match in VECTOR_PATTERN.finditer(path_match.group("body"))
    ]
    stations = [
        float(value[:-1])
        for value in FLOAT_PATTERN.findall(station_match.group("body"))
    ]
    if len(vectors) != EXPECTED_WAYPOINTS + 2 or len(stations) != len(vectors):
        raise CorridorSceneFailure("corridor script sample count drifted")
    corridor = exact_dict(overlay_report.get("corridor"), "overlay corridor")
    waypoints = exact_list(corridor.get("waypoints"), "overlay waypoints")
    for index, value in enumerate(waypoints):
        waypoint = exact_dict(value, f"overlay waypoint {index}")
        expected_position = exact_list(
            waypoint.get("position_m"),
            f"overlay waypoint {index} position",
        )
        for actual, expected in zip(vectors[index + 1], expected_position):
            if abs(actual - finite_number(expected, "position")) > 1.0e-6:
                raise CorridorSceneFailure(
                    f"corridor script position drifted at waypoint {index}"
                )
        expected_station = finite_number(
            waypoint.get("station_m"),
            f"overlay waypoint {index} station",
        )
        if abs(stations[index + 1] - expected_station) > 1.0e-6:
            raise CorridorSceneFailure(
                f"corridor script station drifted at waypoint {index}"
            )
    expected_extensions = (
        ((502.0, 0.198014, 370.000002), -20.0),
        (
            (1400.966797, 0.1, 936.098389),
            EXPECTED_ROUTE_LENGTH_M + 20.0,
        ),
    )
    actual_extensions = (
        (vectors[0], stations[0]),
        (vectors[-1], stations[-1]),
    )
    extensions_match = True
    for (actual_position, actual_station), (
        expected_position,
        expected_station,
    ) in zip(actual_extensions, expected_extensions):
        if (
            abs(actual_station - expected_station) > 1.0e-6
            or any(
                abs(actual - expected) > 1.0e-6
                for actual, expected in zip(
                    actual_position,
                    expected_position,
                )
            )
        ):
            extensions_match = False
    if not extensions_match:
        raise CorridorSceneFailure("corridor script road extensions drifted")
    return {
        "path": FIXTURE_PATH,
        "samples": len(vectors),
        "sha256": sha256_file(script_path),
    }


def files_equal(first: Path, second: Path) -> bool:
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as first_stream, second.open("rb") as second_stream:
        while True:
            first_chunk = first_stream.read(1024 * 1024)
            second_chunk = second_stream.read(1024 * 1024)
            if first_chunk != second_chunk:
                return False
            if not first_chunk:
                return True


def verify_overlay_rebuild(
    cityworld_archive: Path,
    overlay_archive: Path,
    overlay_report: Mapping[str, object],
    repository: Path,
    timeout: int,
) -> dict[str, object]:
    corridor = exact_dict(overlay_report.get("corridor"), "overlay corridor")
    profile = exact_dict(corridor.get("profile"), "corridor profile")
    surface_offset = finite_number(
        profile.get("surface_offset_m"),
        "corridor surface offset",
    )
    builder = repository / "tools/build_cityworld_local_overlay.py"
    builder_sha = sha256_file(builder)
    with tempfile.TemporaryDirectory(
        prefix="ror-cityworld-overlay-rebuild-"
    ) as temporary:
        rebuilt = Path(temporary) / OVERLAY_NAME
        command = (
            sys.executable,
            str(builder),
            "--archive",
            str(cityworld_archive),
            "--repo-root",
            str(repository),
            "--output",
            str(rebuilt),
            "--surface-offset-m",
            format(surface_offset, ".17g"),
        )
        completed = base.run_command(
            command,
            timeout,
            cwd=repository,
        )
        if completed.returncode != 0:
            output = base.decode_output(completed.stdout)
            raise CorridorSceneFailure(
                "independent overlay rebuild failed: " + output[-4000:]
            )
        rebuilt_sha = sha256_file(rebuilt)
        supplied_sha = sha256_file(overlay_archive)
        if rebuilt_sha != supplied_sha or not files_equal(rebuilt, overlay_archive):
            raise CorridorSceneFailure(
                "supplied overlay differs from current deterministic rebuild"
            )
    return {
        "builder": "tools/build_cityworld_local_overlay.py",
        "builder_sha256": builder_sha,
        "byte_identical": True,
        "sha256": supplied_sha,
        "surface_offset_m": surface_offset,
    }


def verify_vehicle_archive(runtime_content: Path) -> dict[str, object]:
    archive_path = runtime_content / VEHICLE_ARCHIVE
    archive_sha = sha256_file(archive_path)
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or archive.testzip() is not None:
                raise CorridorSceneFailure("packaged DAF archive is invalid")
            payload = archive.read(VEHICLE_ENTRY)
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise CorridorSceneFailure(f"cannot read packaged DAF: {error}") from error
    if sha256_bytes(payload) != VEHICLE_ENTRY_SHA256:
        raise CorridorSceneFailure("packaged DAF truck entry drifted")
    return {
        "archive": str(archive_path),
        "archive_sha256": archive_sha,
        "entry": VEHICLE_ENTRY,
        "entry_sha256": VEHICLE_ENTRY_SHA256,
    }


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-map",
            OVERLAY_TERRAIN,
            "-runscript",
            SCRIPT_NAME,
        )
    )
    return tuple(command)


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> dict[str, float | int]:
    if returncode != 0:
        raise CorridorSceneFailure(
            f"RoR corridor scene exited with {returncode}"
        )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker not in combined:
            continue
        if marker == "[RoR|CW2|CorridorRuntime] FAIL":
            failure_line = next(
                (
                    line
                    for line in script_log.splitlines()
                    if marker in line
                ),
                marker,
            )
            context_lines = [
                line
                for line in script_log.splitlines()
                if "[RoR|CW2|CorridorRuntime]" in line
                and (
                    "ARMED" in line
                    or "CAPTURE" in line
                    or "SEAM" in line
                    or "SAMPLE" in line
                )
            ][-8:]
            raise CorridorSceneFailure(
                "runtime logged physical acceptance failure: "
                + failure_line
                + (
                    " | recent markers: " + " | ".join(context_lines)
                    if context_lines
                    else ""
                )
            )
        raise CorridorSceneFailure(f"runtime logged fatal marker: {marker}")
    previous = -1
    for marker in SCRIPT_MARKERS:
        offset = script_log.find(marker)
        if offset <= previous:
            raise CorridorSceneFailure(
                f"AngelScript marker is missing or out of order: {marker}"
            )
        previous = offset
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise CorridorSceneFailure(f"engine marker is missing: {marker}")
    for marker in (
        "base object not found in cityworld_next_local_overlay.material",
        "material rorng_penguin_seam_asphalt has no supportable Techniques "
        "and will be blank",
    ):
        if marker in engine_log:
            raise CorridorSceneFailure(
                "Penguinville transition material did not resolve: "
                + marker
            )
    side_pier_marker = (
        "[RoR|ProceduralRoad|SidePiers] requested=46 built=46 skipped=0"
    )
    if (
        engine_log.count(side_pier_marker) != 1
        or engine_log.count("[RoR|ProceduralRoad|SidePiers] requested=")
        != 1
    ):
        raise CorridorSceneFailure(
            "side-pier runtime summary is missing, duplicated, or nonzero-skip"
        )
    if engine_log.count("===== LOADING VEHICLE: b6b0UID-semi.truck") != 2:
        raise CorridorSceneFailure(
            "runtime did not load exactly two directional DAF actors"
        )
    if engine_log.count(CITYWORLD_FALLBACK_LIGHTING_MARKER) != 1:
        raise CorridorSceneFailure(
            "fallback lighting marker must appear exactly once"
        )
    dependency_matches = list(DEPENDENCY_PATTERN.finditer(engine_log))
    if len(dependency_matches) != 1:
        raise CorridorSceneFailure(
            "terrain dependency did not mount exactly once into the overlay"
        )
    dependency_path = dependency_matches[0].group("path").replace("\\", "/")
    if not dependency_path.endswith("/mods/" + CITYWORLD_NAME):
        raise CorridorSceneFailure(
            "terrain dependency mounted an unexpected CityWorld path"
        )
    light_marker = (
        "[RoR|TerrainObject|Lights] "
        "odef=rorng_city_led_streetlight_bridge.odef "
        "spotlights=0 point_lights=1 local_shadow_casters=0"
    )
    if engine_log.count(light_marker) != EXPECTED_LIGHTS:
        raise CorridorSceneFailure("runtime light instance count drifted")
    if "Error =" in script_log:
        raise CorridorSceneFailure("AngelScript compiler emitted an error")
    if script_log.count("[RoR|CW2|CorridorRuntime] CAPTURE id=") != 4:
        raise CorridorSceneFailure(
            "runtime did not request exactly four fixed seam captures"
        )

    armed_matches = list(ARMED_PATTERN.finditer(script_log))
    pass_matches = list(PASS_PATTERN.finditer(script_log))
    if len(armed_matches) != 1 or len(pass_matches) != 1:
        raise CorridorSceneFailure("runtime did not emit one ARMED and one PASS")
    armed = {key: float(value) for key, value in armed_matches[0].groupdict().items()}
    if not all(math.isfinite(value) for value in armed.values()):
        raise CorridorSceneFailure("ARMED metrics are non-finite")
    if not -20.0 <= armed["station"] <= -12.0:
        raise CorridorSceneFailure(
            "DAF was not armed inside the Penguinville source road"
        )
    if not -2.5 <= armed["cross"] <= 2.5:
        raise CorridorSceneFailure("DAF spawn cross-track offset is invalid")
    if not 0.5 <= armed["height"] <= 3.0:
        raise CorridorSceneFailure("DAF spawn height is invalid")

    record = pass_matches[0].groupdict()
    metrics: dict[str, float | int] = {
        "armed_station_m": armed["station"],
        "distance_m": float(record["distance"]),
        "forward_distance_m": float(record["forward"]),
        "path_error_m": float(record["path"]),
        "physics_steps": int(record["steps"]),
        "regression_m": float(record["regression"]),
        "reverse_distance_m": float(record["reverse"]),
        "speed_mps": float(record["speed"]),
        "vertical_error_m": float(record["vertical"]),
    }
    if not all(
        math.isfinite(value)
        for value in metrics.values()
        if isinstance(value, float)
    ):
        raise CorridorSceneFailure("PASS metrics are non-finite")
    if not 2080.0 <= metrics["distance_m"] <= 2220.0:
        raise CorridorSceneFailure(
            "bidirectional physical traversal distance is invalid"
        )
    if not 1020.0 <= metrics["forward_distance_m"] <= 1120.0:
        raise CorridorSceneFailure(
            "Penguinville-to-NeoQ physical traversal distance is invalid"
        )
    if not 1020.0 <= metrics["reverse_distance_m"] <= 1120.0:
        raise CorridorSceneFailure(
            "NeoQ-to-Penguinville physical traversal distance is invalid"
        )
    if (
        abs(
            metrics["distance_m"]
            - metrics["forward_distance_m"]
            - metrics["reverse_distance_m"]
        )
        > 0.05
    ):
        raise CorridorSceneFailure(
            "directional traversal distances do not sum to total distance"
        )
    if not 0.0 <= metrics["path_error_m"] <= 2.0:
        raise CorridorSceneFailure("corridor path error is excessive")
    if not 0.0 <= metrics["vertical_error_m"] <= 1.5:
        raise CorridorSceneFailure("corridor vertical error is excessive")
    if not 0.0 <= metrics["regression_m"] <= 1.0:
        raise CorridorSceneFailure("corridor progress regressed")
    if not 5.0 <= metrics["speed_mps"] <= 20.0:
        raise CorridorSceneFailure("corridor exit speed is invalid")
    if not 200000 <= metrics["physics_steps"] <= MAX_PHYSICS_STEPS:
        raise CorridorSceneFailure("corridor physics-step count is invalid")
    return metrics


def validate_rgb_screenshots(
    directory: Path,
) -> tuple[list[Path], dict[str, dict[str, object]]]:
    try:
        entries = sorted(directory.iterdir(), key=lambda path: path.name)
    except OSError as error:
        raise CorridorSceneFailure(
            f"cannot inspect screenshot directory: {error}"
        ) from error
    if (
        len(entries) != len(RGB_CAPTURE_IDS)
        or any(
            path.suffix.lower() != ".png"
            or not path.is_file()
            or path.is_symlink()
            for path in entries
        )
    ):
        raise CorridorSceneFailure(
            "runtime must emit exactly four regular PNG screenshots"
        )
    records: dict[str, dict[str, object]] = {}
    hashes: set[str] = set()
    for capture_id, path in zip(RGB_CAPTURE_IDS, entries):
        try:
            record = base.validate_rgb_png(path)
        except base.BridgeSceneFailure as error:
            raise CorridorSceneFailure(
                f"{capture_id} RGB validation failed: {error}"
            ) from error
        digest = record.get("sha256")
        if not isinstance(digest, str) or digest in hashes:
            raise CorridorSceneFailure(
                "the four fixed seam RGB screenshots are not distinct"
            )
        hashes.add(digest)
        records[capture_id] = {
            **record,
            "source_filename": path.name,
        }
    return entries, records


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
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    base.renderer_contract(sys.platform)
    repository = args.repository.resolve()
    if repository != REPOSITORY_ROOT.resolve():
        raise CorridorSceneFailure(
            "--repository must be the checkout containing this runner"
        )
    executable = args.executable.resolve()
    cityworld_archive = args.cityworld_archive.resolve()
    overlay_archive = args.overlay_archive.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file():
        raise CorridorSceneFailure(f"executable is missing: {executable}")
    if artifact_dir.exists():
        raise CorridorSceneFailure(
            f"artifact directory already exists: {artifact_dir}"
        )
    cityworld_record = validate_cityworld_archive(cityworld_archive)
    overlay_report, overlay_record = validate_overlay_archive(
        overlay_archive,
        repository,
    )
    script_path = repository / FIXTURE_PATH
    script_record = validate_script_route(script_path, overlay_report)
    overlay_rebuild = verify_overlay_rebuild(
        cityworld_archive,
        overlay_archive,
        overlay_report,
        repository,
        args.timeout,
    )
    runtime_content = (
        base.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise CorridorSceneFailure("runtime content directory is missing")
    vehicle_record = verify_vehicle_archive(runtime_content)
    artifact_dir.parent.mkdir(parents=True, exist_ok=True)
    artifact_staging = Path(
        tempfile.mkdtemp(
            prefix=f".{artifact_dir.name}.partial-",
            dir=artifact_dir.parent,
        )
    )

    with tempfile.TemporaryDirectory(
        prefix="ror-cityworld-corridor-"
    ) as temporary:
        isolated_home = Path(temporary)
        layout = base.runtime_layout(isolated_home, sys.platform)
        for key in ("config", "logs", "mods", "screenshots", "user"):
            layout[key].mkdir(parents=True, exist_ok=True)
        scripts = layout["user"] / "scripts"
        scripts.mkdir()
        staged_script = scripts / SCRIPT_NAME
        staged_cityworld = layout["mods"] / CITYWORLD_NAME
        staged_overlay = layout["mods"] / OVERLAY_NAME
        staged_vehicle = layout["mods"] / VEHICLE_ARCHIVE
        shutil.copyfile(script_path, staged_script)
        shutil.copyfile(cityworld_archive, staged_cityworld)
        shutil.copyfile(overlay_archive, staged_overlay)
        shutil.copyfile(
            runtime_content / VEHICLE_ARCHIVE,
            staged_vehicle,
        )
        staged_inputs = (
            (
                staged_script,
                script_record["sha256"],
                "corridor script",
            ),
            (
                staged_cityworld,
                cityworld_record["sha256"],
                "CityWorld archive",
            ),
            (
                staged_overlay,
                overlay_record["sha256"],
                "overlay archive",
            ),
            (
                staged_vehicle,
                vehicle_record["archive_sha256"],
                "packaged DAF archive",
            ),
        )
        for staged_path, expected_sha, label in staged_inputs:
            verify_staged_file(staged_path, expected_sha, label)
        config_paths = base.write_runtime_config(
            layout["config"],
            target_platform=sys.platform,
        )

        environment = os.environ.copy()
        environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
        environment["ALSOFT_DRIVERS"] = "null"
        environment["ALSOFT_LOGLEVEL"] = "0"
        command = build_command(executable)
        completed = base.run_command(
            command,
            args.timeout,
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
        metrics = validate_runtime_logs(
            completed.returncode,
            stdout,
            engine_log,
            script_log,
        )
        screenshots, rgb_records = validate_rgb_screenshots(
            layout["screenshots"]
        )
        renderer = base.parse_renderer_identity(engine_log, sys.platform)

        diagnostics = artifact_staging / "diagnostics"
        rgb_directory = artifact_staging / "rgb"
        diagnostics.mkdir()
        rgb_directory.mkdir()
        (diagnostics / "runtime.stdout").write_text(stdout, encoding="utf-8")
        (diagnostics / "RoR.log").write_text(engine_log, encoding="utf-8")
        (diagnostics / "Angelscript.log").write_text(
            script_log,
            encoding="utf-8",
        )
        rgb_artifacts: dict[str, str] = {}
        for capture_id, screenshot in zip(
            RGB_CAPTURE_IDS,
            screenshots,
        ):
            destination = rgb_directory / f"{capture_id}.png"
            shutil.copy2(screenshot, destination)
            rgb_artifacts[capture_id] = f"rgb/{destination.name}"
        config_records: dict[str, dict[str, object]] = {}
        for path in config_paths:
            destination = diagnostics / path.name
            shutil.copy2(path, destination)
            config_records[path.name] = {
                "artifact": f"diagnostics/{path.name}",
                "sha256": sha256_file(destination),
                "size": destination.stat().st_size,
            }

    report: dict[str, object] = {
        "acceptance": {
            "bidirectional_wheel_path_traversal_verified": True,
            "city_road_surface_connection_verified": True,
            "collision_endcaps_open_verified": True,
            "fixed_seam_rgb_views_verified": True,
            "status": "accepted-v4-seamless-corridor",
            "swept_wheel_path_clearance_verified": True,
            "paired_outboard_support_runtime_counts_verified": True,
        },
        "archives": {
            "cityworld": cityworld_record,
            "overlay": overlay_record,
        },
        "artifacts": {
            "engine_log": "diagnostics/RoR.log",
            "rgb": rgb_artifacts,
            "script_log": "diagnostics/Angelscript.log",
            "stdout": "diagnostics/runtime.stdout",
        },
        "command": list(command),
        "configs": config_records,
        "executable": {
            "path": str(executable),
            "sha256": sha256_file(executable),
        },
        "fixture": script_record,
        "format": REPORT_FORMAT,
        "machine": platform.machine(),
        "metrics": metrics,
        "overlay_rebuild": overlay_rebuild,
        "platform": platform.platform(),
        "renderer": renderer,
        "repository_commit": base.git_output(repository, ("rev-parse", "HEAD")),
        "rgb": rgb_records,
        "runners": {
            "tools/run_cityworld_bridge_scene.py": {
                "sha256": sha256_file(BASE_PATH),
            },
            "tools/run_cityworld_corridor_scene.py": {
                "sha256": sha256_file(Path(__file__).resolve()),
            },
        },
        "vehicle": vehicle_record,
    }
    report_name = "cityworld_corridor_runtime.report.json"
    report_path = artifact_staging / report_name
    report_path.write_text(
        base.canonical_json(report) + "\n",
        encoding="utf-8",
    )
    report_sha = sha256_file(report_path)
    publish_artifact_directory(artifact_staging, artifact_dir)
    published_report = artifact_dir / report_name
    print(
        "CityWorld seamless-corridor runtime acceptance passed: "
        f"{published_report} sha256={report_sha}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorridorSceneFailure, base.BridgeSceneFailure) as error:
        print(
            f"CityWorld seamless-corridor runtime acceptance failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
