#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic regional-infill contract for the CityWorld local overlay.

The placement plan fills authenticated empty parcels without copying geometry
from the private CityWorld dependency.  Every emitted object is one of the
project-authored, texture-free regional-infill assets.  Native-road handoffs
are pinned to decoded road-surface edges; the access roads remain flat,
curb-free, and omit collision endcaps so a seam has one collision authority.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
from typing import Any, Iterable, Sequence


FORMAT = "ror-cityworld-regional-infill-plan-v2"
VERSION = 2
SOURCE_AUDIT_FORMAT = "ror-cityworld-regional-land-audit-v2"
CONNECTOR_FORMAT = "ror-cityworld-regional-infill-connectors-v1"
PINNED_ARCHIVE_NAME = "CityWorld.zip"
PINNED_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)
PINNED_TOBJ_MEMBER = "CityWorld.tobj"
PINNED_TOBJ_SHA256 = (
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48"
)
PINNED_HIGHWAY_PLACEMENT_LINE = 378
PINNED_HIGHWAY_OBJECT = "autopistaQr"
PINNED_HIGHWAY_PLACEMENT_POSITION_M = (0.0, -0.4, 0.0)
PINNED_HIGHWAY_PLACEMENT_ROTATION_DEGREES = (90.0, 0.0, 90.0)
PINNED_HIGHWAY_COLLISION_MEMBER = "autopistaQr.mesh"
PINNED_HIGHWAY_COLLISION_SHA256 = (
    "931dea1993ad1b42db68e27abe376f082b25da4aadf96755a54fbb997fe7f80d"
)
PENGUIN_CORRIDOR_CONTRACT = "tools/cityworld_penguin_neoq_corridor.py"
PENGUIN_CORRIDOR_FORMAT = "ror-cityworld-penguin-neoq-seams-v1"

ROAD_SURFACE_ELEVATION_M = 0.1
ROAD_SURFACE_MATERIAL = "road2"
ROAD_TYPE = "flat"
OPEN_ENDCAP_DIRECTIVE = "collision_endcaps_enabled false"
POSITION_EPSILON_M = 1.0e-6
MINIMUM_PLACEMENT_GAP_M = 8.0
MINIMUM_ACCESS_ROAD_CLEARANCE_M = 12.0
MAXIMUM_CONNECTOR_SEAM_GAP_M = 0.001

FARMSTEAD_ASSET_ID = "rorng_city_infill_farmstead_98x86"
SUBURB_ASSET_ID = "rorng_city_infill_suburb_block_96x88"
SERVICE_STATION_ASSET_ID = "rorng_city_infill_service_station_90x65"
RED_MESA_ASSET_ID = "rorng_city_infill_red_mesa_19m"
ARROYO_OASIS_ASSET_ID = "rorng_city_infill_arroyo_oasis_19m"

PointXZ = tuple[float, float]
Vector3 = tuple[float, float, float]


class InfillFailure(RuntimeError):
    """The checked regional-infill plan is stale, unsafe, or incomplete."""


@dataclass(frozen=True)
class CollisionComponent:
    component_id: str
    collision_width_m: float
    collision_depth_m: float
    collision_center_local_x_m: float
    collision_center_local_z_m: float


@dataclass(frozen=True)
class AssetContract:
    asset_id: str
    category: str
    manifest: str
    render_width_m: float
    render_depth_m: float
    collision_profile: str
    collision_components: tuple[CollisionComponent, ...]


@dataclass(frozen=True)
class InfillSite:
    site_id: str
    display_name: str
    category: str
    polygon_xz_m: tuple[PointXZ, ...]
    center_xz_m: PointXZ
    access_route_ids: tuple[str, ...]
    infrastructure_clearance_m: tuple[tuple[str, float], ...]


@dataclass(frozen=True)
class SourceAnchor:
    anchor_id: str
    authority: str
    position_m: Vector3
    heading_degrees: float
    extraction_method: str
    source_contract: str
    source_contract_format: str
    source_archive_sha256: str
    source_tobj_member: str
    source_tobj_sha256: str
    placement_line: int | None = None
    placement_object: str | None = None
    placement_position_m: Vector3 | None = None
    placement_rotation_degrees: Vector3 | None = None
    collision_member: str | None = None
    collision_sha256: str | None = None
    upstream_route_id: str | None = None
    outer_wall_point_m: Vector3 | None = None
    surface_seam_point_m: Vector3 | None = None
    surface_interval_x_m: tuple[float, float] | None = None


@dataclass(frozen=True)
class AccessRoadPoint:
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
class AccessRoute:
    route_id: str
    source_anchor_id: str
    destination_site_id: str
    served_site_ids: tuple[str, ...]
    destination_position_m: Vector3
    points: tuple[AccessRoadPoint, ...]
    comments: tuple[str, ...]
    collision_enabled: bool = True
    collision_endcaps_enabled: bool = False
    smoothing_num_splits: int = 0
    surface_material: str = ROAD_SURFACE_MATERIAL


@dataclass(frozen=True)
class InfillPlacement:
    placement_id: str
    site_id: str
    asset_id: str
    x: float
    y: float
    z: float
    yaw_degrees: float
    instance_name: str
    station_m: float = 0.0
    side: str = "district"


@dataclass(frozen=True)
class RouteAssetConnector:
    connector_id: str
    status: str
    route_id: str
    placement_id: str
    surface_id: str
    route_segment_index: int
    route_edge: str
    target_surface_local_polygon_xz_m: tuple[PointXZ, ...]
    target_seam_local_xz_m: tuple[PointXZ, ...]
    target_surface_local_y_m: float
    expected_render_overlap_depth_m: float
    blocker: str | None = None
    maximum_seam_gap_m: float = MAXIMUM_CONNECTOR_SEAM_GAP_M


@dataclass(frozen=True)
class InfillPlan:
    assets: tuple[AssetContract, ...]
    sites: tuple[InfillSite, ...]
    source_anchors: tuple[SourceAnchor, ...]
    routes: tuple[AccessRoute, ...]
    placements: tuple[InfillPlacement, ...]
    connectors: tuple[RouteAssetConnector, ...]


ASSETS = (
    AssetContract(
        asset_id=FARMSTEAD_ASSET_ID,
        category="farmland",
        manifest=(
            "resources/nextgen/cityworld/regional_infill/"
            "rorng_city_infill_farmstead_98x86/"
            "rorng_city_infill_farmstead_98x86.asset.json"
        ),
        render_width_m=98.0,
        render_depth_m=86.0,
        collision_profile="single-watertight-proxy-v1",
        collision_components=(
            CollisionComponent(
                component_id="farmhouse",
                collision_width_m=23.2,
                collision_depth_m=16.2,
                collision_center_local_x_m=-32.0,
                collision_center_local_z_m=-26.0,
            ),
        ),
    ),
    AssetContract(
        asset_id=SUBURB_ASSET_ID,
        category="suburb",
        manifest=(
            "resources/nextgen/cityworld/regional_infill/"
            "rorng_city_infill_suburb_block_96x88/"
            "rorng_city_infill_suburb_block_96x88.asset.json"
        ),
        render_width_m=96.0,
        render_depth_m=88.0,
        collision_profile="compound-watertight-proxy-v1",
        collision_components=(
            *(
                CollisionComponent(
                    component_id=f"house-{index:02d}",
                    collision_width_m=24.0,
                    collision_depth_m=17.0,
                    collision_center_local_x_m=x,
                    collision_center_local_z_m=z,
                )
                for index, (x, z) in enumerate(
                    (
                        (-27.0, 24.0),
                        (27.0, 24.0),
                        (-27.0, 0.0),
                        (27.0, 0.0),
                        (-27.0, -24.0),
                        (27.0, -24.0),
                    )
                )
            ),
            CollisionComponent(
                component_id="west-perimeter-wall",
                collision_width_m=1.5,
                collision_depth_m=84.0,
                collision_center_local_x_m=-47.0,
                collision_center_local_z_m=0.0,
            ),
            CollisionComponent(
                component_id="east-perimeter-wall",
                collision_width_m=1.5,
                collision_depth_m=84.0,
                collision_center_local_x_m=47.0,
                collision_center_local_z_m=0.0,
            ),
        ),
    ),
    AssetContract(
        asset_id=SERVICE_STATION_ASSET_ID,
        category="service-station",
        manifest=(
            "resources/nextgen/cityworld/regional_infill/"
            "rorng_city_infill_service_station_90x65/"
            "rorng_city_infill_service_station_90x65.asset.json"
        ),
        render_width_m=90.0,
        render_depth_m=65.0,
        collision_profile="compound-watertight-proxy-v1",
        collision_components=(
            CollisionComponent(
                component_id="market",
                collision_width_m=32.5,
                collision_depth_m=18.2,
                collision_center_local_x_m=0.0,
                collision_center_local_z_m=-22.0,
            ),
            CollisionComponent(
                component_id="canopy",
                collision_width_m=44.3,
                collision_depth_m=23.3,
                collision_center_local_x_m=3.0,
                collision_center_local_z_m=4.0,
            ),
            *(
                CollisionComponent(
                    component_id=f"canopy-column-{index:02d}",
                    collision_width_m=0.84,
                    collision_depth_m=0.84,
                    collision_center_local_x_m=x,
                    collision_center_local_z_m=z,
                )
                for index, (x, z) in enumerate(
                    (
                        (-15.0, 11.0),
                        (21.0, 11.0),
                        (-15.0, -3.0),
                        (21.0, -3.0),
                    )
                )
            ),
            *(
                CollisionComponent(
                    component_id=f"fuel-pump-{index:02d}",
                    collision_width_m=1.35,
                    collision_depth_m=0.9,
                    collision_center_local_x_m=x,
                    collision_center_local_z_m=z,
                )
                for index, (x, z) in enumerate(
                    (
                        (-12.0, 8.0),
                        (0.0, 8.0),
                        (12.0, 8.0),
                        (-12.0, 0.0),
                        (0.0, 0.0),
                        (12.0, 0.0),
                    )
                )
            ),
            CollisionComponent(
                component_id="price-pylon",
                collision_width_m=3.2,
                collision_depth_m=1.1,
                collision_center_local_x_m=-38.0,
                collision_center_local_z_m=24.0,
            ),
            *(
                CollisionComponent(
                    component_id=f"ev-charger-{index:02d}",
                    collision_width_m=0.72,
                    collision_depth_m=0.7,
                    collision_center_local_x_m=x,
                    collision_center_local_z_m=-24.0,
                )
                for index, x in enumerate((28.0, 31.2, 34.4, 37.6))
            ),
        ),
    ),
    AssetContract(
        asset_id=RED_MESA_ASSET_ID,
        category="natural-landmark",
        manifest=(
            "resources/nextgen/cityworld/regional_infill/"
            "rorng_city_infill_red_mesa_19m/"
            "rorng_city_infill_red_mesa_19m.asset.json"
        ),
        render_width_m=19.0,
        render_depth_m=19.0,
        collision_profile="single-watertight-proxy-v1",
        collision_components=(
            CollisionComponent(
                component_id="mesa",
                collision_width_m=11.5,
                collision_depth_m=9.0,
                collision_center_local_x_m=-2.4,
                collision_center_local_z_m=-0.8,
            ),
        ),
    ),
    AssetContract(
        asset_id=ARROYO_OASIS_ASSET_ID,
        category="natural-landmark",
        manifest=(
            "resources/nextgen/cityworld/regional_infill/"
            "rorng_city_infill_arroyo_oasis_19m/"
            "rorng_city_infill_arroyo_oasis_19m.asset.json"
        ),
        render_width_m=19.0,
        render_depth_m=19.0,
        collision_profile="single-watertight-proxy-v1",
        collision_components=(
            CollisionComponent(
                component_id="palm-trunk",
                collision_width_m=1.8,
                collision_depth_m=1.8,
                collision_center_local_x_m=-4.6,
                collision_center_local_z_m=3.2,
            ),
        ),
    ),
)
ASSET_BY_ID = {asset.asset_id: asset for asset in ASSETS}

