#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Accept CityWorld v7 regional infill with thirteen UI-free native RGB views.

This standard-library-only gate performs no downloads and never uses the
developer's normal RoR profile. It authenticates CityWorld and the complete
derived overlay through the seamless-corridor gate, requires the embedded
regional-infill manifest to be the exact canonical project plan, rebuilds the
overlay byte-for-byte, proves all five active route-to-asset connector seams,
and runs one isolated fixed-camera native scene.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import platform
import re
import shutil
import sys
import tempfile
from typing import Mapping, Sequence
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
CORRIDOR_RUNNER_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_corridor_scene.py"
)
INFILL_PLAN_PATH = REPOSITORY_ROOT / "tools/cityworld_infill.py"


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


corridor = load_module(
    "ror_cityworld_infill_scene_corridor",
    CORRIDOR_RUNNER_PATH,
)
infill = load_module(
    "ror_cityworld_infill_scene_plan",
    INFILL_PLAN_PATH,
)
base = corridor.base


CITYWORLD_NAME = corridor.CITYWORLD_NAME
OVERLAY_NAME = corridor.OVERLAY_NAME
OVERLAY_TERRAIN = corridor.OVERLAY_TERRAIN
OVERLAY_REPORT_FORMAT = "ror-cityworld-local-overlay-v7"
INFILL_MANIFEST_MEMBER = "cityworld_next_infill_manifest.v2.json"
INFILL_MANIFEST_ROLE = "regional-infill-plan"
INFILL_SOURCE_AUTHENTICATION_FORMAT = (
    "ror-cityworld-regional-infill-source-authentication-v1"
)
FIXTURE_PATH = (
    "tests/fixtures/cityworld_infill_runtime/"
    "cityworld_infill_runtime.as"
)
SCRIPT_NAME = "cityworld_infill_runtime.as"
REPORT_FORMAT = "ror-cityworld-infill-runtime-report-v2"
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
CANONICAL_INFILL_MANIFEST_SHA256 = (
    "735fca0fd917763cdfe02d8d3cbd7871ebd7f4feb3ccf4fc73771de9c9c0c0af"
)
EXPECTED_PLACEMENTS = 46
EXPECTED_ROUTES = 7
EXPECTED_SITES = 8
EXPECTED_STATIONS = 2
EXPECTED_STATION_LIGHTS = 12
EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS = 91
CAPTURE_HOLD_FRAMES = 40
PASS_FRAME = 545
FIXED_PHYSICS_STEPS_PER_FRAME = 4
EXPECTED_PHYSICS_STEPS = PASS_FRAME * FIXED_PHYSICS_STEPS_PER_FRAME
SOURCE_RGB_CAPTURE_IDS = (
    "west_farm_belt",
    "sunset_courts",
    "west_highway_service",
    "coyote_arch",
    "arroyo_vista",
    "intercity_service",
    "intercity_farm",
    "sagebrush_arroyo",
)
SEAM_RGB_CAPTURE_IDS = (
    "west_station_frontage_seam",
    "sunset_shared_lane_seam",
    "intercity_station_forecourt_seam",
    "arroyo_internal_street_seam",
    "intercity_farm_driveway_seam",
)
RGB_CAPTURE_IDS = SOURCE_RGB_CAPTURE_IDS + SEAM_RGB_CAPTURE_IDS
SOURCE_CAMERA_CONTRACT = (
    (
        SOURCE_RGB_CAPTURE_IDS[0],
        "vector3(860.0f, 80.0f, 560.0f)",
        "vector3(860.0f, 0.1f, 250.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[1],
        "vector3(1055.0f, 95.0f, 1780.0f)",
        "vector3(1055.0f, 0.1f, 1360.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[2],
        "vector3(970.0f, 32.0f, 1520.0f)",
        "vector3(805.0f, 2.0f, 1395.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[3],
        "vector3(3925.0f, 90.0f, 3010.0f)",
        "vector3(3925.0f, 12.0f, 2575.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[4],
        "vector3(4240.0f, 115.0f, 3960.0f)",
        "vector3(4240.0f, 0.1f, 3550.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[5],
        "vector3(3910.0f, 28.0f, 3710.0f)",
        "vector3(3820.0f, 2.0f, 3635.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[6],
        "vector3(4070.0f, 85.0f, 4630.0f)",
        "vector3(4070.0f, 0.1f, 4325.0f)",
    ),
    (
        SOURCE_RGB_CAPTURE_IDS[7],
        "vector3(1255.0f, 75.0f, 750.0f)",
        "vector3(1255.0f, 0.1f, 450.0f)",
    ),
)
SEAM_CAMERA_CONTRACT = (
    (
        SEAM_RGB_CAPTURE_IDS[0],
        "vector3(805.0f, 2.25f, 1440.0f)",
        "vector3(805.0f, 0.12f, 1515.0f)",
    ),
    (
        SEAM_RGB_CAPTURE_IDS[1],
        "vector3(910.0f, 2.40f, 1488.0f)",
        "vector3(910.0f, 0.12f, 1535.0f)",
    ),
    (
        SEAM_RGB_CAPTURE_IDS[2],
        "vector3(3715.0f, 2.25f, 3580.0f)",
        "vector3(3785.0f, 0.12f, 3580.0f)",
    ),
    (
        SEAM_RGB_CAPTURE_IDS[3],
        "vector3(4140.0f, 2.40f, 3442.0f)",
        "vector3(4140.0f, 0.12f, 3395.0f)",
    ),
    (
        SEAM_RGB_CAPTURE_IDS[4],
        "vector3(3865.0f, 2.30f, 4285.0f)",
        "vector3(3935.0f, 0.10f, 4285.0f)",
    ),
)
CAMERA_CONTRACT = SOURCE_CAMERA_CONTRACT + SEAM_CAMERA_CONTRACT
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|InfillRuntime\] PASS cameras=13 seam_cameras=5 "
    r"active_connectors=(?P<active_connectors>[0-9]+) hold_frames=40 "
    r"frames=(?P<frames>[0-9]+) "
    r"physics_steps=(?P<steps>[0-9]+) "
    r"placements=46 routes=7 stations=2 station_lights=12"
)
SCREENSHOT_FILENAME_PATTERN = re.compile(
    r"^screenshot_(?P<timestamp>[0-9]{4}-[0-9]{2}-[0-9]{2}_"
    r"[0-9]{2}-[0-9]{2}-[0-9]{2})_(?P<index>[1-9][0-9]*)\.png$"
)
DEPENDENCY_PATTERN = corridor.DEPENDENCY_PATTERN
SIDE_PIER_SUMMARY_PATTERN = corridor.SIDE_PIER_SUMMARY_PATTERN
STATION_LIGHT_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_infill_service_station_90x65.odef "
    "spotlights=0 point_lights=6 local_shadow_casters=0"
)
BRIDGE_LIGHT_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_led_streetlight_bridge.odef "
    "spotlights=0 point_lights=1 local_shadow_casters=0"
)
FATAL_MARKERS = (
    "[RoR|CW2|InfillRuntime] FAIL",
    "Could not load script 'cityworld_infill_runtime.as",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "GL_INVALID_",
)
INFILL_MATERIAL_FAILURE_TOKENS = (
    "error",
    "not found",
    "cannot",
    "can't",
    "unable",
    "missing",
    "no supportable",
    "blank",
)
HIGHWAY_ODEF_SHA256 = (
    "9caa5752ce5b1cb11f26ad086b93e052f376486d6a8503eb04db0503fe73e602"
)
CONNECTOR_FORMAT = "ror-cityworld-regional-infill-connectors-v1"
MAXIMUM_CONNECTOR_SEAM_GAP_M = 0.001
CONNECTOR_POSITION_EPSILON_M = 1e-9
PINNED_ACTIVE_CONNECTOR_GEOMETRY = {
    "sunset-frontage-west-service-forecourt": {
        "collision_clearance_m": 5.4,
        "placement_id": "west-highway-service-station-01",
        "placement_position_m": [805.0, 0.1, 1460.0],
        "placement_rotation_degrees": [0.0, 90.0, 0.0],
        "render_overlap_depth_m": 0.0,
        "road_width_m": 10.0,
        "route_edge": "right",
        "route_id": "sunset-frontage-road",
        "route_segment_index": 3,
        "route_segment_positions_m": [
            [772.5, 0.1, 1510.0],
            [837.5, 0.1, 1510.0],
        ],
        "route_segment_yaws_degrees": [0.0, 0.0],
        "route_world_seam_xz_m": [
            [772.5, 1505.0],
            [837.5, 1505.0],
        ],
        "surface_id": "full-concrete-forecourt-west-edge",
        "target_seam_local_xz_m": [
            [-45.0, -32.5],
            [-45.0, 32.5],
        ],
        "target_seam_width_m": 65.0,
        "target_surface_local_polygon_xz_m": [
            [-45.0, -32.5],
            [45.0, -32.5],
            [45.0, 32.5],
            [-45.0, 32.5],
        ],
        "target_world_seam_xz_m": [
            [772.5, 1505.0],
            [837.5, 1505.0],
        ],
    },
    "sunset-frontage-sunset-courts-internal-street": {
        "collision_clearance_m": 13.0,
        "placement_id": "sunset-courts-suburb-block-07",
        "placement_position_m": [910.0, 0.1, 1570.0],
        "placement_rotation_degrees": [0.0, 0.0, 0.0],
        "render_overlap_depth_m": 2.0,
        "road_width_m": 10.0,
        "route_edge": "end",
        "route_id": "sunset-frontage-road",
        "route_segment_index": 7,
        "route_segment_positions_m": [
            [910.0, 0.1, 1510.0],
            [910.0, 0.1, 1528.0],
        ],
        "route_segment_yaws_degrees": [329.036243468, 270.0],
        "route_world_seam_xz_m": [
            [905.0, 1528.0],
            [915.0, 1528.0],
        ],
        "surface_id": "shared-internal-street-south-edge",
        "target_seam_local_xz_m": [
            [-5.0, -42.0],
            [5.0, -42.0],
        ],
        "target_seam_width_m": 10.0,
        "target_surface_local_polygon_xz_m": [
            [-5.0, -42.0],
            [5.0, -42.0],
            [5.0, 42.0],
            [-5.0, 42.0],
        ],
        "target_world_seam_xz_m": [
            [905.0, 1528.0],
            [915.0, 1528.0],
        ],
    },
    "arroyo-vista-internal-street": {
        "collision_clearance_m": 13.793114224,
        "placement_id": "arroyo-vista-suburb-block-02",
        "placement_position_m": [4140.0, 0.1, 3360.0],
        "placement_rotation_degrees": [0.0, 0.0, 0.0],
        "render_overlap_depth_m": 2.0,
        "road_width_m": 10.0,
        "route_edge": "end",
        "route_id": "arroyo-vista-boulevard",
        "route_segment_index": 6,
        "route_segment_positions_m": [
            [4140.0, 0.1, 3425.0],
            [4140.0, 0.1, 3402.0],
        ],
        "route_segment_yaws_degrees": [90.0, 90.0],
        "route_world_seam_xz_m": [
            [4145.0, 3402.0],
            [4135.0, 3402.0],
        ],
        "surface_id": "shared-internal-street-north-edge",
        "target_seam_local_xz_m": [
            [5.0, 42.0],
            [-5.0, 42.0],
        ],
        "target_seam_width_m": 10.0,
        "target_surface_local_polygon_xz_m": [
            [-5.0, -42.0],
            [5.0, -42.0],
            [5.0, 42.0],
            [-5.0, 42.0],
        ],
        "target_world_seam_xz_m": [
            [4145.0, 3402.0],
            [4135.0, 3402.0],
        ],
    },
    "intercity-service-forecourt": {
        "collision_clearance_m": 19.224008427,
        "placement_id": "intercity-service-station-01",
        "placement_position_m": [3785.0, 0.1, 3580.0],
        "placement_rotation_degrees": [0.0, 0.0, 0.0],
        "render_overlap_depth_m": 0.0,
        "road_width_m": 10.0,
        "route_edge": "end",
        "route_id": "intercity-service-road",
        "route_segment_index": 1,
        "route_segment_positions_m": [
            [3720.0, 0.1, 3580.0],
            [3740.0, 0.1, 3580.0],
        ],
        "route_segment_yaws_degrees": [0.0, 0.0],
        "route_world_seam_xz_m": [
            [3740.0, 3585.0],
            [3740.0, 3575.0],
        ],
        "surface_id": "full-concrete-forecourt-west-edge",
        "target_seam_local_xz_m": [
            [-45.0, -5.0],
            [-45.0, 5.0],
        ],
        "target_seam_width_m": 10.0,
        "target_surface_local_polygon_xz_m": [
            [-45.0, -32.5],
            [45.0, -32.5],
            [45.0, 32.5],
            [-45.0, 32.5],
        ],
        "target_world_seam_xz_m": [
            [3740.0, 3575.0],
            [3740.0, 3585.0],
        ],
    },
    "intercity-farm-lane": {
        "collision_clearance_m": 49.197256021,
        "placement_id": "intercity-farm-farmstead-01",
        "placement_position_m": [3965.0, 0.1, 4250.0],
        "placement_rotation_degrees": [0.0, 0.0, 0.0],
        "render_overlap_depth_m": 0.0,
        "road_width_m": 8.0,
        "route_edge": "end",
        "route_id": "intercity-farm-road",
        "route_segment_index": 4,
        "route_segment_positions_m": [
            [3880.0, 0.1, 4285.0],
            [3916.0, 0.1, 4285.0],
        ],
        "route_segment_yaws_degrees": [61.020292302, 0.0],
        "route_world_seam_xz_m": [
            [3916.0, 4289.0],
            [3916.0, 4281.0],
        ],
        "surface_id": "authored-asphalt-driveway-west-edge",
        "target_seam_local_xz_m": [
            [-49.0, 39.0],
            [-49.0, 31.0],
        ],
        "target_seam_width_m": 8.0,
        "target_surface_local_polygon_xz_m": [
            [-49.0, 31.0],
            [-36.0, 31.0],
            [-36.0, -17.5],
            [-28.0, -17.5],
            [-28.0, 43.0],
            [-36.0, 43.0],
            [-36.0, 39.0],
            [-49.0, 39.0],
        ],
        "target_world_seam_xz_m": [
            [3916.0, 4289.0],
            [3916.0, 4281.0],
        ],
    },
}


