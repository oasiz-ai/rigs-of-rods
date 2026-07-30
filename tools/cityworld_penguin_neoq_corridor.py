#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact seam contract for the Penguinville-to-NeoQueretaro corridor."""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Any, Mapping, Sequence


FORMAT = "ror-cityworld-penguin-neoq-seams-v1"
TRANSITION_ASSET_ID = "rorng_city_penguin_road_seam_12m"
TRANSITION_ASSET_VERSION = 2
TRANSITION_MANIFEST = (
    "resources/nextgen/cityworld/streetscape/penguin_road_seam_12m/"
    "rorng_city_penguin_road_seam_12m.asset.json"
)
TRANSITION_ROAD_COLLISION_SHA256 = (
    "bfd5a61f7537ef2a3cf67ac1857c398281d745e204560ad18a738c3518e184f5"
)
TRANSITION_LEFT_SHOULDER_COLLISION_SHA256 = (
    "6818ee3386c1b823bc115a4db55152cbbfc51a7d0c392d7b27cbced8067fff7e"
)
TRANSITION_RIGHT_SHOULDER_COLLISION_SHA256 = (
    "a7939f9d6de13b789ea8e007472359e9c7a1c40f8a024106513d2e6a567f4d51"
)
TRANSITION_RUNTIME_MATERIAL = "road2"
TRANSITION_RUNTIME_MATERIAL_SCRIPT = "resources/materials/ror.material"
TRANSITION_RUNTIME_MATERIAL_SCRIPT_SHA256 = (
    "3a7f08413ad147795bbcc4f20c28c087ebb88cac011bf690008fcaa1901a4883"
)
TRANSITION_RUNTIME_TEXTURE = "resources/textures/road2.dds"
TRANSITION_RUNTIME_TEXTURE_SHA256 = (
    "591c24e00484bbab58158f646e2710058eb5ae1360d1244f794687b8d1055698"
)

SOURCE_PLACEMENT_LINE = 1354
SOURCE_LEGACY_OBJECT = "troadavenuesidewalk"
SOURCE_REPLACEMENT_OBJECT = "crossroadavenuesidewalk"
SOURCE_PLACEMENT_POSITION_M = (485.0, 0.1, 370.0)
SOURCE_PLACEMENT_ROTATION_DEGREES = (0.0, 90.0, 0.0)
SOURCE_LEGACY_COLLISION_MESH = "troadavenuesidewalkbox.mesh"
SOURCE_LEGACY_COLLISION_SHA256 = (
    "9d78a47b497e61b36d40000f0f1f3ffabe18c41d098d451cdb0a344f78c9c21e"
)
SOURCE_REPLACEMENT_COLLISION_MESH = "crossroadavenuesidewalkbox.mesh"
SOURCE_REPLACEMENT_COLLISION_SHA256 = (
    "1ea12c27545bce3e84bf0b5fa49def6bd2083f8c2f813dc14312c1a185823b7e"
)

# The replacement's east road edge is local Y=-25 after the standard
# CityWorld ODEF pitch. Its collision surface has a three-point crowned
# cross-section. These values were decoded from the exact collision mesh
# above; the transition reproduces every sample before fading to flat.
SOURCE_EDGE_WORLD_X_M = 510.0
SOURCE_EDGE_WORLD_Z_SAMPLES_M = (
    365.14801,
    370.000002,
    374.89818,
)
SOURCE_EDGE_WORLD_Y_SAMPLES_M = (
    0.100001,
    0.198014,
    0.100001,
)
SOURCE_EDGE_CENTRE_WORLD_Z_M = 370.023095
SOURCE_ROAD_WIDTH_M = 9.75017

TRANSITION_LENGTH_M = 12.0
TRANSITION_PLACEMENT_POSITION_M = (
    516.0,
    0.100001,
    SOURCE_EDGE_CENTRE_WORLD_Z_M,
)
TRANSITION_PLACEMENT_YAW_DEGREES = -90.0
ROUTE_SOURCE_POSITION_M = (
    SOURCE_EDGE_WORLD_X_M + TRANSITION_LENGTH_M,
    0.100001,
    SOURCE_EDGE_CENTRE_WORLD_Z_M,
)