SITES = (
    InfillSite(
        site_id="west-farm-belt",
        display_name="West Farm Belt",
        category="farmland",
        polygon_xz_m=(
            (620.0, 80.0),
            (1150.0, 80.0),
            (1150.0, 430.0),
            (900.0, 430.0),
            (620.0, 280.0),
        ),
        center_xz_m=(850.0, 240.0),
        access_route_ids=("west-farm-spine",),
        infrastructure_clearance_m=(
            ("base-highway", 499.0),
            ("penguin-neoq-bridge", 105.7),
        ),
    ),
    InfillSite(
        site_id="intercity-farm",
        display_name="Intercity Farm",
        category="farmland",
        polygon_xz_m=(
            (3900.0, 4180.0),
            (4230.0, 4180.0),
            (4230.0, 4470.0),
            (3940.0, 4470.0),
        ),
        center_xz_m=(4070.0, 4325.0),
        access_route_ids=("intercity-farm-road",),
        infrastructure_clearance_m=(
            ("base-highway", 109.2),
            ("neoq-intercity-bridge", 171.5),
            ("neoq-distributor-collision", 89.5),
            ("ceresoQr-collision", 133.0),
        ),
    ),
    InfillSite(
        site_id="sunset-courts",
        display_name="Sunset Courts",
        category="suburb",
        polygon_xz_m=(
            (850.0, 1080.0),
            (1260.0, 1080.0),
            (1260.0, 1640.0),
            (850.0, 1640.0),
        ),
        center_xz_m=(1055.0, 1360.0),
        access_route_ids=("sunset-frontage-road",),
        infrastructure_clearance_m=(
            ("base-highway-before-connector", 63.9),
            ("penguin-neoq-bridge", 165.4),
        ),
    ),
    InfillSite(
        site_id="arroyo-vista",
        display_name="Arroyo Vista",
        category="suburb",
        polygon_xz_m=(
            (3930.0, 3300.0),
            (4550.0, 3300.0),
            (4550.0, 3820.0),
            (3930.0, 3820.0),
        ),
        center_xz_m=(4240.0, 3560.0),
        access_route_ids=("arroyo-vista-boulevard",),
        infrastructure_clearance_m=(
            ("base-highway", 117.5),
            ("neoq-intercity-bridge", 161.4),
        ),
    ),
    InfillSite(
        site_id="west-highway-service",
        display_name="West Highway Service",
        category="service-station",
        polygon_xz_m=(
            (770.0, 1350.0),
            (840.0, 1350.0),
            (840.0, 1510.0),
            (770.0, 1510.0),
        ),
        center_xz_m=(805.0, 1425.0),
        access_route_ids=("sunset-frontage-road",),
        infrastructure_clearance_m=(("base-highway-frontage", 61.0),),
    ),
    InfillSite(
        site_id="intercity-service",
        display_name="Intercity Service",
        category="service-station",
        polygon_xz_m=(
            (3740.0, 3480.0),
            (3880.0, 3480.0),
            (3880.0, 3680.0),
            (3740.0, 3680.0),
        ),
        center_xz_m=(3810.0, 3580.0),
        access_route_ids=("intercity-service-road",),
        infrastructure_clearance_m=(
            ("base-highway-frontage", 49.0),
            ("neoq-intercity-bridge", 300.8),
        ),
    ),
    InfillSite(
        site_id="coyote-arch",
        display_name="Coyote Arch",
        category="natural-landmark",
        polygon_xz_m=(
            (3750.0, 2350.0),
            (4100.0, 2350.0),
            (4100.0, 2800.0),
            (3750.0, 2800.0),
        ),
        center_xz_m=(3925.0, 2575.0),
        access_route_ids=("coyote-arch-turnout",),
        infrastructure_clearance_m=(
            ("base-highway", 122.3),
            ("penguin-neoq-bridge", 1180.0),
        ),
    ),
    InfillSite(
        site_id="sagebrush-arroyo",
        display_name="Sagebrush Arroyo",
        category="natural-landmark",
        polygon_xz_m=(
            (1180.0, 250.0),
            (1330.0, 250.0),
            (1330.0, 650.0),
            (1180.0, 650.0),
        ),
        center_xz_m=(1255.0, 450.0),
        access_route_ids=("sagebrush-arroyo-trail",),
        infrastructure_clearance_m=(
            ("base-highway", 279.1),
            ("penguin-neoq-bridge", 139.8),
        ),
    ),
)
SITE_BY_ID = {site.site_id: site for site in SITES}


def _highway_anchor(
    anchor_id: str,
    position_m: Vector3,
    heading_degrees: float,
    outer_wall_point_m: Vector3,
    surface_interval_x_m: tuple[float, float],
) -> SourceAnchor:
    return SourceAnchor(
        anchor_id=anchor_id,
        authority="authenticated-native-road-edge",
        position_m=position_m,
        heading_degrees=heading_degrees,
        extraction_method=(
            "offline-decode-of-pinned-upward-facing-road-collision"
        ),
        source_contract="tools/cityworld_neoq_intercity_bridge.py",
        source_contract_format="ror-cityworld-neoq-bridge-authentication-v2",
        source_archive_sha256=PINNED_ARCHIVE_SHA256,
        source_tobj_member=PINNED_TOBJ_MEMBER,
        source_tobj_sha256=PINNED_TOBJ_SHA256,
        placement_line=PINNED_HIGHWAY_PLACEMENT_LINE,
        placement_object=PINNED_HIGHWAY_OBJECT,
        placement_position_m=PINNED_HIGHWAY_PLACEMENT_POSITION_M,
        placement_rotation_degrees=(
            PINNED_HIGHWAY_PLACEMENT_ROTATION_DEGREES
        ),
        collision_member=PINNED_HIGHWAY_COLLISION_MEMBER,
        collision_sha256=PINNED_HIGHWAY_COLLISION_SHA256,
        outer_wall_point_m=outer_wall_point_m,
        surface_seam_point_m=position_m,
        surface_interval_x_m=surface_interval_x_m,
    )


SOURCE_ANCHORS = (
    SourceAnchor(
        anchor_id=(
            "legacy.troadavenuesidewalk.l1354."
            "replacement_surface_edge.south"
        ),
        authority="authenticated-native-replacement-road-edge",
        position_m=(500.0, 0.100001, 365.14801),
        heading_degrees=90.0,
        extraction_method=(
            "decoded-south-edge-sample-of-pinned-line-1354-east-road-edge"
        ),
        source_contract=PENGUIN_CORRIDOR_CONTRACT,
        source_contract_format=PENGUIN_CORRIDOR_FORMAT,
        source_archive_sha256=PINNED_ARCHIVE_SHA256,
        source_tobj_member=PINNED_TOBJ_MEMBER,
        source_tobj_sha256=PINNED_TOBJ_SHA256,
        placement_line=1354,
        placement_object="troadavenuesidewalk",
        placement_position_m=(485.0, 0.1, 370.0),
        placement_rotation_degrees=(0.0, 90.0, 0.0),
        collision_member="crossroadavenuesidewalkbox.mesh",
        collision_sha256=(
            "1ea12c27545bce3e84bf0b5fa49def6bd2083f8c2f813dc14312c1a185823b7e"
        ),
        surface_seam_point_m=(500.0, 0.100001, 365.14801),
    ),
    _highway_anchor(
        "legacy.autopista_qr.l0378.surface_edge.east_z1425",
        (706.967, ROAD_SURFACE_ELEVATION_M, 1425.0),
        0.0,
        (708.967, -0.4, 1425.0),
        (682.967, 706.967),
    ),
    _highway_anchor(
        "legacy.autopista_qr.l0378.surface_edge.west_z2500",
        (4264.970, ROAD_SURFACE_ELEVATION_M, 2500.0),
        180.0,
        (4262.970, -0.4, 2500.0),
        (4264.970, 4288.970),
    ),
    _highway_anchor(
        "legacy.autopista_qr.l0378.surface_edge.west_z3500",
        (4732.970, ROAD_SURFACE_ELEVATION_M, 3500.0),
        180.0,
        (4730.970, -0.4, 3500.0),
        (4732.970, 4756.970),
    ),
    _highway_anchor(
        "legacy.autopista_qr.l0378.surface_edge.east_z3580",
        (3688.970, ROAD_SURFACE_ELEVATION_M, 3580.0),
        0.0,
        (3690.970, -0.4, 3580.0),
        (3664.970, 3688.970),
    ),
    _highway_anchor(
        "legacy.autopista_qr.l0378.surface_edge.east_curve_z4350",
        (3723.199038, ROAD_SURFACE_ELEVATION_M, 4350.0),
        0.0,
        (3725.335203, -0.4, 4350.0),
        (3697.534001, 3723.199038),
    ),
    SourceAnchor(
        anchor_id="west-farm-generated-east-edge-300",
        authority="generated-infill-road-seam",
        position_m=(1150.0, 0.100001, 300.0),
        heading_degrees=0.0,
        extraction_method="exact-upstream-generated-route-endpoint",
        source_contract="tools/cityworld_infill.py",
        source_contract_format=FORMAT,
        source_archive_sha256=PINNED_ARCHIVE_SHA256,
        source_tobj_member=PINNED_TOBJ_MEMBER,
        source_tobj_sha256=PINNED_TOBJ_SHA256,
        upstream_route_id="west-farm-spine",
    ),
)
ANCHOR_BY_ID = {anchor.anchor_id: anchor for anchor in SOURCE_ANCHORS}