class InfillSceneFailure(RuntimeError):
    """Fail-closed native acceptance failure for CityWorld regional infill."""


def sha256_file(path: Path) -> str:
    try:
        return corridor.sha256_file(path)
    except corridor.CorridorSceneFailure as error:
        raise InfillSceneFailure(str(error)) from error


def exact_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise InfillSceneFailure(f"{label} is not an object")
    return value


def exact_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise InfillSceneFailure(f"{label} is not an array")
    return value


def ordered_markers(text: str, markers: Sequence[str], label: str) -> None:
    previous = -1
    for marker in markers:
        offset = text.find(marker)
        if offset <= previous:
            raise InfillSceneFailure(
                f"{label} marker is missing or out of order: {marker}"
            )
        previous = offset


def runtime_script_markers(
    active_connector_count: int,
) -> tuple[str, ...]:
    if (
        isinstance(active_connector_count, bool)
        or not isinstance(active_connector_count, int)
        or active_connector_count <= 0
    ):
        raise InfillSceneFailure(
            "active connector count is not a positive integer"
        )
    return (
        "[RoR|CW2|InfillRuntime] START cameras=13 seam_cameras=5 "
        "hold_frames=40 batch=4 "
        f"active_connectors={active_connector_count} "
        "placements=46 routes=7 stations=2 station_lights=12",
        *(
            f"[RoR|CW2|InfillRuntime] CAPTURE index={index} "
            f"id={capture_id} hold_frames=40"
            for index, capture_id in enumerate(RGB_CAPTURE_IDS)
        ),
    )


def _replace_script_once(
    text: str,
    old: str,
    new: str,
    label: str,
) -> str:
    if text.count(old) != 1:
        raise InfillSceneFailure(
            f"source fixture transform marker drifted: {label}"
        )
    return text.replace(old, new, 1)


def build_runtime_script_payload(
    source_text: str,
    *,
    active_connector_count: int,
) -> bytes:
    """Derive the extended native script without modifying its source fixture."""

    runtime_script_markers(active_connector_count)
    text = source_text
    replacements = (
        (
            "Captures all eight project-authored infill districts without UI.",
            "Captures eight districts and five close connector seams without UI.",
            "brief",
        ),
        (
            "const uint CAPTURE_COUNT = 8;",
            "const uint CAPTURE_COUNT = 13;",
            "capture count",
        ),
        (
            "const uint PASS_FRAME = 345;",
            "const uint PASS_FRAME = 545;",
            "pass frame",
        ),
        (
            """    if (captureIndex == 6)
        return "intercity_farm";
    return "sagebrush_arroyo";""",
            """    if (captureIndex == 6)
        return "intercity_farm";
    if (captureIndex == 7)
        return "sagebrush_arroyo";
    if (captureIndex == 8)
        return "west_station_frontage_seam";
    if (captureIndex == 9)
        return "sunset_shared_lane_seam";
    if (captureIndex == 10)
        return "intercity_station_forecourt_seam";
    if (captureIndex == 11)
        return "arroyo_internal_street_seam";
    return "intercity_farm_driveway_seam";""",
            "capture identifiers",
        ),
        (
            """    else
    {
        game.setCameraPosition(vector3(1255.0f, 75.0f, 750.0f));
        game.cameraLookAt(vector3(1255.0f, 0.1f, 450.0f));
    }""",
            """    else if (captureIndex == 7)
    {
        game.setCameraPosition(vector3(1255.0f, 75.0f, 750.0f));
        game.cameraLookAt(vector3(1255.0f, 0.1f, 450.0f));
    }
    else if (captureIndex == 8)
    {
        game.setCameraPosition(vector3(805.0f, 2.25f, 1440.0f));
        game.cameraLookAt(vector3(805.0f, 0.12f, 1515.0f));
    }
    else if (captureIndex == 9)
    {
        game.setCameraPosition(vector3(910.0f, 2.40f, 1488.0f));
        game.cameraLookAt(vector3(910.0f, 0.12f, 1535.0f));
    }
    else if (captureIndex == 10)
    {
        game.setCameraPosition(vector3(3715.0f, 2.25f, 3580.0f));
        game.cameraLookAt(vector3(3785.0f, 0.12f, 3580.0f));
    }
    else if (captureIndex == 11)
    {
        game.setCameraPosition(vector3(4140.0f, 2.40f, 3442.0f));
        game.cameraLookAt(vector3(4140.0f, 0.12f, 3395.0f));
    }
    else
    {
        game.setCameraPosition(vector3(3865.0f, 2.30f, 4285.0f));
        game.cameraLookAt(vector3(3935.0f, 0.10f, 4285.0f));
    }""",
            "close seam cameras",
        ),
        (
            """        "[RoR|CW2|InfillRuntime] START cameras=8 hold_frames=40 "
        "batch=4 placements=46 routes=7 stations=2 station_lights=12");""",
            """        "[RoR|CW2|InfillRuntime] START cameras=13 seam_cameras=5 "
        "hold_frames=40 batch=4 active_connectors="""
            + str(active_connector_count)
            + """ placements=46 routes=7 stations=2 station_lights=12");""",
            "start marker",
        ),
        (
            """        "[RoR|CW2|InfillRuntime] PASS cameras=8 hold_frames=40 frames=" +
        gReadyFrames + " physics_steps=" + steps +
        " placements=46 routes=7 stations=2 station_lights=12");""",
            """        "[RoR|CW2|InfillRuntime] PASS cameras=13 seam_cameras=5 "
        "active_connectors="""
            + str(active_connector_count)
            + """ hold_frames=40 frames=" + gReadyFrames +
        " physics_steps=" + steps +
        " placements=46 routes=7 stations=2 station_lights=12");""",
            "pass marker",
        ),
    )
    for old, new, label in replacements:
        text = _replace_script_once(text, old, new, label)
    return text.encode("utf-8")


