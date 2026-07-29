#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a deterministic, local-only CityWorld Next overlay package.

The source ``CityWorld.zip`` is a user-supplied compatibility dependency. This
tool audits it in place and reads only three named members for provenance. It
does not extract the archive, execute archive content, use the network, or copy
original CityWorld payloads into the generated package.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
from typing import Any, Sequence
import zipfile


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from audit_cityworld_visuals import (  # noqa: E402
    AuditFailure,
    NEOQ_LIGHT_RANGE_LIMIT_M,
    NEOQ_LUMINARIA_FAMILIES,
    NEOQ_RELIGHT_FORMAT,
    NEOQ_RELIGHT_RADIUS_M,
    NEOQ_RELIGHT_TELEPOINT,
    READ_CHUNK_BYTES,
    archive_sha256,
    audit_archive,
    validate_archive_members,
)
from compile_cityworld_asset import (  # noqa: E402
    CompileFailure,
    SceneCompiler,
    default_output_directory,
    validate_checked_outputs,
)
from solve_cityworld_bridge_corridor import (  # noqa: E402
    AssetProfile,
    CorridorFailure,
    Placement,
    load_asset_profile,
    report as corridor_report,
    solve_corridor,
    stable_float,
    tobj_text,
)
from validate_cityworld_asset import Validator  # noqa: E402


FORMAT = "ror-cityworld-local-overlay-v4"
BUILD_RESULT_FORMAT = "ror-cityworld-local-overlay-build-result-v4"
PINNED_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)
RESOURCE_BUNDLE_IDENTITY = "CityWorld.zip:CityWorld.terrn2"
SOURCE_TELEPOINT = "Penguinville Spawn"
DESTINATION_TELEPOINT = "NeoQueretaro Spawn"
SOURCE_MEMBERS = ("CityWorld.terrn2", "CityWorld.otc", "CityWorld.tobj")
TERRAIN_NAME = "CityWorldNextLocalOverlay.terrn2"
OVERLAY_NAME = "cityworld_next_local_overlay.tobj"
REPORT_NAME = "cityworld_next_local_overlay.report.json"
MERGED_MATERIAL_NAME = "cityworld_next_local_overlay.material"
NEOQ_LIGHT_CANDIDATE_NAME = (
    "cityworld_next_neoq_core_lights.candidates.json"
)
NEOQ_LIGHT_CANDIDATE_FORMAT = (
    "ror-cityworld-neoq-core-light-candidates-v1"
)
NEOQ_LIGHT_POLICY_ID = "ror-cityworld-local-light-budget-v1"
NEOQ_EXPECTED_MAP_FAMILY_COUNTS = {
    "luminariaLQr": 528,
    "luminariaQr": 239,
    "luminariaYQr": 12,
}
NEOQ_EXPECTED_CANDIDATE_FAMILY_COUNTS = {
    "luminariaLQr": 42,
    "luminariaQr": 25,
    "luminariaYQr": 0,
}
NEOQ_LIGHT_ADAPTERS = {
    "luminariaLQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_l_lightonly",
        "local_position_m": (-2.75, 0.0, 9.7),
    },
    "luminariaQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_dual_lightonly",
        "local_position_m": (0.0, 0.0, 9.7),
    },
    "luminariaYQr": {
        "future_object_definition":
            "rorng_city_neoq_luminaria_triple_lightonly",
        "local_position_m": (0.0, 0.0, 9.7),
    },
}
MIN_SURFACE_OFFSET_M = -2.0
MAX_SURFACE_OFFSET_M = 20.0
MAX_RUNTIME_FILE_BYTES = 256 * 1024 * 1024
MAX_MATERIAL_SCRIPT_BYTES = 4 * 1024 * 1024
MAX_SOURCE_TOBJ_BYTES = 32 * 1024 * 1024
MAX_SOURCE_PLACEMENTS = 50_000
MAX_MATERIAL_DEFINITIONS = 512
MAX_MATERIAL_TOKENS = 100_000
OUTPUT_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*\.zip$")
WINDOWS_RESERVED_BASENAMES = {
    "aux",
    "con",
    "nul",
    "prn",
    *(f"com{index}" for index in range(1, 10)),
    *(f"lpt{index}" for index in range(1, 10)),
}
MATERIAL_NAME_PATTERN = re.compile(r"^[A-Za-z0-9_./-]+$")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644
POSITION_EPSILON = 1e-6
ROUTE_SAMPLE_SPACING_M = 20.0
ROUTE_TANGENT_HANDLE_M = 160.0
ROUTE_GROUND_LEAD_M = 40.0
ROUTE_RAMP_LENGTH_M = 160.0
ROUTE_DECK_CLEARANCE_M = 8.0
ROUTE_WIDTH_M = 8.9
ROUTE_BRIDGE_BORDER_WIDTH_M = 0.45
ROUTE_BRIDGE_BORDER_HEIGHT_M = 0.95
ROUTE_STREETLIGHT_SPACING_M = 40.0
ROUTE_STREETLIGHT_DECK_MARGIN_M = 20.0
ROUTE_FLAT_BORDER_WIDTH_M = 1.0
ROUTE_FLAT_BORDER_HEIGHT_M = 0.15
ROUTE_SOURCE_APRON_START_X_M = 480.0
ROUTE_SOURCE_APRON_RISE_X_M = 490.0
ROUTE_SOURCE_LEGACY_ROAD_SURFACE_Y_M = 0.198
ROUTE_SOURCE_LEGACY_CURB_TOP_Y_M = 0.3
ROUTE_SOURCE_CURB_CLEARANCE_M = 0.01
ROUTE_ARC_TABLE_STEPS = 8192
ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES = 0.0
ROUTE_MAX_CONNECTION_TAPER_GRADE = 0.02
ROUTE_OPEN_GAP_BOUNDS_XZ_M = (500.0, 1380.0, 400.0, 1000.0)
ROUTE_SOURCE_ANCHOR = {
    "city": "Penguinville",
    "connection": "east T-junction",
    "connection_position_m": (494.8491, 0.1, 370.0),
    "object": "troadavenuesidewalk",
    "placement_position_m": (485.0, 0.1, 370.0),
    "rotation_degrees": (0.0, 90.0, 0.0),
}
ROUTE_DESTINATION_ANCHOR = {
    "city": "NeoQueretaro",
    "connection": "west perimeter T-junction carriageway",
    "connection_position_m": (1380.966797, 0.1, 936.098389),
    "object": "crucetQr",
    "placement_position_m": (1460.966797, 0.1, 903.098389),
    "rotation_degrees": (0.0, -180.0, 0.0),
}