def _stable_number(value: float) -> float:
    result = round(float(value), 9)
    return 0.0 if result == -0.0 else result


def _normalized_degrees(value: float) -> float:
    result = float(value) % 360.0
    return 0.0 if abs(result - 360.0) <= POSITION_EPSILON_M else result


def _build_route(
    *,
    route_id: str,
    source_anchor_id: str,
    destination_site_id: str,
    served_site_ids: tuple[str, ...],
    xz_points: tuple[PointXZ, ...],
    width_m: float,
    comments: tuple[str, ...],
) -> AccessRoute:
    anchor = ANCHOR_BY_ID[source_anchor_id]
    road_y = anchor.position_m[1]
    expected_source = (anchor.position_m[0], anchor.position_m[2])
    if xz_points[0] != expected_source:
        raise InfillFailure(f"{route_id} does not start at its source anchor")
    stations = [0.0]
    for first, second in zip(xz_points, xz_points[1:]):
        length = math.dist(first, second)
        if not math.isfinite(length) or length <= POSITION_EPSILON_M:
            raise InfillFailure(f"{route_id} contains a zero-length segment")
        stations.append(stations[-1] + length)
    points = []
    for index, ((x, z), station) in enumerate(zip(xz_points, stations)):
        tangent_start = xz_points[max(index - 1, 0)]
        tangent_end = xz_points[min(index + 1, len(xz_points) - 1)]
        if index not in (0, len(xz_points) - 1):
            tangent_start = xz_points[index - 1]
            tangent_end = xz_points[index + 1]
        dx = tangent_end[0] - tangent_start[0]
        dz = tangent_end[1] - tangent_start[1]
        yaw = _normalized_degrees(-math.degrees(math.atan2(dz, dx)))
        points.append(
            AccessRoadPoint(
                station_m=_stable_number(station),
                x=x,
                y=road_y,
                z=z,
                yaw_degrees=_stable_number(yaw),
                road_type=ROAD_TYPE,
                width_m=width_m,
                border_width_m=0.0,
                border_height_m=0.0,
            )
        )
    destination = points[-1]
    return AccessRoute(
        route_id=route_id,
        source_anchor_id=source_anchor_id,
        destination_site_id=destination_site_id,
        served_site_ids=served_site_ids,
        destination_position_m=(
            destination.x,
            destination.y,
            destination.z,
        ),
        points=tuple(points),
        comments=comments,
    )


ROUTES = (
    _build_route(
        route_id="west-farm-spine",
        source_anchor_id=(
            "legacy.troadavenuesidewalk.l1354."
            "replacement_surface_edge.south"
        ),
        destination_site_id="west-farm-belt",
        served_site_ids=("west-farm-belt",),
        xz_points=(
            (500.0, 365.14801),
            (500.0, 330.0),
            (660.0, 300.0),
            (900.0, 300.0),
            (1150.0, 300.0),
        ),
        width_m=8.0,
        comments=(
            "Project-authored curb-free access spine for West Farm Belt.",
            "The east endpoint is the Sagebrush Arroyo trail handoff.",
        ),
    ),
    _build_route(
        route_id="sunset-frontage-road",
        source_anchor_id=(
            "legacy.autopista_qr.l0378.surface_edge.east_z1425"
        ),
        destination_site_id="sunset-courts",
        served_site_ids=("west-highway-service", "sunset-courts"),
        xz_points=(
            (706.967, 1425.0),
            (730.0, 1425.0),
            (750.0, 1510.0),
            (772.5, 1510.0),
            (837.5, 1510.0),
            (860.0, 1510.0),
            (880.0, 1510.0),
            (910.0, 1510.0),
            (910.0, 1528.0),
        ),
        width_m=10.0,
        comments=(
            "Shared highway frontage road for Sunset Courts and fuel service.",
            "Its south edge meets the complete station forecourt without overlap.",
            "Its endpoint crosses the two-metre apron to the shared internal lane.",
        ),
    ),
    _build_route(
        route_id="coyote-arch-turnout",
        source_anchor_id=(
            "legacy.autopista_qr.l0378.surface_edge.west_z2500"
        ),
        destination_site_id="coyote-arch",
        served_site_ids=("coyote-arch",),
        xz_points=((4264.970, 2500.0), (4180.0, 2500.0), (4100.0, 2500.0)),
        width_m=8.0,
        comments=(
            "Curb-free scenic turnout from the authenticated highway edge.",
        ),
    ),
    _build_route(
        route_id="arroyo-vista-boulevard",
        source_anchor_id=(
            "legacy.autopista_qr.l0378.surface_edge.west_z3500"
        ),
        destination_site_id="arroyo-vista",
        served_site_ids=("arroyo-vista",),
        xz_points=(
            (4732.970, 3500.0),
            (4630.0, 3500.0),
            (4550.0, 3500.0),
            (4440.0, 3500.0),
            (4240.0, 3500.0),
            (4140.0, 3500.0),
            (4140.0, 3425.0),
            (4140.0, 3402.0),
        ),
        width_m=10.0,
        comments=(
            "Curb-free boulevard from the highway into Arroyo Vista.",
            "The north approach crosses the two-metre apron to an internal lane.",
        ),
    ),
    _build_route(
        route_id="intercity-service-road",
        source_anchor_id=(
            "legacy.autopista_qr.l0378.surface_edge.east_z3580"
        ),
        destination_site_id="intercity-service",
        served_site_ids=("intercity-service",),
        xz_points=(
            (3688.970, 3580.0),
            (3720.0, 3580.0),
            (3740.0, 3580.0),
        ),
        width_m=10.0,
        comments=(
            "Short curb-free service-road handoff beside the intercity highway.",
            "Its endpoint meets the west forecourt edge without overlap.",
        ),
    ),
    _build_route(
        route_id="intercity-farm-road",
        source_anchor_id=(
            "legacy.autopista_qr.l0378.surface_edge.east_curve_z4350"
        ),
        destination_site_id="intercity-farm",
        served_site_ids=("intercity-farm",),
        xz_points=(
            (3723.199038, 4350.0),
            (3800.0, 4350.0),
            (3860.0, 4350.0),
            (3880.0, 4350.0),
            (3880.0, 4285.0),
            (3916.0, 4285.0),
        ),
        width_m=8.0,
        comments=(
            "Curb-free farm road from the authenticated curved highway edge.",
            "Its final bend meets the authored crop and fence opening exactly.",
        ),
    ),
    _build_route(
        route_id="sagebrush-arroyo-trail",
        source_anchor_id="west-farm-generated-east-edge-300",
        destination_site_id="sagebrush-arroyo",
        served_site_ids=("sagebrush-arroyo",),
        xz_points=((1150.0, 300.0), (1180.0, 300.0)),
        width_m=6.0,
        comments=(
            "Narrow flat trail continuing the West Farm Belt access spine.",
        ),
    ),
)
ROUTE_BY_ID = {route.route_id: route for route in ROUTES}


def _placements(
    *,
    site_id: str,
    asset_id: str,
    prefix: str,
    positions: Sequence[tuple[float, float, float]],
) -> tuple[InfillPlacement, ...]:
    result = []
    for ordinal, (x, z, yaw_degrees) in enumerate(positions, start=1):
        placement_id = f"{prefix}-{ordinal:02d}"
        result.append(
            InfillPlacement(
                placement_id=placement_id,
                site_id=site_id,
                asset_id=asset_id,
                x=x,
                y=ROAD_SURFACE_ELEVATION_M,
                z=z,
                yaw_degrees=yaw_degrees,
                instance_name="cityworld_next_" + placement_id.replace("-", "_"),
            )
        )
    return tuple(result)


PLACEMENTS = (
    *_placements(
        site_id="west-farm-belt",
        asset_id=FARMSTEAD_ASSET_ID,
        prefix="west-farm-belt-farmstead",
        positions=(
            (700.0, 130.0, 0.0),
            (820.0, 130.0, 0.0),
            (940.0, 130.0, 0.0),
            (1060.0, 130.0, 0.0),
            (760.0, 230.0, 0.0),
            (880.0, 230.0, 0.0),
            (1000.0, 230.0, 0.0),
        ),
    ),
    *_placements(
        site_id="intercity-farm",
        asset_id=FARMSTEAD_ASSET_ID,
        prefix="intercity-farm-farmstead",
        positions=(
            (3965.0, 4250.0, 0.0),
            (4072.0, 4250.0, 0.0),
            (4180.0, 4250.0, 0.0),
            (3985.0, 4360.0, 90.0),
            (4080.0, 4360.0, 90.0),
            (4180.0, 4360.0, 90.0),
        ),
    ),
    *_placements(
        site_id="sunset-courts",
        asset_id=SUBURB_ASSET_ID,
        prefix="sunset-courts-suburb-block",
        positions=tuple(
            (x, z, 0.0)
            for z in (1140.0, 1260.0, 1570.0)
            for x in (910.0, 1055.0, 1200.0)
        ),
    ),
    *_placements(
        site_id="arroyo-vista",
        asset_id=SUBURB_ASSET_ID,
        prefix="arroyo-vista-suburb-block",
        positions=tuple(
            (x, z, 0.0)
            for z in (3360.0, 3690.0)
            for x in (3990.0, 4140.0, 4290.0, 4440.0)
        ),
    ),
    *_placements(
        site_id="west-highway-service",
        asset_id=SERVICE_STATION_ASSET_ID,
        prefix="west-highway-service-station",
        positions=((805.0, 1460.0, 90.0),),
    ),
    *_placements(
        site_id="intercity-service",
        asset_id=SERVICE_STATION_ASSET_ID,
        prefix="intercity-service-station",
        positions=((3785.0, 3580.0, 0.0),),
    ),
    *_placements(
        site_id="coyote-arch",
        asset_id=RED_MESA_ASSET_ID,
        prefix="coyote-arch-red-mesa",
        positions=(
            (3900.0, 2520.0, 0.0),
            (3930.0, 2570.0, 27.0),
            (3890.0, 2630.0, 73.0),
            (3965.0, 2650.0, 118.0),
            (4020.0, 2580.0, 164.0),
            (4015.0, 2700.0, 211.0),
            (3845.0, 2705.0, 302.0),
        ),
    ),
    *_placements(
        site_id="sagebrush-arroyo",
        asset_id=ARROYO_OASIS_ASSET_ID,
        prefix="sagebrush-arroyo-oasis",
        positions=(
            (1220.0, 350.0, 0.0),
            (1280.0, 350.0, 43.0),
            (1245.0, 420.0, 91.0),
            (1300.0, 470.0, 137.0),
            (1205.0, 510.0, 184.0),
            (1265.0, 560.0, 229.0),
            (1310.0, 620.0, 311.0),
        ),
    ),
)