def validate_fixture(
    path: Path,
    *,
    active_connector_count: int,
) -> tuple[dict[str, object], bytes]:
    if not path.is_file() or path.is_symlink():
        raise InfillSceneFailure(f"fixture is missing: {path}")
    if not 1 <= path.stat().st_size <= 1024 * 1024:
        raise InfillSceneFailure(f"fixture size is invalid: {path}")
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise InfillSceneFailure(
            f"cannot read infill fixture: {error}"
        ) from error
    required = (
        ("const uint CAPTURE_COUNT = 8;", 1),
        ("const uint CAPTURE_HOLD_FRAMES = 40;", 1),
        ("const uint PASS_FRAME = 345;", 1),
        (
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");',
            1,
        ),
        ('console.cVarSet("ui_hide_gui", "true");', 1),
        ("MSG_APP_SCREENSHOT_REQUESTED", 1),
        ("placements=46 routes=7 stations=2 station_lights=12", 2),
    )
    for marker, count in required:
        if text.count(marker) != count:
            raise InfillSceneFailure(
                f"fixture contract marker drifted: {marker}"
            )
    for capture_id, position, target in SOURCE_CAMERA_CONTRACT:
        for marker in (f'"{capture_id}"', position, target):
            if text.count(marker) != 1:
                raise InfillSceneFailure(
                    f"fixture camera contract drifted: {capture_id}"
                )
    payload = build_runtime_script_payload(
        text,
        active_connector_count=active_connector_count,
    )
    runtime_text = payload.decode("utf-8")
    runtime_required = (
        ("const uint CAPTURE_COUNT = 13;", 1),
        ("const uint CAPTURE_HOLD_FRAMES = 40;", 1),
        ("const uint PASS_FRAME = 545;", 1),
        (
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");',
            1,
        ),
        ('console.cVarSet("ui_hide_gui", "true");', 1),
        ("MSG_APP_SCREENSHOT_REQUESTED", 1),
        ("placements=46 routes=7 stations=2 station_lights=12", 2),
    )
    for marker, count in runtime_required:
        if runtime_text.count(marker) != count:
            raise InfillSceneFailure(
                f"derived runtime fixture marker drifted: {marker}"
            )
    for capture_id, position, target in CAMERA_CONTRACT:
        for marker in (f'"{capture_id}"', position, target):
            if runtime_text.count(marker) != 1:
                raise InfillSceneFailure(
                    f"derived runtime camera contract drifted: {capture_id}"
                )
    for marker in (
        '"[RoR|CW2|InfillRuntime] START cameras=13 seam_cameras=5 "',
        (
            f'"hold_frames=40 batch=4 active_connectors='
            f'{active_connector_count} placements=46 routes=7 stations=2 '
            'station_lights=12");'
        ),
        '"[RoR|CW2|InfillRuntime] PASS cameras=13 seam_cameras=5 "',
        (
            f'"active_connectors={active_connector_count} '
            'hold_frames=40 frames="'
        ),
    ):
        if marker not in runtime_text:
            raise InfillSceneFailure(
                f"derived runtime log contract drifted: {marker}"
            )
    return {
        "active_connectors": active_connector_count,
        "cameras": len(CAMERA_CONTRACT),
        "capture_hold_frames": CAPTURE_HOLD_FRAMES,
        "path": path.relative_to(REPOSITORY_ROOT).as_posix(),
        "runtime_sha256": corridor.sha256_bytes(payload),
        "runtime_size": len(payload),
        "seam_cameras": len(SEAM_CAMERA_CONTRACT),
        "sha256": sha256_file(path),
        "size": path.stat().st_size,
        "source_cameras": len(SOURCE_CAMERA_CONTRACT),
    }, payload


