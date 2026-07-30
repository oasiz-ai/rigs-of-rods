#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a deterministic, local-only CityWorld Next overlay package.

The source ``CityWorld.zip`` is a user-supplied compatibility dependency. This
tool audits it in place and reads only authenticated terrain, placement, and
road-endpoint members for provenance and topology. It does not extract the
archive, execute archive content, use the network, or copy original CityWorld
payloads into the generated package.
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
import cityworld_neoq_intercity_bridge as neoq_bridge  # noqa: E402
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
from validate_cityworld_tree_family import (  # noqa: E402
    FamilyValidator,
    load_json as load_tree_family_json,
)
import cityworld_penguin_neoq_corridor as penguin_neoq_seam  # noqa: E402


FORMAT = "ror-cityworld-local-overlay-v5"
BUILD_RESULT_FORMAT = "ror-cityworld-local-overlay-build-result-v5"
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
NEOQ_TREE_REPLACEMENT_NAME = (
    "cityworld_next_neoq_tree_replacements.v1.json"
)
NEOQ_TREE_REPLACEMENT_FORMAT = (
    "ror-cityworld-neoq-tree-replacements-v1"
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
NEOQ_EXPECTED_POLE_DEFINITIONS = {
    "luminariaLQr": {
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
    "luminariaQr": {
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
    "luminariaYQr": {
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
TREE_IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9_]+$")
TREE_PLAN_FLOAT = r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?"
TREE_PLAN_LINE_PATTERN = re.compile(
    rf"^CITYWORLD_NEOQ_TREE_REPLACEMENT\("
    rf"([0-9]+)U, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf"({TREE_PLAN_FLOAT})f, ({TREE_PLAN_FLOAT})f, "
    rf'"([a-z0-9_]+)", ({TREE_PLAN_FLOAT})f, '
    rf'"([a-z0-9_]+)", ({TREE_PLAN_FLOAT})f\)$'
)
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644
POSITION_EPSILON = 1e-6
ROUTE_SAMPLE_SPACING_M = 20.0
ROUTE_TANGENT_HANDLE_M = 160.0
ROUTE_GROUND_LEAD_M = 40.0
ROUTE_RAMP_LENGTH_M = 160.0
ROUTE_DECK_CLEARANCE_M = 8.0
ROUTE_BRIDGE_BORDER_WIDTH_M = 0.45
ROUTE_BRIDGE_BORDER_HEIGHT_M = 0.95
ROUTE_STREETLIGHT_SPACING_M = 40.0
ROUTE_STREETLIGHT_DECK_MARGIN_M = 20.0
ROUTE_FLAT_BORDER_WIDTH_M = 1.0
ROUTE_FLAT_BORDER_HEIGHT_M = 0.15
ROUTE_ARC_TABLE_STEPS = 8192
ROUTE_EXPECTED_PROCEDURAL_YAW_DEGREES = 0.0
ROUTE_MAX_CONNECTION_TAPER_GRADE = 0.02
ROUTE_OPEN_GAP_BOUNDS_XZ_M = (500.0, 1380.0, 400.0, 1000.0)
ROUTE_SOURCE_ANCHOR = {
    "city": "Penguinville",
    "connection": "east opened road seam after crowned-to-flat transition",
    "connection_position_m": penguin_neoq_seam.ROUTE_SOURCE_POSITION_M,
    "object": penguin_neoq_seam.SOURCE_LEGACY_OBJECT,
    "placement_position_m": penguin_neoq_seam.SOURCE_PLACEMENT_POSITION_M,
    "rotation_degrees":
        penguin_neoq_seam.SOURCE_PLACEMENT_ROTATION_DEGREES,
}
ROUTE_DESTINATION_ANCHOR = {
    "city": "NeoQueretaro",
    "connection": "west perimeter T-junction carriageway",
    "connection_position_m":
        penguin_neoq_seam.ROUTE_DESTINATION_POSITION_M,
    "object": penguin_neoq_seam.DESTINATION_OBJECT,
    "placement_position_m":
        penguin_neoq_seam.DESTINATION_PLACEMENT_POSITION_M,
    "rotation_degrees":
        penguin_neoq_seam.DESTINATION_PLACEMENT_ROTATION_DEGREES,
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
PENGUIN_ROAD_SEAM_MANIFEST = penguin_neoq_seam.TRANSITION_MANIFEST
PENGUIN_ROAD_SEAM_ASSET_ID = penguin_neoq_seam.TRANSITION_ASSET_ID
NEOQ_TREE_FAMILY_MANIFEST = (
    "content-source/cityworld_next/vegetation/"
    "rorng_city_neoq_tree_family.v1.json"
)
NEOQ_TREE_NATIVE_PLAN = (
    "source/main/resources/tobj_fileformat/"
    "CityWorldNeoQTreePlan.inc"
)
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
    "tools/cityworld_neoq_intercity_bridge.py",
    "tools/cityworld_penguin_neoq_corridor.py",
    "tools/compile_cityworld_asset.py",
    "tools/run_cityworld_neoq_bridge_scene.py",
    "tools/solve_cityworld_bridge_corridor.py",
    "tools/validate_cityworld_asset.py",
    "tools/validate_cityworld_tree_family.py",
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
class NativeTreePlanEntry:
    ordinal: int
    source_line: int
    position: tuple[float, float, float]
    original_rotation_degrees: tuple[float, float, float]
    variant: str
    scale: float
    object_definition: str
    yaw_degrees: float


@dataclass(frozen=True)
class PreparedTreeFamily:
    assets: tuple[PreparedAsset, ...]
    family_provenance: dict[str, Any]
    replacements: tuple[NativeTreePlanEntry, ...]
    wrappers: tuple[RuntimeFile, ...]


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


def read_native_tree_plan(
    repository: Path,
) -> tuple[NativeTreePlanEntry, ...]:
    plan_path = (
        repository / safe_package_path(NEOQ_TREE_NATIVE_PLAN)
    ).resolve()
    try:
        plan_path.relative_to(repository)
        text = plan_path.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise OverlayFailure("native NeoQ tree plan is unavailable") from error
    if len(text.encode("ascii")) > 64 * 1024:
        raise OverlayFailure("native NeoQ tree plan exceeds the read limit")

    entries: list[NativeTreePlanEntry] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line or line.startswith("//"):
            continue
        match = TREE_PLAN_LINE_PATTERN.fullmatch(line)
        if match is None:
            raise OverlayFailure(
                f"native NeoQ tree plan line {line_number} is invalid"
            )
        values = tuple(float(match.group(index)) for index in range(2, 8))
        scale = float(match.group(9))
        yaw = float(match.group(11))
        if not all(math.isfinite(value) for value in (*values, scale, yaw)):
            raise OverlayFailure("native NeoQ tree plan contains non-finite values")
        entries.append(
            NativeTreePlanEntry(
                ordinal=len(entries),
                source_line=int(match.group(1)),
                position=(values[0], values[1], values[2]),
                original_rotation_degrees=(
                    values[3],
                    values[4],
                    values[5],
                ),
                variant=match.group(8),
                scale=scale,
                object_definition=match.group(10),
                yaw_degrees=yaw,
            )
        )

    if len(entries) != 18:
        raise OverlayFailure("native NeoQ tree plan does not contain 18 entries")
    if [entry.source_line for entry in entries] != list(range(9, 27)):
        raise OverlayFailure("native NeoQ tree plan source lines are not 9-26")
    if len({entry.object_definition.casefold() for entry in entries}) != 18:
        raise OverlayFailure("native NeoQ tree wrapper names are not unique")
    for entry in entries:
        expected_name = f"rorng_city_neoq_tree_instance_{entry.ordinal:02d}"
        if (
            entry.object_definition != expected_name
            or TREE_IDENTIFIER_PATTERN.fullmatch(
                entry.object_definition
            )
            is None
        ):
            raise OverlayFailure(
                "native NeoQ tree wrapper name is not canonical"
            )
    return tuple(entries)


def authenticate_neoq_tree_placements(
    placements: Sequence[SourcePlacement],
    plan: Sequence[NativeTreePlanEntry],
) -> tuple[SourcePlacement, ...]:
    legacy = tuple(
        placement
        for placement in placements
        if placement.object_name == "arbol1Qr"
    )
    if len(legacy) != 18:
        raise OverlayFailure(
            "expected exactly 18 legacy arbol1Qr placements"
        )
    by_line: dict[int, SourcePlacement] = {}
    for placement in placements:
        if placement.line_number < 9 or placement.line_number > 26:
            continue
        if placement.line_number in by_line:
            raise OverlayFailure("NeoQ tree source line is duplicated")
        by_line[placement.line_number] = placement
    if set(by_line) != set(range(9, 27)):
        raise OverlayFailure(
            "NeoQ tree source lines 9-26 are incomplete"
        )

    authenticated: list[SourcePlacement] = []
    for entry in plan:
        observed = by_line[entry.source_line]
        if (
            observed.object_name != "arbol1Qr"
            or observed.position != entry.position
            or observed.rotation_degrees
            != entry.original_rotation_degrees
        ):
            raise OverlayFailure(
                f"CityWorld.tobj line {entry.source_line} does not "
                "match the exact NeoQ tree plan"
            )
        authenticated.append(observed)
    return tuple(authenticated)


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
    if len(pole_definitions) != len(NEOQ_EXPECTED_POLE_DEFINITIONS):
        raise OverlayFailure(
            "CityWorld source-pole definition inventory count drifted"
        )
    authenticated: dict[str, dict[str, Any]] = {}
    for index, value in enumerate(pole_definitions):
        if not isinstance(value, dict):
            raise OverlayFailure(
                "CityWorld source-pole definition inventory is malformed"
            )
        family = value.get("family")
        if (
            type(family) is not str
            or family not in NEOQ_EXPECTED_POLE_DEFINITIONS
            or family in authenticated
        ):
            raise OverlayFailure(
                "CityWorld source-pole definition families drifted"
            )
        expected = NEOQ_EXPECTED_POLE_DEFINITIONS[family]
        if set(value) != set(expected):
            raise OverlayFailure(
                f"CityWorld source-pole definition {index} fields drifted"
            )
        for field, expected_value in expected.items():
            actual = value.get(field)
            if (
                type(actual) is not type(expected_value)
                or actual != expected_value
            ):
                raise OverlayFailure(
                    "CityWorld source-pole definition "
                    f"{family!r} field {field!r} drifted"
                )
        authenticated[family] = value
    if set(authenticated) != set(NEOQ_EXPECTED_POLE_DEFINITIONS):
        raise OverlayFailure(
            "CityWorld source-pole definition families are incomplete"
        )
    return [
        authenticated[family]
        for family in NEOQ_LUMINARIA_FAMILIES
    ]


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
    runtime_material_provenance: dict[str, Any] = {}
    runtime_material_dependencies = compiler.manifest.get(
        "runtime_material_dependencies"
    )
    if runtime_material_dependencies is not None:
        if (
            not isinstance(runtime_material_dependencies, list)
            or not runtime_material_dependencies
        ):
            raise OverlayFailure(
                f"{compiler.asset_id} has invalid runtime material dependencies"
            )
        checked_dependencies: list[dict[str, Any]] = []
        dependency_keys = {
            "material",
            "material_script_path",
            "material_script_sha256",
            "texture_path",
            "texture_sha256",
        }
        for index, dependency in enumerate(runtime_material_dependencies):
            if (
                not isinstance(dependency, dict)
                or set(dependency) != dependency_keys
                or not isinstance(dependency.get("material"), str)
                or not dependency["material"]
            ):
                raise OverlayFailure(
                    f"{compiler.asset_id} runtime material dependency "
                    f"{index} has an invalid contract"
                )
            for path_key, hash_key in (
                ("material_script_path", "material_script_sha256"),
                ("texture_path", "texture_sha256"),
            ):
                path_value = dependency.get(path_key)
                expected_hash = dependency.get(hash_key)
                if (
                    not isinstance(path_value, str)
                    or not isinstance(expected_hash, str)
                    or len(expected_hash) != 64
                ):
                    raise OverlayFailure(
                        f"{compiler.asset_id} runtime material dependency "
                        f"{index} is incomplete"
                    )
                source = (
                    repository / safe_package_path(path_value)
                ).resolve()
                try:
                    source.relative_to(repository)
                except ValueError as error:
                    raise OverlayFailure(
                        f"{compiler.asset_id} runtime material dependency "
                        "escapes the repository"
                    ) from error
                actual_hash = sha256_regular_file(
                    source,
                    max_bytes=64 * 1024 * 1024,
                )
                if actual_hash != expected_hash:
                    raise OverlayFailure(
                        f"{compiler.asset_id} runtime material dependency "
                        f"hash drifted: {path_value}"
                    )
            checked_dependencies.append(dict(dependency))
        runtime_material_provenance = {
            "materials": compiler.manifest.get("materials"),
            "runtime_material_dependencies": checked_dependencies,
        }
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
            "collision": compiler.manifest.get("collision"),
            "connectors": compiler.manifest.get("connectors"),
            "geometry": compiler.manifest.get("geometry"),
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
            **runtime_material_provenance,
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


def prepare_penguin_road_seam_asset(repository: Path) -> PreparedAsset:
    asset = prepare_asset(
        repository,
        PENGUIN_ROAD_SEAM_MANIFEST,
        corridor_module=False,
    )
    profile = asset.provenance.get("asset", {}).get("profile")
    if (
        asset.asset_id != PENGUIN_ROAD_SEAM_ASSET_ID
        or profile is not None
        or asset.centerline_length_m is not None
        or asset.profile is not None
    ):
        raise OverlayFailure(
            "Penguinville crowned-road transition does not match "
            "its pinned standalone seam contract"
        )
    return asset


def tree_scale_wrapper(
    asset: PreparedAsset,
    entry: NativeTreePlanEntry,
) -> RuntimeFile:
    terrain_objects = tuple(
        runtime_file
        for runtime_file in asset.runtime_files
        if runtime_file.role == "terrain-object"
    )
    if len(terrain_objects) != 1:
        raise OverlayFailure(
            f"{asset.asset_id} has no unique terrain-object definition"
        )
    source = terrain_objects[0]
    try:
        lines = source.payload.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise OverlayFailure(
            f"{source.package_path} is not UTF-8"
        ) from error
    expected_render_mesh = f"{asset.asset_id}_lod0.mesh"
    expected_collision_mesh = (
        f"mesh {asset.asset_id}_collision_fixture.mesh"
    )
    if (
        len(lines) < 8
        or lines[0] != expected_render_mesh
        or [part.strip() for part in lines[1].split(",")]
        != ["1", "1", "1"]
        or lines.count("beginmesh") != 1
        or lines.count("endmesh") != 1
        or expected_collision_mesh not in lines
        or lines[-1] != "end"
    ):
        raise OverlayFailure(
            f"{source.package_path} cannot be scaled by a safe wrapper"
        )
    scale = stable_float(entry.scale)
    lines[1] = f"{scale}, {scale}, {scale}"
    payload = ("\n".join(lines) + "\n").encode("utf-8")
    package_path = f"{entry.object_definition}.odef"
    return RuntimeFile(
        package_path=package_path,
        repository_path=NEOQ_TREE_NATIVE_PLAN,
        role="terrain-object-scale-wrapper",
        sha256=sha256_bytes(payload),
        size=len(payload),
        payload=payload,
    )


def prepare_tree_family(
    repository: Path,
    plan: Sequence[NativeTreePlanEntry],
) -> PreparedTreeFamily:
    family_path = (
        repository / safe_package_path(NEOQ_TREE_FAMILY_MANIFEST)
    ).resolve()
    try:
        family_path.relative_to(repository)
    except ValueError as error:
        raise OverlayFailure(
            "NeoQ tree family manifest escapes the repository"
        ) from error

    validation = FamilyValidator(repository, family_path).validate()
    if validation.get("summary", {}).get("valid") is not True:
        codes = sorted(
            {
                diagnostic.get("code", "UNKNOWN")
                for diagnostic in validation.get("diagnostics", [])
                if isinstance(diagnostic, dict)
            }
        )
        raise OverlayFailure(
            "NeoQ tree family validation failed: " + ", ".join(codes)
        )
    try:
        family = load_tree_family_json(family_path)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise OverlayFailure("NeoQ tree family manifest is unreadable") from error

    asset_identity = family.get("asset")
    placement_target = family.get("placement_target")
    selector = family.get("selector")
    variants = family.get("variants")
    if (
        not isinstance(asset_identity, dict)
        or asset_identity.get("id") != "rorng_city_neoq_tree_family"
        or asset_identity.get("license") != "GPL-3.0-or-later"
        or placement_target
        != {
            "integration_status": "asset-ready-placement-deferred",
            "legacy_object": "arbol1Qr",
            "map": "CityWorld/NeoQueretaro",
            "placement_count": 18,
        }
        or not isinstance(selector, dict)
        or selector.get("algorithm")
        != "sha256-little-endian-modulo-v1"
        or not isinstance(variants, list)
    ):
        raise OverlayFailure("NeoQ tree family integration contract is invalid")

    assignments = selector.get("assignments")
    if not isinstance(assignments, list) or len(assignments) != len(plan):
        raise OverlayFailure("NeoQ tree family selector is incomplete")
    for entry, assignment in zip(plan, assignments):
        if (
            not isinstance(assignment, dict)
            or assignment.get("placement_ordinal") != entry.ordinal
            or assignment.get("variant") != entry.variant
            or assignment.get("scale") != entry.scale
            or assignment.get("yaw_degrees") != entry.yaw_degrees
        ):
            raise OverlayFailure(
                "native NeoQ tree plan disagrees with the family selector"
            )

    manifest_by_id: dict[str, str] = {}
    for variant in variants:
        if not isinstance(variant, dict):
            raise OverlayFailure("NeoQ tree family has an invalid variant")
        asset_id = variant.get("asset_id")
        manifest = variant.get("manifest")
        if (
            not isinstance(asset_id, str)
            or not isinstance(manifest, str)
            or asset_id in manifest_by_id
        ):
            raise OverlayFailure("NeoQ tree family variant is not unique")
        manifest_by_id[asset_id] = manifest
    if set(manifest_by_id) != {entry.variant for entry in plan}:
        raise OverlayFailure(
            "native NeoQ tree plan does not use the exact family variants"
        )

    assets = tuple(
        prepare_asset(
            repository,
            manifest_by_id[variant["asset_id"]],
            corridor_module=False,
        )
        for variant in variants
    )
    for asset in assets:
        if (
            asset.provenance.get("asset", {}).get("profile")
            != "static-fixture-v1"
            or asset.centerline_length_m is not None
            or asset.profile is not None
            or asset.provenance.get("runtime_lights") != []
        ):
            raise OverlayFailure(
                f"{asset.asset_id} does not match the tree runtime profile"
            )
    by_id = {asset.asset_id: asset for asset in assets}
    wrappers = tuple(
        tree_scale_wrapper(by_id[entry.variant], entry)
        for entry in plan
    )
    if len({wrapper.package_path.casefold() for wrapper in wrappers}) != 18:
        raise OverlayFailure("NeoQ tree scale-wrapper paths are not unique")

    return PreparedTreeFamily(
        assets=assets,
        family_provenance={
            "asset": asset_identity,
            "family_manifest": {
                "path": NEOQ_TREE_FAMILY_MANIFEST,
                "sha256": sha256_regular_file(
                    family_path,
                    max_bytes=4 * 1024 * 1024,
                ),
            },
            "native_plan": {
                "path": NEOQ_TREE_NATIVE_PLAN,
                "sha256": sha256_regular_file(
                    repository / NEOQ_TREE_NATIVE_PLAN,
                    max_bytes=64 * 1024,
                ),
            },
            "selector": {
                "algorithm": selector["algorithm"],
                "namespace": selector["namespace"],
            },
            "validation": {
                "format": validation["format"],
                "summary": validation["summary"],
            },
        },
        replacements=tuple(plan),
        wrappers=wrappers,
    )


def neoq_tree_replacement_manifest(
    tree_family: PreparedTreeFamily,
    authenticated: Sequence[SourcePlacement],
    source_tobj_sha256: str,
) -> dict[str, Any]:
    if (
        len(authenticated) != 18
        or len(tree_family.replacements) != 18
        or len(tree_family.wrappers) != 18
    ):
        raise OverlayFailure("NeoQ tree replacement set is incomplete")
    replacements: list[dict[str, Any]] = []
    for source, entry, wrapper in zip(
        authenticated,
        tree_family.replacements,
        tree_family.wrappers,
    ):
        if source.line_number != entry.source_line:
            raise OverlayFailure("NeoQ tree replacement ordering drifted")
        replacements.append(
            {
                "legacy_object": source.object_name,
                "object_definition": entry.object_definition,
                "ordinal": entry.ordinal,
                "position_m": [
                    round(value, 9)
                    for value in source.position
                ],
                "position_preserved": True,
                "rotation_degrees": [
                    round(entry.original_rotation_degrees[0], 9),
                    round(entry.yaw_degrees, 9),
                    round(entry.original_rotation_degrees[2], 9),
                ],
                "scale": round(entry.scale, 9),
                "source_line": entry.source_line,
                "source_rotation_degrees": [
                    round(value, 9)
                    for value in source.rotation_degrees
                ],
                "variant": entry.variant,
                "wrapper": {
                    "path": wrapper.package_path,
                    "sha256": wrapper.sha256,
                    "size": wrapper.size,
                },
            }
        )
    return {
        "activation": {
            "duplicate_placements_emitted": 0,
            "fail_closed": True,
            "mode": "native-authenticated-in-place-replacement-v1",
            "requires_exact_archive_dependency": True,
            "requires_exact_tobj_sha256": True,
            "runtime_resource_preflight": "all-18-scale-wrapper-odefs",
        },
        "family": tree_family.family_provenance,
        "format": NEOQ_TREE_REPLACEMENT_FORMAT,
        "replacements": replacements,
        "source": {
            "legacy_object": "arbol1Qr",
            "placement_count": 18,
            "source_lines": [9, 26],
            "tobj": "CityWorld.tobj",
            "tobj_sha256": source_tobj_sha256,
        },
        "summary": {
            "collision_scale_matches_visual_scale": True,
            "positions_preserved": 18,
            "replacement_count": 18,
            "unique_scale_wrappers": 18,
            "variants": sorted(
                {entry.variant for entry in tree_family.replacements}
            ),
        },
    }


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
        "// Generated by ror-cityworld-local-overlay-v5.",
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
    paths = {*TOOL_PATHS, NEOQ_TREE_NATIVE_PLAN}
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
    source_road_y_m: float,
    destination_road_y_m: float,
    surface_offset_m: float,
) -> float:
    station = float(station_m)
    total = float(total_length_m)
    source_road_y = float(source_road_y_m)
    destination_road_y = float(destination_road_y_m)
    surface_offset = float(surface_offset_m)
    if not all(
        math.isfinite(value)
        for value in (
            station,
            total,
            source_road_y,
            destination_road_y,
            surface_offset,
        )
    ):
        raise OverlayFailure("route elevation inputs must be finite")
    if total <= 0.0 or not 0.0 <= station <= total:
        raise OverlayFailure("route elevation station lies outside the corridor")

    road_y = source_road_y + (
        destination_road_y - source_road_y
    ) * smoothstep(station / total)
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
    if (
        len(source) != 3
        or len(destination) != 3
        or not all(
            math.isfinite(float(value))
            for value in (*source, *destination)
        )
    ):
        raise OverlayFailure("intercity road anchors must be finite vectors")
    control_points = route_control_points(source, destination)
    arc_table = route_arc_table(control_points)
    core_length = arc_table[-1][1]
    stations = route_stations(core_length)
    connection_taper_grade = (
        1.5 * abs(float(surface_offset_m)) / ROUTE_GROUND_LEAD_M
    )
    if connection_taper_grade > ROUTE_MAX_CONNECTION_TAPER_GRADE:
        raise OverlayFailure(
            "road connection exceeds the safe connection-taper grade"
        )
    points: list[ProceduralRoutePoint] = []
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
        elevation = route_elevation(
            station,
            core_length,
            source[1],
            destination[1],
            float(surface_offset_m),
        )
        if (
            station <= ROUTE_GROUND_LEAD_M
            or station >= core_length - ROUTE_GROUND_LEAD_M
        ):
            road_type = "flat"
        elif elevation - min(source[1], destination[1]) < 1.0:
            # Keep bridge cross-section/visuals on the two very low ramp
            # segments, but do not ask native support generation to create a
            # terrain-reaching pier that cannot meet its height contract.
            road_type = penguin_neoq_seam.BRIDGE_NO_PILLARS_TOKEN
        else:
            road_type = penguin_neoq_seam.BRIDGE_TOKEN
        point = ProceduralRoutePoint(
            station_m=station,
            x=x,
            y=elevation,
            z=z,
            yaw_degrees=normalized_degrees(yaw),
            road_type=road_type,
            width_m=penguin_neoq_seam.width_at_station(
                station,
                core_length,
            ),
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

    total_length = core_length
    source_gap = math.dist(
        (points[0].x, points[0].y, points[0].z),
        source,
    )
    destination_gap = math.dist(
        (points[-1].x, points[-1].y, points[-1].z),
        destination,
    )
    if source_gap > POSITION_EPSILON or destination_gap > POSITION_EPSILON:
        raise OverlayFailure("intercity route does not close against its road anchors")
    if any(
        point.x < source[0] - POSITION_EPSILON
        or point.x > destination[0] + POSITION_EPSILON
        for point in points
    ):
        raise OverlayFailure("intercity route enters an existing city envelope")

    requested_support_points = [
        point
        for point in points
        if point.road_type == penguin_neoq_seam.BRIDGE_TOKEN
    ]
    no_pillar_bridge_points = [
        point
        for point in points
        if point.road_type == penguin_neoq_seam.BRIDGE_NO_PILLARS_TOKEN
    ]
    supported_support_points = [
        point
        for point in requested_support_points
        if point.y - min(source[1], destination[1]) >= 1.0
    ]
    if len(supported_support_points) != len(requested_support_points):
        raise OverlayFailure(
            "side-pier request is below the support-height threshold"
        )
    bridge_spacing = max(
        (
            second.station_m - first.station_m
            for first, second in zip(
                requested_support_points,
                requested_support_points[1:],
            )
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
        destination[0] - source[0],
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
        "collision": {
            "endcap_collision_enabled": False,
            "endcap_collision_triangle_count": 0,
            "endpoint_wheel_path_intrusion_m": 0.0,
        },
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
        "format": "ror-cityworld-intercity-corridor-v4",
        "obstacle_avoidance": {
            "derivation":
                "authenticated-open-road-mouth-then-strictly-monotonic-x",
            "destination_city_min_x_m": round(destination[0], 9),
            "source_city_max_x_m": round(source[0], 9),
            "centerline_monotonic_x": True,
            "existing_ground_road_envelopes_intersected": 0,
            "intentional_source_overlap_m": 0.0,
            "procedural_centerline_x_bounds_m": [
                round(source[0], 9),
                round(destination[0], 9),
            ],
            "source_transition_x_bounds_m": [
                penguin_neoq_seam.SOURCE_EDGE_WORLD_X_M,
                round(source[0], 9),
            ],
        },
        "profile": {
            "destination_connection_surface_y_m":
                round(destination[1], 9),
            "source_connection_surface_y_m": round(source[1], 9),
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
            "source_surface_y_m": round(
                source[1] + float(surface_offset_m),
                9,
            ),
            "destination_surface_y_m": round(
                destination[1] + float(surface_offset_m),
                9,
            ),
            "source_width_m": penguin_neoq_seam.SOURCE_ROAD_WIDTH_M,
            "destination_width_m":
                penguin_neoq_seam.DESTINATION_ROAD_WIDTH_M,
            "width_transition": "full-corridor-cubic-smoothstep",
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
                round(source[0], 9),
                round(source[1], 9),
                round(source[2], 9),
            ],
            "collision_handoff": {
                "authorities_per_station": 1,
                "legacy_curb_collision_retained": False,
                "replacement_mode":
                    "native-authenticated-in-place-object-definition-swap",
                "transition_asset_id":
                    penguin_neoq_seam.TRANSITION_ASSET_ID,
            },
        },
        "supports": {
            "enabled": True,
            "expected_built_count": len(requested_support_points),
            "expected_skipped_count": 0,
            "maximum_station_spacing_m": round(bridge_spacing, 9),
            "no_pillar_bridge_count": len(no_pillar_bridge_points),
            "no_pillar_bridge_stations_m": [
                round(point.station_m, 9)
                for point in no_pillar_bridge_points
            ],
            "road_type_token": penguin_neoq_seam.BRIDGE_TOKEN,
            "requested_count": len(requested_support_points),
            "stations_m": [
                round(point.station_m, 9)
                for point in requested_support_points
            ],
            "expected_built_stations_m": [
                round(point.station_m, 9)
                for point in requested_support_points
            ],
            "style": "ror-native-procedural-paired-outboard-piers-v1",
            "centerline_pillars_requested": 0,
            "paired_outboard": True,
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
                "width_m": round(point.width_m, 9),
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
        point
        for point in points
        if point.road_type == penguin_neoq_seam.BRIDGE_TOKEN
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
            if point.road_type != penguin_neoq_seam.BRIDGE_TOKEN:
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
                "lateral_mount_offset_m": round(
                    point.width_m / 2.0
                    + point.border_width_m / 2.0,
                    9,
                ),
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
                "road_width_m": round(point.width_m, 9),
            }
        )

    lateral_offsets_m = [
        point.width_m / 2.0 + point.border_width_m / 2.0
        for point in selected_points
    ]
    report = {
        "arm_orientation": "alternating-inward-over-roadway",
        "asset_id": LED_STREETLIGHT_ASSET_ID,
        "collision_authority": "native-procedural-road-v4-open-seams",
        "format": "ror-cityworld-streetlight-placement-v2",
        "instance_count": len(placements),
        "lateral_mount_offset_range_m": [
            round(min(lateral_offsets_m), 9),
            round(max(lateral_offsets_m), 9),
        ],
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


def procedural_route_text(
    points: Sequence[ProceduralRoutePoint],
    *,
    comments: Sequence[str] = (
        "Generated full Penguinville-to-NeoQueretaro intercity road.",
        "The Penguinville overlap apron clears the legacy curb.",
        "The NeoQueretaro endpoint is an authenticated perimeter road.",
        "Bridge points request terrain-reaching pillars.",
    ),
) -> str:
    if len(points) < 2:
        raise OverlayFailure("intercity route requires at least two waypoints")
    if (
        not comments
        or any(
            not isinstance(comment, str)
            or not comment
            or "\n" in comment
            or "\r" in comment
            for comment in comments
        )
    ):
        raise OverlayFailure("procedural route comments are invalid")
    lines = [
        *(f"// {comment}" for comment in comments),
        "begin_procedural_roads",
        "    smoothing_num_splits 0",
        "    collision_enabled true",
        f"    {penguin_neoq_seam.OPEN_ENDCAP_DIRECTIVE}",
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
        "// Blender-authored road transition and bridge fixtures.",
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
        "Name = CityWorld Next Enhanced (Use This)",
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
        "Version = 5",
        "GUID = rorng-cityworld-next-local-overlay-v5",
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
    *,
    additional_routes: Sequence[
        tuple[Sequence[ProceduralRoutePoint], Sequence[str]]
    ] = (),
) -> bytes:
    header = (
        "// LOCAL-ONLY: requires the pinned user-supplied CityWorld.zip.\n"
        "// Redistribution and shipping are disabled.\n"
    )
    route_text = procedural_route_text(route_points)
    for points, comments in additional_routes:
        route_text += "\n" + procedural_route_text(
            points,
            comments=comments,
        )
    return (
        header
        + route_text
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
    source_tobj_records = [
        record
        for record in member_records
        if record.get("name") == "CityWorld.tobj"
    ]
    if (
        len(source_tobj_records) != 1
        or not isinstance(source_tobj_records[0].get("sha256"), str)
    ):
        raise OverlayFailure(
            "CityWorld.tobj has no unique source provenance record"
        )
    source_tobj_sha256 = source_tobj_records[0]["sha256"]
    legacy_placements = source_placements(source_archive)
    anchor_evidence = authenticate_route_anchors(legacy_placements)
    open_gap_audit = audit_open_intercity_gap(legacy_placements)
    native_tree_plan = read_native_tree_plan(repository)
    authenticated_tree_placements = authenticate_neoq_tree_placements(
        legacy_placements,
        native_tree_plan,
    )
    neoq_bridge_authentication = neoq_bridge.authenticate_inputs(
        source_archive,
        legacy_placements,
    )
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
    penguin_road_seam_asset = prepare_penguin_road_seam_asset(repository)
    tree_family = prepare_tree_family(repository, native_tree_plan)
    tree_replacements = neoq_tree_replacement_manifest(
        tree_family,
        authenticated_tree_placements,
        source_tobj_sha256,
    )
    tree_replacement_payload = canonical_json_bytes(tree_replacements)
    assets = (
        *corridor_assets,
        streetlight_asset,
        penguin_road_seam_asset,
        *tree_family.assets,
    )
    if len({asset.asset_id for asset in assets}) != len(assets):
        raise OverlayFailure("overlay assets contain duplicate identifiers")
    route_points, segment = build_intercity_route(
        source=source,
        destination=destination,
        surface_offset_m=surface_offset_m,
    )
    neoq_bridge_points, neoq_bridge_segment = neoq_bridge.build_route(
        surface_offset_m=surface_offset_m,
    )
    streetlight_placements, streetlight_report = (
        build_streetlight_placements(route_points)
    )
    seam_placement = penguin_neoq_seam.transition_placement()
    penguin_road_seam_placement = TerrainObjectPlacement(
        station_m=0.0,
        side="center",
        x=seam_placement.x,
        y=seam_placement.y,
        z=seam_placement.z,
        yaw_degrees=seam_placement.yaw_degrees,
        asset_id=seam_placement.asset_id,
        instance_name=seam_placement.instance_name,
    )
    (
        neoq_bridge_streetlight_placements,
        neoq_bridge_streetlight_report,
    ) = neoq_bridge.build_streetlights(neoq_bridge_points)
    segment["fixtures"] = streetlight_report
    segment["seams"] = penguin_neoq_seam.validate_seams(
        route_points,
        procedural_text=procedural_route_text(route_points),
        transition_asset_provenance=
            penguin_road_seam_asset.provenance,
    )
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
    neoq_bridge_segment["authentication"] = neoq_bridge_authentication
    neoq_bridge_segment["fixtures"] = neoq_bridge_streetlight_report
    neoq_bridge_ground_clearance = (
        neoq_bridge.validate_ground_road_clearance(
            neoq_bridge_segment,
            neoq_bridge_authentication,
        )
    )
    neoq_bridge_segment["obstacle_avoidance"] = {
        "destination_existing_lane_collision_preserved": True,
        "destination_generated_overlap_m": 0.0,
        "ground_level_support_clearance": neoq_bridge_ground_clearance,
        "source_existing_lane_collision_preserved": True,
        "source_flush_join_at_authenticated_mesh_edge": True,
        "source_generated_overlap_m": 0.0,
        "open_gap_placement_origin_audit":
            neoq_bridge_authentication["open_gap"],
        "swept_mesh_clearance":
            "native-multi-camera-and-drive-gate-required",
    }

    descriptor = terrain_descriptor(
        audit,
        source_telepoint,
        0.0,
    )
    placement = overlay_placement(
        route_points,
        (
            penguin_road_seam_placement,
            *streetlight_placements,
            *neoq_bridge_streetlight_placements,
        ),
        additional_routes=(
            (
                neoq_bridge_points,
                (
                    "Generated NeoQueretaro-to-NeoQ2.0 highway bridge.",
                    "Both decoded city-road seams merge flush with zero generated overlap.",
                    "Stations 80..760 are authored no-pillar above authenticated autopistaQr polygons.",
                    "Fifty-six paired side piers elsewhere clear the deck and heavy trucks.",
                ),
            ),
        ),
    )
    runtime_assets = (
        penguin_road_seam_asset,
        streetlight_asset,
        *tree_family.assets,
    )
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
    for wrapper in tree_family.wrappers:
        add_payload(
            payloads,
            wrapper.package_path,
            wrapper.payload,
        )
        package_roles[wrapper.package_path] = wrapper.role
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
    add_payload(
        payloads,
        NEOQ_TREE_REPLACEMENT_NAME,
        tree_replacement_payload,
    )
    package_roles[NEOQ_TREE_REPLACEMENT_NAME] = (
        "authenticated-in-place-tree-replacement-plan"
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
    tree_replacement_record = payload_record(
        NEOQ_TREE_REPLACEMENT_NAME,
        tree_replacement_payload,
        "authenticated-in-place-tree-replacement-plan",
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
        "city_visuals": {
            "neoq_trees": {
                "activation": tree_replacements["activation"],
                "family": tree_replacements["family"],
                "replacement_manifest": tree_replacement_record,
                "source": tree_replacements["source"],
                "summary": tree_replacements["summary"],
            },
        },
        "corridor": segment,
        "corridors": {
            "neoq_to_neoq20": neoq_bridge_segment,
            "penguinville_to_neoq": segment,
        },
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
                light_candidates["candidate_poles"] + 19 + 5,
            "source_textures_copied": False,
        },
        "visual_asset_usage": {
            "corridor_placement_mode":
                "native-procedural-v5-two-corridor-open-seams-side-piers-with-"
                "blender-transition-v2",
            "disabled_light_candidate_manifest":
                NEOQ_LIGHT_CANDIDATE_NAME,
            "neoq_core_runtime_light_activation": "blocked-fail-closed",
            "packaged_asset_ids": [
                asset.asset_id
                for asset in runtime_assets
            ],
            "placed_asset_ids": [
                PENGUIN_ROAD_SEAM_ASSET_ID,
                LED_STREETLIGHT_ASSET_ID,
                *[
                    asset.asset_id
                    for asset in tree_family.assets
                ],
            ],
            "unplaced_asset_ids": [
                asset.asset_id
                for asset in corridor_assets
            ],
            "validated_asset_ids": [
                asset.asset_id
                for asset in assets
            ],
            "purpose":
                "authenticated Penguinville curb-bearing T-junction replacement "
                "plus a crowned-to-flat Blender road transition inheriting the "
                "procedural road2 surface and marking atlas, open procedural collision "
                "endcaps, paired outboard bridge piers, and route-safe Blender "
                "lighting; a second raised bridge leaves NeoQueretaro from an "
                "authenticated flush mesh edge and merges flush at NeoQ2.0 without "
                "covering its median or live lanes, with continuous collision, "
                "18 polygon-authenticated no-pillar stations above autopistaQr, "
                "paired outboard terrain-reaching side piers elsewhere, and bounded "
                "LED fixtures; all 18 authenticated legacy NeoQueretaro trees "
                "are replaced in place by the rights-cleared three-variant "
                "family with per-instance visual/collision scale wrappers; "
                "deterministic NeoQueretaro pole-light candidates remain "
                "disabled pending the bounded renderer light budget and "
                "fixed-camera visual gate; bridge modules remain "
                "validated candidates for deck and abutment replacement",
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
                "tree_replacement_manifest":
                    NEOQ_TREE_REPLACEMENT_NAME,
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
            tree_replacement_record,
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