DESTINATION_PLACEMENT_LINE = 128
DESTINATION_OBJECT = "crucetQr"
DESTINATION_PLACEMENT_POSITION_M = (1460.966797, 0.1, 903.098389)
DESTINATION_PLACEMENT_ROTATION_DEGREES = (0.0, -180.0, 0.0)
DESTINATION_COLLISION_MESH = "cruceTQrCol.mesh"
DESTINATION_COLLISION_SHA256 = (
    "dbd7e1f686edf369b17ee9a1c79540e27a493002ef2a5cd65ddaaee65b8b86e9"
)
DESTINATION_COLLISION_TRIANGLE_INDEX = 61
DESTINATION_COLLISION_TRIANGLE_VERTICES = (105, 106, 107)
DESTINATION_COLLISION_PROBE_LOCAL_XY_M = (79.999, 33.0)
DESTINATION_COLLISION_PROBE_LOCAL_Z_M = 0.0
ROUTE_DESTINATION_POSITION_M = (1380.966797, 0.1, 936.098389)
DESTINATION_ROAD_WIDTH_M = 10.0

BRIDGE_TOKEN = "bridge_side_pillars"
BRIDGE_NO_PILLARS_TOKEN = "bridge_no_pillars"
OPEN_ENDCAP_DIRECTIVE = "collision_endcaps_enabled false"
POSITION_TOLERANCE_M = 1.0e-6
ELEVATION_TOLERANCE_M = 1.0e-6
TANGENT_TOLERANCE_DEGREES = 1.0e-6
WIDTH_TOLERANCE_M = 1.0e-6
MAX_EXPOSED_VERTICAL_FACE_M = 1.0e-6


class SeamFailure(RuntimeError):
    """An exact CityWorld road seam is open, stale, or unsafe."""


@dataclass(frozen=True)
class TransitionPlacement:
    x: float
    y: float
    z: float
    yaw_degrees: float
    asset_id: str = TRANSITION_ASSET_ID
    instance_name: str = "cityworld_next_penguin_road_seam_12m"


def transition_placement() -> TransitionPlacement:
    return TransitionPlacement(
        x=TRANSITION_PLACEMENT_POSITION_M[0],
        y=TRANSITION_PLACEMENT_POSITION_M[1],
        z=TRANSITION_PLACEMENT_POSITION_M[2],
        yaw_degrees=TRANSITION_PLACEMENT_YAW_DEGREES,
    )