def finite_number(value: object, label: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        raise InfillSceneFailure(f"{label} is not a finite number")
    return float(value)


def finite_vector(
    value: object,
    length: int,
    label: str,
) -> tuple[float, ...]:
    values = exact_list(value, label)
    if len(values) != length:
        raise InfillSceneFailure(f"{label} length drifted")
    return tuple(
        finite_number(component, f"{label}[{index}]")
        for index, component in enumerate(values)
    )


def stable_number(value: float) -> float:
    rounded = round(value, 9)
    return 0.0 if rounded == 0.0 else rounded


def stable_xz(points: Sequence[Sequence[float]]) -> list[list[float]]:
    return [
        [stable_number(point[0]), stable_number(point[1])]
        for point in points
    ]


def normalized_xz(
    start: Sequence[float],
    end: Sequence[float],
    label: str,
) -> tuple[float, float]:
    dx = end[0] - start[0]
    dz = end[1] - start[1]
    length = math.hypot(dx, dz)
    if length <= CONNECTOR_POSITION_EPSILON_M:
        raise InfillSceneFailure(f"{label} is degenerate")
    return dx / length, dz / length


def point_on_polygon_boundary(
    point: Sequence[float],
    polygon: Sequence[Sequence[float]],
) -> bool:
    for start, end in zip(polygon, (*polygon[1:], polygon[0])):
        dx = end[0] - start[0]
        dz = end[1] - start[1]
        length_squared = dx * dx + dz * dz
        if length_squared <= CONNECTOR_POSITION_EPSILON_M:
            raise InfillSceneFailure(
                "connector target surface has a degenerate edge"
            )
        parameter = (
            (point[0] - start[0]) * dx
            + (point[1] - start[1]) * dz
        ) / length_squared
        if (
            -CONNECTOR_POSITION_EPSILON_M
            <= parameter
            <= 1.0 + CONNECTOR_POSITION_EPSILON_M
        ):
            projection = (
                start[0] + parameter * dx,
                start[1] + parameter * dz,
            )
            if (
                math.dist(point, projection)
                <= CONNECTOR_POSITION_EPSILON_M
            ):
                return True
    return False


def route_point_cross_section(
    point: Mapping[str, object],
    label: str,
) -> tuple[tuple[float, float], tuple[float, float]]:
    position = finite_vector(point.get("position_m"), 3, label + " position")
    width = finite_number(point.get("width_m"), label + " width")
    yaw = finite_number(point.get("yaw_degrees"), label + " yaw")
    if width <= 0.0:
        raise InfillSceneFailure(f"{label} width is not positive")
    radians = math.radians(yaw)
    normal_x = math.sin(radians)
    normal_z = math.cos(radians)
    half_width = width / 2.0
    return (
        (
            position[0] + normal_x * half_width,
            position[2] + normal_z * half_width,
        ),
        (
            position[0] - normal_x * half_width,
            position[2] - normal_z * half_width,
        ),
    )


def local_to_world_xz(
    point: Sequence[float],
    placement_position: Sequence[float],
    yaw_degrees: float,
) -> tuple[float, float]:
    radians = math.radians(yaw_degrees)
    cosine = math.cos(radians)
    sine = math.sin(radians)
    return (
        placement_position[0] + cosine * point[0] + sine * point[1],
        placement_position[2] - sine * point[0] + cosine * point[1],
    )


def maximum_seam_gap_m(
    route_seam: Sequence[Sequence[float]],
    target_seam: Sequence[Sequence[float]],
) -> float:
    forward = max(
        math.dist(route_seam[0], target_seam[0]),
        math.dist(route_seam[1], target_seam[1]),
    )
    reverse = max(
        math.dist(route_seam[0], target_seam[1]),
        math.dist(route_seam[1], target_seam[0]),
    )
    return stable_number(min(forward, reverse))


def _indexed_records(
    values: object,
    id_key: str,
    label: str,
) -> tuple[list[dict[str, object]], dict[str, dict[str, object]]]:
    records = [
        exact_dict(value, f"{label} {index}")
        for index, value in enumerate(exact_list(values, label))
    ]
    identifiers = [record.get(id_key) for record in records]
    if (
        any(not isinstance(identifier, str) or not identifier
            for identifier in identifiers)
        or len(set(identifiers)) != len(identifiers)
    ):
        raise InfillSceneFailure(f"{label} identifiers drifted")
    return records, dict(zip(identifiers, records))


def _validate_open_route_collision(
    route: Mapping[str, object],
    label: str,
) -> None:
    collision = exact_dict(route.get("collision"), label + " collision")
    if (
        collision.get("enabled") is not True
        or collision.get("endcaps_enabled") is not False
        or collision.get("endcap_directive")
        != "collision_endcaps_enabled false"
        or collision.get("single_surface_at_source_seam") is not True
    ):
        raise InfillSceneFailure(
            f"{label} does not preserve open collision endcaps"
        )


def _validate_pending_connector(
    contract: Mapping[str, object],
    evidence: Mapping[str, object],
    route: Mapping[str, object],
    placement: Mapping[str, object],
) -> dict[str, object]:
    connector_id = contract.get("connector_id")
    contract_keys = {
        "blocker",
        "connector_id",
        "expected_render_overlap_depth_m",
        "maximum_seam_gap_m",
        "placement_id",
        "route_edge",
        "route_id",
        "route_segment_index",
        "status",
        "surface_id",
        "target_seam_local_xz_m",
        "target_surface_local_polygon_xz_m",
        "target_surface_local_y_m",
    }
    evidence_keys = {
        "blocker",
        "collision_profile",
        "collision_proxy_overlap_count",
        "connector_id",
        "flush",
        "minimum_collision_proxy_clearance_m",
        "observed_render_clearance_m",
        "placement_id",
        "route_id",
        "seam_gap_m",
        "status",
        "surface_id",
    }
    if set(contract) != contract_keys or set(evidence) != evidence_keys:
        raise InfillSceneFailure(
            f"{connector_id} pending connector fields drifted"
        )
    blocker = contract.get("blocker")
    if (
        not isinstance(blocker, str)
        or not blocker
        or evidence.get("blocker") != blocker
        or contract.get("route_edge") != "pending"
        or contract.get("status") != "pending"
        or evidence.get("status") != "pending"
        or contract.get("expected_render_overlap_depth_m") != 0.0
        or contract.get("maximum_seam_gap_m")
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or contract.get("target_seam_local_xz_m") != []
        or contract.get("target_surface_local_polygon_xz_m") != []
        or contract.get("target_surface_local_y_m") != 0.0
        or evidence.get("flush") is not False
        or evidence.get("seam_gap_m") is not None
        or evidence.get("collision_proxy_overlap_count") != 0
        or evidence.get("collision_profile")
        != placement.get("collision_profile")
    ):
        raise InfillSceneFailure(
            f"{connector_id} pending connector is not fail-closed"
        )
    if (
        evidence.get("connector_id") != connector_id
        or evidence.get("route_id") != contract.get("route_id")
        or evidence.get("placement_id") != contract.get("placement_id")
        or evidence.get("surface_id") != contract.get("surface_id")
    ):
        raise InfillSceneFailure(
            f"{connector_id} pending evidence identity drifted"
        )
    segment_index = contract.get("route_segment_index")
    points = exact_list(route.get("points"), f"{connector_id} route points")
    if (
        isinstance(segment_index, bool)
        or not isinstance(segment_index, int)
        or segment_index < 0
        or segment_index >= len(points) - 1
    ):
        raise InfillSceneFailure(
            f"{connector_id} pending segment is unavailable"
        )
    _validate_open_route_collision(route, str(connector_id))
    if (
        finite_number(
            evidence.get("minimum_collision_proxy_clearance_m"),
            f"{connector_id} pending collision clearance",
        )
        <= 0.0
        or finite_number(
            evidence.get("observed_render_clearance_m"),
            f"{connector_id} pending render clearance",
        )
        <= 0.0
    ):
        raise InfillSceneFailure(
            f"{connector_id} pending connector is not clear"
        )
    return {
        "blocker": blocker,
        "connector_id": connector_id,
        "flush": False,
        "status": "pending",
    }


def _validate_active_connector(
    contract: Mapping[str, object],
    evidence: Mapping[str, object],
    route: Mapping[str, object],
    placement: Mapping[str, object],
) -> dict[str, object]:
    connector_id = contract.get("connector_id")
    contract_keys = {
        "connector_id",
        "expected_render_overlap_depth_m",
        "maximum_seam_gap_m",
        "placement_id",
        "route_edge",
        "route_id",
        "route_segment_index",
        "status",
        "surface_id",
        "target_seam_local_xz_m",
        "target_surface_local_polygon_xz_m",
        "target_surface_local_y_m",
    }
    evidence_keys = {
        "collision_profile",
        "collision_proxy_overlap_count",
        "connector_id",
        "expected_render_overlap_depth_m",
        "maximum_seam_gap_m",
        "minimum_collision_proxy_clearance_m",
        "observed_render_overlap_depth_m",
        "placement_id",
        "road_width_m",
        "route_edge",
        "route_id",
        "route_segment_index",
        "route_world_seam_xz_m",
        "seam_gap_m",
        "status",
        "surface_id",
        "target_seam_width_m",
        "target_world_seam_xz_m",
    }
    if set(contract) != contract_keys or set(evidence) != evidence_keys:
        raise InfillSceneFailure(
            f"{connector_id} active connector fields drifted"
        )
    identity_keys = (
        "connector_id",
        "placement_id",
        "route_edge",
        "route_id",
        "route_segment_index",
        "status",
        "surface_id",
    )
    if (
        any(evidence.get(key) != contract.get(key) for key in identity_keys)
        or contract.get("status") != "active"
        or contract.get("route_edge") not in {"left", "right", "end"}
        or contract.get("maximum_seam_gap_m")
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or evidence.get("maximum_seam_gap_m")
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or contract.get("target_surface_local_y_m") != 0.0
    ):
        raise InfillSceneFailure(
            f"{connector_id} active connector identity drifted"
        )
    _validate_open_route_collision(route, str(connector_id))
    if evidence.get("collision_profile") != placement.get(
        "collision_profile"
    ):
        raise InfillSceneFailure(
            f"{connector_id} collision profile drifted"
        )
    if evidence.get("collision_proxy_overlap_count") != 0:
        raise InfillSceneFailure(
            f"{connector_id} overlaps a collision proxy"
        )

    segment_index = contract.get("route_segment_index")
    points = exact_list(route.get("points"), f"{connector_id} route points")
    if (
        isinstance(segment_index, bool)
        or not isinstance(segment_index, int)
        or segment_index < 0
        or segment_index >= len(points) - 1
        or (
            contract.get("route_edge") == "end"
            and segment_index != len(points) - 2
        )
    ):
        raise InfillSceneFailure(
            f"{connector_id} selected route segment drifted"
        )
    start = exact_dict(points[segment_index], f"{connector_id} route start")
    end = exact_dict(points[segment_index + 1], f"{connector_id} route end")
    start_position = finite_vector(
        start.get("position_m"),
        3,
        f"{connector_id} route start position",
    )
    end_position = finite_vector(
        end.get("position_m"),
        3,
        f"{connector_id} route end position",
    )
    start_width = finite_number(
        start.get("width_m"),
        f"{connector_id} route start width",
    )
    end_width = finite_number(
        end.get("width_m"),
        f"{connector_id} route end width",
    )
    if (
        start_width <= 0.0
        or start_width != end_width
        or evidence.get("road_width_m") != start_width
        or any(
            point.get("road_type") != "flat"
            or point.get("border_height_m") != 0.0
            or point.get("border_width_m") != 0.0
            for point in (start, end)
        )
    ):
        raise InfillSceneFailure(
            f"{connector_id} road width or flat profile drifted"
        )

    placement_position = finite_vector(
        placement.get("position_m"),
        3,
        f"{connector_id} placement position",
    )
    placement_rotation = finite_vector(
        placement.get("rotation_degrees"),
        3,
        f"{connector_id} placement rotation",
    )
    if placement_rotation[0] != 0.0 or placement_rotation[2] != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} placement tilt is unsupported"
        )
    elevation = (
        placement_position[1]
        + finite_number(
            contract.get("target_surface_local_y_m"),
            f"{connector_id} target elevation",
        )
    )
    elevation_gap = stable_number(max(
        abs(start_position[1] - elevation),
        abs(end_position[1] - elevation),
    ))
    if elevation_gap != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} road and target elevations are not exact"
        )

    route_left_start, route_right_start = route_point_cross_section(
        start,
        f"{connector_id} route start",
    )
    route_left_end, route_right_end = route_point_cross_section(
        end,
        f"{connector_id} route end",
    )
    route_edge = contract.get("route_edge")
    if route_edge == "left":
        route_seam = (route_left_start, route_left_end)
    elif route_edge == "right":
        route_seam = (route_right_start, route_right_end)
    else:
        route_seam = (route_left_end, route_right_end)

    target_local = tuple(
        finite_vector(
            value,
            2,
            f"{connector_id} target seam point {index}",
        )
        for index, value in enumerate(exact_list(
            contract.get("target_seam_local_xz_m"),
            f"{connector_id} target seam",
        ))
    )
    target_polygon = tuple(
        finite_vector(
            value,
            2,
            f"{connector_id} target surface point {index}",
        )
        for index, value in enumerate(exact_list(
            contract.get("target_surface_local_polygon_xz_m"),
            f"{connector_id} target surface",
        ))
    )
    if (
        len(target_local) != 2
        or len(target_polygon) < 4
        or any(
            not point_on_polygon_boundary(point, target_polygon)
            for point in target_local
        )
    ):
        raise InfillSceneFailure(
            f"{connector_id} target seam left its declared surface edge"
        )
    target_seam = tuple(
        local_to_world_xz(
            point,
            placement_position,
            placement_rotation[1],
        )
        for point in target_local
    )
    computed_route_seam = stable_xz(route_seam)
    computed_target_seam = stable_xz(target_seam)
    if (
        evidence.get("route_world_seam_xz_m") != computed_route_seam
        or evidence.get("target_world_seam_xz_m")
        != computed_target_seam
    ):
        raise InfillSceneFailure(
            f"{connector_id} reported seam geometry was not recomputed"
        )
    seam_gap = maximum_seam_gap_m(route_seam, target_seam)
    if evidence.get("seam_gap_m") != seam_gap or seam_gap != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} seam is not exactly zero-gap"
        )
    route_seam_width = stable_number(math.dist(*route_seam))
    target_seam_width = stable_number(math.dist(*target_seam))
    if (
        evidence.get("target_seam_width_m") != target_seam_width
        or (
            route_edge == "end"
            and (
                route_seam_width != stable_number(start_width)
                or target_seam_width != stable_number(start_width)
            )
        )
        or (
            route_edge != "end"
            and route_seam_width != target_seam_width
        )
    ):
        raise InfillSceneFailure(
            f"{connector_id} connector width drifted"
        )

    horizontal_length = math.dist(
        (start_position[0], start_position[2]),
        (end_position[0], end_position[2]),
    )
    if horizontal_length <= CONNECTOR_POSITION_EPSILON_M:
        raise InfillSceneFailure(
            f"{connector_id} selected segment is degenerate"
        )
    grade = stable_number(
        abs(end_position[1] - start_position[1]) / horizontal_length
    )
    if grade != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} connector grade is not flat"
        )
    route_tangent = normalized_xz(
        (start_position[0], start_position[2]),
        (end_position[0], end_position[2]),
        f"{connector_id} route tangent",
    )
    endpoint_yaw = finite_number(
        end.get("yaw_degrees"),
        f"{connector_id} endpoint yaw",
    )
    yaw_radians = math.radians(endpoint_yaw)
    native_tangent = (
        math.cos(yaw_radians),
        -math.sin(yaw_radians),
    )
    native_dot = max(-1.0, min(
        1.0,
        route_tangent[0] * native_tangent[0]
        + route_tangent[1] * native_tangent[1],
    ))
    route_tangent_error = stable_number(math.degrees(math.acos(native_dot)))
    if route_tangent_error != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} route yaw and tangent drifted"
        )
    seam_tangent = normalized_xz(
        route_seam[0],
        route_seam[1],
        f"{connector_id} seam tangent",
    )
    seam_dot = abs(
        route_tangent[0] * seam_tangent[0]
        + route_tangent[1] * seam_tangent[1]
    )
    seam_angle = math.degrees(math.acos(max(-1.0, min(1.0, seam_dot))))
    seam_tangent_error = stable_number(
        abs(seam_angle - (90.0 if route_edge == "end" else 0.0))
    )
    if seam_tangent_error != 0.0:
        raise InfillSceneFailure(
            f"{connector_id} route-to-seam tangent drifted"
        )

    seam_center = (
        (route_seam[0][0] + route_seam[1][0]) / 2.0,
        (route_seam[0][1] + route_seam[1][1]) / 2.0,
    )
    polygon_center_local = (
        sum(point[0] for point in target_polygon) / len(target_polygon),
        sum(point[1] for point in target_polygon) / len(target_polygon),
    )
    polygon_center = local_to_world_xz(
        polygon_center_local,
        placement_position,
        placement_rotation[1],
    )
    inward = (
        polygon_center[0] - seam_center[0],
        polygon_center[1] - seam_center[1],
    )
    if route_edge == "end":
        approach = route_tangent
    else:
        route_center = (
            (start_position[0] + end_position[0]) / 2.0,
            (start_position[2] + end_position[2]) / 2.0,
        )
        approach = (
            seam_center[0] - route_center[0],
            seam_center[1] - route_center[1],
        )
    if approach[0] * inward[0] + approach[1] * inward[1] <= 0.0:
        raise InfillSceneFailure(
            f"{connector_id} route does not point into its target surface"
        )

    expected_overlap = finite_number(
        contract.get("expected_render_overlap_depth_m"),
        f"{connector_id} expected render overlap",
    )
    collision_clearance = finite_number(
        evidence.get("minimum_collision_proxy_clearance_m"),
        f"{connector_id} collision clearance",
    )
    if (
        expected_overlap < 0.0
        or evidence.get("expected_render_overlap_depth_m")
        != expected_overlap
        or evidence.get("observed_render_overlap_depth_m")
        != expected_overlap
        or collision_clearance <= 0.0
    ):
        raise InfillSceneFailure(
            f"{connector_id} overlap or collision clearance drifted"
        )

    pinned = PINNED_ACTIVE_CONNECTOR_GEOMETRY.get(str(connector_id))
    if pinned is not None:
        pinned_contract = {
            "placement_id": pinned["placement_id"],
            "route_edge": pinned["route_edge"],
            "route_id": pinned["route_id"],
            "route_segment_index": pinned["route_segment_index"],
            "surface_id": pinned["surface_id"],
            "target_seam_local_xz_m":
                pinned["target_seam_local_xz_m"],
            "target_surface_local_polygon_xz_m":
                pinned["target_surface_local_polygon_xz_m"],
        }
        if (
            any(
                contract.get(key) != value
                for key, value in pinned_contract.items()
            )
            or placement.get("position_m")
            != pinned["placement_position_m"]
            or placement.get("rotation_degrees")
            != pinned["placement_rotation_degrees"]
            or [start.get("position_m"), end.get("position_m")]
            != pinned["route_segment_positions_m"]
            or [start.get("yaw_degrees"), end.get("yaw_degrees")]
            != pinned["route_segment_yaws_degrees"]
            or computed_route_seam != pinned["route_world_seam_xz_m"]
            or computed_target_seam != pinned["target_world_seam_xz_m"]
            or start_width != pinned["road_width_m"]
            or target_seam_width != pinned["target_seam_width_m"]
            or expected_overlap != pinned["render_overlap_depth_m"]
            or collision_clearance != pinned["collision_clearance_m"]
        ):
            raise InfillSceneFailure(
                f"{connector_id} pinned canonical geometry drifted"
            )

    return {
        "collision_clearance_m": collision_clearance,
        "collision_proxy_overlap_count": 0,
        "connector_id": connector_id,
        "elevation_gap_m": elevation_gap,
        "grade": grade,
        "open_collision_endcaps": True,
        "road_width_m": start_width,
        "route_id": contract.get("route_id"),
        "route_tangent_error_degrees": route_tangent_error,
        "route_world_seam_xz_m": computed_route_seam,
        "seam_gap_m": seam_gap,
        "seam_tangent_error_degrees": seam_tangent_error,
        "status": "active",
        "target_seam_width_m": target_seam_width,
        "target_world_seam_xz_m": computed_target_seam,
    }