CONNECTORS = (
    RouteAssetConnector(
        connector_id="sunset-frontage-west-service-forecourt",
        status="active",
        route_id="sunset-frontage-road",
        placement_id="west-highway-service-station-01",
        surface_id="full-concrete-forecourt-west-edge",
        route_segment_index=3,
        route_edge="right",
        target_surface_local_polygon_xz_m=(
            (-45.0, -32.5),
            (45.0, -32.5),
            (45.0, 32.5),
            (-45.0, 32.5),
        ),
        target_seam_local_xz_m=((-45.0, -32.5), (-45.0, 32.5)),
        target_surface_local_y_m=0.0,
        expected_render_overlap_depth_m=0.0,
    ),
    RouteAssetConnector(
        connector_id="sunset-frontage-sunset-courts-internal-street",
        status="active",
        route_id="sunset-frontage-road",
        placement_id="sunset-courts-suburb-block-07",
        surface_id="shared-internal-street-south-edge",
        route_segment_index=7,
        route_edge="end",
        target_surface_local_polygon_xz_m=(
            (-5.0, -42.0),
            (5.0, -42.0),
            (5.0, 42.0),
            (-5.0, 42.0),
        ),
        target_seam_local_xz_m=((-5.0, -42.0), (5.0, -42.0)),
        target_surface_local_y_m=0.0,
        expected_render_overlap_depth_m=2.0,
    ),
    RouteAssetConnector(
        connector_id="arroyo-vista-internal-street",
        status="active",
        route_id="arroyo-vista-boulevard",
        placement_id="arroyo-vista-suburb-block-02",
        surface_id="shared-internal-street-north-edge",
        route_segment_index=6,
        route_edge="end",
        target_surface_local_polygon_xz_m=(
            (-5.0, -42.0),
            (5.0, -42.0),
            (5.0, 42.0),
            (-5.0, 42.0),
        ),
        target_seam_local_xz_m=((5.0, 42.0), (-5.0, 42.0)),
        target_surface_local_y_m=0.0,
        expected_render_overlap_depth_m=2.0,
    ),
    RouteAssetConnector(
        connector_id="intercity-service-forecourt",
        status="active",
        route_id="intercity-service-road",
        placement_id="intercity-service-station-01",
        surface_id="full-concrete-forecourt-west-edge",
        route_segment_index=1,
        route_edge="end",
        target_surface_local_polygon_xz_m=(
            (-45.0, -32.5),
            (45.0, -32.5),
            (45.0, 32.5),
            (-45.0, 32.5),
        ),
        target_seam_local_xz_m=((-45.0, -5.0), (-45.0, 5.0)),
        target_surface_local_y_m=0.0,
        expected_render_overlap_depth_m=0.0,
    ),
    RouteAssetConnector(
        connector_id="intercity-farm-lane",
        status="active",
        route_id="intercity-farm-road",
        placement_id="intercity-farm-farmstead-01",
        surface_id="authored-asphalt-driveway-west-edge",
        route_segment_index=4,
        route_edge="end",
        target_surface_local_polygon_xz_m=(
            (-49.0, 31.0),
            (-36.0, 31.0),
            (-36.0, -17.5),
            (-28.0, -17.5),
            (-28.0, 43.0),
            (-36.0, 43.0),
            (-36.0, 39.0),
            (-49.0, 39.0),
        ),
        target_seam_local_xz_m=((-49.0, 39.0), (-49.0, 31.0)),
        target_surface_local_y_m=0.0,
        expected_render_overlap_depth_m=0.0,
    ),
)


def build_infill_plan() -> InfillPlan:
    """Return the immutable, ordering-stable checked infill plan."""

    return InfillPlan(
        assets=ASSETS,
        sites=SITES,
        source_anchors=SOURCE_ANCHORS,
        routes=ROUTES,
        placements=PLACEMENTS,
        connectors=CONNECTORS,
    )


def point_in_polygon(
    point: PointXZ,
    polygon: Sequence[PointXZ],
    *,
    epsilon: float = POSITION_EPSILON_M,
) -> bool:
    """Return whether a point is inside or on a simple XZ polygon."""

    x, z = point
    inside = False
    for first, second in zip(polygon, (*polygon[1:], polygon[0])):
        ax, az = first
        bx, bz = second
        cross = (x - ax) * (bz - az) - (z - az) * (bx - ax)
        if (
            abs(cross) <= epsilon
            and min(ax, bx) - epsilon <= x <= max(ax, bx) + epsilon
            and min(az, bz) - epsilon <= z <= max(az, bz) + epsilon
        ):
            return True
        if (az > z) != (bz > z):
            edge_x = ax + (z - az) * (bx - ax) / (bz - az)
            if x < edge_x:
                inside = not inside
    return inside


def _rotate_local_xz(
    local_x: float,
    local_z: float,
    yaw_degrees: float,
) -> PointXZ:
    radians = math.radians(yaw_degrees)
    cosine = math.cos(radians)
    sine = math.sin(radians)
    return (
        cosine * local_x + sine * local_z,
        -sine * local_x + cosine * local_z,
    )


def placement_footprint(
    placement: InfillPlacement,
    *,
    collision: bool = False,
) -> tuple[PointXZ, ...]:
    """Return the render footprint or conservative collision outer bounds."""

    asset = ASSET_BY_ID[placement.asset_id]
    if collision:
        if not asset.collision_components:
            raise InfillFailure(
                f"{asset.asset_id} has no collision components"
            )
        minimum_x = min(
            component.collision_center_local_x_m
            - component.collision_width_m / 2.0
            for component in asset.collision_components
        )
        maximum_x = max(
            component.collision_center_local_x_m
            + component.collision_width_m / 2.0
            for component in asset.collision_components
        )
        minimum_z = min(
            component.collision_center_local_z_m
            - component.collision_depth_m / 2.0
            for component in asset.collision_components
        )
        maximum_z = max(
            component.collision_center_local_z_m
            + component.collision_depth_m / 2.0
            for component in asset.collision_components
        )
        width = maximum_x - minimum_x
        depth = maximum_z - minimum_z
        offset_x = (minimum_x + maximum_x) / 2.0
        offset_z = (minimum_z + maximum_z) / 2.0
    else:
        width = asset.render_width_m
        depth = asset.render_depth_m
        offset_x = 0.0
        offset_z = 0.0
    result = []
    for local_x, local_z in (
        (offset_x - width / 2.0, offset_z - depth / 2.0),
        (offset_x + width / 2.0, offset_z - depth / 2.0),
        (offset_x + width / 2.0, offset_z + depth / 2.0),
        (offset_x - width / 2.0, offset_z + depth / 2.0),
    ):
        dx, dz = _rotate_local_xz(
            local_x,
            local_z,
            placement.yaw_degrees,
        )
        result.append(
            (
                _stable_number(placement.x + dx),
                _stable_number(placement.z + dz),
            )
        )
    return tuple(result)


def collision_component_footprint(
    placement: InfillPlacement,
    component: CollisionComponent,
) -> tuple[PointXZ, ...]:
    """Return one exact collision component footprint in world XZ."""

    result = []
    for local_x, local_z in (
        (
            component.collision_center_local_x_m
            - component.collision_width_m / 2.0,
            component.collision_center_local_z_m
            - component.collision_depth_m / 2.0,
        ),
        (
            component.collision_center_local_x_m
            + component.collision_width_m / 2.0,
            component.collision_center_local_z_m
            - component.collision_depth_m / 2.0,
        ),
        (
            component.collision_center_local_x_m
            + component.collision_width_m / 2.0,
            component.collision_center_local_z_m
            + component.collision_depth_m / 2.0,
        ),
        (
            component.collision_center_local_x_m
            - component.collision_width_m / 2.0,
            component.collision_center_local_z_m
            + component.collision_depth_m / 2.0,
        ),
    ):
        result.append(placement_local_to_world_xz(
            placement,
            (local_x, local_z),
        ))
    return tuple(result)


def placement_collision_footprints(
    placement: InfillPlacement,
) -> tuple[tuple[PointXZ, ...], ...]:
    """Return every exact world-space collision component footprint."""

    asset = ASSET_BY_ID[placement.asset_id]
    return tuple(
        collision_component_footprint(placement, component)
        for component in asset.collision_components
    )


def placement_local_to_world_xz(
    placement: InfillPlacement,
    point: PointXZ,
) -> PointXZ:
    """Transform an exact asset-local XZ point through a TOBJ placement."""

    dx, dz = _rotate_local_xz(point[0], point[1], placement.yaw_degrees)
    return (
        _stable_number(placement.x + dx),
        _stable_number(placement.z + dz),
    )


def placement_world_to_local_xz(
    placement: InfillPlacement,
    point: PointXZ,
) -> PointXZ:
    """Invert the exact asset-local XZ transform for connector auditing."""

    radians = math.radians(placement.yaw_degrees)
    cosine = math.cos(radians)
    sine = math.sin(radians)
    dx = point[0] - placement.x
    dz = point[1] - placement.z
    return (
        _stable_number(cosine * dx - sine * dz),
        _stable_number(sine * dx + cosine * dz),
    )


def route_point_cross_section(
    point: AccessRoadPoint,
) -> tuple[PointXZ, PointXZ]:
    """Return the native yaw-oriented left/right edges of a road point."""

    radians = math.radians(point.yaw_degrees)
    normal_x = math.sin(radians)
    normal_z = math.cos(radians)
    half_width = point.width_m / 2.0
    return (
        (
            _stable_number(point.x + normal_x * half_width),
            _stable_number(point.z + normal_z * half_width),
        ),
        (
            _stable_number(point.x - normal_x * half_width),
            _stable_number(point.z - normal_z * half_width),
        ),
    )


