#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Authenticated NeoQueretaro-to-NeoQ2.0 bridge corridor.

The route joins two existing highway stubs in the pinned private CityWorld
archive.  Only placement coordinates and hashes are derived from that archive;
the generated road, barriers, supports, collision, and fixture placements are
project-owned procedural content.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
from typing import Any, Iterable, Sequence
import zipfile


FORMAT = "ror-cityworld-neoq-intercity-bridge-v2"
AUTHENTICATION_FORMAT = "ror-cityworld-neoq-bridge-authentication-v1"
PINNED_TOBJ_SHA256 = (
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48"
)
POSITION_EPSILON = 1.0e-6
ANGLE_EPSILON_DEGREES = 1.0e-6
READ_CHUNK_BYTES = 1024 * 1024

# Both source meshes are native Y-up assets. Their authored TOBJ +90 degree
# X rotations cancel RoR's legacy -90 degree terrain-object pitch.
SOURCE_PLACEMENT = {
    "city": "NeoQueretaro",
    "connection": "east distributor highway stub",
    "line_number": 366,
    "object": "distribuidorQr",
    "position_m": (3676.970703, 0.3, 3993.104004),
    "rotation_degrees": (90.0, 0.0, 0.0),
}
DESTINATION_PLACEMENT = {
    "city": "NeoQ2.0",
    "connection": "west industrial distributor highway stub",
    "line_number": 1230,
    "object": "NeoQ2-0industrial-zone-distributor-road",
    "position_m": (7000.0, 50.0, 4018.0),
    "rotation_degrees": (90.0, 0.0, 0.0),
}

# These seam positions were decoded from the exact render meshes below and
# checked against their separately hashed collision meshes. The source keeps a
# ten-metre overlap because it is a single open carriageway. The destination is
# a divided highway: the generated road stops flush at its west mesh edge so it
# cannot cover the median, raise a transverse ridge, or block either live lane.
SOURCE_SEAM = (3790.970703, 0.1, 3993.104004)
SOURCE_OVERLAP_START = (3780.970703, 0.1, 3993.104004)
DESTINATION_SEAM = (6867.0, 0.2, 4018.0)
SOURCE_LOCAL_SEAM = (114.0, -0.2, 0.0)
DESTINATION_LOCAL_SEAM = (-133.0, 0.2, 0.0)
SOURCE_RUNTIME_PLACEMENT_ORIGIN = SOURCE_PLACEMENT["position_m"]
DESTINATION_RUNTIME_PLACEMENT_ORIGIN = (
    DESTINATION_PLACEMENT["position_m"][0],
    0.0,
    DESTINATION_PLACEMENT["position_m"][2],
)
SOURCE_OVERLAP_LENGTH_M = 10.0
DESTINATION_OVERLAP_LENGTH_M = 0.0

# Exact decoded destination collision cross-section at local x=-133 m. The
# outer barriers occupy 7.55..8.15 m on either side, the median occupies
# -0.7..0.7 m, and both carriageway surfaces are y=0.2 m. The generated
# endpoint narrows to the 15.1 m inner-barrier span and terminates at x=-133,
# preserving every independently authored destination collision triangle.
DESTINATION_COLLISION_MEMBER = (
    "NeoQ2-0industrial-zone-distributor-roadColisionante.mesh"
)
DESTINATION_COLLISION_SHA256 = (
    "1b6dc9c956bfe74c6a920317380a308075ca643207328dee78b658b1bda16283"
)
DESTINATION_OUTER_BOUNDS_LOCAL_Z_M = (-8.15, 8.15)
DESTINATION_OPEN_CARRIAGEWAYS_LOCAL_Z_M = (
    (-7.55, -0.7),
    (0.7, 7.55),
)
DESTINATION_MEDIAN_LOCAL_Z_M = (-0.7, 0.7)
DESTINATION_MERGE_WIDTH_M = 15.1
DESTINATION_LANE_CENTER_LOCAL_Z_M = 4.125
DESTINATION_LANE_HANDOFF = (
    DESTINATION_SEAM[0],
    DESTINATION_SEAM[1],
    DESTINATION_SEAM[2] + DESTINATION_LANE_CENTER_LOCAL_Z_M,
)
DESTINATION_WIDTH_TAPER_LENGTH_M = 160.0

AUTHENTICATED_MEMBERS = (
    {
        "name": "distribuidorQr.mesh",
        "role": "source-render-mesh",
        "size": 487679,
        "sha256":
            "66c788a68dc935f9c6f608c620888196aaca751c54ba57949956a6d22a22498e",
    },
    {
        "name": "distribuidorQrCol.mesh",
        "role": "source-collision-mesh",
        "size": 505770,
        "sha256":
            "b7be0f0158e4a0a6e3595b7a9da74da42fa190b97bde45138f3f62758de978be",
    },
    {
        "name": "distribuidorQr.odef",
        "role": "source-object-definition",
        "size": 82,
        "sha256":
            "e4f611fdacf0c18d0ea419147d3e019e3294c5c937376bc6448927db7669a30a",
    },
    {
        "name": "NeoQ2-0industrial-zone-distributor-road.mesh",
        "role": "destination-render-mesh",
        "size": 6440343,
        "sha256":
            "ea5abc8612f02d3159e222e336a76a9019e729c69011d7488626fa9572bc6e82",
    },
    {
        "name":
            "NeoQ2-0industrial-zone-distributor-roadColisionante.mesh",
        "role": "destination-collision-mesh",
        "size": 580046,
        "sha256":
            "1b6dc9c956bfe74c6a920317380a308075ca643207328dee78b658b1bda16283",
    },
    {
        "name": "NeoQ2-0industrial-zone-distributor-road.odef",
        "role": "destination-object-definition",
        "size": 139,
        "sha256":
            "044bf09f0e0c2b9c032f651596dd9d5507032b523234d76d8e0f6f123ac589e0",
    },
)