def validate_active_connector_geometry(
    manifest: Mapping[str, object],
) -> dict[str, object]:
    """Independently prove every declared connector from manifest geometry."""

    connectors = exact_dict(
        manifest.get("connectors"),
        "infill connector contracts",
    )
    if (
        set(connectors) != {
            "collision_policy",
            "contracts",
            "format",
            "maximum_seam_gap_m",
        }
        or connectors.get("format") != CONNECTOR_FORMAT
        or connectors.get("maximum_seam_gap_m")
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or connectors.get("collision_policy")
        != "no-generated-road-overlap-with-building-proxies"
    ):
        raise InfillSceneFailure("infill connector contract header drifted")
    audit = exact_dict(manifest.get("audit"), "infill audit")
    audit_connectors = exact_dict(
        audit.get("connectors"),
        "infill connector audit",
    )
    if (
        set(audit_connectors) != {
            "active",
            "contracts",
            "format",
            "maximum_allowed_seam_gap_m",
            "maximum_observed_active_seam_gap_m",
            "non_designated_route_asset_intersection_count",
            "pending",
        }
        or audit_connectors.get("format") != CONNECTOR_FORMAT
        or audit_connectors.get("maximum_allowed_seam_gap_m")
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or audit_connectors.get(
            "non_designated_route_asset_intersection_count"
        )
        != 0
    ):
        raise InfillSceneFailure("infill connector audit header drifted")
    collision_audit = exact_dict(
        audit.get("collision"),
        "infill collision audit",
    )
    if (
        collision_audit.get(
            "generated_road_collision_proxy_overlap_count"
        )
        != 0
        or collision_audit.get("collision_endcaps_enabled") is not False
        or collision_audit.get("directive")
        != "collision_endcaps_enabled false"
        or collision_audit.get("single_surface_at_access_seams") is not True
    ):
        raise InfillSceneFailure(
            "infill global connector collision contract drifted"
        )

    contracts, contract_by_id = _indexed_records(
        connectors.get("contracts"),
        "connector_id",
        "infill connector contracts",
    )
    evidence_records, evidence_by_id = _indexed_records(
        audit_connectors.get("contracts"),
        "connector_id",
        "infill connector evidence",
    )
    if (
        [record["connector_id"] for record in contracts]
        != [record["connector_id"] for record in evidence_records]
        or set(contract_by_id) != set(evidence_by_id)
    ):
        raise InfillSceneFailure(
            "infill connector contract and evidence identities drifted"
        )
    routes, route_by_id = _indexed_records(
        manifest.get("access_routes"),
        "route_id",
        "infill access routes",
    )
    placements, placement_by_id = _indexed_records(
        manifest.get("placements"),
        "placement_id",
        "infill placements",
    )
    if not routes or not placements:
        raise InfillSceneFailure(
            "infill connector geometry inventory is empty"
        )

    active_records: list[dict[str, object]] = []
    pending_records: list[dict[str, object]] = []
    for contract in contracts:
        connector_id = str(contract["connector_id"])
        evidence = evidence_by_id[connector_id]
        route_id = contract.get("route_id")
        placement_id = contract.get("placement_id")
        if (
            not isinstance(route_id, str)
            or route_id not in route_by_id
            or not isinstance(placement_id, str)
            or placement_id not in placement_by_id
        ):
            raise InfillSceneFailure(
                f"{connector_id} references missing route or placement"
            )
        route = route_by_id[route_id]
        placement = placement_by_id[placement_id]
        served_sites = exact_list(
            route.get("served_site_ids"),
            f"{connector_id} served sites",
        )
        if placement.get("site_id") not in served_sites:
            raise InfillSceneFailure(
                f"{connector_id} route does not serve its placement"
            )
        status = contract.get("status")
        if status == "active":
            active_records.append(_validate_active_connector(
                contract,
                evidence,
                route,
                placement,
            ))
        elif status == "pending":
            pending_records.append(_validate_pending_connector(
                contract,
                evidence,
                route,
                placement,
            ))
        else:
            raise InfillSceneFailure(
                f"{connector_id} status drifted"
            )

    active_ids = {
        str(record["connector_id"]) for record in active_records
    }
    missing_pinned = set(PINNED_ACTIVE_CONNECTOR_GEOMETRY) - active_ids
    if missing_pinned:
        raise InfillSceneFailure(
            "required active connector geometry is missing: "
            + ", ".join(sorted(missing_pinned))
        )
    active_count = len(active_records)
    pending_count = len(pending_records)
    if (
        isinstance(audit_connectors.get("active"), bool)
        or audit_connectors.get("active") != active_count
        or isinstance(audit_connectors.get("pending"), bool)
        or audit_connectors.get("pending") != pending_count
    ):
        raise InfillSceneFailure(
            "infill connector status counts were not derived"
        )
    maximum_gap = max(
        (float(record["seam_gap_m"]) for record in active_records),
        default=0.0,
    )
    if (
        audit_connectors.get("maximum_observed_active_seam_gap_m")
        != maximum_gap
    ):
        raise InfillSceneFailure(
            "infill maximum active seam gap was not derived"
        )
    return {
        "active": active_count,
        "active_connectors": active_records,
        "collision_policy": connectors["collision_policy"],
        "format": CONNECTOR_FORMAT,
        "maximum_active_seam_gap_m": maximum_gap,
        "maximum_allowed_seam_gap_m": MAXIMUM_CONNECTOR_SEAM_GAP_M,
        "pending": pending_count,
        "pending_connectors": pending_records,
        "zero_gap_active_connectors": active_count,
    }