def polygon_is_strictly_convex(
    polygon: Sequence[PointXZ],
    *,
    epsilon: float = POSITION_EPSILON_M,
) -> bool:
    """Return whether every ordered turn has one non-zero orientation."""

    if len(polygon) < 3:
        return False
    orientation_sign = 0
    for first, second, third in zip(
        polygon,
        (*polygon[1:], polygon[0]),
        (*polygon[2:], polygon[0], polygon[1]),
    ):
        cross = (
            (second[0] - first[0]) * (third[1] - second[1])
            - (second[1] - first[1]) * (third[0] - second[0])
        )
        if abs(cross) <= epsilon:
            return False
        current_sign = 1 if cross > 0.0 else -1
        if orientation_sign and current_sign != orientation_sign:
            return False
        orientation_sign = current_sign
    return True


def route_segment_surface_footprint(
    route: AccessRoute,
    segment_index: int,
) -> tuple[PointXZ, ...]:
    """Return the exact yaw-oriented surface quad for a generated segment."""

    if (
        isinstance(segment_index, bool)
        or not isinstance(segment_index, int)
        or segment_index < 0
        or segment_index >= len(route.points) - 1
    ):
        raise InfillFailure(
            f"{route.route_id} connector segment index is invalid"
        )
    start = route.points[segment_index]
    end = route.points[segment_index + 1]
    start_left, start_right = route_point_cross_section(start)
    end_left, end_right = route_point_cross_section(end)
    footprint = (start_left, end_left, end_right, start_right)
    if not polygon_is_strictly_convex(footprint):
        raise InfillFailure(
            f"{route.route_id} segment {segment_index} surface quad "
            "is not strictly convex"
        )
    return footprint


def route_connector_world_seam(
    route: AccessRoute,
    connector: RouteAssetConnector,
) -> tuple[PointXZ, PointXZ]:
    """Return the selected generated-road side or terminal cross-section."""

    if connector.route_segment_index >= len(route.points) - 1:
        raise InfillFailure(
            f"{connector.connector_id} route segment is unavailable"
        )
    start = route.points[connector.route_segment_index]
    end = route.points[connector.route_segment_index + 1]
    start_left, start_right = route_point_cross_section(start)
    end_left, end_right = route_point_cross_section(end)
    if connector.route_edge == "left":
        return (start_left, end_left)
    if connector.route_edge == "right":
        return (start_right, end_right)
    if connector.route_edge == "end":
        if connector.route_segment_index != len(route.points) - 2:
            raise InfillFailure(
                f"{connector.connector_id} does not use the terminal segment"
            )
        return (end_left, end_right)
    raise InfillFailure(
        f"{connector.connector_id} route edge is unsupported"
    )


def connector_target_world_seam(
    connector: RouteAssetConnector,
    placement: InfillPlacement,
) -> tuple[PointXZ, PointXZ]:
    return tuple(
        placement_local_to_world_xz(placement, point)
        for point in connector.target_seam_local_xz_m
    )


def connector_seam_gap_m(
    connector: RouteAssetConnector,
    route: AccessRoute,
    placement: InfillPlacement,
) -> float:
    """Return the maximum endpoint gap under the best seam orientation."""

    route_start, route_end = route_connector_world_seam(route, connector)
    target_start, target_end = connector_target_world_seam(
        connector,
        placement,
    )
    forward = max(
        math.dist(route_start, target_start),
        math.dist(route_end, target_end),
    )
    reverse = max(
        math.dist(route_start, target_end),
        math.dist(route_end, target_start),
    )
    return _stable_number(min(forward, reverse))


def _point_on_polygon_boundary(
    point: PointXZ,
    polygon: Sequence[PointXZ],
) -> bool:
    return any(
        _point_segment_distance(point, start, end)
        <= POSITION_EPSILON_M
        for start, end in zip(polygon, (*polygon[1:], polygon[0]))
    )


def _endpoint_render_overlap_depth_m(
    route: AccessRoute,
    placement: InfillPlacement,
) -> float:
    """Measure the terminal road-center penetration into a render footprint."""

    previous = placement_world_to_local_xz(
        placement,
        (route.points[-2].x, route.points[-2].z),
    )
    endpoint = placement_world_to_local_xz(
        placement,
        (route.points[-1].x, route.points[-1].z),
    )
    dx = endpoint[0] - previous[0]
    dz = endpoint[1] - previous[1]
    length = math.hypot(dx, dz)
    if length <= POSITION_EPSILON_M:
        raise InfillFailure(
            f"{route.route_id} terminal connector segment is degenerate"
        )
    backwards = (-dx / length, -dz / length)
    asset = ASSET_BY_ID[placement.asset_id]
    half_width = asset.render_width_m / 2.0
    half_depth = asset.render_depth_m / 2.0
    if (
        abs(endpoint[0]) > half_width + POSITION_EPSILON_M
        or abs(endpoint[1]) > half_depth + POSITION_EPSILON_M
    ):
        raise InfillFailure(
            f"{route.route_id} terminal connector misses its render footprint"
        )
    candidates = []
    for origin, direction, half_extent, other, other_direction, other_half in (
        (endpoint[0], backwards[0], half_width,
         endpoint[1], backwards[1], half_depth),
        (endpoint[1], backwards[1], half_depth,
         endpoint[0], backwards[0], half_width),
    ):
        if abs(direction) <= POSITION_EPSILON_M:
            continue
        for boundary in (-half_extent, half_extent):
            distance = (boundary - origin) / direction
            if distance < -POSITION_EPSILON_M:
                continue
            other_at_boundary = other + distance * other_direction
            if abs(other_at_boundary) <= other_half + POSITION_EPSILON_M:
                candidates.append(max(0.0, distance))
    if not candidates:
        raise InfillFailure(
            f"{route.route_id} terminal connector has no render-boundary entry"
        )
    return _stable_number(min(candidates))


def _polygon_axes(polygon: Sequence[PointXZ]) -> tuple[PointXZ, ...]:
    axes = []
    for first, second in zip(polygon, (*polygon[1:], polygon[0])):
        dx = second[0] - first[0]
        dz = second[1] - first[1]
        length = math.hypot(dx, dz)
        if length <= POSITION_EPSILON_M:
            raise InfillFailure("polygon contains a zero-length edge")
        axes.append((-dz / length, dx / length))
    return tuple(axes)


def polygons_overlap(
    first: Sequence[PointXZ],
    second: Sequence[PointXZ],
    *,
    epsilon: float = POSITION_EPSILON_M,
) -> bool:
    """Return whether two convex polygons have positive overlapping area."""

    for axis_x, axis_z in (*_polygon_axes(first), *_polygon_axes(second)):
        first_projection = [
            x * axis_x + z * axis_z for x, z in first
        ]
        second_projection = [
            x * axis_x + z * axis_z for x, z in second
        ]
        if (
            max(first_projection) <= min(second_projection) + epsilon
            or max(second_projection) <= min(first_projection) + epsilon
        ):
            return False
    return True


def _orientation(first: PointXZ, second: PointXZ, third: PointXZ) -> float:
    return (
        (second[0] - first[0]) * (third[1] - first[1])
        - (second[1] - first[1]) * (third[0] - first[0])
    )


def _segments_intersect(
    first_a: PointXZ,
    first_b: PointXZ,
    second_a: PointXZ,
    second_b: PointXZ,
) -> bool:
    orientations = (
        _orientation(first_a, first_b, second_a),
        _orientation(first_a, first_b, second_b),
        _orientation(second_a, second_b, first_a),
        _orientation(second_a, second_b, first_b),
    )
    if (
        orientations[0] * orientations[1] < 0.0
        and orientations[2] * orientations[3] < 0.0
    ):
        return True
    for point, edge_a, edge_b, orientation in (
        (second_a, first_a, first_b, orientations[0]),
        (second_b, first_a, first_b, orientations[1]),
        (first_a, second_a, second_b, orientations[2]),
        (first_b, second_a, second_b, orientations[3]),
    ):
        if (
            abs(orientation) <= POSITION_EPSILON_M
            and min(edge_a[0], edge_b[0]) - POSITION_EPSILON_M
            <= point[0]
            <= max(edge_a[0], edge_b[0]) + POSITION_EPSILON_M
            and min(edge_a[1], edge_b[1]) - POSITION_EPSILON_M
            <= point[1]
            <= max(edge_a[1], edge_b[1]) + POSITION_EPSILON_M
        ):
            return True
    return False


def _point_segment_distance(
    point: PointXZ,
    start: PointXZ,
    end: PointXZ,
) -> float:
    dx = end[0] - start[0]
    dz = end[1] - start[1]
    length_squared = dx * dx + dz * dz
    if length_squared <= POSITION_EPSILON_M:
        return math.dist(point, start)
    parameter = (
        (point[0] - start[0]) * dx + (point[1] - start[1]) * dz
    ) / length_squared
    parameter = max(0.0, min(1.0, parameter))
    projection = (
        start[0] + parameter * dx,
        start[1] + parameter * dz,
    )
    return math.dist(point, projection)


def _segment_polygon_distance(
    start: PointXZ,
    end: PointXZ,
    polygon: Sequence[PointXZ],
) -> float:
    if point_in_polygon(start, polygon) or point_in_polygon(end, polygon):
        return 0.0
    edges = tuple(zip(polygon, (*polygon[1:], polygon[0])))
    if any(
        _segments_intersect(start, end, edge_start, edge_end)
        for edge_start, edge_end in edges
    ):
        return 0.0
    distances = [
        _point_segment_distance(vertex, start, end)
        for vertex in polygon
    ]
    distances.extend(
        _point_segment_distance(start, edge_start, edge_end)
        for edge_start, edge_end in edges
    )
    distances.extend(
        _point_segment_distance(end, edge_start, edge_end)
        for edge_start, edge_end in edges
    )
    return min(distances)


def polygon_clearance(
    first: Sequence[PointXZ],
    second: Sequence[PointXZ],
) -> float:
    if polygons_overlap(first, second):
        return 0.0
    first_edges = tuple(zip(first, (*first[1:], first[0])))
    second_edges = tuple(zip(second, (*second[1:], second[0])))
    return min(
        _segment_polygon_distance(start, end, second)
        for start, end in first_edges
    )


def route_placement_clearance_m(
    route: AccessRoute,
    placement: InfillPlacement,
) -> float:
    """Return road-edge to render-footprint clearance in metres."""

    footprint = placement_footprint(placement)
    clearances = []
    for first, second in zip(route.points, route.points[1:]):
        centerline_distance = _segment_polygon_distance(
            (first.x, first.z),
            (second.x, second.z),
            footprint,
        )
        clearances.append(
            centerline_distance - max(first.width_m, second.width_m) / 2.0
        )
    return min(clearances)


