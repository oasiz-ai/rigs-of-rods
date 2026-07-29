#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnose the reported v3 curb-clearing route with the packaged DAF.

CityWorld is third-party content and is intentionally absent from this
repository. This diagnostic accepts the authenticated original and locally
derived overlay, validates their complete relationship, stages them in an
ephemeral RoR home, and retains only logs, one UI-free RGB image, and hashes.
It does not certify visible road joins, swept-mesh clearance, placed bridge
modules, or visible supports.
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
REPORT_FORMAT = "ror-cityworld-corridor-runtime-report-v1"
RGB_NAME = "cityworld_corridor_rgb.png"
MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 64
MAX_OVERLAY_MEMBER_BYTES = 64 * 1024 * 1024
MAX_OVERLAY_TOTAL_BYTES = 128 * 1024 * 1024
EXPECTED_WAYPOINTS = 59
EXPECTED_LIGHTS = 16
MAX_PHYSICS_STEPS = 240000
CITYWORLD_FALLBACK_LIGHTING_MARKER = base.fallback_lighting_marker(
    (0.93, 0.86, 0.76)
)
EXPECTED_UNPLACED_ASSETS = [
    "rorng_city_gateway_block_40m",
    "rorng_city_bridge_transition_12m",
    "rorng_city_bridge_curve_left_15deg_20m",
    "rorng_city_bridge_span_20m",
]
REQUIRED_OVERLAY_TOOLS = frozenset(
    (
        "tools/audit_cityworld_visuals.py",
        "tools/build_cityworld_local_overlay.py",
        "tools/compile_cityworld_asset.py",
        "tools/solve_cityworld_bridge_corridor.py",
        "tools/validate_cityworld_asset.py",
    )
)

SCRIPT_MARKERS = (
    "[RoR|CW2|CorridorRuntime] START route_m=1075.447727259",
    "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901",
    "[RoR|CW2|CorridorRuntime] SOURCE_SEAM",
    "[RoR|CW2|CorridorRuntime] MIDPOINT",
    "[RoR|CW2|CorridorRuntime] CAPTURE",
    "[RoR|CW2|CorridorRuntime] DESTINATION_SEAM",
    "[RoR|CW2|CorridorRuntime] PASS seams=2 route_m=1075.447727259",
)
ENGINE_MARKERS = (
    CITYWORLD_FALLBACK_LIGHTING_MARKER,
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
)
ARMED_PATTERN = re.compile(
    r"\[RoR\|CW2\|CorridorRuntime\] ARMED actor=2026072901 "
    r"heading=(?P<heading>-?[0-9.eE+]+) "
    r"station=(?P<station>-?[0-9.eE+]+) "
    r"cross_track=(?P<cross>-?[0-9.eE+]+) "
    r"height=(?P<height>-?[0-9.eE+]+)"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|CorridorRuntime\] PASS seams=2 "
    r"route_m=1075\.447727259 "
    r"distance_m=(?P<distance>-?[0-9.eE+]+) "
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


class CorridorSceneFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid private-content runtime gate."""


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
            if report.get("format") != "ror-cityworld-local-overlay-v3":
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
                "source_placements_copied": False,
                "source_textures_copied": False,
            }
            for key, expected in required_rights.items():
                if rights.get(key) is not expected:
                    raise CorridorSceneFailure(
                        f"overlay rights contract drifted: {key}"
                    )

            package = exact_dict(report.get("package"), "overlay package")
            package_files = exact_list(package.get("files"), "package files")
            expected_names = {OVERLAY_REPORT_MEMBER}
            for index, value in enumerate(package_files):
                record = exact_dict(value, f"package file {index}")
                name = record.get("path")
                if not isinstance(name, str) or PurePosixPath(name).name != name:
                    raise CorridorSceneFailure("package file path is unsafe")
                if name in expected_names or name not in names:
                    raise CorridorSceneFailure("package inventory is inconsistent")
                payload = archive.read(name)
                if (
                    record.get("sha256") != sha256_bytes(payload)
                    or exact_int(record.get("size"), "package file size")
                    != len(payload)
                ):
                    raise CorridorSceneFailure(
                        f"overlay payload differs from report: {name}"
                    )
                expected_names.add(name)
            if (
                exact_int(package.get("entries"), "package entries")
                != len(infos)
                or expected_names != set(names)
            ):
                raise CorridorSceneFailure("overlay member inventory drifted")

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

    corridor = exact_dict(report.get("corridor"), "overlay corridor")
    if corridor.get("format") != "ror-cityworld-intercity-corridor-v3":
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
        (waypoints[0], (480.0, 0.198, 370.0), "source"),
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
    expected_apron_waypoints = (
        ((480.0, 0.198, 370.0), 0.0),
        ((490.0, 0.31, 370.0), 10.0),
        ((494.8491, 0.31, 370.0), 14.8491),
    )
    for index, (expected_position, expected_station) in enumerate(
        expected_apron_waypoints
    ):
        waypoint = exact_dict(waypoints[index], f"apron waypoint {index}")
        position = exact_list(
            waypoint.get("position_m"),
            f"apron waypoint {index} position",
        )
        station = finite_number(
            waypoint.get("station_m"),
            f"apron waypoint {index} station",
        )
        if (
            abs(station - expected_station) > 1.0e-6
            or any(
                abs(finite_number(actual, "apron position") - expected)
                > 1.0e-6
                for actual, expected in zip(position, expected_position)
            )
        ):
            raise CorridorSceneFailure(
                f"Penguinville curb apron drifted at waypoint {index}"
            )
    covered = finite_number(
        corridor.get("covered_centerline_length_m"),
        "covered centerline length",
    )
    if abs(covered - 1075.447727259) > 1.0e-6:
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
    apron = exact_dict(source.get("apron"), "corridor source apron")
    if (
        apron.get("collision_authority") != "native-procedural-road-v3"
        or apron.get("legacy_collision_mesh")
        != "troadavenuesidewalkbox.mesh"
        or apron.get("surface_continuous") is not True
    ):
        raise CorridorSceneFailure("Penguinville curb apron contract drifted")
    apron_numbers = {
        "curb_clearance_m": 0.01,
        "curb_top_y_m": 0.3,
        "legacy_road_surface_y_m": 0.198,
        "overlap_length_m": 14.8491,
        "plateau_y_m": 0.31,
        "rise_length_m": 10.0,
    }
    for key, expected in apron_numbers.items():
        actual = finite_number(apron.get(key), f"source apron {key}")
        if abs(actual - expected) > 1.0e-9:
            raise CorridorSceneFailure(
                f"Penguinville curb apron contract drifted: {key}"
            )
    if (
        abs(
            finite_number(apron.get("plateau_y_m"), "apron plateau")
            - finite_number(apron.get("curb_top_y_m"), "apron curb top")
            - finite_number(apron.get("curb_clearance_m"), "apron clearance")
        )
        > 1.0e-9
        or finite_number(
            exact_list(
                exact_dict(waypoints[2], "curb waypoint").get("position_m"),
                "curb waypoint position",
            )[1],
            "curb waypoint height",
        )
        <= finite_number(apron.get("curb_top_y_m"), "apron curb top")
    ):
        raise CorridorSceneFailure("Penguinville curb is not physically cleared")
    fixtures = exact_dict(corridor.get("fixtures"), "corridor fixtures")
    if (
        exact_int(fixtures.get("instance_count"), "fixture count")
        != EXPECTED_LIGHTS
        or exact_int(
            fixtures.get("runtime_point_lights_per_instance"),
            "fixture light count",
        )
        != 1
        or fixtures.get("collision_authority") != "native-procedural-road-v3"
    ):
        raise CorridorSceneFailure("corridor lighting contract drifted")
    profile = exact_dict(corridor.get("profile"), "corridor profile")
    if (
        abs(finite_number(profile.get("width_m"), "corridor width") - 8.9)
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
        or exact_int(supports.get("requested_count"), "support count") != 47
        or supports.get("terrain_contact_resolved_at_runtime") is not True
    ):
        raise CorridorSceneFailure("corridor support contract drifted")
    usage = exact_dict(
        report.get("visual_asset_usage"),
        "overlay visual asset usage",
    )
    if usage.get("collision_authority") is not None:
        raise CorridorSceneFailure(
            "visual asset usage cannot override collision authority"
        )
    if (
        usage.get("purpose")
        != (
            "curb-free Penguinville overlap apron plus route-safe Blender "
            "lighting; bridge modules remain validated candidates for deck "
            "and abutment replacement"
        )
        or usage.get("corridor_placement_mode")
        != "native-procedural-v3-curb-cut-with-blender-fixtures-v1"
        or usage.get("packaged_asset_ids")
        != ["rorng_city_led_streetlight_bridge"]
        or usage.get("placed_asset_ids")
        != ["rorng_city_led_streetlight_bridge"]
        or usage.get("unplaced_asset_ids") != EXPECTED_UNPLACED_ASSETS
        or usage.get("validated_asset_ids")
        != EXPECTED_UNPLACED_ASSETS + ["rorng_city_led_streetlight_bridge"]
    ):
        raise CorridorSceneFailure(
            "diagnostic only supports the known incomplete v3 visual pass"
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
        ((470.0, 0.198, 370.0), -10.0),
        ((1400.966797, 0.1, 936.098389), 1095.447727259),
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
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise CorridorSceneFailure(f"runtime logged fatal marker: {marker}")
    if "Error =" in script_log:
        raise CorridorSceneFailure("AngelScript compiler emitted an error")

    armed_matches = list(ARMED_PATTERN.finditer(script_log))
    pass_matches = list(PASS_PATTERN.finditer(script_log))
    if len(armed_matches) != 1 or len(pass_matches) != 1:
        raise CorridorSceneFailure("runtime did not emit one ARMED and one PASS")
    armed = {key: float(value) for key, value in armed_matches[0].groupdict().items()}
    if not all(math.isfinite(value) for value in armed.values()):
        raise CorridorSceneFailure("ARMED metrics are non-finite")
    if not -10.0 <= armed["station"] <= -3.0:
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
        "path_error_m": float(record["path"]),
        "physics_steps": int(record["steps"]),
        "regression_m": float(record["regression"]),
        "speed_mps": float(record["speed"]),
        "vertical_error_m": float(record["vertical"]),
    }
    if not all(
        math.isfinite(value)
        for value in metrics.values()
        if isinstance(value, float)
    ):
        raise CorridorSceneFailure("PASS metrics are non-finite")
    if not 1075.0 <= metrics["distance_m"] <= 1130.0:
        raise CorridorSceneFailure("physical traversal distance is invalid")
    if not 0.0 <= metrics["path_error_m"] <= 2.0:
        raise CorridorSceneFailure("corridor path error is excessive")
    if not 0.0 <= metrics["vertical_error_m"] <= 1.5:
        raise CorridorSceneFailure("corridor vertical error is excessive")
    if not 0.0 <= metrics["regression_m"] <= 1.0:
        raise CorridorSceneFailure("corridor progress regressed")
    if not 5.0 <= metrics["speed_mps"] <= 20.0:
        raise CorridorSceneFailure("corridor exit speed is invalid")
    if not 100000 <= metrics["physics_steps"] <= MAX_PHYSICS_STEPS:
        raise CorridorSceneFailure("corridor physics-step count is invalid")
    return metrics


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--cityworld-archive", required=True, type=Path)
    parser.add_argument("--overlay-archive", required=True, type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument(
        "--diagnostic-allow-incomplete-overlay",
        action="store_true",
        help=(
            "acknowledge that a successful run follows the procedural "
            "centreline but is not a CityWorld connection/support acceptance"
        ),
    )
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not args.diagnostic_allow_incomplete_overlay:
        parser.error(
            "the current v3 overlay is incomplete; explicitly pass "
            "--diagnostic-allow-incomplete-overlay for a non-acceptance run"
        )
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
        shutil.copyfile(script_path, staged_script)
        shutil.copyfile(cityworld_archive, staged_cityworld)
        shutil.copyfile(overlay_archive, staged_overlay)
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
        screenshot = base.find_single_screenshot(layout["screenshots"])
        rgb_record = base.validate_rgb_png(screenshot)
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
        shutil.copy2(screenshot, rgb_directory / RGB_NAME)
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
            "city_road_surface_connection_verified": False,
            "status": "diagnostic-only-current-v3",
            "swept_visual_clearance_verified": False,
            "visible_bridge_modules_verified": False,
            "visible_supports_verified": False,
        },
        "archives": {
            "cityworld": cityworld_record,
            "overlay": overlay_record,
        },
        "artifacts": {
            "engine_log": "diagnostics/RoR.log",
            "rgb": f"rgb/{RGB_NAME}",
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
        "rgb": rgb_record,
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
        "CityWorld procedural-route diagnostic passed (NOT acceptance): "
        f"{published_report} sha256={report_sha}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CorridorSceneFailure, base.BridgeSceneFailure) as error:
        print(
            f"CityWorld procedural-route diagnostic failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