def validate_manifest_payload(payload: bytes) -> dict[str, object]:
    expected_payload = infill.canonical_manifest_bytes()
    if payload != expected_payload:
        raise InfillSceneFailure(
            "embedded infill manifest differs from the canonical project plan"
        )
    if (
        corridor.sha256_bytes(payload)
        != CANONICAL_INFILL_MANIFEST_SHA256
    ):
        raise InfillSceneFailure(
            "canonical infill manifest digest drifted"
        )
    try:
        manifest = json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=corridor.reject_duplicate_keys,
        )
    except (
        corridor.DuplicateKeyError,
        RecursionError,
        UnicodeDecodeError,
        json.JSONDecodeError,
    ) as error:
        raise InfillSceneFailure(
            f"embedded infill manifest is invalid JSON: {error}"
        ) from error
    manifest = exact_dict(manifest, "infill manifest")
    if (
        manifest.get("format") != infill.FORMAT
        or manifest.get("version") != infill.VERSION
    ):
        raise InfillSceneFailure("embedded infill manifest identity drifted")
    audit = exact_dict(manifest.get("audit"), "infill audit")
    summary = exact_dict(audit.get("summary"), "infill summary")
    expected_summary = {
        "access_routes": EXPECTED_ROUTES,
        "assets": 5,
        "placements": EXPECTED_PLACEMENTS,
        "placements_by_category": {
            "farmland": 13,
            "natural-landmark": 14,
            "service-station": EXPECTED_STATIONS,
            "suburb": 17,
        },
        "sites": EXPECTED_SITES,
        "sites_by_category": {
            "farmland": 2,
            "natural-landmark": 2,
            "service-station": EXPECTED_STATIONS,
            "suburb": 2,
        },
    }
    if summary != expected_summary:
        raise InfillSceneFailure("embedded infill summary drifted")
    if (
        len(exact_list(manifest.get("placements"), "infill placements"))
        != EXPECTED_PLACEMENTS
        or len(exact_list(manifest.get("access_routes"), "infill routes"))
        != EXPECTED_ROUTES
        or len(exact_list(manifest.get("sites"), "infill sites"))
        != EXPECTED_SITES
    ):
        raise InfillSceneFailure("embedded infill inventory drifted")
    validate_active_connector_geometry(manifest)
    return manifest


def expected_source_authentication(
    manifest: Mapping[str, object],
) -> dict[str, object]:
    anchors = exact_list(
        manifest.get("source_anchors"),
        "infill source anchors",
    )
    anchor_records = [
        exact_dict(value, f"infill source anchor {index}")
        for index, value in enumerate(anchors)
    ]
    anchor_ids = [
        value.get("anchor_id")
        for value in anchor_records
    ]
    if (
        len(anchor_ids) != 7
        or any(not isinstance(value, str) for value in anchor_ids)
        or len(set(anchor_ids)) != 7
    ):
        raise InfillSceneFailure("infill source-anchor identities drifted")
    line_1354_records = [
        value
        for value in anchor_records
        if value.get("placement_line") == 1354
    ]
    line_0378_records = [
        value
        for value in anchor_records
        if value.get("placement_line") == 378
    ]
    generated = [
        value
        for value in anchor_records
        if "placement_line" not in value
    ]
    if (
        len(line_1354_records) != 1
        or len(line_0378_records) != 5
        or len(generated) != 1
    ):
        raise InfillSceneFailure("infill source-anchor provenance drifted")
    line_1354 = line_1354_records[0]
    highway_position = list(infill.PINNED_HIGHWAY_PLACEMENT_POSITION_M)
    highway_rotation = list(
        infill.PINNED_HIGHWAY_PLACEMENT_ROTATION_DEGREES
    )
    expected_line_0378 = {
        "authored_position_m": highway_position,
        "city": "NeoQueretaro",
        "collision_member": infill.PINNED_HIGHWAY_COLLISION_MEMBER,
        "collision_sha256": infill.PINNED_HIGHWAY_COLLISION_SHA256,
        "connection": "divided surface highway beneath source bridge ramp",
        "decoded_surface_materials": [
            "calleunsolosentido",
            "pavimento",
        ],
        "decoded_surface_triangle_count": 9599,
        "line_number": 378,
        "local_to_world_mapping": [
            "world_x=-local_z",
            "world_y=local_y-0.4",
            "world_z=local_x",
        ],
        "member": infill.PINNED_TOBJ_MEMBER,
        "object": infill.PINNED_HIGHWAY_OBJECT,
        "odef_member": "autopistaQr.odef",
        "odef_sha256": HIGHWAY_ODEF_SHA256,
        "position_m": highway_position,
        "rotation_degrees": highway_rotation,
        "runtime_grounding_applied": False,
        "runtime_position_m": highway_position,
    }
    expected_line_1354 = {
        "line_number": 1354,
        "member": infill.PINNED_TOBJ_MEMBER,
        "object": line_1354.get("placement_object"),
        "position_m": line_1354.get("placement_position_m"),
        "rotation_degrees": line_1354.get(
            "placement_rotation_degrees"
        ),
    }
    return {
        "anchor_ids": anchor_ids,
        "archive_sha256": infill.PINNED_ARCHIVE_SHA256,
        "authenticated_placement_lines": [378, 1354],
        "format": INFILL_SOURCE_AUTHENTICATION_FORMAT,
        "generated_anchor_count": 1,
        "line_0378": expected_line_0378,
        "line_1354": expected_line_1354,
        "native_anchor_count": 6,
        "source_anchor_count": 7,
        "source_tobj": {
            "member": infill.PINNED_TOBJ_MEMBER,
            "sha256": infill.PINNED_TOBJ_SHA256,
        },
    }