def _minimum_pairwise_clearance(
    placements: Sequence[InfillPlacement],
    *,
    collision: bool,
) -> tuple[float, tuple[str, str]]:
    minimum = math.inf
    pair = ("", "")
    for index, first in enumerate(placements):
        first_polygons = (
            placement_collision_footprints(first)
            if collision
            else (placement_footprint(first),)
        )
        for second in placements[index + 1:]:
            second_polygons = (
                placement_collision_footprints(second)
                if collision
                else (placement_footprint(second),)
            )
            clearance = math.inf
            for first_polygon in first_polygons:
                for second_polygon in second_polygons:
                    if polygons_overlap(first_polygon, second_polygon):
                        raise InfillFailure(
                            f"placements overlap: {first.placement_id}, "
                            f"{second.placement_id}"
                        )
                    clearance = min(
                        clearance,
                        polygon_clearance(
                            first_polygon,
                            second_polygon,
                        ),
                    )
            if clearance < minimum:
                minimum = clearance
                pair = (first.placement_id, second.placement_id)
    return minimum, pair


def route_collision_component_clearance_m(
    route: AccessRoute,
    placement: InfillPlacement,
) -> float:
    """Return road-surface clearance from every exact collision component."""

    minimum = math.inf
    asset = ASSET_BY_ID[placement.asset_id]
    component_footprints = zip(
        asset.collision_components,
        placement_collision_footprints(placement),
    )
    checked_components = tuple(component_footprints)
    if not checked_components:
        raise InfillFailure(
            f"{placement.placement_id} has no collision components"
        )
    for segment_index in range(len(route.points) - 1):
        road_surface = route_segment_surface_footprint(
            route,
            segment_index,
        )
        for component, collision_footprint in checked_components:
            if polygons_overlap(road_surface, collision_footprint):
                raise InfillFailure(
                    f"{route.route_id} overlaps building collision component "
                    f"{placement.placement_id}/{component.component_id}"
                )
            minimum = min(
                minimum,
                polygon_clearance(road_surface, collision_footprint),
            )
    return minimum


def _audit_connector_contract(
    connector: RouteAssetConnector,
    route: AccessRoute,
    placement: InfillPlacement,
) -> dict[str, Any]:
    asset = ASSET_BY_ID.get(placement.asset_id)
    if asset is None:
        raise InfillFailure(
            f"{connector.connector_id} target asset is unavailable"
        )
    if placement.site_id not in route.served_site_ids:
        raise InfillFailure(
            f"{connector.connector_id} target site is not served by its route"
        )
    if connector.status == "pending":
        if (
            connector.route_edge != "pending"
            or connector.blocker is None
            or not connector.blocker
            or connector.target_surface_local_polygon_xz_m
            or connector.target_seam_local_xz_m
            or connector.expected_render_overlap_depth_m != 0.0
            or connector.target_surface_local_y_m != 0.0
            or connector.maximum_seam_gap_m
            != MAXIMUM_CONNECTOR_SEAM_GAP_M
        ):
            raise InfillFailure(
                f"{connector.connector_id} pending contract drifted"
            )
        route_segment_surface_footprint(
            route,
            connector.route_segment_index,
        )
        collision_clearance = route_collision_component_clearance_m(
            route,
            placement,
        )
        render_clearance = route_placement_clearance_m(route, placement)
        if render_clearance < MINIMUM_ACCESS_ROAD_CLEARANCE_M:
            raise InfillFailure(
                f"{connector.connector_id} pending road is not fail-closed"
            )
        return {
            "blocker": connector.blocker,
            "collision_profile": asset.collision_profile,
            "collision_proxy_overlap_count": 0,
            "connector_id": connector.connector_id,
            "flush": False,
            "minimum_collision_proxy_clearance_m":
                _stable_number(collision_clearance),
            "observed_render_clearance_m":
                _stable_number(render_clearance),
            "placement_id": connector.placement_id,
            "route_id": connector.route_id,
            "seam_gap_m": None,
            "status": "pending",
            "surface_id": connector.surface_id,
        }
    if (
        not connector.connector_id
        or not connector.surface_id
        or connector.status != "active"
        or connector.blocker is not None
        or connector.maximum_seam_gap_m
        != MAXIMUM_CONNECTOR_SEAM_GAP_M
        or connector.route_edge not in {"left", "right", "end"}
        or len(connector.target_surface_local_polygon_xz_m) < 4
        or len(connector.target_seam_local_xz_m) != 2
        or connector.target_surface_local_y_m != 0.0
        or not math.isfinite(connector.expected_render_overlap_depth_m)
        or connector.expected_render_overlap_depth_m < 0.0
    ):
        raise InfillFailure(
            f"{connector.connector_id} connector profile drifted"
        )
    route_segment_surface_footprint(
        route,
        connector.route_segment_index,
    )
    selected_points = (
        route.points[connector.route_segment_index],
        route.points[connector.route_segment_index + 1],
    )
    if (
        any(
            abs(
                point.y
                - (
                    placement.y
                    + connector.target_surface_local_y_m
                )
            )
            > POSITION_EPSILON_M
            for point in selected_points
        )
        or selected_points[0].width_m != selected_points[1].width_m
    ):
        raise InfillFailure(
            f"{connector.connector_id} road elevation or width drifted"
        )

    half_width = asset.render_width_m / 2.0
    half_depth = asset.render_depth_m / 2.0
    surface = connector.target_surface_local_polygon_xz_m
    if any(
        abs(x) > half_width + POSITION_EPSILON_M
        or abs(z) > half_depth + POSITION_EPSILON_M
        for x, z in surface
    ):
        raise InfillFailure(
            f"{connector.connector_id} target surface left the asset footprint"
        )
    if any(
        not _point_on_polygon_boundary(point, surface)
        for point in connector.target_seam_local_xz_m
    ):
        raise InfillFailure(
            f"{connector.connector_id} target seam left its surface edge"
        )

    route_seam = route_connector_world_seam(route, connector)
    target_seam = connector_target_world_seam(connector, placement)
    route_seam_width = math.dist(*route_seam)
    target_seam_width = math.dist(*target_seam)
    road_width = selected_points[0].width_m
    if connector.route_edge == "end":
        if (
            abs(route_seam_width - road_width) > POSITION_EPSILON_M
            or abs(target_seam_width - road_width) > POSITION_EPSILON_M
        ):
            raise InfillFailure(
                f"{connector.connector_id} terminal seam width drifted"
            )
        observed_overlap_depth = _endpoint_render_overlap_depth_m(
            route,
            placement,
        )
    else:
        target_on_render_edge = all(
            abs(abs(x) - half_width) <= POSITION_EPSILON_M
            or abs(abs(z) - half_depth) <= POSITION_EPSILON_M
            for x, z in connector.target_seam_local_xz_m
        )
        if (
            not target_on_render_edge
            or abs(route_seam_width - target_seam_width)
            > POSITION_EPSILON_M
        ):
            raise InfillFailure(
                f"{connector.connector_id} side seam width drifted"
            )
        observed_overlap_depth = 0.0
    if (
        abs(
            observed_overlap_depth
            - connector.expected_render_overlap_depth_m
        )
        > POSITION_EPSILON_M
    ):
        raise InfillFailure(
            f"{connector.connector_id} render overlap depth drifted"
        )

    seam_gap = connector_seam_gap_m(connector, route, placement)
    if seam_gap > connector.maximum_seam_gap_m:
        raise InfillFailure(
            f"{connector.connector_id} seam gap exceeds one millimetre"
        )
    if route_placement_clearance_m(route, placement) > seam_gap:
        raise InfillFailure(
            f"{connector.connector_id} route does not reach its target surface"
        )

    collision_clearance = route_collision_component_clearance_m(
        route,
        placement,
    )
    return {
        "collision_profile": asset.collision_profile,
        "collision_proxy_overlap_count": 0,
        "connector_id": connector.connector_id,
        "expected_render_overlap_depth_m":
            _stable_number(connector.expected_render_overlap_depth_m),
        "maximum_seam_gap_m":
            _stable_number(connector.maximum_seam_gap_m),
        "minimum_collision_proxy_clearance_m":
            _stable_number(collision_clearance),
        "observed_render_overlap_depth_m":
            _stable_number(observed_overlap_depth),
        "placement_id": connector.placement_id,
        "road_width_m": _stable_number(road_width),
        "route_edge": connector.route_edge,
        "route_id": connector.route_id,
        "route_segment_index": connector.route_segment_index,
        "route_world_seam_xz_m": [
            list(point) for point in route_seam
        ],
        "seam_gap_m": seam_gap,
        "status": "active",
        "surface_id": connector.surface_id,
        "target_seam_width_m": _stable_number(target_seam_width),
        "target_world_seam_xz_m": [
            list(point) for point in target_seam
        ],
    }