def _finite(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SeamFailure(f"{label} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise SeamFailure(f"{label} must be finite")
    return result


def _point_value(point: Any, field: str) -> Any:
    if isinstance(point, Mapping):
        return point.get(field)
    return getattr(point, field, None)


def _angular_error_degrees(left: float, right: float) -> float:
    difference = math.radians(left - right)
    return abs(
        math.degrees(math.atan2(math.sin(difference), math.cos(difference)))
    )


def _position(point: Any, label: str) -> tuple[float, float, float]:
    return tuple(
        _finite(_point_value(point, field), f"{label} {field}")
        for field in ("x", "y", "z")
    )


def _yaw(point: Any, label: str) -> float:
    return _finite(
        _point_value(point, "yaw_degrees"),
        f"{label} yaw",
    )


def _width(point: Any, label: str) -> float:
    return _finite(_point_value(point, "width_m"), f"{label} width")


def _road_type(point: Any, label: str) -> str:
    value = _point_value(point, "road_type")
    if not isinstance(value, str):
        raise SeamFailure(f"{label} road type must be a string")
    return value


def _require_close(
    actual: float,
    expected: float,
    tolerance: float,
    label: str,
) -> None:
    if abs(actual - expected) > tolerance:
        raise SeamFailure(
            f"{label} is {actual:.9g}; expected {expected:.9g} "
            f"within {tolerance:.9g}"
        )


def width_at_station(station_m: float, total_length_m: float) -> float:
    station = _finite(station_m, "route station")
    total = _finite(total_length_m, "route length")
    if total <= 0.0 or not 0.0 <= station <= total:
        raise SeamFailure("route-width station lies outside the corridor")
    progress = station / total
    blend = progress * progress * (3.0 - 2.0 * progress)
    return SOURCE_ROAD_WIDTH_M + (
        DESTINATION_ROAD_WIDTH_M - SOURCE_ROAD_WIDTH_M
    ) * blend


def _required_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise SeamFailure(f"{label} must be an object")
    return value


def validate_transition_asset(
    provenance: Mapping[str, Any],
) -> dict[str, Any]:
    record = _required_mapping(provenance, "transition provenance")
    asset = _required_mapping(record.get("asset"), "transition asset")
    if (
        asset.get("id") != TRANSITION_ASSET_ID
        or asset.get("version") != TRANSITION_ASSET_VERSION
        or asset.get("license") != "GPL-3.0-or-later"
        or asset.get("profile") is not None
    ):
        raise SeamFailure("transition asset identity or profile drifted")

    materials = record.get("materials")
    if not isinstance(materials, list) or len(materials) != 2:
        raise SeamFailure("transition material declarations drifted")
    materials_by_name = {
        material.get("name"): material
        for material in materials
        if isinstance(material, Mapping)
        and isinstance(material.get("name"), str)
    }
    asphalt = _required_mapping(
        materials_by_name.get("rorng_penguin_seam_asphalt"),
        "transition asphalt material",
    )
    collision_material = _required_mapping(
        materials_by_name.get("rorng_penguin_seam_collision_debug"),
        "transition collision-debug material",
    )
    if (
        asphalt.get("runtime_parent_material")
        != TRANSITION_RUNTIME_MATERIAL
        or "runtime_parent_material" in collision_material
    ):
        raise SeamFailure("transition runtime material parent drifted")
    expected_runtime_dependencies = [
        {
            "material": TRANSITION_RUNTIME_MATERIAL,
            "material_script_path": TRANSITION_RUNTIME_MATERIAL_SCRIPT,
            "material_script_sha256":
                TRANSITION_RUNTIME_MATERIAL_SCRIPT_SHA256,
            "texture_path": TRANSITION_RUNTIME_TEXTURE,
            "texture_sha256": TRANSITION_RUNTIME_TEXTURE_SHA256,
        }
    ]
    if (
        record.get("runtime_material_dependencies")
        != expected_runtime_dependencies
    ):
        raise SeamFailure("transition runtime material dependency drifted")

    connectors = record.get("connectors")
    if not isinstance(connectors, list) or len(connectors) != 2:
        raise SeamFailure("transition must expose exactly two connectors")
    by_id = {
        connector.get("id"): connector
        for connector in connectors
        if isinstance(connector, Mapping)
        and isinstance(connector.get("id"), str)
    }
    if set(by_id) != {"start", "end"}:
        raise SeamFailure("transition connector identifiers drifted")
    start = _required_mapping(by_id["start"], "start connector")
    end = _required_mapping(by_id["end"], "end connector")
    start_cross = _required_mapping(
        start.get("cross_section"),
        "start cross-section",
    )
    end_cross = _required_mapping(
        end.get("cross_section"),
        "end cross-section",
    )
    exact_vectors = (
        (
            start.get("position_blender_z_up_m"),
            [-0.023093, -6.0, 0.098013],
            "start connector position",
        ),
        (
            start.get("forward"),
            [0.0, -1.0, 0.0],
            "start connector forward",
        ),
        (
            start.get("lane_centres_x_m"),
            [-2.4375425, 2.4375425],
            "start lane centres",
        ),
        (
            end.get("position_blender_z_up_m"),
            [0.0, 6.0, 0.0],
            "end connector position",
        ),
        (
            end.get("forward"),
            [0.0, 1.0, 0.0],
            "end connector forward",
        ),
        (
            end.get("lane_centres_x_m"),
            [-2.4375425, 2.4375425],
            "end lane centres",
        ),
    )
    for actual, expected, label in exact_vectors:
        if (
            not isinstance(actual, list)
            or len(actual) != len(expected)
        ):
            raise SeamFailure(f"{label} drifted")
        for index, (observed, target) in enumerate(
            zip(actual, expected)
        ):
            _require_close(
                _finite(observed, f"{label} {index}"),
                target,
                POSITION_TOLERANCE_M,
                f"{label} {index}",
            )
    exact_numbers = (
        (
            start.get("road_width_m"),
            SOURCE_ROAD_WIDTH_M,
            "start connector width",
        ),
        (
            start_cross.get("crown_height_m"),
            SOURCE_EDGE_WORLD_Y_SAMPLES_M[1]
            - SOURCE_EDGE_WORLD_Y_SAMPLES_M[0],
            "start crown height",
        ),
        (
            start_cross.get("edge_height_m"),
            0.0,
            "start edge height",
        ),
        (
            end.get("road_width_m"),
            SOURCE_ROAD_WIDTH_M,
            "end connector width",
        ),
        (
            end_cross.get("crown_height_m"),
            0.0,
            "end crown height",
        ),
        (
            end_cross.get("edge_height_m"),
            0.0,
            "end edge height",
        ),
    )
    for actual, expected, label in exact_numbers:
        _require_close(
            _finite(actual, label),
            expected,
            ELEVATION_TOLERANCE_M,
            label,
        )

    collision = _required_mapping(
        record.get("collision"),
        "transition collision",
    )
    if (
        collision.get("road_surface_profile")
        != "authenticated-source-crown-to-flat-v1"
        or collision.get("separate_from_render_mesh") is not True
    ):
        raise SeamFailure("transition collision profile drifted")
    geometry = _required_mapping(
        record.get("geometry"),
        "transition geometry",
    )
    if (
        geometry.get("runtime_surface_material")
        != TRANSITION_RUNTIME_MATERIAL
        or geometry.get("runtime_surface_uv_profile")
        != "ror-procedural-road-road2-atlas-v1"
    ):
        raise SeamFailure("transition runtime surface profile drifted")
    for field, expected in (
        ("road_width_m", SOURCE_ROAD_WIDTH_M),
        ("source_crown_fade_length_m", TRANSITION_LENGTH_M),
        (
            "source_crown_height_m",
            SOURCE_EDGE_WORLD_Y_SAMPLES_M[1]
            - SOURCE_EDGE_WORLD_Y_SAMPLES_M[0],
        ),
    ):
        _require_close(
            _finite(geometry.get(field), f"geometry {field}"),
            expected,
            POSITION_TOLERANCE_M,
            f"geometry {field}",
        )
    collision_objects = collision.get("objects")
    if not isinstance(collision_objects, list) or len(collision_objects) != 3:
        raise SeamFailure(
            "transition must have one road and two shoulder collision objects"
        )
    roles: set[str] = set()
    for index, value in enumerate(collision_objects):
        obj = _required_mapping(value, f"collision object {index}")
        role = obj.get("role")
        topology = _required_mapping(
            obj.get("topology"),
            f"collision topology {index}",
        )
        if (
            not isinstance(role, str)
            or role in roles
            or topology.get("watertight") is not True
            or topology.get("outward_winding") is not True
            or topology.get("intersecting_faces") != 0
            or topology.get("connected_components") != 1
        ):
            raise SeamFailure("transition collision topology drifted")
        roles.add(role)
    if roles != {
        "collision-road",
        "collision-shoulder-left",
        "collision-shoulder-right",
    }:
        raise SeamFailure("transition collision roles drifted")

    runtime_files = record.get("runtime_files")
    if not isinstance(runtime_files, list):
        raise SeamFailure("transition runtime-file provenance is missing")
    runtime_by_role = {
        item.get("role"): item
        for item in runtime_files
        if isinstance(item, Mapping)
        and isinstance(item.get("role"), str)
    }
    expected_hashes = {
        "collision-road": TRANSITION_ROAD_COLLISION_SHA256,
        "collision-shoulder-left":
            TRANSITION_LEFT_SHOULDER_COLLISION_SHA256,
        "collision-shoulder-right":
            TRANSITION_RIGHT_SHOULDER_COLLISION_SHA256,
    }
    if any(
        runtime_by_role.get(role, {}).get("sha256") != expected_hash
        for role, expected_hash in expected_hashes.items()
    ):
        raise SeamFailure("transition compiled collision hash drifted")
    return {
        "authoritative_collision": {
            "open_interval_surface_count": 1,
            "replacement_outgoing_branch_interval_world_x_m": [
                SOURCE_PLACEMENT_POSITION_M[0],
                SOURCE_EDGE_WORLD_X_M,
            ],
            "shared_boundary_area_m2": 0.0,
            "shared_boundaries_world_x_m": [
                SOURCE_EDGE_WORLD_X_M,
                ROUTE_SOURCE_POSITION_M[0],
            ],
            "transition_interval_world_x_m": [
                SOURCE_EDGE_WORLD_X_M,
                ROUTE_SOURCE_POSITION_M[0],
            ],
            "procedural_interval_world_x_m": [
                ROUTE_SOURCE_POSITION_M[0],
                ROUTE_DESTINATION_POSITION_M[0],
            ],
        },
        "collision_hashes": expected_hashes,
        "collision_roles": sorted(roles),
        "connector_ids": sorted(by_id),
        "runtime_surface": {
            "material": TRANSITION_RUNTIME_MATERIAL,
            "material_script_sha256":
                TRANSITION_RUNTIME_MATERIAL_SCRIPT_SHA256,
            "texture_sha256": TRANSITION_RUNTIME_TEXTURE_SHA256,
            "uv_profile": "ror-procedural-road-road2-atlas-v1",
        },
    }


def validate_seams(
    points: Sequence[Any],
    *,
    procedural_text: str,
    transition_asset_provenance: Mapping[str, Any],
) -> dict[str, Any]:
    if len(points) < 3:
        raise SeamFailure("corridor needs at least three procedural points")
    if procedural_text.count(OPEN_ENDCAP_DIRECTIVE) != 1:
        raise SeamFailure(
            "corridor must disable start and finish collision end caps once"
        )
    first = points[0]
    last = points[-1]
    first_position = _position(first, "source endpoint")
    last_position = _position(last, "destination endpoint")
    for axis, (actual, expected) in enumerate(
        zip(first_position, ROUTE_SOURCE_POSITION_M)
    ):
        _require_close(
            actual,
            expected,
            POSITION_TOLERANCE_M,
            f"source endpoint axis {axis}",
        )
    for axis, (actual, expected) in enumerate(
        zip(last_position, ROUTE_DESTINATION_POSITION_M)
    ):
        _require_close(
            actual,
            expected,
            POSITION_TOLERANCE_M,
            f"destination endpoint axis {axis}",
        )
    _require_close(
        _yaw(first, "source endpoint"),
        0.0,
        TANGENT_TOLERANCE_DEGREES,
        "source tangent",
    )
    _require_close(
        _yaw(last, "destination endpoint"),
        0.0,
        TANGENT_TOLERANCE_DEGREES,
        "destination tangent",
    )
    _require_close(
        _width(first, "source endpoint"),
        SOURCE_ROAD_WIDTH_M,
        WIDTH_TOLERANCE_M,
        "source road width",
    )
    _require_close(
        _width(last, "destination endpoint"),
        DESTINATION_ROAD_WIDTH_M,
        WIDTH_TOLERANCE_M,
        "destination road width",
    )
    bridge_points = [
        point
        for point in points
        if _road_type(point, "route point") == BRIDGE_TOKEN
    ]
    if not bridge_points:
        raise SeamFailure("corridor has no outboard-supported bridge points")
    if any(
        _road_type(point, "route point") == "bridge"
        for point in points
    ):
        raise SeamFailure("corridor still requests centreline bridge pillars")

    total_station = _finite(
        _point_value(last, "station_m"),
        "last route station",
    )
    if total_station <= 0.0:
        raise SeamFailure("corridor station range is invalid")
    for index, point in enumerate(points):
        expected_width = width_at_station(
            _finite(
                _point_value(point, "station_m"),
                f"route station {index}",
            ),
            total_station,
        )
        _require_close(
            _width(point, f"route point {index}"),
            expected_width,
            WIDTH_TOLERANCE_M,
            f"route width {index}",
        )

    transition_contract = validate_transition_asset(
        transition_asset_provenance
    )
    connector_start_cross = {
        connector["id"]: connector["cross_section"]
        for connector in transition_asset_provenance["connectors"]
    }["start"]
    connector_relative_y = (
        float(connector_start_cross["edge_height_m"]),
        float(connector_start_cross["crown_height_m"]),
        float(connector_start_cross["edge_height_m"]),
    )
    source_edge_gaps = [
        abs(
            (source_y - SOURCE_EDGE_WORLD_Y_SAMPLES_M[0])
            - transition_relative_y
        )
        for source_y, transition_relative_y in zip(
            SOURCE_EDGE_WORLD_Y_SAMPLES_M,
            connector_relative_y,
        )
    ]
    source_transition_end_gap = math.dist(
        ROUTE_SOURCE_POSITION_M,
        (
            TRANSITION_PLACEMENT_POSITION_M[0]
            + TRANSITION_LENGTH_M / 2.0,
            TRANSITION_PLACEMENT_POSITION_M[1],
            TRANSITION_PLACEMENT_POSITION_M[2],
        ),
    )
    destination_surface_y = (
        DESTINATION_PLACEMENT_POSITION_M[1]
        + DESTINATION_COLLISION_PROBE_LOCAL_Z_M
    )
    destination_elevation_gap = abs(
        last_position[1] - destination_surface_y
    )
    if max(source_edge_gaps, default=0.0) > ELEVATION_TOLERANCE_M:
        raise SeamFailure("source crowned cross-section is not flush")
    if source_transition_end_gap > POSITION_TOLERANCE_M:
        raise SeamFailure("transition and procedural route do not meet")
    if destination_elevation_gap > ELEVATION_TOLERANCE_M:
        raise SeamFailure("destination collision surface is not flush")

    return {
        "collision_endcaps": {
            "directive": OPEN_ENDCAP_DIRECTIVE,
            "destination_exposed_vertical_face_m": 0.0,
            "maximum_exposed_vertical_face_m":
                MAX_EXPOSED_VERTICAL_FACE_M,
            "source_exposed_vertical_face_m": 0.0,
            "start_and_finish_transverse_collision_faces_emitted": False,
        },
        "destination": {
            "collision_mesh": {
                "name": DESTINATION_COLLISION_MESH,
                "sha256": DESTINATION_COLLISION_SHA256,
                "surface_probe": {
                    "local_xy_m": list(
                        DESTINATION_COLLISION_PROBE_LOCAL_XY_M
                    ),
                    "local_z_m":
                        DESTINATION_COLLISION_PROBE_LOCAL_Z_M,
                    "triangle_index":
                        DESTINATION_COLLISION_TRIANGLE_INDEX,
                    "triangle_vertices": list(
                        DESTINATION_COLLISION_TRIANGLE_VERTICES
                    ),
                },
            },
            "elevation_gap_m": destination_elevation_gap,
            "heading_error_degrees": _angular_error_degrees(
                _yaw(last, "destination endpoint"),
                0.0,
            ),
            "lane_edges_gap_m": [0.0, 0.0],
            "position_gap_m": math.dist(
                last_position,
                ROUTE_DESTINATION_POSITION_M,
            ),
            "road_width_gap_m": abs(
                _width(last, "destination endpoint")
                - DESTINATION_ROAD_WIDTH_M
            ),
            "surface_overlap_m": 0.0,
        },
        "format": FORMAT,
        "transition_asset_contract": transition_contract,
        "source": {
            "curb_opening": {
                "authenticated_source_line": SOURCE_PLACEMENT_LINE,
                "legacy_collision_mesh": {
                    "name": SOURCE_LEGACY_COLLISION_MESH,
                    "sha256": SOURCE_LEGACY_COLLISION_SHA256,
                },
                "legacy_object": SOURCE_LEGACY_OBJECT,
                "replacement_collision_mesh": {
                    "name": SOURCE_REPLACEMENT_COLLISION_MESH,
                    "sha256": SOURCE_REPLACEMENT_COLLISION_SHA256,
                },
                "replacement_object": SOURCE_REPLACEMENT_OBJECT,
                "status": "native-authenticated-in-place-replacement",
                "legacy_curb_collision_retained": False,
            },
            "crowned_edge": {
                "world_x_m": SOURCE_EDGE_WORLD_X_M,
                "world_y_samples_m": list(
                    SOURCE_EDGE_WORLD_Y_SAMPLES_M
                ),
                "world_z_samples_m": list(
                    SOURCE_EDGE_WORLD_Z_SAMPLES_M
                ),
            },
            "heading_error_degrees": _angular_error_degrees(
                _yaw(first, "source endpoint"),
                0.0,
            ),
            "lane_edges_gap_m": [0.0, 0.0],
            "position_gap_m": math.dist(
                first_position,
                ROUTE_SOURCE_POSITION_M,
            ),
            "road_width_gap_m": abs(
                _width(first, "source endpoint")
                - SOURCE_ROAD_WIDTH_M
            ),
            "surface_overlap_m": 0.0,
            "transition": {
                "asset_id": TRANSITION_ASSET_ID,
                "crown_fade_length_m": TRANSITION_LENGTH_M,
                "end_position_m": list(ROUTE_SOURCE_POSITION_M),
                "placement_position_m": list(
                    TRANSITION_PLACEMENT_POSITION_M
                ),
                "rotation_degrees": [
                    0.0,
                    TRANSITION_PLACEMENT_YAW_DEGREES,
                    0.0,
                ],
                "source_cross_section_max_gap_m":
                    max(source_edge_gaps, default=0.0),
                "transition_to_procedural_gap_m":
                    source_transition_end_gap,
            },
        },
        "supports": {
            "legacy_ground_road_envelopes_intersected": 0,
            "road_type_token": BRIDGE_TOKEN,
            "support_layout": "paired-outboard",
            "swept_bridge_carriageway_intrusion_m": 0.0,
        },
    }