SAMPLE_SPACING_M = 40.0
TANGENT_HANDLE_M = 240.0
GROUND_LEAD_M = 40.0
RAMP_LENGTH_M = 160.0
DECK_CLEARANCE_M = 8.0
ROAD_WIDTH_M = 24.0
APPROACH_BORDER_WIDTH_M = 0.0
APPROACH_BORDER_HEIGHT_M = 0.0
BRIDGE_BORDER_WIDTH_M = 0.45
BRIDGE_BORDER_HEIGHT_M = 0.95
MAXIMUM_GRADE = 0.075
MAXIMUM_CONNECTION_STEP_M = 0.01
MAXIMUM_CONNECTION_GRADE_DISCONTINUITY = 0.0025
MAXIMUM_CONNECTION_YAW_DISCONTINUITY_DEGREES = 0.1
ARC_TABLE_STEPS = 16384
OPEN_GAP_HALF_WIDTH_M = 64.0
SUPPORT_ENDPOINT_EXCLUSION_M = 80.0
SUPPORT_COLUMN_HALF_EXTENT_M = 0.65
SUPPORT_TRUCK_LATERAL_CLEARANCE_M = 2.5
SUPPORT_COLUMN_TOP_BELOW_SURFACE_M = 0.45
SUPPORT_HAMMERHEAD_TOP_BELOW_SURFACE_M = 0.45
SUPPORT_HAMMERHEAD_THICKNESS_M = 0.6
SUPPORT_HAMMERHEAD_HALF_LONGITUDINAL_M = 0.75
ROAD_COLLISION_SLAB_DEPTH_M = 0.4
ROAD_SWEEP_VEHICLE_HEIGHT_M = 5.0
STREETLIGHT_SPACING_M = 80.0
STREETLIGHT_DECK_MARGIN_M = 40.0
STREETLIGHT_ASSET_ID = "rorng_city_led_streetlight_bridge"
STREETLIGHT_INSTANCE_PREFIX = "cityworld_next_neoq_link_led"


class BridgeFailure(RuntimeError):
    """A stable failure caused by stale or unsafe CityWorld bridge input."""


@dataclass(frozen=True)
class BridgePoint:
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
class BridgeFixturePlacement:
    station_m: float
    side: str
    x: float
    y: float
    z: float
    yaw_degrees: float
    asset_id: str
    instance_name: str


def _stable_number(value: float) -> float:
    result = round(float(value), 9)
    return 0.0 if result == -0.0 else result


def _finite_vector(value: Iterable[float], label: str) -> tuple[float, ...]:
    result = tuple(float(component) for component in value)
    if not result or not all(math.isfinite(component) for component in result):
        raise BridgeFailure(f"{label} must contain finite coordinates")
    return result


def _angular_error(first: float, second: float) -> float:
    return abs((first - second + 180.0) % 360.0 - 180.0)


def _matching_placement(
    placements: Sequence[Any],
    expected: dict[str, Any],
) -> Any:
    expected_position = _finite_vector(
        expected["position_m"],
        "expected placement position",
    )
    expected_rotation = _finite_vector(
        expected["rotation_degrees"],
        "expected placement rotation",
    )
    matches = []
    for placement in placements:
        if (
            getattr(placement, "line_number", None)
            != expected["line_number"]
            or getattr(placement, "object_name", None) != expected["object"]
        ):
            continue
        position = _finite_vector(
            getattr(placement, "position", ()),
            "source placement position",
        )
        rotation = _finite_vector(
            getattr(placement, "rotation_degrees", ()),
            "source placement rotation",
        )
        if (
            len(position) == 3
            and len(rotation) == 3
            and all(
                abs(actual - target) <= POSITION_EPSILON
                for actual, target in zip(position, expected_position)
            )
            and all(
                _angular_error(actual, target)
                <= ANGLE_EPSILON_DEGREES
                for actual, target in zip(rotation, expected_rotation)
            )
        ):
            matches.append(placement)
    if len(matches) != 1:
        raise BridgeFailure(
            "expected exactly one authenticated "
            f"{expected['city']} bridge placement; found {len(matches)}"
        )
    return matches[0]


def _member_records(
    archive_path: Path,
    member_contract: Sequence[dict[str, Any]],
) -> list[dict[str, Any]]:
    try:
        with zipfile.ZipFile(archive_path) as archive:
            infos = archive.infolist()
            names: dict[str, zipfile.ZipInfo] = {}
            for info in infos:
                folded = info.filename.casefold()
                if folded in names:
                    raise BridgeFailure(
                        "CityWorld archive contains duplicate member names"
                    )
                names[folded] = info

            records = []
            for expected in member_contract:
                name = expected["name"]
                info = names.get(name.casefold())
                if (
                    info is None
                    or info.filename != name
                    or info.is_dir()
                    or info.file_size != expected["size"]
                ):
                    raise BridgeFailure(
                        f"authenticated bridge member changed: {name}"
                    )
                digest = hashlib.sha256()
                observed_size = 0
                with archive.open(info, "r") as stream:
                    while chunk := stream.read(READ_CHUNK_BYTES):
                        digest.update(chunk)
                        observed_size += len(chunk)
                if (
                    observed_size != expected["size"]
                    or digest.hexdigest() != expected["sha256"]
                ):
                    raise BridgeFailure(
                        f"authenticated bridge member changed: {name}"
                    )
                records.append(
                    {
                        "name": name,
                        "role": expected["role"],
                        "sha256": expected["sha256"],
                        "size": expected["size"],
                    }
                )
            return records
    except zipfile.BadZipFile as error:
        raise BridgeFailure("CityWorld bridge input is not a ZIP archive") from error