GATEWAY_MANIFEST = (
    "resources/nextgen/cityworld/streetscape/gateway_block_40m/"
    "rorng_city_gateway_block_40m.asset.json"
)
TRANSITION_MANIFEST = (
    "resources/nextgen/cityworld/bridge/transition_12m/"
    "rorng_city_bridge_transition_12m.asset.json"
)
CURVE_MANIFEST = (
    "resources/nextgen/cityworld/bridge/curve_left_15deg/"
    "rorng_city_bridge_curve_left_15deg_20m.asset.json"
)
TANGENT_MANIFEST = (
    "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
LED_STREETLIGHT_MANIFEST = (
    "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/"
    "rorng_city_led_streetlight_bridge.asset.json"
)
LED_STREETLIGHT_ASSET_ID = "rorng_city_led_streetlight_bridge"
ASSET_MANIFESTS = (
    GATEWAY_MANIFEST,
    TRANSITION_MANIFEST,
    CURVE_MANIFEST,
    TANGENT_MANIFEST,
)
MODULE_ASSET_IDS = (
    "rorng_city_gateway_block_40m",
    "rorng_city_bridge_transition_12m",
    "rorng_city_bridge_curve_left_15deg_20m",
    "rorng_city_bridge_curve_left_15deg_20m",
    "rorng_city_bridge_curve_left_15deg_20m",
    "rorng_city_bridge_span_20m",
    "rorng_city_bridge_span_20m",
    "rorng_city_bridge_span_20m",
    "rorng_city_bridge_span_20m",
)
TOOL_PATHS = (
    "tools/audit_cityworld_visuals.py",
    "tools/build_cityworld_local_overlay.py",
    "tools/compile_cityworld_asset.py",
    "tools/solve_cityworld_bridge_corridor.py",
    "tools/validate_cityworld_asset.py",
)


class OverlayFailure(RuntimeError):
    """A stable failure caused by an unsafe or stale overlay input."""


def resource_bundle_dependency() -> str:
    """Return the authenticated direct-mount identity for CityWorld.zip."""

    return f"{RESOURCE_BUNDLE_IDENTITY}:{PINNED_ARCHIVE_SHA256}"


@dataclass(frozen=True)
class RuntimeFile:
    package_path: str
    repository_path: str
    role: str
    sha256: str
    size: int
    payload: bytes


@dataclass(frozen=True)
class PreparedAsset:
    asset_id: str
    centerline_length_m: float | None
    manifest_path: str
    profile: AssetProfile | None
    provenance: dict[str, Any]
    runtime_files: tuple[RuntimeFile, ...]


@dataclass(frozen=True)
class SourcePlacement:
    line_number: int
    position: tuple[float, float, float]
    rotation_degrees: tuple[float, float, float]
    object_name: str


@dataclass(frozen=True)
class ProceduralRoutePoint:
    station_m: float
    x: float
    y: float
    z: float
    yaw_degrees: float
    road_type: str
    width_m: float
    border_width_m: float
    border_height_m: float


@dataclass(frozen=True)
class TerrainObjectPlacement:
    station_m: float
    side: str
    x: float
    y: float
    z: float
    yaw_degrees: float
    asset_id: str
    instance_name: str


@dataclass(frozen=True)
class MaterialToken:
    kind: str
    value: str


@dataclass(frozen=True)
class MaterialDefinition:
    name: str
    origin: str
    tokens: tuple[MaterialToken, ...]


def canonical_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_regular_file(path: Path, *, max_bytes: int) -> str:
    if not path.is_file() or path.is_symlink():
        raise OverlayFailure(f"required regular file is missing: {path}")
    if path.stat().st_size > max_bytes:
        raise OverlayFailure(f"file exceeds {max_bytes} byte limit: {path.name}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(READ_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def portable_repository_path(repository: Path, path: Path) -> str:
    try:
        relative = path.resolve().relative_to(repository.resolve()).as_posix()
    except ValueError as error:
        raise OverlayFailure(f"path escapes repository root: {path}") from error
    if safe_package_path(relative) != relative:
        raise OverlayFailure(f"path is not portable: {relative}")
    return relative


def safe_package_path(value: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or "\\" in value
        or "\x00" in value
        or not value.isascii()
        or any(ord(character) < 32 or ord(character) == 127 for character in value)
    ):
        raise OverlayFailure(f"unsafe generated package path: {value!r}")
    parsed = PurePosixPath(value)
    if (
        parsed.is_absolute()
        or any(part in ("", ".", "..") for part in parsed.parts)
        or parsed.as_posix() != value
        or ":" in parsed.parts[0]
    ):
        raise OverlayFailure(f"unsafe generated package path: {value!r}")
    return value


def validate_repository(path: Path) -> Path:
    if path.is_symlink():
        raise OverlayFailure("repository root cannot be a symbolic link")
    repository = path.resolve()
    if not repository.is_dir():
        raise OverlayFailure("repository root does not exist or is not a directory")
    expected_tool = repository / "tools/build_cityworld_local_overlay.py"
    try:
        if not expected_tool.samefile(Path(__file__)):
            raise OverlayFailure(
                "repository root does not contain the executing overlay builder"
            )
    except OSError as error:
        raise OverlayFailure(
            "repository root does not contain the executing overlay builder"
        ) from error
    return repository


def validate_archive_path(repository: Path, path: Path) -> Path:
    if path.is_symlink():
        raise OverlayFailure("CityWorld archive cannot be a symbolic link")
    archive = path.resolve()
    if archive.name != "CityWorld.zip":
        raise OverlayFailure("input archive must be named CityWorld.zip")
    if not archive.is_file():
        raise OverlayFailure("input CityWorld.zip does not exist or is not a file")
    try:
        archive.relative_to(repository)
    except ValueError:
        pass
    else:
        raise OverlayFailure("input CityWorld.zip must be outside the repository")
    return archive


def validate_output_path(repository: Path, path: Path) -> Path:
    if not OUTPUT_NAME_PATTERN.fullmatch(path.name):
        raise OverlayFailure(
            "output must be an explicit portable .zip filename"
        )
    windows_device_basename = path.name.split(".", 1)[0].casefold()
    if windows_device_basename in WINDOWS_RESERVED_BASENAMES:
        raise OverlayFailure("output filename is reserved on Windows")
    try:
        parent = path.parent.resolve(strict=True)
    except OSError as error:
        raise OverlayFailure("output parent does not exist") from error
    if not parent.is_dir():
        raise OverlayFailure("output parent is not a directory")
    output = parent / path.name
    try:
        output.relative_to(repository)
    except ValueError:
        pass
    else:
        raise OverlayFailure("output must be outside the repository")
    if os.path.lexists(output):
        raise OverlayFailure("output target already exists")
    return output


def finite_vector3(value: Any, label: str) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) != 3:
        raise OverlayFailure(f"{label} must contain three finite coordinates")
    result: list[float] = []
    for component in value:
        if isinstance(component, bool) or not isinstance(component, (int, float)):
            raise OverlayFailure(f"{label} must contain three finite coordinates")
        converted = float(component)
        if not math.isfinite(converted):
            raise OverlayFailure(f"{label} must contain three finite coordinates")
        result.append(converted)
    return (result[0], result[1], result[2])


def exact_telepoint(
    audit: dict[str, Any], name: str
) -> tuple[float, float, float]:
    terrain = audit.get("terrain")
    telepoints = terrain.get("telepoints") if isinstance(terrain, dict) else None
    if not isinstance(telepoints, list):
        raise OverlayFailure("audited terrain has no telepoint list")
    matches = [
        point
        for point in telepoints
        if isinstance(point, dict) and point.get("name") == name
    ]
    if len(matches) != 1:
        raise OverlayFailure(
            f"expected exactly one telepoint named {name!r}; found {len(matches)}"
        )
    return finite_vector3(matches[0].get("position"), f"telepoint {name!r}")


def source_member_provenance(archive_path: Path) -> list[dict[str, Any]]:
    try:
        with zipfile.ZipFile(archive_path) as archive:
            _, names = validate_archive_members(archive.infolist())
            result: list[dict[str, Any]] = []
            for member_name in SOURCE_MEMBERS:
                info = names.get(member_name.casefold())
                if info is None or info.filename != member_name or info.is_dir():
                    raise OverlayFailure(
                        f"archive must contain the exact member {member_name}"
                    )
                digest = hashlib.sha256()
                with archive.open(info, "r") as stream:
                    while chunk := stream.read(READ_CHUNK_BYTES):
                        digest.update(chunk)
                result.append(
                    {
                        "bytes": info.file_size,
                        "crc32": f"{info.CRC:08x}",
                        "name": member_name,
                        "sha256": digest.hexdigest(),
                    }
                )
            return result
    except zipfile.BadZipFile as error:
        raise OverlayFailure("input is not a valid ZIP archive") from error


def source_placements(archive_path: Path) -> tuple[SourcePlacement, ...]:
    try:
        with zipfile.ZipFile(archive_path) as archive:
            _, names = validate_archive_members(archive.infolist())
            info = names.get("cityworld.tobj")
            if (
                info is None
                or info.filename != "CityWorld.tobj"
                or info.is_dir()
            ):
                raise OverlayFailure(
                    "archive must contain the exact member CityWorld.tobj"
                )
            if info.file_size > MAX_SOURCE_TOBJ_BYTES:
                raise OverlayFailure("CityWorld.tobj exceeds the read limit")
            payload = archive.read(info)
            if len(payload) != info.file_size:
                raise OverlayFailure("short read for CityWorld.tobj")
    except zipfile.BadZipFile as error:
        raise OverlayFailure("input is not a valid ZIP archive") from error

    try:
        text = payload.decode("utf-8-sig")
    except UnicodeDecodeError:
        try:
            text = payload.decode("cp1252")
        except UnicodeDecodeError as error:
            raise OverlayFailure(
                "CityWorld.tobj is not UTF-8 or CP1252"
            ) from error

    placements: list[SourcePlacement] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        fields = raw_line.split(",", 6)
        if len(fields) != 7:
            continue
        try:
            values = tuple(float(field.strip()) for field in fields[:6])
        except ValueError:
            continue
        if not all(math.isfinite(value) for value in values):
            raise OverlayFailure(
                f"CityWorld.tobj line {line_number} has non-finite placement"
            )
        object_field = fields[6].strip()
        if not object_field:
            continue
        object_name = object_field.split()[0]
        placements.append(
            SourcePlacement(
                line_number=line_number,
                position=(values[0], values[1], values[2]),
                rotation_degrees=(values[3], values[4], values[5]),
                object_name=object_name,
            )
        )
        if len(placements) > MAX_SOURCE_PLACEMENTS:
            raise OverlayFailure("CityWorld.tobj exceeds the placement limit")
    return tuple(placements)


def neoq_light_candidate_manifest(
    placements: Sequence[SourcePlacement],
    telepoint: tuple[float, float, float],
) -> dict[str, Any]:
    map_counts = {family: 0 for family in NEOQ_LUMINARIA_FAMILIES}
    candidates: list[dict[str, Any]] = []
    candidate_counts = {
        family: 0 for family in NEOQ_LUMINARIA_FAMILIES
    }

    for placement in placements:
        family = placement.object_name
        if family not in map_counts:
            continue
        map_counts[family] += 1
        distance = math.hypot(
            placement.position[0] - telepoint[0],
            placement.position[2] - telepoint[2],
        )
        if distance > NEOQ_RELIGHT_RADIUS_M:
            continue
        candidate_counts[family] += 1
        adapter = NEOQ_LIGHT_ADAPTERS[family]
        candidates.append(
            {
                "adapter": {
                    "coordinate_system": "legacy-odef-local-z-up",
                    "future_object_definition":
                        adapter["future_object_definition"],
                    "light_only_mesh_header": "none",
                    "local_light_position_m": [
                        round(float(value), 9)
                        for value in adapter["local_position_m"]
                    ],
                    "runtime_definition_emitted": False,
                },
                "candidate_id":
                    f"neoq-core-light-line-{placement.line_number:06d}",
                "light": {
                    "color_rgb": [1.0, 0.72, 0.3],
                    "hard_max_range_m": NEOQ_LIGHT_RANGE_LIMIT_M,
                    "representative_lights": 1,
                    "shadow_casting_requested": False,
                    "type": "point",
                },
                "source": {
                    "distance_from_telepoint_m": round(distance, 9),
                    "line": placement.line_number,
                    "object": family,
                    "position_m": [
                        round(float(value), 9)
                        for value in placement.position
                    ],
                    "rotation_degrees": [
                        round(float(value), 9)
                        for value in placement.rotation_degrees
                    ],
                },
            }
        )

    if map_counts != NEOQ_EXPECTED_MAP_FAMILY_COUNTS:
        raise OverlayFailure(
            "authenticated CityWorld luminaria family counts changed: "
            f"expected {NEOQ_EXPECTED_MAP_FAMILY_COUNTS}, found {map_counts}"
        )
    if candidate_counts != NEOQ_EXPECTED_CANDIDATE_FAMILY_COUNTS:
        raise OverlayFailure(
            "authenticated NeoQueretaro core luminaria counts changed: "
            f"expected {NEOQ_EXPECTED_CANDIDATE_FAMILY_COUNTS}, "
            f"found {candidate_counts}"
        )
    expected_candidate_count = sum(
        NEOQ_EXPECTED_CANDIDATE_FAMILY_COUNTS.values()
    )
    if len(candidates) != expected_candidate_count:
        raise OverlayFailure(
            "authenticated NeoQueretaro light candidate total changed"
        )
    candidate_ids = [candidate["candidate_id"] for candidate in candidates]
    if len(set(candidate_ids)) != len(candidate_ids):
        raise OverlayFailure(
            "NeoQueretaro light candidates contain duplicate source lines"
        )

    return {
        "activation": {
            "blockers": [
                "renderer-local-light-budget-policy-unavailable",
                "zero-local-shadow-runtime-contract-unavailable",
                "neoq-fixed-camera-runtime-visual-gate-unavailable",
            ],
            "enabled": False,
            "fail_closed": True,
            "runtime_adapter_definitions_emitted": 0,
            "runtime_candidate_placements_emitted": 0,
            "runtime_point_lights_emitted": 0,
            "status": "blocked",
        },
        "candidate_family_counts": candidate_counts,
        "candidate_poles": len(candidates),
        "candidate_runtime_point_lights": len(candidates),
        "candidates": candidates,
        "format": NEOQ_LIGHT_CANDIDATE_FORMAT,
        "policy_contract": {
            "hard_max_range_m": NEOQ_LIGHT_RANGE_LIMIT_M,
            "maximum_candidate_lights": len(candidates),
            "policy_id": NEOQ_LIGHT_POLICY_ID,
            "required_local_shadow_casters": 0,
            "sampling_strategy":
                "one-bounded-representative-light-per-existing-pole",
        },
        "scope": {
            "map_family_counts": map_counts,
            "radius_m": NEOQ_RELIGHT_RADIUS_M,
            "source_telepoint": NEOQ_RELIGHT_TELEPOINT,
            "source_telepoint_position_m": [
                round(float(value), 9) for value in telepoint
            ],
        },
        "visual_geometry": {
            "duplicate_pole_geometry_emitted": False,
            "existing_cityworld_poles_reused": True,
            "future_adapter_mesh_header": "none",
        },
    }


def authenticate_neoq_light_audit(
    audit: dict[str, Any],
    manifest: dict[str, Any],
) -> list[dict[str, Any]]:
    lighting = audit.get("lighting")
    relight = (
        lighting.get("neoq_core_relight")
        if isinstance(lighting, dict)
        else None
    )
    definitions = (
        lighting.get("object_definitions")
        if isinstance(lighting, dict)
        else None
    )
    if (
        not isinstance(relight, dict)
        or relight.get("format") != NEOQ_RELIGHT_FORMAT
    ):
        raise OverlayFailure(
            "CityWorld audit has no supported NeoQueretaro relight section"
        )
    expected_fields = {
        "activation_enabled": False,
        "candidate_family_counts": manifest["candidate_family_counts"],
        "candidate_poles": manifest["candidate_poles"],
        "candidate_runtime_point_lights":
            manifest["candidate_runtime_point_lights"],
        "hard_max_range_m": NEOQ_LIGHT_RANGE_LIMIT_M,
        "map_family_counts": manifest["scope"]["map_family_counts"],
        "radius_m": NEOQ_RELIGHT_RADIUS_M,
        "source_telepoint": NEOQ_RELIGHT_TELEPOINT,
        "source_telepoint_available": True,
        "source_visual_geometry_replicated": False,
        "source_visual_geometry_reused": True,
        "zero_local_shadow_contract_required": True,
    }
    for field, expected in expected_fields.items():
        if relight.get(field) != expected:
            raise OverlayFailure(
                f"CityWorld NeoQueretaro relight audit field {field!r} "
                "does not match the authenticated candidate derivation"
            )
    pole_definitions = (
        definitions.get("source_pole_definitions")
        if isinstance(definitions, dict)
        else None
    )
    if not isinstance(pole_definitions, list):
        raise OverlayFailure(
            "CityWorld audit has no source-pole definition inventory"
        )
    return pole_definitions


def authenticate_route_anchors(
    placements: Sequence[SourcePlacement],
) -> dict[str, dict[str, Any]]:
    evidence: dict[str, dict[str, Any]] = {}
    for label, expected in (
        ("source", ROUTE_SOURCE_ANCHOR),
        ("destination", ROUTE_DESTINATION_ANCHOR),
    ):
        expected_position = tuple(expected["placement_position_m"])
        expected_rotation = tuple(expected["rotation_degrees"])
        expected_object = str(expected["object"])
        matches = [
            placement
            for placement in placements
            if (
                placement.object_name == expected_object
                and all(
                    abs(actual - authored) <= POSITION_EPSILON
                    for actual, authored in zip(
                        placement.position,
                        expected_position,
                    )
                )
                and all(
                    angular_error_degrees(actual, authored)
                    <= POSITION_EPSILON
                    for actual, authored in zip(
                        placement.rotation_degrees,
                        expected_rotation,
                    )
                )
            )
        ]
        if len(matches) != 1:
            raise OverlayFailure(
                f"expected exactly one authenticated {label} road placement; "
                f"found {len(matches)}"
            )
        match = matches[0]
        evidence[label] = {
            "line_number": match.line_number,
            "member": "CityWorld.tobj",
            "object": match.object_name,
            "position_m": list(match.position),
            "rotation_degrees": list(match.rotation_degrees),
        }
    return evidence


def audit_open_intercity_gap(
    placements: Sequence[SourcePlacement],
) -> dict[str, Any]:
    min_x, max_x, min_z, max_z = ROUTE_OPEN_GAP_BOUNDS_XZ_M
    matches = [
        placement
        for placement in placements
        if (
            min_x < placement.position[0] < max_x
            and min_z < placement.position[2] < max_z
        )
    ]
    if matches:
        first = matches[0]
        raise OverlayFailure(
            "intercity placement-origin gap is no longer empty: "
            f"{first.object_name} at CityWorld.tobj line {first.line_number}"
        )
    return {
        "bounds_xz_m": [min_x, max_x, min_z, max_z],
        "member": "CityWorld.tobj",
        "placement_origin_count": 0,
        "verified": True,
    }


def finite_positive(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise OverlayFailure(f"{label} must be a finite positive number")
    result = float(value)
    if not math.isfinite(result) or result <= 0.0:
        raise OverlayFailure(f"{label} must be a finite positive number")
    return result


def asset_centerline_length(manifest: dict[str, Any], asset_id: str) -> float:
    geometry = manifest.get("geometry")
    if not isinstance(geometry, dict):
        raise OverlayFailure(f"{asset_id} has no geometry contract")
    value = geometry.get(
        "centerline_length_m",
        geometry.get("bridge_length_m"),
    )
    return finite_positive(value, f"{asset_id} centerline length")


def read_runtime_file(
    repository: Path, record: dict[str, Any]
) -> RuntimeFile:
    source_value = record.get("path")
    if not isinstance(source_value, str):
        raise OverlayFailure("compiled output has no repository path")
    source = repository / safe_package_path(source_value)
    source = source.resolve()
    try:
        source.relative_to(repository)
    except ValueError as error:
        raise OverlayFailure("compiled output escapes repository") from error
    package_path = safe_package_path(source.name)
    if source.suffix not in {".mesh", ".material", ".odef"}:
        raise OverlayFailure(
            f"compiled output is not a runtime resource: {source.name}"
        )
    expected_size = record.get("size")
    if (
        isinstance(expected_size, bool)
        or not isinstance(expected_size, int)
        or expected_size < 0
        or expected_size > MAX_RUNTIME_FILE_BYTES
    ):
        raise OverlayFailure(f"invalid checked size for {source.name}")
    payload = source.read_bytes()
    if len(payload) != expected_size:
        raise OverlayFailure(f"checked output size is stale: {source.name}")
    digest = sha256_bytes(payload)
    if digest != record.get("sha256"):
        raise OverlayFailure(f"checked output hash is stale: {source.name}")
    role = record.get("role")
    if not isinstance(role, str) or not role:
        raise OverlayFailure(f"checked output role is invalid: {source.name}")
    return RuntimeFile(
        package_path=package_path,
        repository_path=source_value,
        role=role,
        sha256=digest,
        size=len(payload),
        payload=payload,
    )


def expected_runtime_role_counts(
    manifest: dict[str, Any],
) -> dict[str, int]:
    expected = {
        "material-fallback": 1,
        "render-lod0": 1,
        "render-lod1": 1,
        "render-lod2": 1,
        "terrain-object": 1,
    }
    collision = manifest.get("collision")
    collision_objects = (
        collision.get("objects")
        if isinstance(collision, dict)
        else None
    )
    if not isinstance(collision_objects, list):
        raise OverlayFailure("asset manifest has no collision object contract")
    if not collision_objects:
        asset = manifest.get("asset")
        if (
            not isinstance(asset, dict)
            or asset.get("profile") != "static-visual-v1"
            or collision.get("profile") != "collisionless-visual-v1"
        ):
            raise OverlayFailure(
                "collisionless asset does not use the static visual contract"
            )
    for collision_object in collision_objects:
        role = (
            collision_object.get("role")
            if isinstance(collision_object, dict)
            else None
        )
        if not isinstance(role, str) or not role.startswith("collision-"):
            raise OverlayFailure("asset manifest has an invalid collision role")
        expected[role] = expected.get(role, 0) + 1
    return expected


def prepare_asset(
    repository: Path,
    manifest_relative: str,
    *,
    corridor_module: bool = True,
) -> PreparedAsset:
    manifest_path = (repository / safe_package_path(manifest_relative)).resolve()
    try:
        manifest_path.relative_to(repository)
    except ValueError as error:
        raise OverlayFailure(f"asset manifest escapes repository: {manifest_relative}") from error

    validation = Validator(repository, manifest_path).validate()
    if validation.get("summary", {}).get("valid") is not True:
        diagnostics = validation.get("diagnostics", [])
        codes = sorted(
            {
                item.get("code", "UNKNOWN")
                for item in diagnostics
                if isinstance(item, dict)
            }
        )
        raise OverlayFailure(
            f"asset validation failed for {manifest_relative}: "
            + ", ".join(codes)
        )

    compiler = SceneCompiler(repository, manifest_path)
    compiler.prepare()
    compiled_directory = default_output_directory(compiler)
    checked_report = validate_checked_outputs(compiler, compiled_directory)
    runtime_files = tuple(
        read_runtime_file(repository, record)
        for record in checked_report["outputs"]
    )
    role_counts: dict[str, int] = {}
    for runtime_file in runtime_files:
        role_counts[runtime_file.role] = role_counts.get(runtime_file.role, 0) + 1
    expected_role_counts = expected_runtime_role_counts(compiler.manifest)
    if role_counts != expected_role_counts:
        raise OverlayFailure(
            f"compiled runtime role set is incomplete for {compiler.asset_id}"
        )

    runtime_lights = checked_report.get("runtime_lights")
    if not isinstance(runtime_lights, list):
        raise OverlayFailure(
            f"compiled runtime lights are invalid for {compiler.asset_id}"
        )
    odef = next(item for item in runtime_files if item.role == "terrain-object")
    try:
        odef_text = odef.payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise OverlayFailure(f"{odef.package_path} is not UTF-8") from error
    if odef_text.count("\npointlight ") != len(runtime_lights):
        raise OverlayFailure(
            f"compiled runtime lights are not retained by {odef.package_path}"
        )

    manifest_hash = sha256_regular_file(
        manifest_path,
        max_bytes=4 * 1024 * 1024,
    )
    report_path_value = compiler.manifest["compiled"]["report"]["path"]
    report_path = (repository / safe_package_path(report_path_value)).resolve()
    report_hash = sha256_regular_file(
        report_path,
        max_bytes=4 * 1024 * 1024,
    )
    profile = (
        load_asset_profile(repository, manifest_relative)
        if corridor_module
        else None
    )
    return PreparedAsset(
        asset_id=compiler.asset_id,
        centerline_length_m=(
            asset_centerline_length(
                compiler.manifest,
                compiler.asset_id,
            )
            if corridor_module
            else None
        ),
        manifest_path=manifest_relative,
        profile=profile,
        provenance={
            "asset": compiler.manifest["asset"],
            "checked_compile_report": {
                "path": report_path_value,
                "sha256": report_hash,
            },
            "generator": compiler.manifest["authoring"]["generator"],
            "manifest": {
                "path": manifest_relative,
                "sha256": manifest_hash,
            },
            "runtime_files": [
                {
                    "package_path": item.package_path,
                    "path": item.repository_path,
                    "role": item.role,
                    "sha256": item.sha256,
                    "size": item.size,
                }
                for item in runtime_files
            ],
            "runtime_lights": runtime_lights,
            "validation": {
                "format": validation["format"],
                "summary": validation["summary"],
            },
        },
        runtime_files=runtime_files,
    )


def prepare_assets(repository: Path) -> tuple[PreparedAsset, ...]:
    assets = tuple(
        prepare_asset(repository, manifest)
        for manifest in ASSET_MANIFESTS
    )
    identifiers = [asset.asset_id for asset in assets]
    if len(set(identifiers)) != len(identifiers):
        raise OverlayFailure("asset manifests declare duplicate identifiers")
    if set(identifiers) != set(MODULE_ASSET_IDS):
        raise OverlayFailure("asset manifests do not match the pinned module sequence")
    return assets


def prepare_streetlight_asset(repository: Path) -> PreparedAsset:
    asset = prepare_asset(
        repository,
        LED_STREETLIGHT_MANIFEST,
        corridor_module=False,
    )
    profile = asset.provenance.get("asset", {}).get("profile")
    if (
        asset.asset_id != LED_STREETLIGHT_ASSET_ID
        or profile != "static-visual-v1"
        or asset.centerline_length_m is not None
        or asset.profile is not None
    ):
        raise OverlayFailure("streetlight asset does not match its pinned fixture contract")
    return asset


def tokenize_material_script(
    runtime_file: RuntimeFile,
) -> tuple[MaterialToken, ...]:
    if len(runtime_file.payload) > MAX_MATERIAL_SCRIPT_BYTES:
        raise OverlayFailure(
            f"material script exceeds byte limit: {runtime_file.package_path}"
        )
    try:
        text = runtime_file.payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise OverlayFailure(
            f"material script is not UTF-8: {runtime_file.package_path}"
        ) from error
    if "\x00" in text:
        raise OverlayFailure(
            f"material script contains a null byte: {runtime_file.package_path}"
        )
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    tokens: list[MaterialToken] = []
    index = 0

    def append_token(kind: str, value: str) -> None:
        if len(tokens) >= MAX_MATERIAL_TOKENS:
            raise OverlayFailure(
                f"material script exceeds token limit: "
                f"{runtime_file.package_path}"
            )
        tokens.append(MaterialToken(kind, value))

    while index < len(text):
        character = text[index]
        if character in " \t\f\v":
            index += 1
            continue
        if character == "\n":
            append_token("newline", "\n")
            index += 1
            continue
        if text.startswith("//", index):
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", index):
            comment_end = text.find("*/", index + 2)
            if comment_end < 0:
                raise OverlayFailure(
                    f"unterminated material comment: "
                    f"{runtime_file.package_path}"
                )
            for _ in range(text.count("\n", index, comment_end + 2)):
                append_token("newline", "\n")
            index = comment_end + 2
            continue
        if character in "{}:":
            append_token("symbol", character)
            index += 1
            continue
        if character in "\"'":
            quote = character
            start = index
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                if text[index] == "\n":
                    raise OverlayFailure(
                        f"unterminated material string: "
                        f"{runtime_file.package_path}"
                    )
                index += 1
            else:
                raise OverlayFailure(
                    f"unterminated material string: "
                    f"{runtime_file.package_path}"
                )
            append_token("string", text[start:index])
            continue

        start = index
        while index < len(text):
            if text[index].isspace() or text[index] in "{}:\"'":
                break
            if text.startswith("//", index) or text.startswith("/*", index):
                break
            index += 1
        if index == start:
            raise OverlayFailure(
                f"unsupported material token in "
                f"{runtime_file.package_path}"
            )
        append_token("word", text[start:index])
    return tuple(tokens)


def normalize_material_definition_tokens(
    tokens: Sequence[MaterialToken],
) -> tuple[MaterialToken, ...]:
    following_non_newline: list[MaterialToken | None] = [None] * len(tokens)
    following: MaterialToken | None = None
    for index in range(len(tokens) - 1, -1, -1):
        following_non_newline[index] = following
        if tokens[index].kind != "newline":
            following = tokens[index]

    normalized: list[MaterialToken] = []
    previous: MaterialToken | None = None
    for index, token in enumerate(tokens):
        if token.kind != "newline":
            normalized.append(token)
            previous = token
            continue
        following = following_non_newline[index]
        if (
            previous is None
            or following is None
            or (normalized and normalized[-1].kind == "newline")
            or previous.value in "{}"
            or following.value in "{}"
        ):
            continue
        normalized.append(MaterialToken("newline", "\n"))
    while normalized and normalized[-1].kind == "newline":
        normalized.pop()
    return tuple(normalized)


def parse_material_definitions(
    runtime_file: RuntimeFile,
) -> tuple[MaterialDefinition, ...]:
    tokens = tokenize_material_script(runtime_file)
    definitions: list[MaterialDefinition] = []
    cursor = 0
    while cursor < len(tokens):
        while cursor < len(tokens) and tokens[cursor].kind == "newline":
            cursor += 1
        if cursor == len(tokens):
            break
        start = cursor
        if tokens[cursor] != MaterialToken("word", "material"):
            raise OverlayFailure(
                f"unsupported top-level material statement in "
                f"{runtime_file.package_path}: {tokens[cursor].value!r}"
            )
        cursor += 1
        while cursor < len(tokens) and tokens[cursor].kind == "newline":
            cursor += 1
        if (
            cursor == len(tokens)
            or tokens[cursor].kind != "word"
            or MATERIAL_NAME_PATTERN.fullmatch(tokens[cursor].value) is None
        ):
            raise OverlayFailure(
                f"material declaration has an invalid name in "
                f"{runtime_file.package_path}"
            )
        material_name = tokens[cursor].value
        cursor += 1
        while cursor < len(tokens) and tokens[cursor].value != "{":
            if tokens[cursor].value == "}":
                raise OverlayFailure(
                    f"material declaration has no opening brace: "
                    f"{material_name}"
                )
            cursor += 1
        if cursor == len(tokens):
            raise OverlayFailure(
                f"material declaration has no opening brace: "
                f"{material_name}"
            )

        depth = 0
        while cursor < len(tokens):
            if tokens[cursor].value == "{":
                depth += 1
            elif tokens[cursor].value == "}":
                depth -= 1
                if depth < 0:
                    raise OverlayFailure(
                        f"material declaration has an unmatched brace: "
                        f"{material_name}"
                    )
                if depth == 0:
                    cursor += 1
                    break
            cursor += 1
        if depth != 0:
            raise OverlayFailure(
                f"material declaration has an unterminated block: "
                f"{material_name}"
            )
        definitions.append(
            MaterialDefinition(
                name=material_name,
                origin=runtime_file.package_path,
                tokens=normalize_material_definition_tokens(
                    tokens[start:cursor]
                ),
            )
        )
        if len(definitions) > MAX_MATERIAL_DEFINITIONS:
            raise OverlayFailure(
                f"material definition count exceeds limit in "
                f"{runtime_file.package_path}"
            )
    if not definitions:
        raise OverlayFailure(
            f"material script defines no materials: "
            f"{runtime_file.package_path}"
        )
    return tuple(definitions)


def render_material_definition(tokens: Sequence[MaterialToken]) -> str:
    lines: list[str] = []
    line_tokens: list[str] = []
    depth = 0

    def flush_line() -> None:
        if line_tokens:
            lines.append("  " * depth + " ".join(line_tokens))
            line_tokens.clear()

    for token in tokens:
        if token.kind == "newline":
            flush_line()
        elif token.value == "{":
            flush_line()
            lines.append("  " * depth + "{")
            depth += 1
        elif token.value == "}":
            flush_line()
            depth -= 1
            if depth < 0:
                raise OverlayFailure(
                    "internal material merge produced an invalid brace depth"
                )
            lines.append("  " * depth + "}")
        else:
            line_tokens.append(token.value)
    flush_line()
    if depth != 0:
        raise OverlayFailure(
            "internal material merge produced an invalid brace depth"
        )
    return "\n".join(lines)


def merge_material_scripts(
    assets: Sequence[PreparedAsset],
) -> bytes:
    material_files: list[RuntimeFile] = []
    for asset in assets:
        for runtime_file in asset.runtime_files:
            is_material_path = runtime_file.package_path.endswith(".material")
            is_material_role = runtime_file.role == "material-fallback"
            if is_material_path != is_material_role:
                raise OverlayFailure(
                    f"material runtime role/path mismatch: "
                    f"{runtime_file.package_path}"
                )
            if is_material_role:
                material_files.append(runtime_file)
    if not material_files:
        raise OverlayFailure("overlay assets define no material scripts")

    definitions: dict[str, MaterialDefinition] = {}
    folded_names: dict[str, str] = {}
    for runtime_file in sorted(
        material_files,
        key=lambda item: (
            item.package_path.casefold(),
            item.package_path,
            item.sha256,
        ),
    ):
        for definition in parse_material_definitions(runtime_file):
            folded = definition.name.casefold()
            prior_spelling = folded_names.get(folded)
            if prior_spelling is not None and prior_spelling != definition.name:
                raise OverlayFailure(
                    "material names differ only by case: "
                    f"{prior_spelling!r} and {definition.name!r}"
                )
            folded_names[folded] = definition.name
            prior = definitions.get(definition.name)
            if prior is None:
                definitions[definition.name] = definition
            elif prior.tokens != definition.tokens:
                raise OverlayFailure(
                    f"conflicting material definition "
                    f"{definition.name!r} in {prior.origin} and "
                    f"{definition.origin}"
                )
    if len(definitions) > MAX_MATERIAL_DEFINITIONS:
        raise OverlayFailure(
            "merged material definition count exceeds limit"
        )

    rendered = [
        "// Generated by ror-cityworld-local-overlay-v4.",
        "// Canonical merged material script; duplicate definitions removed.",
        "",
    ]
    for name in sorted(definitions):
        rendered.append(render_material_definition(definitions[name].tokens))
        rendered.append("")
    payload = "\n".join(rendered).encode("utf-8")
    if len(payload) > MAX_MATERIAL_SCRIPT_BYTES:
        raise OverlayFailure("merged material script exceeds byte limit")
    return payload


def tool_provenance(
    repository: Path,
    assets: Sequence[PreparedAsset],
) -> list[dict[str, str]]:
    paths = set(TOOL_PATHS)
    for asset in assets:
        generator = asset.provenance.get("generator", {})
        if isinstance(generator, dict):
            path = generator.get("path")
            if isinstance(path, str):
                paths.add(path)
            dependencies = generator.get("dependencies", [])
            if isinstance(dependencies, list):
                for dependency in dependencies:
                    if isinstance(dependency, dict) and isinstance(
                        dependency.get("path"), str
                    ):
                        paths.add(dependency["path"])
    records = []
    for relative in sorted(paths):
        path = (repository / safe_package_path(relative)).resolve()
        records.append(
            {
                "path": portable_repository_path(repository, path),
                "sha256": sha256_regular_file(
                    path,
                    max_bytes=16 * 1024 * 1024,
                ),
            }
        )
    return records


def normalized_degrees(value: float) -> float:
    result = math.degrees(
        math.atan2(math.sin(math.radians(value)), math.cos(math.radians(value)))
    )
    return 0.0 if abs(result) < 5e-10 else result


def angular_error_degrees(left: float, right: float) -> float:
    return abs(normalized_degrees(left - right))


def smoothstep(value: float) -> float:
    if not math.isfinite(value) or not 0.0 <= value <= 1.0:
        raise OverlayFailure("smoothstep input must be finite and within [0, 1]")
    return value * value * (3.0 - 2.0 * value)


def cubic_bezier(
    control_points: Sequence[tuple[float, float]],
    parameter: float,
) -> tuple[float, float]:
    if len(control_points) != 4:
        raise OverlayFailure("intercity route requires four Bezier control points")
    t = float(parameter)
    if not math.isfinite(t) or not 0.0 <= t <= 1.0:
        raise OverlayFailure("Bezier parameter must be finite and within [0, 1]")
    inverse = 1.0 - t
    weights = (
        inverse * inverse * inverse,
        3.0 * inverse * inverse * t,
        3.0 * inverse * t * t,
        t * t * t,
    )
    return (
        sum(weights[index] * control_points[index][0] for index in range(4)),
        sum(weights[index] * control_points[index][1] for index in range(4)),
    )


def cubic_bezier_derivative(
    control_points: Sequence[tuple[float, float]],
    parameter: float,
) -> tuple[float, float]:
    if len(control_points) != 4:
        raise OverlayFailure("intercity route requires four Bezier control points")
    t = float(parameter)
    if not math.isfinite(t) or not 0.0 <= t <= 1.0:
        raise OverlayFailure("Bezier parameter must be finite and within [0, 1]")
    inverse = 1.0 - t
    return (
        3.0
        * (
            inverse * inverse
            * (control_points[1][0] - control_points[0][0])
            + 2.0
            * inverse
            * t
            * (control_points[2][0] - control_points[1][0])
            + t * t * (control_points[3][0] - control_points[2][0])
        ),
        3.0
        * (
            inverse * inverse
            * (control_points[1][1] - control_points[0][1])
            + 2.0
            * inverse
            * t
            * (control_points[2][1] - control_points[1][1])
            + t * t * (control_points[3][1] - control_points[2][1])
        ),
    )


def route_control_points(
    source: tuple[float, float, float],
    destination: tuple[float, float, float],
) -> tuple[tuple[float, float], ...]:
    if destination[0] - source[0] <= 2.0 * ROUTE_TANGENT_HANDLE_M:
        raise OverlayFailure("intercity road anchors are too close for safe approaches")
    if destination[2] <= source[2]:
        raise OverlayFailure("intercity road anchors have an unsupported ordering")
    return (
        (source[0], source[2]),
        (source[0] + ROUTE_TANGENT_HANDLE_M, source[2]),
        (destination[0] - ROUTE_TANGENT_HANDLE_M, destination[2]),
        (destination[0], destination[2]),
    )


def route_arc_table(
    control_points: Sequence[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    if (
        isinstance(ROUTE_ARC_TABLE_STEPS, bool)
        or not isinstance(ROUTE_ARC_TABLE_STEPS, int)
        or ROUTE_ARC_TABLE_STEPS < 1024
        or ROUTE_ARC_TABLE_STEPS > 65536
    ):
        raise OverlayFailure("route arc table resolution is unsafe")
    result: list[tuple[float, float]] = [(0.0, 0.0)]
    previous = cubic_bezier(control_points, 0.0)
    accumulated = 0.0
    for index in range(1, ROUTE_ARC_TABLE_STEPS + 1):
        parameter = index / ROUTE_ARC_TABLE_STEPS
        current = cubic_bezier(control_points, parameter)
        segment = math.hypot(
            current[0] - previous[0],
            current[1] - previous[1],
        )
        if not math.isfinite(segment) or segment <= 0.0:
            raise OverlayFailure("intercity route is not strictly traversable")
        accumulated += segment
        result.append((parameter, accumulated))
        previous = current
    if (
        not math.isfinite(accumulated)
        or accumulated
        <= 2.0 * (ROUTE_GROUND_LEAD_M + ROUTE_RAMP_LENGTH_M)
    ):
        raise OverlayFailure("intercity route is too short for two safe approaches")
    return tuple(result)


def parameter_at_station(
    arc_table: Sequence[tuple[float, float]],
    station_m: float,
) -> float:
    if len(arc_table) < 2:
        raise OverlayFailure("route arc table is incomplete")
    total = arc_table[-1][1]
    station = float(station_m)
    if not math.isfinite(station) or not 0.0 <= station <= total:
        raise OverlayFailure("route station lies outside the corridor")
    low = 0
    high = len(arc_table) - 1
    while low + 1 < high:
        middle = (low + high) // 2
        if arc_table[middle][1] < station:
            low = middle
        else:
            high = middle
    first_parameter, first_station = arc_table[low]
    second_parameter, second_station = arc_table[high]
    if station == first_station:
        return first_parameter
    if station == second_station:
        return second_parameter
    span = second_station - first_station
    if span <= 0.0:
        raise OverlayFailure("route arc table is not strictly increasing")
    blend = (station - first_station) / span
    return first_parameter + blend * (second_parameter - first_parameter)


def route_elevation(
    station_m: float,
    total_length_m: float,
    road_y_m: float,
    surface_offset_m: float,
) -> float:
    station = float(station_m)
    total = float(total_length_m)
    road_y = float(road_y_m)
    surface_offset = float(surface_offset_m)
    if not all(
        math.isfinite(value)
        for value in (station, total, road_y, surface_offset)
    ):
        raise OverlayFailure("route elevation inputs must be finite")
    if not 0.0 <= station <= total:
        raise OverlayFailure("route elevation station lies outside the corridor")

    ascent_start = ROUTE_GROUND_LEAD_M
    ascent_end = ascent_start + ROUTE_RAMP_LENGTH_M
    descent_start = total - ascent_end
    descent_end = total - ascent_start
    if station < ascent_start:
        baseline = road_y + surface_offset * smoothstep(
            station / ROUTE_GROUND_LEAD_M
        )
    elif station > descent_end:
        baseline = road_y + surface_offset * smoothstep(
            (total - station) / ROUTE_GROUND_LEAD_M
        )
    else:
        baseline = road_y + surface_offset

    if station <= ascent_start or station >= descent_end:
        return baseline
    if station < ascent_end:
        progress = (station - ascent_start) / ROUTE_RAMP_LENGTH_M
        return baseline + ROUTE_DECK_CLEARANCE_M * smoothstep(progress)
    if station <= descent_start:
        return baseline + ROUTE_DECK_CLEARANCE_M
    progress = (station - descent_start) / ROUTE_RAMP_LENGTH_M
    return baseline + ROUTE_DECK_CLEARANCE_M * (1.0 - smoothstep(progress))


def route_stations(total_length_m: float) -> tuple[float, ...]:
    total = float(total_length_m)
    if not math.isfinite(total) or total <= 0.0:
        raise OverlayFailure("route length must be finite and positive")
    values = {
        0.0,
        total,
        ROUTE_GROUND_LEAD_M,
        ROUTE_GROUND_LEAD_M + ROUTE_RAMP_LENGTH_M,
        total - ROUTE_GROUND_LEAD_M,
        total - ROUTE_GROUND_LEAD_M - ROUTE_RAMP_LENGTH_M,
    }
    count = int(math.floor(total / ROUTE_SAMPLE_SPACING_M))
    values.update(
        index * ROUTE_SAMPLE_SPACING_M
        for index in range(1, count + 1)
        if index * ROUTE_SAMPLE_SPACING_M < total
    )
    stations = tuple(sorted(values))
    if any(
        second - first > ROUTE_SAMPLE_SPACING_M + 1e-6
        for first, second in zip(stations, stations[1:])
    ):
        raise OverlayFailure("route sampling exceeds the support-spacing limit")
    return stations


def build_intercity_route(
    *,
    source: tuple[float, float, float],
    destination: tuple[float, float, float],
    surface_offset_m: float,
) -> tuple[tuple[ProceduralRoutePoint, ...], dict[str, Any]]:
    if (
        isinstance(surface_offset_m, bool)
        or not isinstance(surface_offset_m, (int, float))
        or not math.isfinite(float(surface_offset_m))
        or not MIN_SURFACE_OFFSET_M
        <= float(surface_offset_m)
        <= MAX_SURFACE_OFFSET_M
    ):
        raise OverlayFailure(
            "surface offset must be finite and between "
            f"{MIN_SURFACE_OFFSET_M:g} and {MAX_SURFACE_OFFSET_M:g} metres"
        )
    if abs(source[1] - destination[1]) > POSITION_EPSILON:
        raise OverlayFailure("intercity road anchors must share a road elevation")
    control_points = route_control_points(source, destination)
    arc_table = route_arc_table(control_points)
    core_length = arc_table[-1][1]
    stations = route_stations(core_length)
    road_y = source[1]
    surface_y = road_y + float(surface_offset_m)
    destination_taper_grade = (
        1.5 * abs(float(surface_offset_m)) / ROUTE_GROUND_LEAD_M
    )
    source_apron_length = source[0] - ROUTE_SOURCE_APRON_START_X_M
    source_apron_rise_length = (
        ROUTE_SOURCE_APRON_RISE_X_M
        - ROUTE_SOURCE_APRON_START_X_M
    )
    source_clearance_y = (
        ROUTE_SOURCE_LEGACY_CURB_TOP_Y_M
        + ROUTE_SOURCE_CURB_CLEARANCE_M
    )
    source_apron_grade = (
        source_clearance_y
        - ROUTE_SOURCE_LEGACY_ROAD_SURFACE_Y_M
    ) / source_apron_rise_length
    connection_taper_grade = max(
        destination_taper_grade,
        source_apron_grade,
    )
    if (
        source_apron_length <= source_apron_rise_length
        or source_apron_rise_length <= 0.0
        or ROUTE_SOURCE_APRON_RISE_X_M >= source[0]
    ):
        raise OverlayFailure("Penguinville curb apron dimensions are invalid")
    if connection_taper_grade > ROUTE_MAX_CONNECTION_TAPER_GRADE:
        raise OverlayFailure(
            "road connection exceeds the safe connection-taper grade"
        )
    points: list[ProceduralRoutePoint] = [
        ProceduralRoutePoint(
            station_m=0.0,
            x=ROUTE_SOURCE_APRON_START_X_M,
            y=ROUTE_SOURCE_LEGACY_ROAD_SURFACE_Y_M,
            z=source[2],
            yaw_degrees=ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES,
            road_type="flat",
            width_m=ROUTE_WIDTH_M,
            border_width_m=ROUTE_FLAT_BORDER_WIDTH_M,
            border_height_m=ROUTE_FLAT_BORDER_HEIGHT_M,
        ),
        ProceduralRoutePoint(
            station_m=source_apron_rise_length,
            x=ROUTE_SOURCE_APRON_RISE_X_M,
            y=source_clearance_y,
            z=source[2],
            yaw_degrees=ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES,
            road_type="flat",
            width_m=ROUTE_WIDTH_M,
            border_width_m=ROUTE_FLAT_BORDER_WIDTH_M,
            border_height_m=ROUTE_FLAT_BORDER_HEIGHT_M,
        ),
    ]
    for station in stations:
        parameter = parameter_at_station(arc_table, station)
        x, z = cubic_bezier(control_points, parameter)
        derivative_x, derivative_z = cubic_bezier_derivative(
            control_points,
            parameter,
        )
        derivative_length = math.hypot(derivative_x, derivative_z)
        if not math.isfinite(derivative_length) or derivative_length <= 0.0:
            raise OverlayFailure("intercity route has an invalid tangent")
        # RoR's procedural-road cross-section is local +Z. Positive OGRE yaw
        # rotates +Z toward +X, so the authored rotation is the negative of
        # the mathematical XZ tangent angle.
        yaw = -math.degrees(math.atan2(derivative_z, derivative_x))
        road_type = (
            "flat"
            if (
                station <= ROUTE_GROUND_LEAD_M
                or station >= core_length - ROUTE_GROUND_LEAD_M
            )
            else "bridge"
        )
        elevation = route_elevation(
            station,
            core_length,
            road_y,
            float(surface_offset_m),
        )
        if station < ROUTE_GROUND_LEAD_M:
            elevation += (
                source_clearance_y - road_y
            ) * (
                1.0
                - smoothstep(station / ROUTE_GROUND_LEAD_M)
            )
        point = ProceduralRoutePoint(
            station_m=source_apron_length + station,
            x=x,
            y=elevation,
            z=z,
            yaw_degrees=normalized_degrees(yaw),
            road_type=road_type,
            width_m=ROUTE_WIDTH_M,
            border_width_m=(
                ROUTE_FLAT_BORDER_WIDTH_M
                if road_type == "flat"
                else ROUTE_BRIDGE_BORDER_WIDTH_M
            ),
            border_height_m=(
                ROUTE_FLAT_BORDER_HEIGHT_M
                if road_type == "flat"
                else ROUTE_BRIDGE_BORDER_HEIGHT_M
            ),
        )
        if points and point.x <= points[-1].x:
            raise OverlayFailure("intercity route must leave both city envelopes")
        points.append(point)

    total_length = source_apron_length + core_length
    source_surface_position = (
        ROUTE_SOURCE_APRON_START_X_M,
        ROUTE_SOURCE_LEGACY_ROAD_SURFACE_Y_M,
        source[2],
    )
    source_gap = math.dist(
        (points[0].x, points[0].y, points[0].z),
        source_surface_position,
    )
    destination_gap = math.dist(
        (points[-1].x, points[-1].y, points[-1].z),
        destination,
    )
    if source_gap > POSITION_EPSILON or destination_gap > POSITION_EPSILON:
        raise OverlayFailure("intercity route does not close against its road anchors")
    if any(
        point.x < ROUTE_SOURCE_APRON_START_X_M - POSITION_EPSILON
        or point.x > destination[0] + POSITION_EPSILON
        for point in points
    ):
        raise OverlayFailure("intercity route enters an existing city envelope")

    support_points = [
        point
        for point in points
        if point.road_type == "bridge"
        and point.y - surface_y >= 1.0
    ]
    bridge_spacing = max(
        (
            second.station_m - first.station_m
            for first, second in zip(support_points, support_points[1:])
        ),
        default=0.0,
    )
    ramp_grade = 1.5 * ROUTE_DECK_CLEARANCE_M / ROUTE_RAMP_LENGTH_M
    sampled_max_grade = max(
        (
            abs(second.y - first.y)
            / (second.station_m - first.station_m)
            for first, second in zip(points, points[1:])
        ),
        default=0.0,
    )
    straight_distance = math.hypot(
        destination[0] - ROUTE_SOURCE_APRON_START_X_M,
        destination[2] - source[2],
    )
    source_heading_error = angular_error_degrees(
        points[0].yaw_degrees,
        ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES,
    )
    destination_heading_error = angular_error_degrees(
        points[-1].yaw_degrees,
        ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES,
    )
    report = {
        "connection": {
            "destination_heading_error_degrees": round(
                destination_heading_error,
                9,
            ),
            "destination_position_gap_m": round(destination_gap, 9),
            "source_heading_error_degrees": round(
                source_heading_error,
                9,
            ),
            "source_position_gap_m": round(source_gap, 9),
        },
        "control_points_xz_m": [
            [round(x, 9), round(z, 9)]
            for x, z in control_points
        ],
        "covered_centerline_length_m": round(total_length, 9),
        "destination": {
            **ROUTE_DESTINATION_ANCHOR,
            "position_m": [
                round(destination[0], 9),
                round(destination[1], 9),
                round(destination[2], 9),
            ],
        },
        "format": "ror-cityworld-intercity-corridor-v3",
        "obstacle_avoidance": {
            "derivation":
                "curb-clearing-source-overlap-then-strictly-monotonic-x",
            "destination_city_min_x_m": round(destination[0], 9),
            "source_city_max_x_m": round(source[0], 9),
            "centerline_monotonic_x": True,
            "intentional_source_overlap_m": round(
                source_apron_length,
                9,
            ),
        },
        "profile": {
            "connection_surface_y_m": round(road_y, 9),
            "connection_taper_grade": round(connection_taper_grade, 9),
            "connection_taper_length_m": ROUTE_GROUND_LEAD_M,
            "deck_clearance_m": ROUTE_DECK_CLEARANCE_M,
            "flat_lead_length_m": ROUTE_GROUND_LEAD_M,
            "maximum_grade": round(
                max(ramp_grade, connection_taper_grade),
                9,
            ),
            "ramp_length_m": ROUTE_RAMP_LENGTH_M,
            "rotation_convention":
                "ogre-yaw-local-plus-z-cross-section",
            "sampled_maximum_grade": round(sampled_max_grade, 9),
            "sample_spacing_limit_m": ROUTE_SAMPLE_SPACING_M,
            "surface_offset_m": round(float(surface_offset_m), 9),
            "surface_y_m": round(surface_y, 9),
            "width_m": ROUTE_WIDTH_M,
        },
        "remaining_straight_line_distance_m": round(
            math.hypot(
                points[-1].x - destination[0],
                points[-1].z - destination[2],
            ),
            9,
        ),
        "source": {
            **ROUTE_SOURCE_ANCHOR,
            "position_m": [
                round(source_surface_position[0], 9),
                round(source_surface_position[1], 9),
                round(source_surface_position[2], 9),
            ],
            "apron": {
                "collision_authority":
                    "native-procedural-road-v3",
                "curb_clearance_m": round(
                    ROUTE_SOURCE_CURB_CLEARANCE_M,
                    9,
                ),
                "curb_top_y_m": round(
                    ROUTE_SOURCE_LEGACY_CURB_TOP_Y_M,
                    9,
                ),
                "legacy_collision_mesh":
                    "troadavenuesidewalkbox.mesh",
                "legacy_road_surface_y_m": round(
                    ROUTE_SOURCE_LEGACY_ROAD_SURFACE_Y_M,
                    9,
                ),
                "overlap_length_m": round(
                    source_apron_length,
                    9,
                ),
                "plateau_y_m": round(source_clearance_y, 9),
                "rise_length_m": round(
                    source_apron_rise_length,
                    9,
                ),
                "surface_continuous": True,
            },
        },
        "supports": {
            "enabled": True,
            "maximum_station_spacing_m": round(bridge_spacing, 9),
            "requested_count": len(support_points),
            "stations_m": [
                round(point.station_m, 9)
                for point in support_points
            ],
            "style": "ror-native-procedural-bridge-pillar-v1",
            "terrain_contact_resolved_at_runtime": True,
        },
        "target_distance_m": round(straight_distance, 9),
        "waypoints": [
            {
                "index": index,
                "position_m": [
                    round(point.x, 9),
                    round(point.y, 9),
                    round(point.z, 9),
                ],
                "road_type": point.road_type,
                "station_m": round(point.station_m, 9),
                "yaw_degrees": round(point.yaw_degrees, 9),
            }
            for index, point in enumerate(points)
        ],
    }
    return tuple(points), report


def build_streetlight_placements(
    points: Sequence[ProceduralRoutePoint],
) -> tuple[tuple[TerrainObjectPlacement, ...], dict[str, Any]]:
    if len(points) < 2:
        raise OverlayFailure("streetlight placement requires a complete route")
    bridge_points = [
        point for point in points if point.road_type == "bridge"
    ]
    if not bridge_points:
        raise OverlayFailure("streetlight placement requires a raised bridge")
    deck_y_m = max(point.y for point in bridge_points)
    deck_points = [
        point
        for point in bridge_points
        if abs(point.y - deck_y_m) <= POSITION_EPSILON
    ]
    if len(deck_points) < 2:
        raise OverlayFailure("raised bridge deck plateau is incomplete")
    full_deck_start_m = (
        deck_points[0].station_m
        + ROUTE_STREETLIGHT_DECK_MARGIN_M
    )
    full_deck_end_m = (
        deck_points[-1].station_m
        - ROUTE_STREETLIGHT_DECK_MARGIN_M
    )
    selected_points: list[ProceduralRoutePoint] = []
    for point in points:
        station_multiple = round(
            (
                point.station_m
                - full_deck_start_m
            )
            / ROUTE_STREETLIGHT_SPACING_M
        )
        aligned_station = (
            full_deck_start_m
            + station_multiple * ROUTE_STREETLIGHT_SPACING_M
        )
        if (
            full_deck_start_m - POSITION_EPSILON
            <= point.station_m
            <= full_deck_end_m + POSITION_EPSILON
            and abs(point.station_m - aligned_station) <= POSITION_EPSILON
        ):
            if point.road_type != "bridge":
                raise OverlayFailure(
                    "streetlight station is not on the raised bridge deck"
                )
            selected_points.append(point)
    if not selected_points:
        raise OverlayFailure("intercity route has no valid streetlight stations")
    if any(
        abs(second.station_m - first.station_m - ROUTE_STREETLIGHT_SPACING_M)
        > POSITION_EPSILON
        for first, second in zip(selected_points, selected_points[1:])
    ):
        raise OverlayFailure("streetlight stations are not evenly spaced")

    placements: list[TerrainObjectPlacement] = []
    for index, point in enumerate(selected_points):
        yaw_radians = math.radians(point.yaw_degrees)
        left_normal_x = math.sin(yaw_radians)
        left_normal_z = math.cos(yaw_radians)
        lateral_offset_m = (
            point.width_m / 2.0 + point.border_width_m / 2.0
        )
        mount_y = point.y + point.border_height_m
        station_label = f"{int(round(point.station_m)):04d}"
        side = "left" if index % 2 == 0 else "right"
        multiplier = 1.0 if side == "left" else -1.0
        yaw_offset = 0.0 if side == "left" else 180.0
        placements.append(
            TerrainObjectPlacement(
                station_m=point.station_m,
                side=side,
                x=point.x + multiplier * left_normal_x * lateral_offset_m,
                y=mount_y,
                z=point.z + multiplier * left_normal_z * lateral_offset_m,
                yaw_degrees=normalized_degrees(
                    point.yaw_degrees + yaw_offset
                ),
                asset_id=LED_STREETLIGHT_ASSET_ID,
                instance_name=(
                    f"cityworld_next_led_{station_label}_{side}"
                ),
            )
        )

    if len({item.instance_name for item in placements}) != len(placements):
        raise OverlayFailure("streetlight instance names are not unique")
    station_records = []
    for point, placement in zip(selected_points, placements):
        station_records.append(
            {
                "centerline_position_m": [
                    round(point.x, 9),
                    round(point.y, 9),
                    round(point.z, 9),
                ],
                "instance_name": placement.instance_name,
                "placement_position_m": [
                    round(placement.x, 9),
                    round(placement.y, 9),
                    round(placement.z, 9),
                ],
                "rotation_degrees": [
                    0.0,
                    round(placement.yaw_degrees, 9),
                    0.0,
                ],
                "side": placement.side,
                "station_m": round(point.station_m, 9),
            }
        )

    lateral_offset_m = (
        selected_points[0].width_m / 2.0
        + selected_points[0].border_width_m / 2.0
    )
    report = {
        "arm_orientation": "alternating-inward-over-roadway",
        "asset_id": LED_STREETLIGHT_ASSET_ID,
        "collision_authority": "native-procedural-road-v3",
        "format": "ror-cityworld-streetlight-placement-v1",
        "instance_count": len(placements),
        "lateral_mount_offset_m": round(lateral_offset_m, 9),
        "mount_elevation_above_road_m": round(
            selected_points[0].border_height_m,
            9,
        ),
        "paired": False,
        "runtime_point_lights_per_instance": 1,
        "station_count": len(selected_points),
        "station_spacing_m": ROUTE_STREETLIGHT_SPACING_M,
        "stations": station_records,
    }
    return tuple(placements), report


def procedural_route_text(points: Sequence[ProceduralRoutePoint]) -> str:
    if len(points) < 2:
        raise OverlayFailure("intercity route requires at least two waypoints")
    lines = [
        "// Generated full Penguinville-to-NeoQueretaro intercity road.",
        "// The Penguinville overlap apron clears the legacy curb.",
        "// The NeoQueretaro endpoint is an authenticated perimeter road.",
        "// Bridge points request terrain-reaching pillars.",
        "begin_procedural_roads",
        "    smoothing_num_splits 0",
        "    collision_enabled true",
    ]
    for point in points:
        lines.append(
            "    "
            + ", ".join(
                (
                    stable_float(point.x),
                    stable_float(point.y),
                    stable_float(point.z),
                    "0",
                    stable_float(point.yaw_degrees),
                    "0",
                    stable_float(point.width_m),
                    stable_float(point.border_width_m),
                    stable_float(point.border_height_m),
                    point.road_type,
                )
            )
        )
    lines.append("end_procedural_roads")
    return "\n".join(lines) + "\n"


def terrain_object_placement_text(
    placements: Sequence[TerrainObjectPlacement],
) -> str:
    if not placements:
        return ""
    lines = [
        "",
        "// Blender-authored bridge fixtures mounted outside the carriageway.",
    ]
    for placement in placements:
        lines.append(
            ", ".join(
                (
                    stable_float(placement.x),
                    stable_float(placement.y),
                    stable_float(placement.z),
                    "0",
                    stable_float(placement.yaw_degrees),
                    "0",
                    (
                        f"{placement.asset_id} - "
                        f"{placement.instance_name}"
                    ),
                )
            )
        )
    return "\n".join(lines) + "\n"


def solve_segment(
    assets: Sequence[PreparedAsset],
    source: tuple[float, float, float],
    destination: tuple[float, float, float],
    surface_offset_m: float,
) -> tuple[
    tuple[AssetProfile, ...],
    tuple[Placement, ...],
    float,
    dict[str, Any],
]:
    if (
        isinstance(surface_offset_m, bool)
        or not isinstance(surface_offset_m, (int, float))
        or not math.isfinite(float(surface_offset_m))
        or not MIN_SURFACE_OFFSET_M
        <= float(surface_offset_m)
        <= MAX_SURFACE_OFFSET_M
    ):
        raise OverlayFailure(
            "surface offset must be finite and between "
            f"{MIN_SURFACE_OFFSET_M:g} and {MAX_SURFACE_OFFSET_M:g} metres"
        )
    by_id = {asset.asset_id: asset for asset in assets}
    try:
        ordered_assets = tuple(by_id[identifier] for identifier in MODULE_ASSET_IDS)
    except KeyError as error:
        raise OverlayFailure(
            f"module sequence asset is missing: {error.args[0]}"
        ) from error
    if any(
        asset.profile is None or asset.centerline_length_m is None
        for asset in ordered_assets
    ):
        raise OverlayFailure("module sequence contains a non-corridor asset")
    profiles = tuple(asset.profile for asset in ordered_assets)
    target_dx = destination[0] - source[0]
    target_dz = destination[2] - source[2]
    target_distance = math.hypot(target_dx, target_dz)
    if not math.isfinite(target_distance) or target_distance <= 0.0:
        raise OverlayFailure("source and destination telepoints must be distinct")
    target_heading = math.degrees(math.atan2(target_dx, target_dz))

    probe = solve_corridor(
        profiles,
        entry_x=0.0,
        entry_z=0.0,
        heading_degrees=0.0,
    )
    heading_change = probe[-1].exit_heading_degrees
    initial_heading = normalized_degrees(target_heading - heading_change)
    placements = solve_corridor(
        profiles,
        entry_x=source[0],
        entry_z=source[2],
        heading_degrees=initial_heading,
    )
    for first, second in zip(placements, placements[1:]):
        if (
            first.exit_x != second.entry_x
            or first.exit_z != second.entry_z
            or first.exit_heading_degrees
            != second.entry_heading_degrees
        ):
            raise OverlayFailure("corridor solver produced an inexact module seam")
    final_heading_error = angular_error_degrees(
        placements[-1].exit_heading_degrees,
        target_heading,
    )
    if final_heading_error > 1e-6:
        raise OverlayFailure("final corridor tangent does not point at destination")

    surface_y = source[1] + float(surface_offset_m)
    if not math.isfinite(surface_y):
        raise OverlayFailure("derived surface elevation is not finite")
    base_report = corridor_report(
        profiles,
        placements,
        surface_y=surface_y,
    )
    module_lengths = [
        ordered_assets[index].centerline_length_m
        for index in range(len(ordered_assets))
    ]
    for index, module in enumerate(base_report["modules"]):
        module["centerline_length_m"] = round(module_lengths[index], 9)
        module["sequence_index"] = index
    covered_length = sum(module_lengths)
    remaining_distance = math.hypot(
        destination[0] - placements[-1].exit_x,
        destination[2] - placements[-1].exit_z,
    )
    base_report.update(
        {
            "covered_centerline_length_m": round(covered_length, 9),
            "destination": {
                "name": DESTINATION_TELEPOINT,
                "position_m": [round(value, 9) for value in destination],
            },
            "heading": {
                "derivation": "degrees(atan2(destination_x-source_x,destination_z-source_z))",
                "final_error_degrees": round(final_heading_error, 9),
                "initial_heading_degrees": round(initial_heading, 9),
                "module_heading_change_degrees": round(heading_change, 9),
                "target_heading_degrees": round(target_heading, 9),
            },
            "remaining_straight_line_distance_m": round(
                remaining_distance,
                9,
            ),
            "source": {
                "name": SOURCE_TELEPOINT,
                "position_m": [round(value, 9) for value in source],
            },
            "surface": {
                "offset_m": round(float(surface_offset_m), 9),
                "source_y_m": round(source[1], 9),
                "y_m": round(surface_y, 9),
            },
            "target_distance_m": round(target_distance, 9),
        }
    )
    return profiles, placements, surface_y, base_report


def terrain_descriptor(
    audit: dict[str, Any],
    source: tuple[float, float, float],
    initial_heading: float,
) -> bytes:
    terrain = audit.get("terrain")
    ambient = (
        finite_vector3(terrain.get("ambient_color"), "ambient color")
        if isinstance(terrain, dict)
        else None
    )
    if ambient is None:
        raise OverlayFailure("audited terrain has no finite ambient color")
    lines = [
        "# Generated local-only CityWorld Next overlay.",
        "# Requires the separately supplied pinned CityWorld.zip.",
        "# Redistribution and shipping of this derived package are disabled.",
        "[General]",
        "Name = CityWorld Next Local Overlay",
        "GeometryConfig = CityWorld.otc",
        "Water = 0",
        "WaterLine = 0",
        "AmbientColor = "
        + ", ".join(stable_float(value) for value in ambient),
        "StartPosition = "
        + " ".join(stable_float(value) for value in source),
        f"StartRotation = {stable_float(initial_heading)}",
        "Gravity = -9.81",
        "CategoryID = 129",
        "Version = 4",
        "GUID = rorng-cityworld-next-local-overlay-v4",
        "",
        "[Authors]",
        "overlay = Oasiz AI and Rigs of Rods contributors",
        "",
        "[Objects]",
        "CityWorld.tobj =",
        f"{OVERLAY_NAME} =",
        "",
        "[ResourceBundles]",
        f"Dependency = {resource_bundle_dependency()}",
        "",
        "[Scripts]",
        "",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def overlay_placement(
    route_points: Sequence[ProceduralRoutePoint],
    terrain_objects: Sequence[TerrainObjectPlacement],
) -> bytes:
    header = (
        "// LOCAL-ONLY: requires the pinned user-supplied CityWorld.zip.\n"
        "// Redistribution and shipping are disabled.\n"
    )
    return (
        header
        + procedural_route_text(route_points)
        + terrain_object_placement_text(terrain_objects)
    ).encode("utf-8")


def add_payload(
    payloads: dict[str, bytes],
    name: str,
    payload: bytes,
) -> None:
    safe = safe_package_path(name)
    folded = safe.casefold()
    if any(existing.casefold() == folded for existing in payloads):
        raise OverlayFailure(f"duplicate generated package name: {safe}")
    payloads[safe] = payload


def payload_record(name: str, payload: bytes, role: str) -> dict[str, Any]:
    return {
        "path": name,
        "role": role,
        "sha256": sha256_bytes(payload),
        "size": len(payload),
    }


def write_deterministic_zip(path: Path, payloads: dict[str, bytes]) -> None:
    with zipfile.ZipFile(
        path,
        mode="w",
        compression=zipfile.ZIP_STORED,
        allowZip64=True,
    ) as archive:
        for name in sorted(payloads):
            info = zipfile.ZipInfo(name, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.create_version = 20
            info.extract_version = 20
            info.external_attr = ZIP_MODE << 16
            info.extra = b""
            info.comment = b""
            archive.writestr(info, payloads[name])


def sync_regular_file(path: Path) -> None:
    """Flush a file via writable binary I/O, as required by Windows fsync."""
    with path.open("r+b") as stream:
        stream.flush()
        os.fsync(stream.fileno())


def publish_no_replace(temporary_path: Path, output: Path) -> None:
    """Atomically publish a completed sibling file without replacing a target."""
    try:
        os.link(temporary_path, output)
    except FileExistsError as error:
        raise OverlayFailure("output target appeared during the build") from error
    except OSError as error:
        raise OverlayFailure(
            "output filesystem cannot atomically publish without replacement"
        ) from error


def build_local_overlay(
    *,
    archive_path: Path,
    repository_path: Path,
    output_path: Path,
    surface_offset_m: float,
) -> dict[str, Any]:
    repository = validate_repository(repository_path)
    source_archive = validate_archive_path(repository, archive_path)
    output = validate_output_path(repository, output_path)

    audit = audit_archive(
        source_archive,
        expected_sha256=PINNED_ARCHIVE_SHA256,
    )
    if audit.get("ok") is not True:
        raise OverlayFailure("CityWorld archive audit did not pass")
    member_records = source_member_provenance(source_archive)
    legacy_placements = source_placements(source_archive)
    anchor_evidence = authenticate_route_anchors(legacy_placements)
    open_gap_audit = audit_open_intercity_gap(legacy_placements)
    source_telepoint = exact_telepoint(audit, SOURCE_TELEPOINT)
    destination_telepoint = exact_telepoint(audit, DESTINATION_TELEPOINT)
    light_candidates = neoq_light_candidate_manifest(
        legacy_placements,
        destination_telepoint,
    )
    source_pole_definitions = authenticate_neoq_light_audit(
        audit,
        light_candidates,
    )
    light_candidate_payload = canonical_json_bytes(light_candidates)
    source = tuple(ROUTE_SOURCE_ANCHOR["connection_position_m"])
    destination = tuple(ROUTE_DESTINATION_ANCHOR["connection_position_m"])
    corridor_assets = prepare_assets(repository)
    streetlight_asset = prepare_streetlight_asset(repository)
    assets = (*corridor_assets, streetlight_asset)
    if len({asset.asset_id for asset in assets}) != len(assets):
        raise OverlayFailure("overlay assets contain duplicate identifiers")
    route_points, segment = build_intercity_route(
        source=source,
        destination=destination,
        surface_offset_m=surface_offset_m,
    )
    streetlight_placements, streetlight_report = (
        build_streetlight_placements(route_points)
    )
    segment["fixtures"] = streetlight_report
    segment["source"]["authenticated_placement"] = anchor_evidence["source"]
    segment["destination"]["authenticated_placement"] = (
        anchor_evidence["destination"]
    )
    segment["obstacle_avoidance"]["city_edge_seams_authenticated"] = True
    segment["obstacle_avoidance"]["open_gap_placement_origin_audit"] = (
        open_gap_audit
    )
    segment["obstacle_avoidance"]["swept_mesh_clearance"] = (
        "native-visual-and-drive-gate-required"
    )

    descriptor = terrain_descriptor(
        audit,
        source_telepoint,
        0.0,
    )
    placement = overlay_placement(
        route_points,
        streetlight_placements,
    )
    runtime_assets = (streetlight_asset,)
    merged_material = merge_material_scripts(runtime_assets)
    payloads: dict[str, bytes] = {}
    package_roles: dict[str, str] = {}
    add_payload(payloads, TERRAIN_NAME, descriptor)
    package_roles[TERRAIN_NAME] = "derived-terrain"
    add_payload(payloads, OVERLAY_NAME, placement)
    package_roles[OVERLAY_NAME] = "overlay-placement"
    for asset in runtime_assets:
        for runtime_file in asset.runtime_files:
            if runtime_file.role == "material-fallback":
                continue
            add_payload(
                payloads,
                runtime_file.package_path,
                runtime_file.payload,
            )
            package_roles[runtime_file.package_path] = runtime_file.role
    add_payload(payloads, MERGED_MATERIAL_NAME, merged_material)
    package_roles[MERGED_MATERIAL_NAME] = "material-fallback"
    add_payload(
        payloads,
        NEOQ_LIGHT_CANDIDATE_NAME,
        light_candidate_payload,
    )
    package_roles[NEOQ_LIGHT_CANDIDATE_NAME] = (
        "disabled-light-candidate-manifest"
    )

    source_member_hashes = {
        record["sha256"] for record in member_records
    }
    if any(
        sha256_bytes(payload) in source_member_hashes
        for payload in payloads.values()
    ):
        raise OverlayFailure("generated package duplicates an original source member")

    non_report_records = [
        payload_record(
            name,
            payload,
            package_roles[name],
        )
        for name, payload in sorted(payloads.items())
    ]
    audit_payload = canonical_json_bytes(audit)
    light_candidate_record = payload_record(
        NEOQ_LIGHT_CANDIDATE_NAME,
        light_candidate_payload,
        "disabled-light-candidate-manifest",
    )
    report = {
        "assets": [asset.provenance for asset in assets],
        "city_lighting": {
            "neoq_core": {
                "activation": light_candidates["activation"],
                "candidate_family_counts":
                    light_candidates["candidate_family_counts"],
                "candidate_manifest": light_candidate_record,
                "candidate_poles": light_candidates["candidate_poles"],
                "candidate_runtime_point_lights":
                    light_candidates["candidate_runtime_point_lights"],
                "policy_contract": light_candidates["policy_contract"],
                "scope": light_candidates["scope"],
                "source_pole_definitions": source_pole_definitions,
                "visual_geometry": light_candidates["visual_geometry"],
            },
        },
        "corridor": segment,
        "format": FORMAT,
        "package": {
            "entries": len(payloads) + 1,
            "files": non_report_records,
            "fixed_permissions_octal": "100644",
            "fixed_timestamp_utc": "1980-01-01T00:00:00Z",
            "zip_compression": "stored",
        },
        "rights": {
            "local_only": True,
            "redistribution_allowed": False,
            "shipping_allowed": False,
            "source_archive_copied": False,
            "source_geometry_copied": False,
            "source_objects_copied": False,
            "source_placement_payload_copied": False,
            "source_placements_copied": False,
            "source_placement_records_derived": True,
            "derived_source_placement_record_count":
                light_candidates["candidate_poles"],
            "source_textures_copied": False,
        },
        "visual_asset_usage": {
            "corridor_placement_mode":
                "native-procedural-v3-curb-cut-with-blender-fixtures-v1",
            "disabled_light_candidate_manifest":
                NEOQ_LIGHT_CANDIDATE_NAME,
            "neoq_core_runtime_light_activation": "blocked-fail-closed",
            "packaged_asset_ids": [
                asset.asset_id
                for asset in runtime_assets
            ],
            "placed_asset_ids": [LED_STREETLIGHT_ASSET_ID],
            "unplaced_asset_ids": [
                asset.asset_id
                for asset in corridor_assets
            ],
            "validated_asset_ids": [
                asset.asset_id
                for asset in assets
            ],
            "purpose":
                "curb-free Penguinville overlap apron plus route-safe Blender "
                "lighting; deterministic NeoQueretaro pole-light candidates "
                "remain disabled pending the renderer budget and zero-shadow "
                "contracts; bridge modules remain validated candidates for "
                "deck and abutment replacement",
        },
        "source": {
            "archive": {
                "audit_report_sha256": sha256_bytes(audit_payload),
                "bytes": audit["archive"]["bytes"],
                "expected_sha256": PINNED_ARCHIVE_SHA256,
                "members": member_records,
                "name": "CityWorld.zip",
                "sha256": audit["archive"]["sha256"],
            },
            "references": {
                "geometry_config": "CityWorld.otc",
                "original_placements": "CityWorld.tobj",
                "overlay_placements": OVERLAY_NAME,
                "resource_bundle_dependency":
                    resource_bundle_dependency(),
            },
        },
        "tools": tool_provenance(repository, assets),
    }
    report_payload = canonical_json_bytes(report)
    add_payload(payloads, REPORT_NAME, report_payload)

    post_build_archive_hash = archive_sha256(source_archive)
    if post_build_archive_hash != PINNED_ARCHIVE_SHA256:
        raise OverlayFailure("source CityWorld.zip changed during the build")

    descriptor_record = payload_record(
        TERRAIN_NAME,
        descriptor,
        "derived-terrain",
    )
    placement_record = payload_record(
        OVERLAY_NAME,
        placement,
        "overlay-placement",
    )
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.tmp-",
        suffix=".part",
        dir=output.parent,
    )
    os.close(file_descriptor)
    temporary_path = Path(temporary_name)
    try:
        write_deterministic_zip(temporary_path, payloads)
        if archive_sha256(source_archive) != PINNED_ARCHIVE_SHA256:
            raise OverlayFailure("source CityWorld.zip changed during package write")
        temporary_path.chmod(0o644)
        sync_regular_file(temporary_path)
        publish_no_replace(temporary_path, output)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass

    return {
        "format": BUILD_RESULT_FORMAT,
        "generated": [
            descriptor_record,
            placement_record,
            light_candidate_record,
        ],
        "output": {
            "entries": len(payloads),
            "name": output.name,
            "sha256": sha256_regular_file(
                output,
                max_bytes=512 * 1024 * 1024,
            ),
            "size": output.stat().st_size,
        },
        "report": {
            "path": REPORT_NAME,
            "sha256": sha256_bytes(report_payload),
        },
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--surface-offset-m", required=True, type=float)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        result = build_local_overlay(
            archive_path=args.archive,
            repository_path=args.repo_root,
            output_path=args.output,
            surface_offset_m=args.surface_offset_m,
        )
    except (
        AuditFailure,
        CompileFailure,
        CorridorFailure,
        OverlayFailure,
        OSError,
        ValueError,
        zipfile.BadZipFile,
    ) as error:
        print(f"CityWorld local overlay build failed: {error}", file=sys.stderr)
        return 2
    sys.stdout.buffer.write(canonical_json_bytes(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