def validate_overlay_infill(
    overlay_archive: Path,
    overlay_report: Mapping[str, object],
) -> dict[str, object]:
    if overlay_report.get("format") != OVERLAY_REPORT_FORMAT:
        raise InfillSceneFailure("regional infill requires overlay v7")
    rights = exact_dict(overlay_report.get("rights"), "overlay rights")
    if (
        rights.get("derived_source_placement_record_count")
        != EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS
    ):
        raise InfillSceneFailure(
            "regional infill changed the authenticated source-derived "
            "placement count"
        )
    source = exact_dict(overlay_report.get("source"), "overlay source")
    references = exact_dict(
        source.get("references"),
        "overlay source references",
    )
    if (
        references.get("regional_infill_manifest")
        != INFILL_MANIFEST_MEMBER
    ):
        raise InfillSceneFailure(
            "overlay source does not reference the regional infill manifest"
        )
    package = exact_dict(overlay_report.get("package"), "overlay package")
    files = exact_list(package.get("files"), "overlay package files")
    matches = [
        exact_dict(value, "infill package record")
        for value in files
        if isinstance(value, dict)
        and value.get("path") == INFILL_MANIFEST_MEMBER
    ]
    if len(matches) != 1:
        raise InfillSceneFailure(
            "overlay does not inventory one regional infill manifest"
        )
    record = matches[0]
    if (
        set(record) != {"path", "role", "sha256", "size"}
        or record.get("role") != INFILL_MANIFEST_ROLE
    ):
        raise InfillSceneFailure("regional infill package record drifted")
    regional = exact_dict(
        overlay_report.get("regional_infill"),
        "overlay regional infill",
    )
    if set(regional) != {
        "audit",
        "canonical_manifest_sha256",
        "manifest",
        "source_authentication",
        "summary",
    }:
        raise InfillSceneFailure(
            "overlay regional infill fields drifted"
        )
    if regional.get("manifest") != record:
        raise InfillSceneFailure(
            "overlay regional infill does not reference its package record"
        )
    if (
        regional.get("canonical_manifest_sha256")
        != CANONICAL_INFILL_MANIFEST_SHA256
        or record.get("sha256")
        != CANONICAL_INFILL_MANIFEST_SHA256
    ):
        raise InfillSceneFailure(
            "overlay regional infill canonical hash drifted"
        )
    source_authentication = exact_dict(
        regional.get("source_authentication"),
        "regional infill source authentication",
    )
    try:
        with zipfile.ZipFile(overlay_archive, "r") as archive:
            infos = [
                info
                for info in archive.infolist()
                if info.filename == INFILL_MANIFEST_MEMBER
            ]
            if (
                len(infos) != 1
                or not 1 <= infos[0].file_size <= MAX_MANIFEST_BYTES
            ):
                raise InfillSceneFailure(
                    "embedded infill manifest member is missing or oversized"
                )
            payload = archive.read(infos[0])
    except (OSError, zipfile.BadZipFile) as error:
        raise InfillSceneFailure(
            f"cannot read embedded infill manifest: {error}"
        ) from error
    payload_sha256 = corridor.sha256_bytes(payload)
    if (
        record.get("sha256") != payload_sha256
        or record.get("size") != len(payload)
    ):
        raise InfillSceneFailure(
            "embedded infill manifest differs from its package record"
        )
    manifest = validate_manifest_payload(payload)
    if (
        regional.get("audit") != manifest["audit"]
        or regional.get("summary") != manifest["audit"]["summary"]
    ):
        raise InfillSceneFailure(
            "overlay regional infill audit or summary drifted"
        )
    if source_authentication != expected_source_authentication(manifest):
        raise InfillSceneFailure(
            "overlay regional infill source authentication drifted"
        )
    connector_geometry = validate_active_connector_geometry(manifest)
    return {
        "connector_geometry": connector_geometry,
        "format": manifest["format"],
        "manifest": record,
        "sha256": payload_sha256,
        "summary": manifest["audit"]["summary"],
        "version": manifest["version"],
    }


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(("-map", OVERLAY_TERRAIN, "-runscript", SCRIPT_NAME))
    return tuple(command)


def isolated_runtime_layout(
    isolated_home: Path,
    executable: Path,
    target_platform: str,
) -> dict[str, Path]:
    return corridor.isolated_runtime_layout(
        isolated_home,
        executable,
        target_platform,
    )


def stage_runtime(
    isolated_home: Path,
    *,
    executable: Path,
    script_payload: bytes,
    script_record: Mapping[str, object],
    cityworld_archive: Path,
    cityworld_record: Mapping[str, object],
    overlay_archive: Path,
    overlay_record: Mapping[str, object],
    target_platform: str,
) -> tuple[dict[str, Path], list[Path]]:
    layout = isolated_runtime_layout(
        isolated_home,
        executable,
        target_platform,
    )
    for key in ("config", "logs", "mods", "screenshots", "user"):
        layout[key].mkdir(parents=True, exist_ok=True)
    scripts = layout["user"] / "scripts"
    scripts.mkdir()
    staged_script = scripts / SCRIPT_NAME
    staged_cityworld = layout["mods"] / CITYWORLD_NAME
    staged_overlay = layout["mods"] / OVERLAY_NAME
    staged_script.write_bytes(script_payload)
    shutil.copyfile(cityworld_archive, staged_cityworld)
    shutil.copyfile(overlay_archive, staged_overlay)
    for staged, expected, label in (
        (
            staged_script,
            script_record.get("runtime_sha256"),
            "derived infill runtime script",
        ),
        (
            staged_cityworld,
            cityworld_record.get("sha256"),
            "CityWorld archive",
        ),
        (
            staged_overlay,
            overlay_record.get("sha256"),
            "overlay archive",
        ),
    ):
        try:
            corridor.verify_staged_file(staged, expected, label)
        except corridor.CorridorSceneFailure as error:
            raise InfillSceneFailure(str(error)) from error
    configs = base.write_runtime_config(
        layout["config"],
        target_platform=target_platform,
    )
    return layout, configs


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    *,
    expected_active_connectors: int,
    target_platform: str,
) -> dict[str, object]:
    if returncode != 0:
        raise InfillSceneFailure(
            f"RoR infill scene exited with {returncode}"
        )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise InfillSceneFailure(
                f"runtime logged fatal marker: {marker}"
            )
    if "Error =" in script_log:
        raise InfillSceneFailure("AngelScript compiler emitted an error")
    script_markers = runtime_script_markers(expected_active_connectors)
    ordered_markers(script_log, script_markers, "infill AngelScript")
    passes = list(PASS_PATTERN.finditer(script_log))
    if len(passes) != 1:
        raise InfillSceneFailure(
            "infill scene did not emit exactly one PASS"
        )
    if passes[0].start() <= script_log.find(script_markers[-1]):
        raise InfillSceneFailure(
            "infill PASS preceded the final RGB capture"
        )
    frames = int(passes[0].group("frames"))
    steps = int(passes[0].group("steps"))
    active_connectors = int(passes[0].group("active_connectors"))
    if (
        frames != PASS_FRAME
        or steps != EXPECTED_PHYSICS_STEPS
        or active_connectors != expected_active_connectors
    ):
        raise InfillSceneFailure(
            "infill deterministic frame/physics count drifted"
        )
    if (
        script_log.count("[RoR|CW2|InfillRuntime] CAPTURE index=")
        != len(RGB_CAPTURE_IDS)
    ):
        raise InfillSceneFailure(
            "infill scene did not request exactly thirteen RGB captures"
        )

    for marker in (
        corridor.CITYWORLD_FALLBACK_LIGHTING_MARKER,
        "===== TERRAIN LOADING DONE CityWorldNextLocalOverlay.terrn2",
        corridor.ENGINE_MARKERS[1],
    ):
        if engine_log.count(marker) != 1:
            raise InfillSceneFailure(
                f"engine marker must appear exactly once: {marker}"
            )
    dependencies = list(DEPENDENCY_PATTERN.finditer(engine_log))
    if len(dependencies) != 1:
        raise InfillSceneFailure(
            "CityWorld terrain dependency was not mounted exactly once"
        )
    dependency_path = dependencies[0].group("path").replace("\\", "/")
    if not dependency_path.endswith("/mods/" + CITYWORLD_NAME):
        raise InfillSceneFailure(
            "terrain dependency mounted an unexpected CityWorld path"
        )
    side_piers = sorted(
        tuple(int(value) for value in match.groups())
        for match in SIDE_PIER_SUMMARY_PATTERN.finditer(engine_log)
    )
    expected_side_piers = sorted(((46, 46, 0), (56, 56, 0)))
    if side_piers != expected_side_piers:
        raise InfillSceneFailure(
            "complete two-corridor side-pier summaries drifted"
        )
    if engine_log.count(BRIDGE_LIGHT_MARKER) != corridor.EXPECTED_LIGHTS:
        raise InfillSceneFailure("native bridge light count drifted")
    station_instances = engine_log.count(STATION_LIGHT_MARKER)
    if (
        station_instances != EXPECTED_STATIONS
        or station_instances * 6 != EXPECTED_STATION_LIGHTS
    ):
        raise InfillSceneFailure(
            "native service-station light count drifted"
        )
    for line in engine_log.splitlines():
        lowered = line.casefold()
        if (
            "rorng_city_infill_" in lowered
            and any(token in lowered for token in INFILL_MATERIAL_FAILURE_TOKENS)
        ):
            raise InfillSceneFailure(
                "infill material did not resolve: " + line
            )
    return {
        "active_connectors": active_connectors,
        "captures": len(RGB_CAPTURE_IDS),
        "frames": frames,
        "physics_steps": steps,
        "renderer": base.parse_renderer_identity(
            engine_log,
            target_platform,
        ),
        "side_piers": [list(item) for item in side_piers],
        "station_light_instances": station_instances,
        "station_lights": station_instances * 6,
    }