def _open_gap_audit(placements: Sequence[Any]) -> dict[str, Any]:
    min_x = SOURCE_SEAM[0]
    max_x = DESTINATION_SEAM[0]
    min_z = min(SOURCE_SEAM[2], DESTINATION_SEAM[2]) - OPEN_GAP_HALF_WIDTH_M
    max_z = max(SOURCE_SEAM[2], DESTINATION_SEAM[2]) + OPEN_GAP_HALF_WIDTH_M
    matches = []
    for placement in placements:
        position = getattr(placement, "position", ())
        if len(position) != 3:
            continue
        x, _, z = _finite_vector(position, "placement position")
        if min_x < x < max_x and min_z < z < max_z:
            matches.append(placement)
    if matches:
        first = matches[0]
        raise BridgeFailure(
            "Neo intercity swept placement-origin gap is no longer empty: "
            f"{getattr(first, 'object_name', '<unknown>')} at line "
            f"{getattr(first, 'line_number', '<unknown>')}"
        )
    return {
        "bounds_xz_m": [
            _stable_number(min_x),
            _stable_number(max_x),
            _stable_number(min_z),
            _stable_number(max_z),
        ],
        "member": "CityWorld.tobj",
        "placement_origin_count": 0,
        "verified": True,
    }


def authenticate_inputs(
    archive_path: Path,
    placements: Sequence[Any],
    *,
    member_contract: Sequence[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Authenticate the exact source placements and six endpoint resources."""

    if member_contract is None:
        member_contract = AUTHENTICATED_MEMBERS
    source = _matching_placement(placements, SOURCE_PLACEMENT)
    destination = _matching_placement(placements, DESTINATION_PLACEMENT)
    members = _member_records(archive_path, member_contract)
    gap = _open_gap_audit(placements)

    def placement_record(
        placement: Any,
        expected: dict[str, Any],
    ) -> dict[str, Any]:
        authored_position = [
            _stable_number(value)
            for value in getattr(placement, "position")
        ]
        runtime_position = (
            list(DESTINATION_RUNTIME_PLACEMENT_ORIGIN)
            if expected is DESTINATION_PLACEMENT
            else authored_position
        )
        return {
            "authored_position_m": authored_position,
            "city": expected["city"],
            "connection": expected["connection"],
            "line_number": expected["line_number"],
            "member": "CityWorld.tobj",
            "object": expected["object"],
            "position_m": authored_position,
            "rotation_degrees": [
                _stable_number(value)
                for value in getattr(placement, "rotation_degrees")
            ],
            "runtime_grounding_applied": (
                expected is DESTINATION_PLACEMENT
            ),
            "runtime_position_m": [
                _stable_number(value) for value in runtime_position
            ],
        }

    return {
        "destination": placement_record(destination, DESTINATION_PLACEMENT),
        "format": AUTHENTICATION_FORMAT,
        "members": members,
        "open_gap": gap,
        "source": placement_record(source, SOURCE_PLACEMENT),
        "tobj": {
            "name": "CityWorld.tobj",
            "sha256": PINNED_TOBJ_SHA256,
        },
    }


def _smoothstep(value: float) -> float:
    bounded = min(1.0, max(0.0, float(value)))
    return bounded * bounded * (3.0 - 2.0 * bounded)


def _bezier(
    control_points: Sequence[tuple[float, float]],
    parameter: float,
) -> tuple[float, float]:
    inverse = 1.0 - parameter
    return (
        inverse**3 * control_points[0][0]
        + 3.0 * inverse * inverse * parameter * control_points[1][0]
        + 3.0 * inverse * parameter * parameter * control_points[2][0]
        + parameter**3 * control_points[3][0],
        inverse**3 * control_points[0][1]
        + 3.0 * inverse * inverse * parameter * control_points[1][1]
        + 3.0 * inverse * parameter * parameter * control_points[2][1]
        + parameter**3 * control_points[3][1],
    )


def _bezier_derivative(
    control_points: Sequence[tuple[float, float]],
    parameter: float,
) -> tuple[float, float]:
    inverse = 1.0 - parameter
    return (
        3.0
        * (
            inverse * inverse
            * (control_points[1][0] - control_points[0][0])
            + 2.0
            * inverse
            * parameter
            * (control_points[2][0] - control_points[1][0])
            + parameter
            * parameter
            * (control_points[3][0] - control_points[2][0])
        ),
        3.0
        * (
            inverse * inverse
            * (control_points[1][1] - control_points[0][1])
            + 2.0
            * inverse
            * parameter
            * (control_points[2][1] - control_points[1][1])
            + parameter
            * parameter
            * (control_points[3][1] - control_points[2][1])
        ),
    )


def _control_points() -> tuple[tuple[float, float], ...]:
    source = (SOURCE_SEAM[0], SOURCE_SEAM[2])
    destination = (DESTINATION_SEAM[0], DESTINATION_SEAM[2])
    if destination[0] - source[0] <= 2.0 * TANGENT_HANDLE_M:
        raise BridgeFailure("Neo bridge endpoints are too close")
    return (
        source,
        (source[0] + TANGENT_HANDLE_M, source[1]),
        (destination[0] - TANGENT_HANDLE_M, destination[1]),
        destination,
    )


def _arc_table(
    control_points: Sequence[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    result = [(0.0, 0.0)]
    previous = _bezier(control_points, 0.0)
    accumulated = 0.0
    for index in range(1, ARC_TABLE_STEPS + 1):
        parameter = index / ARC_TABLE_STEPS
        current = _bezier(control_points, parameter)
        segment = math.hypot(
            current[0] - previous[0],
            current[1] - previous[1],
        )
        if not math.isfinite(segment) or segment <= 0.0:
            raise BridgeFailure("Neo bridge centreline is not traversable")
        accumulated += segment
        result.append((parameter, accumulated))
        previous = current
    if accumulated <= 2.0 * (GROUND_LEAD_M + RAMP_LENGTH_M):
        raise BridgeFailure("Neo bridge is too short for its ramps")
    return tuple(result)


def _parameter_at_station(
    arc_table: Sequence[tuple[float, float]],
    station_m: float,
) -> float:
    total = arc_table[-1][1]
    station = float(station_m)
    if not math.isfinite(station) or not 0.0 <= station <= total:
        raise BridgeFailure("Neo bridge station lies outside the route")
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
    blend = (station - first_station) / (second_station - first_station)
    return first_parameter + blend * (second_parameter - first_parameter)


def _stations(total_length_m: float) -> tuple[float, ...]:
    total = float(total_length_m)
    values = {
        0.0,
        total,
        GROUND_LEAD_M,
        GROUND_LEAD_M + RAMP_LENGTH_M,
        total - GROUND_LEAD_M,
        total - GROUND_LEAD_M - RAMP_LENGTH_M,
    }
    count = int(math.floor(total / SAMPLE_SPACING_M))
    values.update(
        index * SAMPLE_SPACING_M
        for index in range(1, count + 1)
        if index * SAMPLE_SPACING_M < total
    )
    result = tuple(sorted(values))
    if any(
        second - first > SAMPLE_SPACING_M + POSITION_EPSILON
        for first, second in zip(result, result[1:])
    ):
        raise BridgeFailure("Neo bridge sampling exceeds support spacing")
    return result


def _elevation(
    station_m: float,
    total_length_m: float,
    surface_offset_m: float,
) -> float:
    station = float(station_m)
    total = float(total_length_m)
    if station <= GROUND_LEAD_M:
        baseline = SOURCE_SEAM[1]
    elif station >= total - GROUND_LEAD_M:
        baseline = DESTINATION_SEAM[1]
    else:
        progress = (
            (station - GROUND_LEAD_M)
            / (total - 2.0 * GROUND_LEAD_M)
        )
        baseline = SOURCE_SEAM[1] + (
            DESTINATION_SEAM[1] - SOURCE_SEAM[1]
        ) * _smoothstep(progress)
    offset_weight = _smoothstep(
        (station - GROUND_LEAD_M) / GROUND_LEAD_M
    ) * _smoothstep(
        (total - GROUND_LEAD_M - station) / GROUND_LEAD_M
    )
    baseline += float(surface_offset_m) * offset_weight
    ascent_start = GROUND_LEAD_M
    ascent_end = ascent_start + RAMP_LENGTH_M
    descent_start = total - ascent_end
    descent_end = total - ascent_start
    if station <= ascent_start or station >= descent_end:
        return baseline
    if station < ascent_end:
        return baseline + DECK_CLEARANCE_M * _smoothstep(
            (station - ascent_start) / RAMP_LENGTH_M
        )
    if station <= descent_start:
        return baseline + DECK_CLEARANCE_M
    return baseline + DECK_CLEARANCE_M * (
        1.0
        - _smoothstep((station - descent_start) / RAMP_LENGTH_M)
    )


def _width_at_station(station_m: float, total_length_m: float) -> float:
    taper_start = total_length_m - DESTINATION_WIDTH_TAPER_LENGTH_M
    if station_m <= taper_start:
        return ROAD_WIDTH_M
    progress = (
        (station_m - taper_start) / DESTINATION_WIDTH_TAPER_LENGTH_M
    )
    return ROAD_WIDTH_M + (
        DESTINATION_MERGE_WIDTH_M - ROAD_WIDTH_M
    ) * _smoothstep(progress)


def _normalized_degrees(value: float) -> float:
    result = (float(value) + 180.0) % 360.0 - 180.0
    return 0.0 if abs(result) < 1.0e-12 else result


def _strict_aabb_intersection(
    first: Sequence[float],
    second: Sequence[float],
) -> bool:
    if len(first) != 6 or len(second) != 6:
        raise BridgeFailure("support clearance AABB is incomplete")
    values = (*_finite_vector(first, "first support AABB"),
              *_finite_vector(second, "second support AABB"))
    if len(values) != 12:
        raise BridgeFailure("support clearance AABB has invalid dimensions")
    return all(
        first[axis] < second[axis + 3] - POSITION_EPSILON
        and second[axis] < first[axis + 3] - POSITION_EPSILON
        for axis in range(3)
    )


def _oriented_xz_aabb(
    point: BridgePoint,
    *,
    half_longitudinal_m: float,
    half_lateral_m: float,
    center_lateral_m: float,
    minimum_y_m: float,
    maximum_y_m: float,
) -> list[float]:
    yaw = math.radians(point.yaw_degrees)
    tangent_x = math.cos(yaw)
    tangent_z = -math.sin(yaw)
    normal_x = math.sin(yaw)
    normal_z = math.cos(yaw)
    center_x = point.x + normal_x * center_lateral_m
    center_z = point.z + normal_z * center_lateral_m
    half_x = (
        abs(tangent_x) * half_longitudinal_m
        + abs(normal_x) * half_lateral_m
    )
    half_z = (
        abs(tangent_z) * half_longitudinal_m
        + abs(normal_z) * half_lateral_m
    )
    return [
        _stable_number(center_x - half_x),
        _stable_number(minimum_y_m),
        _stable_number(center_z - half_z),
        _stable_number(center_x + half_x),
        _stable_number(maximum_y_m),
        _stable_number(center_z + half_z),
    ]


def _support_clearance_report(
    supports: Sequence[BridgePoint],
    *,
    route_length_m: float,
) -> dict[str, Any]:
    if not supports:
        raise BridgeFailure("Neo bridge support schedule is incomplete")
    collision_records = []
    minimum_lateral_clearance = math.inf
    minimum_vertical_clearance = math.inf
    for support_index, point in enumerate(supports):
        deck_outer = point.width_m / 2.0 + point.border_width_m
        column_center_offset = (
            deck_outer
            + SUPPORT_TRUCK_LATERAL_CLEARANCE_M
            + SUPPORT_COLUMN_HALF_EXTENT_M
        )
        roadway_prism = _oriented_xz_aabb(
            point,
            half_longitudinal_m=SUPPORT_HAMMERHEAD_HALF_LONGITUDINAL_M,
            half_lateral_m=(
                deck_outer + SUPPORT_TRUCK_LATERAL_CLEARANCE_M
            ),
            center_lateral_m=0.0,
            minimum_y_m=point.y - ROAD_COLLISION_SLAB_DEPTH_M,
            maximum_y_m=point.y + ROAD_SWEEP_VEHICLE_HEIGHT_M,
        )
        column_top = point.y - SUPPORT_COLUMN_TOP_BELOW_SURFACE_M
        # The lower bound is a conservative finite CityWorld world floor. The
        # engine replaces it with the sampled terrain height when building the
        # actual collision geometry.
        conservative_bottom = point.y - 4096.0
        for side, multiplier in (("left", 1.0), ("right", -1.0)):
            column = _oriented_xz_aabb(
                point,
                half_longitudinal_m=SUPPORT_COLUMN_HALF_EXTENT_M,
                half_lateral_m=SUPPORT_COLUMN_HALF_EXTENT_M,
                center_lateral_m=multiplier * column_center_offset,
                minimum_y_m=conservative_bottom,
                maximum_y_m=column_top,
            )
            if _strict_aabb_intersection(column, roadway_prism):
                raise BridgeFailure(
                    "Neo bridge support column enters the swept roadway prism"
                )
            minimum_lateral_clearance = min(
                minimum_lateral_clearance,
                column_center_offset
                - SUPPORT_COLUMN_HALF_EXTENT_M
                - deck_outer,
            )
            collision_records.append(
                {
                    "aabb_world_m": column,
                    "kind": "terrain-reaching-column",
                    "side": side,
                    "station_m": _stable_number(point.station_m),
                    "support_index": support_index,
                    "terrain_bottom_resolved_at_runtime": True,
                }
            )
        hammerhead = _oriented_xz_aabb(
            point,
            half_longitudinal_m=SUPPORT_HAMMERHEAD_HALF_LONGITUDINAL_M,
            half_lateral_m=column_center_offset + SUPPORT_COLUMN_HALF_EXTENT_M,
            center_lateral_m=0.0,
            minimum_y_m=(
                point.y
                - SUPPORT_HAMMERHEAD_TOP_BELOW_SURFACE_M
                - SUPPORT_HAMMERHEAD_THICKNESS_M
            ),
            maximum_y_m=(
                point.y - SUPPORT_HAMMERHEAD_TOP_BELOW_SURFACE_M
            ),
        )
        if _strict_aabb_intersection(hammerhead, roadway_prism):
            raise BridgeFailure(
                "Neo bridge support hammerhead enters the swept roadway prism"
            )
        minimum_vertical_clearance = min(
            minimum_vertical_clearance,
            (
                point.y - ROAD_COLLISION_SLAB_DEPTH_M
            ) - hammerhead[4],
        )
        collision_records.append(
            {
                "aabb_world_m": hammerhead,
                "kind": "hammerhead",
                "side": "cross-deck",
                "station_m": _stable_number(point.station_m),
                "support_index": support_index,
                "terrain_bottom_resolved_at_runtime": False,
            }
        )

    first_clearance = supports[0].station_m - SOURCE_OVERLAP_LENGTH_M
    final_clearance = route_length_m - supports[-1].station_m
    if (
        first_clearance < SUPPORT_ENDPOINT_EXCLUSION_M - POSITION_EPSILON
        or final_clearance < SUPPORT_ENDPOINT_EXCLUSION_M - POSITION_EPSILON
    ):
        raise BridgeFailure("Neo bridge support enters a ground-road approach")
    if (
        minimum_lateral_clearance
        < SUPPORT_TRUCK_LATERAL_CLEARANCE_M - POSITION_EPSILON
        or minimum_vertical_clearance <= POSITION_EPSILON
    ):
        raise BridgeFailure("Neo bridge support clearance contract failed")
    return {
        "aabb_count": len(collision_records),
        "aabb_vs_swept_roadway_prism": "all-disjoint",
        "collision_aabbs": collision_records,
        "column_half_extent_m": SUPPORT_COLUMN_HALF_EXTENT_M,
        "column_pair_count": len(supports),
        "endpoint_ground_road_exclusion_m": SUPPORT_ENDPOINT_EXCLUSION_M,
        "hammerhead_half_longitudinal_m":
            SUPPORT_HAMMERHEAD_HALF_LONGITUDINAL_M,
        "hammerhead_thickness_m": SUPPORT_HAMMERHEAD_THICKNESS_M,
        "minimum_lateral_clearance_from_deck_edge_m":
            _stable_number(minimum_lateral_clearance),
        "minimum_vertical_clearance_below_collision_slab_m":
            _stable_number(minimum_vertical_clearance),
        "roadway_swept_prism": {
            "collision_slab_depth_m": ROAD_COLLISION_SLAB_DEPTH_M,
            "lateral_truck_envelope_beyond_deck_m":
                SUPPORT_TRUCK_LATERAL_CLEARANCE_M,
            "vehicle_height_m": ROAD_SWEEP_VEHICLE_HEIGHT_M,
        },
    }


def build_route(
    *,
    surface_offset_m: float,
) -> tuple[tuple[BridgePoint, ...], dict[str, Any]]:
    """Build the curb-free, collision-continuous raised link."""

    if (
        isinstance(surface_offset_m, bool)
        or not isinstance(surface_offset_m, (int, float))
        or not math.isfinite(float(surface_offset_m))
        or not -2.0 <= float(surface_offset_m) <= 20.0
    ):
        raise BridgeFailure(
            "surface offset must be finite and between -2 and 20 metres"
        )
    controls = _control_points()
    arc_table = _arc_table(controls)
    core_length = arc_table[-1][1]
    core_stations = _stations(core_length)
    points = [
        BridgePoint(
            station_m=0.0,
            x=SOURCE_OVERLAP_START[0],
            y=SOURCE_OVERLAP_START[1],
            z=SOURCE_OVERLAP_START[2],
            yaw_degrees=0.0,
            road_type="flat",
            width_m=ROAD_WIDTH_M,
            border_width_m=APPROACH_BORDER_WIDTH_M,
            border_height_m=APPROACH_BORDER_HEIGHT_M,
        )
    ]
    for core_station in core_stations:
        parameter = _parameter_at_station(arc_table, core_station)
        x, z = _bezier(controls, parameter)
        tangent_x, tangent_z = _bezier_derivative(controls, parameter)
        tangent_length = math.hypot(tangent_x, tangent_z)
        if not math.isfinite(tangent_length) or tangent_length <= 0.0:
            raise BridgeFailure("Neo bridge tangent is invalid")
        yaw = -math.degrees(math.atan2(tangent_z, tangent_x))
        elevation = _elevation(
            core_station,
            core_length,
            float(surface_offset_m),
        )
        is_bridge = (
            core_station > GROUND_LEAD_M
            and core_station < core_length - GROUND_LEAD_M
        )
        requests_support = (
            is_bridge
            and elevation >= 1.0
            and core_station
                >= SUPPORT_ENDPOINT_EXCLUSION_M - POSITION_EPSILON
            and core_length - core_station
                >= SUPPORT_ENDPOINT_EXCLUSION_M - POSITION_EPSILON
        )
        road_type = (
            "bridge_side_pillars"
            if requests_support
            else ("bridge_no_pillars" if is_bridge else "flat")
        )
        points.append(
            BridgePoint(
                station_m=SOURCE_OVERLAP_LENGTH_M + core_station,
                x=x,
                y=elevation,
                z=z,
                yaw_degrees=_normalized_degrees(yaw),
                road_type=road_type,
                width_m=_width_at_station(core_station, core_length),
                border_width_m=(
                    APPROACH_BORDER_WIDTH_M
                    if road_type == "flat"
                    else BRIDGE_BORDER_WIDTH_M
                ),
                border_height_m=(
                    APPROACH_BORDER_HEIGHT_M
                    if road_type == "flat"
                    else BRIDGE_BORDER_HEIGHT_M
                ),
            )
        )
    total_length = core_length + SOURCE_OVERLAP_LENGTH_M

    if any(
        second.station_m <= first.station_m
        or second.x <= first.x
        or second.station_m - first.station_m
        > SAMPLE_SPACING_M + POSITION_EPSILON
        for first, second in zip(points, points[1:])
    ):
        raise BridgeFailure("Neo bridge points are not strictly continuous")
    if (
        math.dist(
            (points[0].x, points[0].y, points[0].z),
            SOURCE_OVERLAP_START,
        )
        > POSITION_EPSILON
        or math.dist(
            (points[1].x, points[1].y, points[1].z),
            SOURCE_SEAM,
        )
        > POSITION_EPSILON
        or math.dist(
            (points[-1].x, points[-1].y, points[-1].z),
            DESTINATION_SEAM,
        )
        > POSITION_EPSILON
    ):
        raise BridgeFailure("Neo bridge does not close at both road seams")

    sampled_grade = max(
        abs(second.y - first.y)
        / (second.station_m - first.station_m)
        for first, second in zip(points, points[1:])
    )
    if sampled_grade > MAXIMUM_GRADE + 1.0e-9:
        raise BridgeFailure("Neo bridge exceeds its maximum grade")
    supports = [
        point
        for point in points
        if point.road_type == "bridge_side_pillars"
    ]
    maximum_support_spacing = max(
        (
            second.station_m - first.station_m
            for first, second in zip(supports, supports[1:])
        ),
        default=0.0,
    )
    if (
        not supports
        or maximum_support_spacing > SAMPLE_SPACING_M + POSITION_EPSILON
    ):
        raise BridgeFailure("Neo bridge support schedule is incomplete")
    support_clearance = _support_clearance_report(
        supports,
        route_length_m=total_length,
    )
    destination_grade = (
        points[-1].y - points[-2].y
    ) / (
        points[-1].station_m - points[-2].station_m
    )
    destination_yaw_discontinuity = _angular_error(
        points[-1].yaw_degrees,
        0.0,
    )
    destination_step = abs(points[-1].y - DESTINATION_SEAM[1])
    destination_width_error = abs(
        points[-1].width_m - DESTINATION_MERGE_WIDTH_M
    )
    source_decoded_surface_y = (
        SOURCE_RUNTIME_PLACEMENT_ORIGIN[1] + SOURCE_LOCAL_SEAM[1]
    )
    destination_decoded_surface_y = (
        DESTINATION_RUNTIME_PLACEMENT_ORIGIN[1]
        + DESTINATION_LOCAL_SEAM[1]
    )
    source_surface_step = abs(points[1].y - source_decoded_surface_y)
    destination_surface_step = abs(
        points[-1].y - destination_decoded_surface_y
    )
    if (
        destination_step > MAXIMUM_CONNECTION_STEP_M
        or source_surface_step > MAXIMUM_CONNECTION_STEP_M
        or destination_surface_step > MAXIMUM_CONNECTION_STEP_M
        or abs(destination_grade)
            > MAXIMUM_CONNECTION_GRADE_DISCONTINUITY
        or destination_yaw_discontinuity
            > MAXIMUM_CONNECTION_YAW_DISCONTINUITY_DEGREES
        or destination_width_error > POSITION_EPSILON
    ):
        raise BridgeFailure("Neo bridge destination merge is discontinuous")

    report = {
        "collision": {
            "authority": "native-procedural-road-v2-side-piers",
            "continuous": True,
            "endcap_collision_enabled": False,
            "endcap_collision_triangle_count": 0,
            "endpoint_wheel_path_intrusion_m": 0.0,
            "single_surface": True,
        },
        "connection": {
            "destination_generated_overlap_m":
                DESTINATION_OVERLAP_LENGTH_M,
            "destination_grade_discontinuity":
                _stable_number(abs(destination_grade)),
            "destination_heading_error_degrees":
                _stable_number(destination_yaw_discontinuity),
            "destination_position_gap_m": 0.0,
            "destination_vertical_step_m":
                _stable_number(destination_step),
            "destination_route_vs_decoded_surface_step_m":
                _stable_number(destination_surface_step),
            "destination_width_edge_error_m":
                _stable_number(destination_width_error),
            "maximum_grade_discontinuity":
                MAXIMUM_CONNECTION_GRADE_DISCONTINUITY,
            "maximum_heading_error_degrees":
                MAXIMUM_CONNECTION_YAW_DISCONTINUITY_DEGREES,
            "maximum_vertical_step_m": MAXIMUM_CONNECTION_STEP_M,
            "source_heading_error_degrees": 0.0,
            "source_position_gap_m": 0.0,
            "source_route_vs_decoded_surface_step_m":
                _stable_number(source_surface_step),
        },
        "control_points_xz_m": [
            [_stable_number(x), _stable_number(z)] for x, z in controls
        ],
        "covered_centerline_length_m": _stable_number(total_length),
        "destination": {
            "city": DESTINATION_PLACEMENT["city"],
            "collision_member": DESTINATION_COLLISION_MEMBER,
            "collision_sha256": DESTINATION_COLLISION_SHA256,
            "connection": DESTINATION_PLACEMENT["connection"],
            "existing_lanes_preserved": True,
            "elevation_authority": {
                "authored_placement_origin_y_m":
                    DESTINATION_PLACEMENT["position_m"][1],
                "decoded_local_surface_y_m":
                    DESTINATION_LOCAL_SEAM[1],
                "route_surface_y_m": DESTINATION_SEAM[1],
                "runtime_grounding_applied": True,
                "runtime_placement_origin_y_m":
                    DESTINATION_RUNTIME_PLACEMENT_ORIGIN[1],
                "runtime_origin_plus_local_surface_y_m":
                    _stable_number(destination_decoded_surface_y),
                "route_vs_decoded_surface_step_m":
                    _stable_number(destination_surface_step),
            },
            "generated_overlap_length_m":
                DESTINATION_OVERLAP_LENGTH_M,
            "lane_handoff": {
                "carriageway": "positive-local-z",
                "local_position_m": [
                    DESTINATION_LOCAL_SEAM[0],
                    DESTINATION_SEAM[1],
                    DESTINATION_LANE_CENTER_LOCAL_Z_M,
                ],
                "world_position_m": list(DESTINATION_LANE_HANDOFF),
            },
            "local_mesh_seam_m": list(DESTINATION_LOCAL_SEAM),
            "median_local_z_m": list(DESTINATION_MEDIAN_LOCAL_Z_M),
            "merge_width_m": DESTINATION_MERGE_WIDTH_M,
            "open_carriageways_local_z_m": [
                list(interval)
                for interval in DESTINATION_OPEN_CARRIAGEWAYS_LOCAL_Z_M
            ],
            "outer_collision_bounds_local_z_m":
                list(DESTINATION_OUTER_BOUNDS_LOCAL_Z_M),
            "seam_m": list(DESTINATION_SEAM),
            "surface_elevation_m": DESTINATION_SEAM[1],
        },
        "format": FORMAT,
        "profile": {
            "approach_border_height_m": APPROACH_BORDER_HEIGHT_M,
            "approach_border_width_m": APPROACH_BORDER_WIDTH_M,
            "curb_free_approaches": True,
            "deck_clearance_m": DECK_CLEARANCE_M,
            "maximum_grade": MAXIMUM_GRADE,
            "ramp_length_m": RAMP_LENGTH_M,
            "sample_spacing_limit_m": SAMPLE_SPACING_M,
            "sampled_maximum_grade": _stable_number(sampled_grade),
            "surface_offset_m": _stable_number(surface_offset_m),
            "destination_merge_width_m": DESTINATION_MERGE_WIDTH_M,
            "destination_width_taper_length_m":
                DESTINATION_WIDTH_TAPER_LENGTH_M,
            "width_m": ROAD_WIDTH_M,
        },
        "source": {
            "city": SOURCE_PLACEMENT["city"],
            "connection": SOURCE_PLACEMENT["connection"],
            "elevation_authority": {
                "authored_placement_origin_y_m":
                    SOURCE_PLACEMENT["position_m"][1],
                "decoded_local_surface_y_m": SOURCE_LOCAL_SEAM[1],
                "route_surface_y_m": SOURCE_SEAM[1],
                "runtime_grounding_applied": False,
                "runtime_placement_origin_y_m":
                    SOURCE_RUNTIME_PLACEMENT_ORIGIN[1],
                "runtime_origin_plus_local_surface_y_m":
                    _stable_number(source_decoded_surface_y),
                "route_vs_decoded_surface_step_m":
                    _stable_number(source_surface_step),
            },
            "local_mesh_seam_m": list(SOURCE_LOCAL_SEAM),
            "overlap_length_m": SOURCE_OVERLAP_LENGTH_M,
            "overlap_start_m": list(SOURCE_OVERLAP_START),
            "seam_m": list(SOURCE_SEAM),
        },
        "supports": {
            **support_clearance,
            "enabled": True,
            "maximum_station_spacing_m":
                _stable_number(maximum_support_spacing),
            "requested_count": len(supports),
            "stations_m": [
                _stable_number(point.station_m) for point in supports
            ],
            "style": "ror-native-procedural-side-pier-pair-v1",
            "terrain_contact_resolved_at_runtime": True,
        },
        "waypoints": [
            {
                "index": index,
                "position_m": [
                    _stable_number(point.x),
                    _stable_number(point.y),
                    _stable_number(point.z),
                ],
                "road_type": point.road_type,
                "station_m": _stable_number(point.station_m),
                "yaw_degrees": _stable_number(point.yaw_degrees),
            }
            for index, point in enumerate(points)
        ],
    }
    return tuple(points), report


def validate_ground_road_clearance(
    route_report: dict[str, Any],
    authentication: dict[str, Any],
) -> dict[str, Any]:
    """Reject supports outside the authenticated empty ground corridor.

    CityWorld does not provide a world-space acceleration structure for every
    legacy mesh. The pinned TOBJ placement-origin audit is therefore combined
    with an intentionally wide corridor, an 80 m exclusion around both live
    road anchors, exact support footprints, and a mandatory underside render
    gate. The report is explicit about this conservative limit.
    """

    open_gap = authentication.get("open_gap")
    if (
        not isinstance(open_gap, dict)
        or open_gap.get("verified") is not True
        or open_gap.get("placement_origin_count") != 0
    ):
        raise BridgeFailure(
            "Neo bridge ground corridor is not authenticated empty"
        )
    bounds = _finite_vector(
        open_gap.get("bounds_xz_m", ()),
        "authenticated ground corridor bounds",
    )
    if len(bounds) != 4:
        raise BridgeFailure(
            "authenticated ground corridor bounds are incomplete"
        )
    supports = route_report.get("supports")
    if not isinstance(supports, dict):
        raise BridgeFailure("Neo bridge support report is missing")
    collision_aabbs = supports.get("collision_aabbs")
    if not isinstance(collision_aabbs, list) or not collision_aabbs:
        raise BridgeFailure("Neo bridge support collision AABBs are missing")
    columns = [
        item for item in collision_aabbs
        if isinstance(item, dict)
        and item.get("kind") == "terrain-reaching-column"
    ]
    if len(columns) != supports.get("column_pair_count", 0) * 2:
        raise BridgeFailure("Neo bridge support column schedule drifted")
    min_x, max_x, min_z, max_z = bounds
    for column in columns:
        aabb = _finite_vector(
            column.get("aabb_world_m", ()),
            "support column collision AABB",
        )
        if (
            len(aabb) != 6
            or aabb[0] <= min_x + POSITION_EPSILON
            or aabb[3] >= max_x - POSITION_EPSILON
            or aabb[2] <= min_z + POSITION_EPSILON
            or aabb[5] >= max_z - POSITION_EPSILON
        ):
            raise BridgeFailure(
                "Neo bridge support enters an unauthenticated ground area"
            )
    return {
        "approach_anchor_exclusion_m": SUPPORT_ENDPOINT_EXCLUSION_M,
        "authenticated_placement_origin_count": 0,
        "authenticated_tobj_sha256": authentication.get("tobj", {}).get(
            "sha256"
        ),
        "clearance": "all-column-aabbs-inside-empty-corridor",
        "column_aabb_count": len(columns),
        "conservative_corridor_bounds_xz_m": list(bounds),
        "legacy_mesh_world_bounds_available": False,
        "native_underside_visual_gate_required": True,
    }


def build_streetlights(
    points: Sequence[BridgePoint],
) -> tuple[tuple[BridgeFixturePlacement, ...], dict[str, Any]]:
    """Place bounded collisionless LED fixtures along the full-height deck."""

    if len(points) < 2:
        raise BridgeFailure("Neo bridge fixture route is incomplete")
    core_start = SOURCE_OVERLAP_LENGTH_M
    core_end = points[-1].station_m
    full_deck_start = (
        core_start
        + GROUND_LEAD_M
        + RAMP_LENGTH_M
        + STREETLIGHT_DECK_MARGIN_M
    )
    full_deck_end = (
        core_end
        - GROUND_LEAD_M
        - RAMP_LENGTH_M
        - STREETLIGHT_DECK_MARGIN_M
    )
    candidates = [
        point
        for point in points
        if (
            point.road_type.startswith("bridge_")
            and full_deck_start - POSITION_EPSILON
            <= point.station_m
            <= full_deck_end + POSITION_EPSILON
        )
    ]
    if not candidates:
        raise BridgeFailure("Neo bridge has no full-height lighting deck")
    selected = [
        point
        for point in candidates
        if abs(
            (
                point.station_m
                - candidates[0].station_m
            )
            % STREETLIGHT_SPACING_M
        )
        <= POSITION_EPSILON
    ]
    if any(
        abs(
            second.station_m
            - first.station_m
            - STREETLIGHT_SPACING_M
        )
        > POSITION_EPSILON
        for first, second in zip(selected, selected[1:])
    ):
        raise BridgeFailure("Neo bridge streetlights are not evenly spaced")

    placements = []
    for index, point in enumerate(selected):
        yaw_radians = math.radians(point.yaw_degrees)
        left_normal_x = math.sin(yaw_radians)
        left_normal_z = math.cos(yaw_radians)
        lateral_offset = point.width_m / 2.0 + point.border_width_m / 2.0
        side = "left" if index % 2 == 0 else "right"
        multiplier = 1.0 if side == "left" else -1.0
        yaw_offset = 0.0 if side == "left" else 180.0
        station_label = f"{int(round(point.station_m)):04d}"
        placements.append(
            BridgeFixturePlacement(
                station_m=point.station_m,
                side=side,
                x=point.x + multiplier * left_normal_x * lateral_offset,
                y=point.y + point.border_height_m,
                z=point.z + multiplier * left_normal_z * lateral_offset,
                yaw_degrees=_normalized_degrees(
                    point.yaw_degrees + yaw_offset
                ),
                asset_id=STREETLIGHT_ASSET_ID,
                instance_name=(
                    f"{STREETLIGHT_INSTANCE_PREFIX}_{station_label}_{side}"
                ),
            )
        )
    if (
        not placements
        or len({item.instance_name for item in placements})
        != len(placements)
    ):
        raise BridgeFailure("Neo bridge streetlight identifiers are invalid")

    return tuple(placements), {
        "arm_orientation": "alternating-inward-over-roadway",
        "asset_id": STREETLIGHT_ASSET_ID,
        "collision_authority": "native-procedural-road-v2-side-piers",
        "format": "ror-cityworld-neoq-bridge-streetlights-v1",
        "instance_count": len(placements),
        "lateral_mount_offset_m":
            _stable_number(
                selected[0].width_m / 2.0
                + selected[0].border_width_m / 2.0
            ),
        "mount_elevation_above_road_m": BRIDGE_BORDER_HEIGHT_M,
        "paired": False,
        "runtime_point_lights_per_instance": 1,
        "station_count": len(selected),
        "station_spacing_m": STREETLIGHT_SPACING_M,
        "stations": [
            {
                "centerline_position_m": [
                    _stable_number(point.x),
                    _stable_number(point.y),
                    _stable_number(point.z),
                ],
                "instance_name": placement.instance_name,
                "placement_position_m": [
                    _stable_number(placement.x),
                    _stable_number(placement.y),
                    _stable_number(placement.z),
                ],
                "rotation_degrees": [
                    0.0,
                    _stable_number(placement.yaw_degrees),
                    0.0,
                ],
                "side": placement.side,
                "station_m": _stable_number(point.station_m),
            }
            for point, placement in zip(selected, placements)
        ],
    }