def audit_plan(plan: InfillPlan | None = None) -> dict[str, Any]:
    """Fail closed on geometric or provenance drift and return a stable audit."""

    checked = build_infill_plan() if plan is None else plan
    asset_by_id = {asset.asset_id: asset for asset in checked.assets}
    site_by_id = {site.site_id: site for site in checked.sites}
    anchor_by_id = {
        anchor.anchor_id: anchor for anchor in checked.source_anchors
    }
    route_by_id = {route.route_id: route for route in checked.routes}
    placement_by_id = {
        placement.placement_id: placement
        for placement in checked.placements
    }
    connector_by_id = {
        connector.connector_id: connector
        for connector in checked.connectors
    }
    if len(asset_by_id) != len(checked.assets):
        raise InfillFailure("asset identifiers are not unique")
    if checked.assets != ASSETS:
        raise InfillFailure("regional-infill asset contract drifted")
    if len(site_by_id) != len(checked.sites):
        raise InfillFailure("site identifiers are not unique")
    if len(anchor_by_id) != len(checked.source_anchors):
        raise InfillFailure("source-anchor identifiers are not unique")
    if len(route_by_id) != len(checked.routes):
        raise InfillFailure("access-route identifiers are not unique")
    if len(placement_by_id) != len(checked.placements):
        raise InfillFailure("placement identifiers are not unique")
    if len({item.instance_name for item in checked.placements}) != len(
        checked.placements
    ):
        raise InfillFailure("runtime instance names are not unique")
    if len(connector_by_id) != len(checked.connectors):
        raise InfillFailure("connector identifiers are not unique")
    connector_pairs = {
        (connector.route_id, connector.placement_id)
        for connector in checked.connectors
    }
    active_connector_pairs = {
        (connector.route_id, connector.placement_id)
        for connector in checked.connectors
        if connector.status == "active"
    }
    if (
        len(connector_pairs) != len(checked.connectors)
        or len(checked.connectors) != 5
        or len(active_connector_pairs) != 5
        or sum(
            connector.status == "pending"
            for connector in checked.connectors
        )
        != 0
    ):
        raise InfillFailure("route-to-asset connector set drifted")

    for asset in checked.assets:
        component_ids = {
            component.component_id
            for component in asset.collision_components
        }
        expected_component_count = len(asset.collision_components)
        if (
            asset.collision_profile
            not in {
                "single-watertight-proxy-v1",
                "compound-watertight-proxy-v1",
            }
            or not component_ids
            or len(component_ids) != expected_component_count
            or (
                asset.collision_profile == "single-watertight-proxy-v1"
                and expected_component_count != 1
            )
            or (
                asset.collision_profile == "compound-watertight-proxy-v1"
                and expected_component_count <= 1
            )
        ):
            raise InfillFailure(
                f"{asset.asset_id} collision component profile drifted"
            )
        half_render_width = asset.render_width_m / 2.0
        half_render_depth = asset.render_depth_m / 2.0
        for component in asset.collision_components:
            if (
                not component.component_id
                or not all(
                    math.isfinite(value) and value > 0.0
                    for value in (
                        component.collision_width_m,
                        component.collision_depth_m,
                    )
                )
                or not all(
                    math.isfinite(value)
                    for value in (
                        component.collision_center_local_x_m,
                        component.collision_center_local_z_m,
                    )
                )
                or abs(component.collision_center_local_x_m)
                + component.collision_width_m / 2.0
                > half_render_width + POSITION_EPSILON_M
                or abs(component.collision_center_local_z_m)
                + component.collision_depth_m / 2.0
                > half_render_depth + POSITION_EPSILON_M
            ):
                raise InfillFailure(
                    f"{asset.asset_id}/{component.component_id} collision "
                    "component left the render footprint"
                )

    for site in checked.sites:
        if len(site.polygon_xz_m) < 3:
            raise InfillFailure(f"{site.site_id} polygon is incomplete")
        if not point_in_polygon(site.center_xz_m, site.polygon_xz_m):
            raise InfillFailure(f"{site.site_id} center left its parcel")
        if not site.infrastructure_clearance_m or any(
            clearance <= 0.0
            for _, clearance in site.infrastructure_clearance_m
        ):
            raise InfillFailure(
                f"{site.site_id} infrastructure clearance is invalid"
            )
        if any(route_id not in route_by_id for route_id in site.access_route_ids):
            raise InfillFailure(f"{site.site_id} access route is missing")

    for placement in checked.placements:
        site = site_by_id.get(placement.site_id)
        asset = asset_by_id.get(placement.asset_id)
        if site is None or asset is None:
            raise InfillFailure(
                f"{placement.placement_id} references an unknown contract"
            )
        if asset.category != site.category:
            raise InfillFailure(
                f"{placement.placement_id} category does not match its site"
            )
        if not all(
            point_in_polygon(corner, site.polygon_xz_m)
            for corner in placement_footprint(placement)
        ):
            raise InfillFailure(
                f"{placement.placement_id} render footprint left "
                f"{site.site_id}"
            )
        if any(
            not all(
                point_in_polygon(corner, site.polygon_xz_m)
                for corner in collision_footprint
            )
            for collision_footprint in placement_collision_footprints(
                placement
            )
        ):
            raise InfillFailure(
                f"{placement.placement_id} collision footprint left "
                f"{site.site_id}"
            )

    render_clearance, render_pair = _minimum_pairwise_clearance(
        checked.placements,
        collision=False,
    )
    collision_clearance, collision_pair = _minimum_pairwise_clearance(
        checked.placements,
        collision=True,
    )
    if render_clearance < MINIMUM_PLACEMENT_GAP_M:
        raise InfillFailure("render footprints are too close")

    for route in checked.routes:
        anchor = anchor_by_id.get(route.source_anchor_id)
        destination_site = site_by_id.get(route.destination_site_id)
        if anchor is None or destination_site is None:
            raise InfillFailure(f"{route.route_id} references a missing anchor")
        if (
            not route.collision_enabled
            or route.collision_endcaps_enabled
            or route.smoothing_num_splits != 0
            or route.surface_material != ROAD_SURFACE_MATERIAL
            or len(route.points) < 2
        ):
            raise InfillFailure(f"{route.route_id} road profile drifted")
        first = route.points[0]
        last = route.points[-1]
        if math.dist(
            (first.x, first.y, first.z),
            anchor.position_m,
        ) > POSITION_EPSILON_M:
            raise InfillFailure(f"{route.route_id} source handoff drifted")
        if math.dist(
            (last.x, last.y, last.z),
            route.destination_position_m,
        ) > POSITION_EPSILON_M:
            raise InfillFailure(f"{route.route_id} destination drifted")
        if not point_in_polygon(
            (last.x, last.z),
            destination_site.polygon_xz_m,
        ):
            raise InfillFailure(
                f"{route.route_id} does not reach its destination parcel"
            )
        if (
            any(point.road_type != ROAD_TYPE for point in route.points)
            or any(
                abs(point.y - first.y)
                > POSITION_EPSILON_M
                for point in route.points
            )
            or any(
                second.station_m <= first_point.station_m
                for first_point, second in zip(
                    route.points,
                    route.points[1:],
                )
            )
        ):
            raise InfillFailure(f"{route.route_id} is not a flat open route")
        heading_error = abs(
            (first.yaw_degrees - anchor.heading_degrees + 180.0)
            % 360.0
            - 180.0
        )
        if heading_error > POSITION_EPSILON_M:
            raise InfillFailure(f"{route.route_id} source heading drifted")

    connector_evidence = []
    for connector in checked.connectors:
        route = route_by_id.get(connector.route_id)
        placement = placement_by_id.get(connector.placement_id)
        if route is None or placement is None:
            raise InfillFailure(
                f"{connector.connector_id} references a missing route or asset"
            )
        connector_evidence.append(
            _audit_connector_contract(connector, route, placement)
        )

    minimum_route_clearance = math.inf
    minimum_route_pair = ("", "")
    minimum_route_collision_clearance = math.inf
    minimum_route_collision_pair = ("", "")
    for route in checked.routes:
        for placement in checked.placements:
            pair = (route.route_id, placement.placement_id)
            route_collision_clearance = (
                route_collision_component_clearance_m(
                    route,
                    placement,
                )
            )
            if route_collision_clearance < minimum_route_collision_clearance:
                minimum_route_collision_clearance = (
                    route_collision_clearance
                )
                minimum_route_collision_pair = pair

            clearance = route_placement_clearance_m(route, placement)
            if pair in active_connector_pairs:
                continue
            if clearance < MINIMUM_ACCESS_ROAD_CLEARANCE_M:
                raise InfillFailure(
                    "a non-designated access road enters an authored "
                    f"asset footprint: {route.route_id}, "
                    f"{placement.placement_id}"
                )
            if clearance < minimum_route_clearance:
                minimum_route_clearance = clearance
                minimum_route_pair = pair

    for anchor in checked.source_anchors:
        if (
            anchor.source_archive_sha256 != PINNED_ARCHIVE_SHA256
            or anchor.source_tobj_member != PINNED_TOBJ_MEMBER
            or anchor.source_tobj_sha256 != PINNED_TOBJ_SHA256
        ):
            raise InfillFailure(f"{anchor.anchor_id} provenance drifted")
        if anchor.authority == "authenticated-native-road-edge" and (
            anchor.placement_line != PINNED_HIGHWAY_PLACEMENT_LINE
            or anchor.placement_object != PINNED_HIGHWAY_OBJECT
            or anchor.collision_member != PINNED_HIGHWAY_COLLISION_MEMBER
            or anchor.collision_sha256 != PINNED_HIGHWAY_COLLISION_SHA256
            or anchor.surface_seam_point_m != anchor.position_m
            or anchor.outer_wall_point_m is None
            or anchor.surface_interval_x_m is None
        ):
            raise InfillFailure(f"{anchor.anchor_id} highway evidence drifted")
        if anchor.upstream_route_id is not None:
            upstream = route_by_id.get(anchor.upstream_route_id)
            if upstream is None or math.dist(
                (
                    upstream.points[-1].x,
                    upstream.points[-1].y,
                    upstream.points[-1].z,
                ),
                anchor.position_m,
            ) > POSITION_EPSILON_M:
                raise InfillFailure(
                    f"{anchor.anchor_id} generated seam is disconnected"
                )

    site_counts: dict[str, int] = {}
    placement_counts: dict[str, int] = {}
    for site in checked.sites:
        site_counts[site.category] = site_counts.get(site.category, 0) + 1
    for placement in checked.placements:
        category = asset_by_id[placement.asset_id].category
        placement_counts[category] = placement_counts.get(category, 0) + 1
    return {
        "collision": {
            "collision_endcaps_enabled": False,
            "collision_component_count": sum(
                len(asset.collision_components)
                for asset in checked.assets
            ),
            "collision_profiles": dict(sorted(
                (
                    profile,
                    sum(
                        asset.collision_profile == profile
                        for asset in checked.assets
                    ),
                )
                for profile in {
                    asset.collision_profile
                    for asset in checked.assets
                }
            )),
            "directive": OPEN_ENDCAP_DIRECTIVE,
            "generated_road_collision_proxy_overlap_count": 0,
            "minimum_generated_road_to_collision_proxy_clearance_m":
                _stable_number(minimum_route_collision_clearance),
            "minimum_generated_road_to_collision_proxy_pair":
                list(minimum_route_collision_pair),
            "minimum_collision_proxy_clearance_m":
                _stable_number(collision_clearance),
            "minimum_collision_proxy_pair": list(collision_pair),
            "single_surface_at_access_seams": True,
        },
        "connectors": {
            "active": sum(
                connector.status == "active"
                for connector in checked.connectors
            ),
            "contracts": connector_evidence,
            "format": CONNECTOR_FORMAT,
            "maximum_allowed_seam_gap_m":
                MAXIMUM_CONNECTOR_SEAM_GAP_M,
            "maximum_observed_active_seam_gap_m": max(
                evidence["seam_gap_m"]
                for evidence in connector_evidence
                if evidence["status"] == "active"
            ),
            "non_designated_route_asset_intersection_count": 0,
            "pending": sum(
                connector.status == "pending"
                for connector in checked.connectors
            ),
        },
        "format": SOURCE_AUDIT_FORMAT,
        "geometry": {
            "access_road_clearance_scope":
                "non-designated-route-asset-pairs",
            "minimum_access_road_to_render_footprint_clearance_m":
                _stable_number(minimum_route_clearance),
            "minimum_access_road_to_render_footprint_pair":
                list(minimum_route_pair),
            "minimum_render_footprint_clearance_m":
                _stable_number(render_clearance),
            "minimum_render_footprint_pair": list(render_pair),
            "placement_footprints_inside_parcels": True,
            "render_footprints_overlap_count": 0,
            "collision_proxies_overlap_count": 0,
        },
        "provenance": {
            "archive": {
                "name": PINNED_ARCHIVE_NAME,
                "sha256": PINNED_ARCHIVE_SHA256,
            },
            "source_tobj": {
                "member": PINNED_TOBJ_MEMBER,
                "sha256": PINNED_TOBJ_SHA256,
            },
            "source_anchor_count": len(checked.source_anchors),
        },
        "summary": {
            "access_routes": len(checked.routes),
            "assets": len(checked.assets),
            "placements": len(checked.placements),
            "placements_by_category": dict(sorted(placement_counts.items())),
            "sites": len(checked.sites),
            "sites_by_category": dict(sorted(site_counts.items())),
        },
    }