def validate_rgb_screenshots(
    directory: Path,
) -> tuple[list[Path], dict[str, dict[str, object]]]:
    try:
        entries = list(directory.iterdir())
    except OSError as error:
        raise InfillSceneFailure(
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
        raise InfillSceneFailure(
            "runtime must emit exactly thirteen regular PNG screenshots"
        )
    ordered_entries: list[tuple[str, int, Path]] = []
    for path in entries:
        match = SCREENSHOT_FILENAME_PATTERN.fullmatch(path.name)
        if match is None:
            raise InfillSceneFailure(
                "runtime screenshot filename contract drifted"
            )
        ordered_entries.append(
            (
                match.group("timestamp"),
                int(match.group("index")),
                path,
            )
        )
    entries = [
        path
        for _timestamp, _index, path in sorted(ordered_entries)
    ]
    records: dict[str, dict[str, object]] = {}
    hashes: set[str] = set()
    for capture_id, path in zip(RGB_CAPTURE_IDS, entries):
        try:
            record = base.validate_rgb_png(path)
        except base.BridgeSceneFailure as error:
            raise InfillSceneFailure(
                f"{capture_id} RGB validation failed: {error}"
            ) from error
        digest = record.get("sha256")
        if not isinstance(digest, str) or not digest or digest in hashes:
            raise InfillSceneFailure(
                "the thirteen fixed infill RGB screenshots are not distinct"
            )
        hashes.add(digest)
        records[capture_id] = {
            **record,
            "source_filename": path.name,
        }
    return entries, records


def collect_diagnostics(
    artifact_staging: Path,
    *,
    stdout: str,
    engine_log: str,
    script_log: str,
    config_paths: Sequence[Path],
) -> dict[str, object]:
    directory = artifact_staging / "diagnostics"
    directory.mkdir()
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
            "artifact": destination.relative_to(
                artifact_staging
            ).as_posix(),
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
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--rebuild-timeout", type=int, default=180)
    parser.add_argument("--runtime-timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.rebuild_timeout <= 0 or args.runtime_timeout <= 0:
        parser.error("timeouts must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    base.renderer_contract(sys.platform)
    repository = args.repository.resolve()
    if repository != REPOSITORY_ROOT.resolve():
        raise InfillSceneFailure(
            "--repository must be the checkout containing this runner"
        )
    executable = args.executable.resolve()
    cityworld_archive = args.cityworld_archive.resolve()
    overlay_archive = args.overlay_archive.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file() or executable.is_symlink():
        raise InfillSceneFailure(
            f"executable is missing or is a symlink: {executable}"
        )
    base.require_isolated_runtime_executable(executable, sys.platform)
    if artifact_dir.exists():
        raise InfillSceneFailure(
            f"artifact directory already exists: {artifact_dir}"
        )

    cityworld_record = corridor.validate_cityworld_archive(
        cityworld_archive
    )
    overlay_report, overlay_record = corridor.validate_overlay_archive(
        overlay_archive,
        repository,
    )
    infill_record = validate_overlay_infill(
        overlay_archive,
        overlay_report,
    )
    overlay_rebuild = corridor.verify_overlay_rebuild(
        cityworld_archive,
        overlay_archive,
        overlay_report,
        repository,
        args.rebuild_timeout,
    )
    fixture_path = repository / FIXTURE_PATH
    connector_geometry = exact_dict(
        infill_record.get("connector_geometry"),
        "validated connector geometry",
    )
    active_connector_count = connector_geometry.get("active")
    if (
        isinstance(active_connector_count, bool)
        or not isinstance(active_connector_count, int)
        or active_connector_count <= 0
    ):
        raise InfillSceneFailure(
            "validated connector geometry has no active connectors"
        )
    fixture_record, script_payload = validate_fixture(
        fixture_path,
        active_connector_count=active_connector_count,
    )

    artifact_dir.parent.mkdir(parents=True, exist_ok=True)
    artifact_staging = Path(
        tempfile.mkdtemp(
            prefix=f".{artifact_dir.name}.partial-",
            dir=artifact_dir.parent,
        )
    )
    published = False
    try:
        with tempfile.TemporaryDirectory(
            prefix="ror-cityworld-infill-"
        ) as temporary:
            isolated_home = Path(temporary)
            layout, config_paths = stage_runtime(
                isolated_home,
                executable=executable,
                script_payload=script_payload,
                script_record=fixture_record,
                cityworld_archive=cityworld_archive,
                cityworld_record=cityworld_record,
                overlay_archive=overlay_archive,
                overlay_record=overlay_record,
                target_platform=sys.platform,
            )
            environment = base.isolated_runtime_environment(isolated_home)
            command = build_command(executable)
            completed = base.run_command(
                command,
                args.runtime_timeout,
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
                expected_active_connectors=active_connector_count,
                target_platform=sys.platform,
            )
            screenshots, rgb_records = validate_rgb_screenshots(
                layout["screenshots"]
            )
            diagnostics = collect_diagnostics(
                artifact_staging,
                stdout=stdout,
                engine_log=engine_log,
                script_log=script_log,
                config_paths=config_paths,
            )
            rgb_directory = artifact_staging / "rgb"
            rgb_directory.mkdir()
            rgb_artifacts: dict[str, str] = {}
            for capture_id, screenshot in zip(
                RGB_CAPTURE_IDS,
                screenshots,
            ):
                destination = rgb_directory / f"{capture_id}.png"
                shutil.copy2(screenshot, destination)
                rgb_artifacts[capture_id] = (
                    destination.relative_to(artifact_staging).as_posix()
                )

        report: dict[str, object] = {
            "acceptance": {
                "close_connector_seam_rgb_views":
                    len(SEAM_RGB_CAPTURE_IDS),
                "fixed_ui_free_rgb_views": len(RGB_CAPTURE_IDS),
                "infill_material_resolution_verified": True,
                "native_service_station_lights_verified":
                    EXPECTED_STATION_LIGHTS,
                "status": "passed",
                "zero_gap_active_connectors":
                    active_connector_count,
            },
            "archives": {
                "cityworld": cityworld_record,
                "overlay": overlay_record,
            },
            "artifacts": {
                "diagnostics": diagnostics,
                "rgb": rgb_artifacts,
            },
            "command": list(command),
            "executable": {
                "build_commit_binding":
                    "not-asserted-by-content-only-runtime-gate",
                "path": str(executable),
                "sha256": sha256_file(executable),
                "size": executable.stat().st_size,
            },
            "fixture": fixture_record,
            "format": REPORT_FORMAT,
            "infill": infill_record,
            "machine": platform.machine(),
            "metrics": metrics,
            "overlay_rebuild": overlay_rebuild,
            "platform": platform.platform(),
            "repository_commit": base.git_output(
                repository,
                ("rev-parse", "HEAD"),
            ),
            "rgb": rgb_records,
            "runners": {
                "tools/run_cityworld_bridge_scene.py": {
                    "sha256": sha256_file(corridor.BASE_PATH),
                },
                "tools/run_cityworld_corridor_scene.py": {
                    "sha256": sha256_file(CORRIDOR_RUNNER_PATH),
                },
                "tools/run_cityworld_infill_scene.py": {
                    "sha256": sha256_file(Path(__file__).resolve()),
                },
            },
        }
        report_name = "cityworld_infill_runtime.report.json"
        report_path = artifact_staging / report_name
        report_path.write_text(
            base.canonical_json(report) + "\n",
            encoding="utf-8",
        )
        report_sha = sha256_file(report_path)
        corridor.publish_artifact_directory(
            artifact_staging,
            artifact_dir,
        )
        published = True
    finally:
        if not published:
            shutil.rmtree(artifact_staging, ignore_errors=True)

    published_report = artifact_dir / report_name
    print(
        "CityWorld regional-infill runtime acceptance passed: "
        f"{published_report} sha256={report_sha}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        InfillSceneFailure,
        corridor.CorridorSceneFailure,
        base.BridgeSceneFailure,
    ) as error:
        print(
            f"CityWorld regional-infill runtime acceptance failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
