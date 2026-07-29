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


FORMAT = "ror-cityworld-local-overlay-v1"
BUILD_RESULT_FORMAT = "ror-cityworld-local-overlay-build-result-v1"
PINNED_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)
SOURCE_TELEPOINT = "Penguinville Spawn"
DESTINATION_TELEPOINT = "NeoQueretaro Spawn"
SOURCE_MEMBERS = ("CityWorld.terrn2", "CityWorld.otc", "CityWorld.tobj")
TERRAIN_NAME = "CityWorldNextLocalOverlay.terrn2"
OVERLAY_NAME = "cityworld_next_local_overlay.tobj"
REPORT_NAME = "cityworld_next_local_overlay.report.json"
MIN_SURFACE_OFFSET_M = -2.0
MAX_SURFACE_OFFSET_M = 20.0
MAX_RUNTIME_FILE_BYTES = 256 * 1024 * 1024
OUTPUT_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*\.zip$")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644

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
    centerline_length_m: float
    manifest_path: str
    profile: AssetProfile
    provenance: dict[str, Any]
    runtime_files: tuple[RuntimeFile, ...]


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


def prepare_asset(repository: Path, manifest_relative: str) -> PreparedAsset:
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
    if role_counts != {
        "collision-barrier": 2,
        "collision-road": 1,
        "material-fallback": 1,
        "render-lod0": 1,
        "render-lod1": 1,
        "render-lod2": 1,
        "terrain-object": 1,
    }:
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
    profile = load_asset_profile(repository, manifest_relative)
    return PreparedAsset(
        asset_id=compiler.asset_id,
        centerline_length_m=asset_centerline_length(
            compiler.manifest,
            compiler.asset_id,
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
        "Version = 2",
        "GUID = rorng-cityworld-next-local-overlay-v1",
        "",
        "[Authors]",
        "overlay = Oasiz AI and Rigs of Rods contributors",
        "",
        "[Objects]",
        "CityWorld.tobj =",
        f"{OVERLAY_NAME} =",
        "",
        "[Scripts]",
        "",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def overlay_placement(
    placements: Sequence[Placement],
    *,
    surface_y: float,
) -> bytes:
    header = (
        "// LOCAL-ONLY: requires the pinned user-supplied CityWorld.zip.\n"
        "// Redistribution and shipping are disabled.\n"
    )
    return (header + tobj_text(placements, surface_y=surface_y)).encode("utf-8")


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
    source = exact_telepoint(audit, SOURCE_TELEPOINT)
    destination = exact_telepoint(audit, DESTINATION_TELEPOINT)
    assets = prepare_assets(repository)
    profiles, placements, surface_y, segment = solve_segment(
        assets,
        source,
        destination,
        surface_offset_m,
    )
    if len(profiles) != len(MODULE_ASSET_IDS):
        raise OverlayFailure("corridor module count is stale")

    descriptor = terrain_descriptor(
        audit,
        source,
        segment["heading"]["initial_heading_degrees"],
    )
    placement = overlay_placement(placements, surface_y=surface_y)
    payloads: dict[str, bytes] = {}
    add_payload(payloads, TERRAIN_NAME, descriptor)
    add_payload(payloads, OVERLAY_NAME, placement)
    for asset in assets:
        for runtime_file in asset.runtime_files:
            add_payload(
                payloads,
                runtime_file.package_path,
                runtime_file.payload,
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
            (
                "derived-terrain"
                if name == TERRAIN_NAME
                else "overlay-placement"
                if name == OVERLAY_NAME
                else next(
                    runtime_file.role
                    for asset in assets
                    for runtime_file in asset.runtime_files
                    if runtime_file.package_path == name
                )
            ),
        )
        for name, payload in sorted(payloads.items())
    ]
    audit_payload = canonical_json_bytes(audit)
    report = {
        "assets": [asset.provenance for asset in assets],
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
            "source_placements_copied": False,
            "source_textures_copied": False,
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
        with temporary_path.open("rb") as stream:
            os.fsync(stream.fileno())
        publish_no_replace(temporary_path, output)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass

    return {
        "format": BUILD_RESULT_FORMAT,
        "generated": [descriptor_record, placement_record],
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