def _asset_record(asset: AssetContract) -> dict[str, Any]:
    minimum_x = min(
        component.collision_center_local_x_m
        - component.collision_width_m / 2.0
        for component in asset.collision_components
    )
    maximum_x = max(
        component.collision_center_local_x_m
        + component.collision_width_m / 2.0
        for component in asset.collision_components
    )
    minimum_z = min(
        component.collision_center_local_z_m
        - component.collision_depth_m / 2.0
        for component in asset.collision_components
    )
    maximum_z = max(
        component.collision_center_local_z_m
        + component.collision_depth_m / 2.0
        for component in asset.collision_components
    )
    return {
        "asset_id": asset.asset_id,
        "category": asset.category,
        "collision_components": [
            {
                "center_local_xz_m": [
                    component.collision_center_local_x_m,
                    component.collision_center_local_z_m,
                ],
                "component_id": component.component_id,
                "depth_m": component.collision_depth_m,
                "width_m": component.collision_width_m,
            }
            for component in asset.collision_components
        ],
        "collision_footprint": {
            "center_local_xz_m": [
                _stable_number((minimum_x + maximum_x) / 2.0),
                _stable_number((minimum_z + maximum_z) / 2.0),
            ],
            "depth_m": _stable_number(maximum_z - minimum_z),
            "semantics": "conservative-component-outer-bounds",
            "width_m": _stable_number(maximum_x - minimum_x),
        },
        "collision_profile": asset.collision_profile,
        "manifest": asset.manifest,
        "render_footprint_m": [
            asset.render_width_m,
            asset.render_depth_m,
        ],
    }


def _connector_record(
    connector: RouteAssetConnector,
) -> dict[str, Any]:
    record = {
        "connector_id": connector.connector_id,
        "expected_render_overlap_depth_m":
            connector.expected_render_overlap_depth_m,
        "maximum_seam_gap_m": connector.maximum_seam_gap_m,
        "placement_id": connector.placement_id,
        "route_edge": connector.route_edge,
        "route_id": connector.route_id,
        "route_segment_index": connector.route_segment_index,
        "status": connector.status,
        "surface_id": connector.surface_id,
        "target_seam_local_xz_m": [
            list(point) for point in connector.target_seam_local_xz_m
        ],
        "target_surface_local_polygon_xz_m": [
            list(point)
            for point in connector.target_surface_local_polygon_xz_m
        ],
        "target_surface_local_y_m":
            connector.target_surface_local_y_m,
    }
    if connector.blocker is not None:
        record["blocker"] = connector.blocker
    return record


def _site_record(site: InfillSite) -> dict[str, Any]:
    return {
        "access_route_ids": list(site.access_route_ids),
        "category": site.category,
        "center_xz_m": list(site.center_xz_m),
        "display_name": site.display_name,
        "infrastructure_clearance_m": {
            name: value for name, value in site.infrastructure_clearance_m
        },
        "polygon_xz_m": [list(point) for point in site.polygon_xz_m],
        "site_id": site.site_id,
    }


def _anchor_record(anchor: SourceAnchor) -> dict[str, Any]:
    record: dict[str, Any] = {
        "anchor_id": anchor.anchor_id,
        "authority": anchor.authority,
        "extraction_method": anchor.extraction_method,
        "heading_degrees": anchor.heading_degrees,
        "position_m": list(anchor.position_m),
        "source_archive_sha256": anchor.source_archive_sha256,
        "source_contract": anchor.source_contract,
        "source_contract_format": anchor.source_contract_format,
        "source_tobj_member": anchor.source_tobj_member,
        "source_tobj_sha256": anchor.source_tobj_sha256,
    }
    optional = {
        "collision_member": anchor.collision_member,
        "collision_sha256": anchor.collision_sha256,
        "placement_line": anchor.placement_line,
        "placement_object": anchor.placement_object,
        "placement_position_m": (
            list(anchor.placement_position_m)
            if anchor.placement_position_m is not None
            else None
        ),
        "placement_rotation_degrees": (
            list(anchor.placement_rotation_degrees)
            if anchor.placement_rotation_degrees is not None
            else None
        ),
        "outer_wall_point_m": (
            list(anchor.outer_wall_point_m)
            if anchor.outer_wall_point_m is not None
            else None
        ),
        "surface_interval_x_m": (
            list(anchor.surface_interval_x_m)
            if anchor.surface_interval_x_m is not None
            else None
        ),
        "surface_seam_point_m": (
            list(anchor.surface_seam_point_m)
            if anchor.surface_seam_point_m is not None
            else None
        ),
        "upstream_route_id": anchor.upstream_route_id,
    }
    record.update(
        (key, value) for key, value in optional.items() if value is not None
    )
    return record


def _route_record(route: AccessRoute) -> dict[str, Any]:
    return {
        "collision": {
            "enabled": route.collision_enabled,
            "endcaps_enabled": route.collision_endcaps_enabled,
            "endcap_directive": OPEN_ENDCAP_DIRECTIVE,
            "single_surface_at_source_seam": True,
        },
        "comments": list(route.comments),
        "destination_position_m": list(route.destination_position_m),
        "destination_site_id": route.destination_site_id,
        "points": [
            {
                "border_height_m": point.border_height_m,
                "border_width_m": point.border_width_m,
                "position_m": [point.x, point.y, point.z],
                "road_type": point.road_type,
                "station_m": point.station_m,
                "width_m": point.width_m,
                "yaw_degrees": point.yaw_degrees,
            }
            for point in route.points
        ],
        "route_id": route.route_id,
        "served_site_ids": list(route.served_site_ids),
        "smoothing_num_splits": route.smoothing_num_splits,
        "source_anchor_id": route.source_anchor_id,
        "surface_material": route.surface_material,
    }


def _placement_record(placement: InfillPlacement) -> dict[str, Any]:
    asset = ASSET_BY_ID[placement.asset_id]
    return {
        "asset_id": placement.asset_id,
        "collision_component_footprints_xz_m": [
            {
                "component_id": component.component_id,
                "polygon_xz_m": [
                    list(point)
                    for point in collision_component_footprint(
                        placement,
                        component,
                    )
                ],
            }
            for component in asset.collision_components
        ],
        "collision_footprint_xz_m": [
            list(point)
            for point in placement_footprint(placement, collision=True)
        ],
        "collision_footprint_semantics":
            "conservative-component-outer-bounds",
        "collision_profile": asset.collision_profile,
        "instance_name": placement.instance_name,
        "placement_id": placement.placement_id,
        "position_m": [placement.x, placement.y, placement.z],
        "render_footprint_xz_m": [
            list(point) for point in placement_footprint(placement)
        ],
        "rotation_degrees": [0.0, placement.yaw_degrees, 0.0],
        "site_id": placement.site_id,
    }


def build_manifest(plan: InfillPlan | None = None) -> dict[str, Any]:
    """Build the versioned, JSON-ready deterministic episode-independent plan."""

    checked = build_infill_plan() if plan is None else plan
    return {
        "access_routes": [_route_record(route) for route in checked.routes],
        "assets": [_asset_record(asset) for asset in checked.assets],
        "audit": audit_plan(checked),
        "connectors": {
            "collision_policy":
                "no-generated-road-overlap-with-building-proxies",
            "contracts": [
                _connector_record(connector)
                for connector in checked.connectors
            ],
            "format": CONNECTOR_FORMAT,
            "maximum_seam_gap_m": MAXIMUM_CONNECTOR_SEAM_GAP_M,
        },
        "format": FORMAT,
        "placements": [
            _placement_record(placement) for placement in checked.placements
        ],
        "provenance": {
            "external_geometry_copied": False,
            "plan_generator": "tools/cityworld_infill.py",
            "project_authored_assets_only": True,
            "source_archive": {
                "name": PINNED_ARCHIVE_NAME,
                "redistributed": False,
                "sha256": PINNED_ARCHIVE_SHA256,
            },
            "source_audit_format": SOURCE_AUDIT_FORMAT,
            "source_tobj": {
                "member": PINNED_TOBJ_MEMBER,
                "sha256": PINNED_TOBJ_SHA256,
            },
        },
        "sites": [_site_record(site) for site in checked.sites],
        "source_anchors": [
            _anchor_record(anchor) for anchor in checked.source_anchors
        ],
        "version": VERSION,
    }


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


def canonical_manifest_bytes(plan: InfillPlan | None = None) -> bytes:
    return canonical_json_bytes(build_manifest(plan))


def canonical_manifest_sha256(plan: InfillPlan | None = None) -> str:
    return hashlib.sha256(canonical_manifest_bytes(plan)).hexdigest()


def placement_ids(plan: InfillPlan | None = None) -> tuple[str, ...]:
    checked = build_infill_plan() if plan is None else plan
    return tuple(placement.placement_id for placement in checked.placements)


def finite_values(values: Iterable[float]) -> bool:
    """Small public helper used by downstream package gates."""

    return all(math.isfinite(float(value)) for value in values)
