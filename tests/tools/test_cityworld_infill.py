#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from dataclasses import replace
import importlib.util
import json
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/cityworld_infill.py"
SPEC = importlib.util.spec_from_file_location("cityworld_infill_test", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld regional-infill contract")
INFILL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = INFILL
SPEC.loader.exec_module(INFILL)


EXPECTED_ROUTE_IDS = (
    "west-farm-spine",
    "sunset-frontage-road",
    "coyote-arch-turnout",
    "arroyo-vista-boulevard",
    "intercity-service-road",
    "intercity-farm-road",
    "sagebrush-arroyo-trail",
)
EXPECTED_CONNECTOR_IDS = (
    "sunset-frontage-west-service-forecourt",
    "sunset-frontage-sunset-courts-internal-street",
    "arroyo-vista-internal-street",
    "intercity-service-forecourt",
    "intercity-farm-lane",
)
EXPECTED_PLACEMENT_IDS = (
    "west-farm-belt-farmstead-01",
    "west-farm-belt-farmstead-02",
    "west-farm-belt-farmstead-03",
    "west-farm-belt-farmstead-04",
    "west-farm-belt-farmstead-05",
    "west-farm-belt-farmstead-06",
    "west-farm-belt-farmstead-07",
    "intercity-farm-farmstead-01",
    "intercity-farm-farmstead-02",
    "intercity-farm-farmstead-03",
    "intercity-farm-farmstead-04",
    "intercity-farm-farmstead-05",
    "intercity-farm-farmstead-06",
    "sunset-courts-suburb-block-01",
    "sunset-courts-suburb-block-02",
    "sunset-courts-suburb-block-03",
    "sunset-courts-suburb-block-04",
    "sunset-courts-suburb-block-05",
    "sunset-courts-suburb-block-06",
    "sunset-courts-suburb-block-07",
    "sunset-courts-suburb-block-08",
    "sunset-courts-suburb-block-09",
    "arroyo-vista-suburb-block-01",
    "arroyo-vista-suburb-block-02",
    "arroyo-vista-suburb-block-03",
    "arroyo-vista-suburb-block-04",
    "arroyo-vista-suburb-block-05",
    "arroyo-vista-suburb-block-06",
    "arroyo-vista-suburb-block-07",
    "arroyo-vista-suburb-block-08",
    "west-highway-service-station-01",
    "intercity-service-station-01",
    "coyote-arch-red-mesa-01",
    "coyote-arch-red-mesa-02",
    "coyote-arch-red-mesa-03",
    "coyote-arch-red-mesa-04",
    "coyote-arch-red-mesa-05",
    "coyote-arch-red-mesa-06",
    "coyote-arch-red-mesa-07",
    "sagebrush-arroyo-oasis-01",
    "sagebrush-arroyo-oasis-02",
    "sagebrush-arroyo-oasis-03",
    "sagebrush-arroyo-oasis-04",
    "sagebrush-arroyo-oasis-05",
    "sagebrush-arroyo-oasis-06",
    "sagebrush-arroyo-oasis-07",
)
EXPECTED_HIGHWAY_SEAMS = {
    "legacy.autopista_qr.l0378.surface_edge.east_z1425": {
        "outer_wall": (708.967, -0.4, 1425.0),
        "seam": (706.967, 0.1, 1425.0),
        "surface_interval_x_m": (682.967, 706.967),
    },
    "legacy.autopista_qr.l0378.surface_edge.west_z2500": {
        "outer_wall": (4262.970, -0.4, 2500.0),
        "seam": (4264.970, 0.1, 2500.0),
        "surface_interval_x_m": (4264.970, 4288.970),
    },
    "legacy.autopista_qr.l0378.surface_edge.west_z3500": {
        "outer_wall": (4730.970, -0.4, 3500.0),
        "seam": (4732.970, 0.1, 3500.0),
        "surface_interval_x_m": (4732.970, 4756.970),
    },
    "legacy.autopista_qr.l0378.surface_edge.east_z3580": {
        "outer_wall": (3690.970, -0.4, 3580.0),
        "seam": (3688.970, 0.1, 3580.0),
        "surface_interval_x_m": (3664.970, 3688.970),
    },
    "legacy.autopista_qr.l0378.surface_edge.east_curve_z4350": {
        "outer_wall": (3725.335203, -0.4, 4350.0),
        "seam": (3723.199038, 0.1, 4350.0),
        "surface_interval_x_m": (3697.534001, 3723.199038),
    },
}


class CityWorldInfillTests(unittest.TestCase):
    def setUp(self) -> None:
        self.plan = INFILL.build_infill_plan()

    def test_stable_district_route_and_placement_schedule(self) -> None:
        report = INFILL.audit_plan(self.plan)
        self.assertEqual(report["format"], INFILL.SOURCE_AUDIT_FORMAT)
        self.assertEqual(
            report["summary"],
            {
                "access_routes": 7,
                "assets": 5,
                "placements": 46,
                "placements_by_category": {
                    "farmland": 13,
                    "natural-landmark": 14,
                    "service-station": 2,
                    "suburb": 17,
                },
                "sites": 8,
                "sites_by_category": {
                    "farmland": 2,
                    "natural-landmark": 2,
                    "service-station": 2,
                    "suburb": 2,
                },
            },
        )
        self.assertEqual(
            tuple(route.route_id for route in self.plan.routes),
            EXPECTED_ROUTE_IDS,
        )
        self.assertEqual(
            INFILL.placement_ids(self.plan),
            EXPECTED_PLACEMENT_IDS,
        )
        self.assertEqual(
            tuple(asset.asset_id for asset in self.plan.assets),
            (
                INFILL.FARMSTEAD_ASSET_ID,
                INFILL.SUBURB_ASSET_ID,
                INFILL.SERVICE_STATION_ASSET_ID,
                INFILL.RED_MESA_ASSET_ID,
                INFILL.ARROYO_OASIS_ASSET_ID,
            ),
        )
        self.assertEqual(
            tuple(
                connector.connector_id
                for connector in self.plan.connectors
            ),
            EXPECTED_CONNECTOR_IDS,
        )
        self.assertEqual(
            [connector.status for connector in self.plan.connectors],
            ["active", "active", "active", "active", "active"],
        )

    def test_connector_route_topology_and_corrected_collision_basis(self) -> None:
        routes = {route.route_id: route for route in self.plan.routes}
        placements = {
            placement.placement_id: placement
            for placement in self.plan.placements
        }
        self.assertEqual(
            tuple(
                (point.x, point.z)
                for point in routes["sunset-frontage-road"].points
            ),
            (
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
        )
        self.assertEqual(
            tuple(
                (point.x, point.z)
                for point in routes["arroyo-vista-boulevard"].points
            ),
            (
                (4732.97, 3500.0),
                (4630.0, 3500.0),
                (4550.0, 3500.0),
                (4440.0, 3500.0),
                (4240.0, 3500.0),
                (4140.0, 3500.0),
                (4140.0, 3425.0),
                (4140.0, 3402.0),
            ),
        )
        self.assertEqual(
            tuple(
                (point.x, point.z)
                for point in routes["intercity-service-road"].points
            ),
            (
                (3688.97, 3580.0),
                (3720.0, 3580.0),
                (3740.0, 3580.0),
            ),
        )
        self.assertEqual(
            tuple(
                (point.x, point.z)
                for point in routes["intercity-farm-road"].points
            ),
            (
                (3723.199038, 4350.0),
                (3800.0, 4350.0),
                (3860.0, 4350.0),
                (3880.0, 4350.0),
                (3880.0, 4285.0),
                (3916.0, 4285.0),
            ),
        )
        self.assertEqual(
            (
                placements["west-highway-service-station-01"].x,
                placements["west-highway-service-station-01"].z,
            ),
            (805.0, 1460.0),
        )
        self.assertEqual(
            (
                placements["intercity-service-station-01"].x,
                placements["intercity-service-station-01"].z,
            ),
            (3785.0, 3580.0),
        )
        assets = {asset.asset_id: asset for asset in self.plan.assets}
        self.assertEqual(
            {
                asset_id: asset.collision_profile
                for asset_id, asset in assets.items()
            },
            {
                INFILL.FARMSTEAD_ASSET_ID:
                    "single-watertight-proxy-v1",
                INFILL.SUBURB_ASSET_ID:
                    "compound-watertight-proxy-v1",
                INFILL.SERVICE_STATION_ASSET_ID:
                    "compound-watertight-proxy-v1",
                INFILL.RED_MESA_ASSET_ID:
                    "single-watertight-proxy-v1",
                INFILL.ARROYO_OASIS_ASSET_ID:
                    "single-watertight-proxy-v1",
            },
        )
        self.assertEqual(
            {
                INFILL.FARMSTEAD_ASSET_ID:
                    assets[INFILL.FARMSTEAD_ASSET_ID]
                    .collision_components[0]
                    .collision_center_local_z_m,
                INFILL.SERVICE_STATION_ASSET_ID:
                    assets[INFILL.SERVICE_STATION_ASSET_ID]
                    .collision_components[0]
                    .collision_center_local_z_m,
                INFILL.RED_MESA_ASSET_ID:
                    assets[INFILL.RED_MESA_ASSET_ID]
                    .collision_components[0]
                    .collision_center_local_z_m,
                INFILL.ARROYO_OASIS_ASSET_ID:
                    assets[INFILL.ARROYO_OASIS_ASSET_ID]
                    .collision_components[0]
                    .collision_center_local_z_m,
            },
            {
                INFILL.FARMSTEAD_ASSET_ID: -26.0,
                INFILL.SERVICE_STATION_ASSET_ID: -22.0,
                INFILL.RED_MESA_ASSET_ID: -0.8,
                INFILL.ARROYO_OASIS_ASSET_ID: 3.2,
            },
        )
        suburb_components = assets[
            INFILL.SUBURB_ASSET_ID
        ].collision_components
        self.assertEqual(len(suburb_components), 8)
        self.assertEqual(
            {
                (
                    component.collision_center_local_x_m,
                    component.collision_center_local_z_m,
                    component.collision_width_m,
                    component.collision_depth_m,
                )
                for component in suburb_components
            },
            {
                (-27.0, 24.0, 24.0, 17.0),
                (27.0, 24.0, 24.0, 17.0),
                (-27.0, 0.0, 24.0, 17.0),
                (27.0, 0.0, 24.0, 17.0),
                (-27.0, -24.0, 24.0, 17.0),
                (27.0, -24.0, 24.0, 17.0),
                (-47.0, 0.0, 1.5, 84.0),
                (47.0, 0.0, 1.5, 84.0),
            },
        )
        station_components = {
            component.component_id: component
            for component in assets[
                INFILL.SERVICE_STATION_ASSET_ID
            ].collision_components
        }
        self.assertEqual(len(station_components), 17)
        self.assertEqual(
            {
                component_id: (
                    station_components[component_id]
                    .collision_center_local_x_m,
                    station_components[component_id]
                    .collision_center_local_z_m,
                )
                for component_id in (
                    "market",
                    "canopy",
                    "price-pylon",
                    "ev-charger-03",
                )
            },
            {
                "market": (0.0, -22.0),
                "canopy": (3.0, 4.0),
                "price-pylon": (-38.0, 24.0),
                "ev-charger-03": (37.6, -24.0),
            },
        )

    def test_render_and_collision_footprints_fit_without_overlap(self) -> None:
        sites = {site.site_id: site for site in self.plan.sites}
        for placement in self.plan.placements:
            with self.subTest(placement=placement.placement_id):
                site = sites[placement.site_id]
                footprints = (
                    INFILL.placement_footprint(placement),
                    *INFILL.placement_collision_footprints(placement),
                )
                for footprint in footprints:
                    self.assertTrue(
                        all(
                            INFILL.point_in_polygon(
                                point,
                                site.polygon_xz_m,
                            )
                            for point in footprint
                        )
                    )

        for index, first in enumerate(self.plan.placements):
            for second in self.plan.placements[index + 1:]:
                with self.subTest(
                    first=first.placement_id,
                    second=second.placement_id,
                ):
                    self.assertFalse(
                        INFILL.polygons_overlap(
                            INFILL.placement_footprint(first),
                            INFILL.placement_footprint(second),
                        )
                    )
                    self.assertFalse(
                        any(
                            INFILL.polygons_overlap(
                                first_component,
                                second_component,
                            )
                            for first_component in
                            INFILL.placement_collision_footprints(first)
                            for second_component in
                            INFILL.placement_collision_footprints(second)
                        )
                    )

        geometry = INFILL.audit_plan(self.plan)["geometry"]
        self.assertEqual(
            geometry["minimum_render_footprint_clearance_m"],
            9.0,
        )
        self.assertGreaterEqual(
            geometry["minimum_render_footprint_clearance_m"],
            INFILL.MINIMUM_PLACEMENT_GAP_M,
        )
        self.assertEqual(
            INFILL.audit_plan(self.plan)["collision"][
                "minimum_collision_proxy_clearance_m"
            ],
            36.0,
        )

    def test_authenticated_surface_seams_are_route_endpoints(self) -> None:
        anchors = {
            anchor.anchor_id: anchor for anchor in self.plan.source_anchors
        }
        routes = {route.route_id: route for route in self.plan.routes}
        sites = {site.site_id: site for site in self.plan.sites}
        for anchor_id, expected in EXPECTED_HIGHWAY_SEAMS.items():
            with self.subTest(anchor=anchor_id):
                anchor = anchors[anchor_id]
                self.assertEqual(anchor.position_m, expected["seam"])
                self.assertEqual(
                    anchor.surface_seam_point_m,
                    expected["seam"],
                )
                self.assertEqual(
                    anchor.outer_wall_point_m,
                    expected["outer_wall"],
                )
                self.assertEqual(
                    anchor.position_m[1] - anchor.outer_wall_point_m[1],
                    0.5,
                )
                self.assertGreaterEqual(
                    abs(
                        anchor.position_m[0]
                        - anchor.outer_wall_point_m[0]
                    ),
                    2.0,
                )
                self.assertEqual(
                    anchor.surface_interval_x_m,
                    expected["surface_interval_x_m"],
                )
                self.assertEqual(
                    anchor.placement_line,
                    INFILL.PINNED_HIGHWAY_PLACEMENT_LINE,
                )
                self.assertEqual(
                    anchor.placement_object,
                    INFILL.PINNED_HIGHWAY_OBJECT,
                )
                self.assertEqual(
                    anchor.collision_sha256,
                    INFILL.PINNED_HIGHWAY_COLLISION_SHA256,
                )

        for route in self.plan.routes:
            with self.subTest(route=route.route_id):
                anchor = anchors[route.source_anchor_id]
                first = route.points[0]
                last = route.points[-1]
                self.assertEqual(
                    (first.x, first.y, first.z),
                    anchor.position_m,
                )
                self.assertEqual(
                    (last.x, last.y, last.z),
                    route.destination_position_m,
                )
                self.assertTrue(
                    INFILL.point_in_polygon(
                        (last.x, last.z),
                        sites[route.destination_site_id].polygon_xz_m,
                    )
                )

        west = anchors[
            "legacy.troadavenuesidewalk.l1354."
            "replacement_surface_edge.south"
        ]
        self.assertEqual(west.position_m, (500.0, 0.100001, 365.14801))
        self.assertLess(west.position_m[0], 510.0)
        self.assertEqual(west.placement_line, 1354)
        self.assertEqual(west.placement_object, "troadavenuesidewalk")
        self.assertEqual(
            west.collision_member,
            "crossroadavenuesidewalkbox.mesh",
        )
        self.assertEqual(
            west.collision_sha256,
            "1ea12c27545bce3e84bf0b5fa49def6bd2083f8c2f813dc14312c1a185823b7e",
        )

        generated = anchors["west-farm-generated-east-edge-300"]
        upstream = routes[generated.upstream_route_id]
        self.assertEqual(
            (
                upstream.points[-1].x,
                upstream.points[-1].y,
                upstream.points[-1].z,
            ),
            generated.position_m,
        )

    def test_access_roads_are_flat_open_and_clear_every_asset(self) -> None:
        active_connector_pairs = {
            (connector.route_id, connector.placement_id)
            for connector in self.plan.connectors
            if connector.status == "active"
        }
        for route in self.plan.routes:
            with self.subTest(route=route.route_id):
                self.assertTrue(route.collision_enabled)
                self.assertFalse(route.collision_endcaps_enabled)
                self.assertEqual(route.smoothing_num_splits, 0)
                self.assertEqual(route.surface_material, "road2")
                self.assertTrue(
                    all(point.road_type == "flat" for point in route.points)
                )
                self.assertTrue(
                    all(
                        point.border_width_m == 0.0
                        and point.border_height_m == 0.0
                        for point in route.points
                    )
                )
                self.assertEqual(
                    {point.y for point in route.points},
                    {route.points[0].y},
                )
                self.assertTrue(
                    all(
                        second.station_m > first.station_m
                        for first, second in zip(
                            route.points,
                            route.points[1:],
                        )
                    )
                )
                self.assertTrue(
                    all(
                        INFILL.polygon_is_strictly_convex(
                            INFILL.route_segment_surface_footprint(
                                route,
                                segment_index,
                            )
                        )
                        for segment_index in range(
                            len(route.points) - 1
                        )
                    )
                )
                for placement in self.plan.placements:
                    INFILL.route_collision_component_clearance_m(
                        route,
                        placement,
                    )
                    if (
                        route.route_id,
                        placement.placement_id,
                    ) in active_connector_pairs:
                        continue
                    self.assertGreaterEqual(
                        INFILL.route_placement_clearance_m(
                            route,
                            placement,
                        ),
                        INFILL.MINIMUM_ACCESS_ROAD_CLEARANCE_M,
                        (
                            f"{route.route_id} enters "
                            f"{placement.placement_id}"
                        ),
                    )

        report = INFILL.audit_plan(self.plan)
        self.assertEqual(
            report["geometry"][
                "minimum_access_road_to_render_footprint_clearance_m"
            ],
            23.0,
        )
        connector_report = report["connectors"]
        self.assertEqual(
            {
                key: connector_report[key]
                for key in (
                    "active",
                    "format",
                    "maximum_allowed_seam_gap_m",
                    "maximum_observed_active_seam_gap_m",
                    "non_designated_route_asset_intersection_count",
                    "pending",
                )
            },
            {
                "active": 5,
                "format": INFILL.CONNECTOR_FORMAT,
                "maximum_allowed_seam_gap_m": 0.001,
                "maximum_observed_active_seam_gap_m": 0.0,
                "non_designated_route_asset_intersection_count": 0,
                "pending": 0,
            },
        )
        connector_evidence = {
            contract["connector_id"]: contract
            for contract in connector_report["contracts"]
        }
        self.assertEqual(
            tuple(connector_evidence),
            EXPECTED_CONNECTOR_IDS,
        )
        for connector_id in EXPECTED_CONNECTOR_IDS:
            with self.subTest(connector=connector_id):
                evidence = connector_evidence[connector_id]
                self.assertEqual(evidence["status"], "active")
                self.assertLessEqual(evidence["seam_gap_m"], 0.001)
                self.assertEqual(
                    evidence["collision_proxy_overlap_count"],
                    0,
                )
        farm = connector_evidence["intercity-farm-lane"]
        self.assertEqual(
            farm["route_world_seam_xz_m"],
            [[3916.0, 4289.0], [3916.0, 4281.0]],
        )
        self.assertEqual(farm["target_world_seam_xz_m"], [
            [3916.0, 4289.0],
            [3916.0, 4281.0],
        ])
        self.assertEqual(farm["road_width_m"], 8.0)
        self.assertEqual(
            farm["minimum_collision_proxy_clearance_m"],
            49.197256021,
        )
        self.assertEqual(
            report["collision"],
            {
                "collision_endcaps_enabled": False,
                "collision_component_count": 28,
                "collision_profiles": {
                    "compound-watertight-proxy-v1": 2,
                    "single-watertight-proxy-v1": 3,
                },
                "directive": "collision_endcaps_enabled false",
                "generated_road_collision_proxy_overlap_count": 0,
                "minimum_generated_road_to_collision_proxy_clearance_m":
                    5.4,
                "minimum_generated_road_to_collision_proxy_pair": [
                    "sunset-frontage-road",
                    "west-highway-service-station-01",
                ],
                "minimum_collision_proxy_clearance_m": 36.0,
                "minimum_collision_proxy_pair": [
                    "sunset-courts-suburb-block-01",
                    "sunset-courts-suburb-block-04",
                ],
                "single_surface_at_access_seams": True,
            },
        )

    def test_manifest_is_canonical_and_reproducible(self) -> None:
        first = INFILL.canonical_manifest_bytes()
        second = INFILL.canonical_manifest_bytes(
            INFILL.build_infill_plan()
        )
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        decoded = json.loads(first)
        self.assertEqual(decoded, INFILL.build_manifest())
        self.assertEqual(decoded["format"], INFILL.FORMAT)
        self.assertEqual(decoded["version"], INFILL.VERSION)
        self.assertEqual(
            {
                key: decoded["connectors"][key]
                for key in (
                    "collision_policy",
                    "format",
                    "maximum_seam_gap_m",
                )
            },
            {
                "collision_policy":
                    "no-generated-road-overlap-with-building-proxies",
                "format": INFILL.CONNECTOR_FORMAT,
                "maximum_seam_gap_m": 0.001,
            },
        )
        self.assertEqual(
            [
                contract["status"]
                for contract in decoded["connectors"]["contracts"]
            ],
            ["active", "active", "active", "active", "active"],
        )
        manifest_assets = {
            asset["asset_id"]: asset
            for asset in decoded["assets"]
        }
        self.assertEqual(
            manifest_assets[INFILL.SUBURB_ASSET_ID][
                "collision_profile"
            ],
            "compound-watertight-proxy-v1",
        )
        self.assertEqual(
            len(
                manifest_assets[INFILL.SUBURB_ASSET_ID][
                    "collision_components"
                ]
            ),
            8,
        )
        self.assertTrue(
            all(
                placement["collision_footprint_semantics"]
                == "conservative-component-outer-bounds"
                and placement["collision_component_footprints_xz_m"]
                for placement in decoded["placements"]
            )
        )
        self.assertEqual(
            INFILL.canonical_manifest_sha256(),
            "735fca0fd917763cdfe02d8d3cbd7871ebd7f4feb3ccf4fc73771de9c9c0c0af",
        )
        self.assertEqual(
            decoded["provenance"]["source_archive"],
            {
                "name": "CityWorld.zip",
                "redistributed": False,
                "sha256": INFILL.PINNED_ARCHIVE_SHA256,
            },
        )
        self.assertFalse(decoded["provenance"]["external_geometry_copied"])
        self.assertTrue(
            decoded["provenance"]["project_authored_assets_only"]
        )

    def test_unsafe_geometry_and_collision_drift_fail_closed(self) -> None:
        unsafe_route = replace(
            self.plan.routes[0],
            collision_endcaps_enabled=True,
        )
        unsafe_plan = replace(
            self.plan,
            routes=(unsafe_route, *self.plan.routes[1:]),
        )
        with self.assertRaisesRegex(INFILL.InfillFailure, "profile drifted"):
            INFILL.audit_plan(unsafe_plan)

        outside = replace(self.plan.placements[0], x=0.0, z=0.0)
        outside_plan = replace(
            self.plan,
            placements=(outside, *self.plan.placements[1:]),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "footprint left",
        ):
            INFILL.audit_plan(outside_plan)

        moved_farm_component = replace(
            self.plan.assets[0].collision_components[0],
            collision_center_local_x_m=0.0,
            collision_center_local_z_m=0.0,
        )
        moved_farm_asset = replace(
            self.plan.assets[0],
            collision_components=(moved_farm_component,),
        )
        moved_asset_plan = replace(
            self.plan,
            assets=(moved_farm_asset, *self.plan.assets[1:]),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "asset contract drifted",
        ):
            INFILL.audit_plan(moved_asset_plan)

        station_index = next(
            index
            for index, placement in enumerate(self.plan.placements)
            if placement.placement_id
            == "west-highway-service-station-01"
        )
        shifted_station = replace(
            self.plan.placements[station_index],
            x=self.plan.placements[station_index].x + 0.002,
        )
        shifted_placements = list(self.plan.placements)
        shifted_placements[station_index] = shifted_station
        seam_gap_plan = replace(
            self.plan,
            placements=tuple(shifted_placements),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "seam gap exceeds one millimetre",
        ):
            INFILL.audit_plan(seam_gap_plan)

        farm_connector_index = next(
            index
            for index, connector in enumerate(self.plan.connectors)
            if connector.connector_id == "intercity-farm-lane"
        )
        blocked_farm = replace(
            self.plan.connectors[farm_connector_index],
            status="pending",
        )
        blocked_connectors = list(self.plan.connectors)
        blocked_connectors[farm_connector_index] = blocked_farm
        blocked_farm_plan = replace(
            self.plan,
            connectors=tuple(blocked_connectors),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "connector set drifted",
        ):
            INFILL.audit_plan(blocked_farm_plan)

        farm_route_index = next(
            index
            for index, route in enumerate(self.plan.routes)
            if route.route_id == "intercity-farm-road"
        )
        farm_route = self.plan.routes[farm_route_index]
        concave_farm_route = INFILL._build_route(
            route_id=farm_route.route_id,
            source_anchor_id=farm_route.source_anchor_id,
            destination_site_id=farm_route.destination_site_id,
            served_site_ids=farm_route.served_site_ids,
            xz_points=(
                (3723.199038, 4350.0),
                (3800.0, 4350.0),
                (3860.0, 4350.0),
                (3908.0, 4350.0),
                (3908.0, 4285.0),
                (3916.0, 4285.0),
            ),
            width_m=8.0,
            comments=farm_route.comments,
        )
        concave_farm_routes = list(self.plan.routes)
        concave_farm_routes[farm_route_index] = concave_farm_route
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "surface quad is not strictly convex",
        ):
            INFILL.audit_plan(
                replace(
                    self.plan,
                    routes=tuple(concave_farm_routes),
                )
            )

        unsafe_farm_route = INFILL._build_route(
            route_id=farm_route.route_id,
            source_anchor_id=farm_route.source_anchor_id,
            destination_site_id=farm_route.destination_site_id,
            served_site_ids=farm_route.served_site_ids,
            xz_points=(
                *((point.x, point.z) for point in farm_route.points[:-1]),
                (3916.002, 4285.0),
            ),
            width_m=8.0,
            comments=farm_route.comments,
        )
        unsafe_farm_routes = list(self.plan.routes)
        unsafe_farm_routes[farm_route_index] = unsafe_farm_route
        unsafe_farm_plan = replace(
            self.plan,
            routes=tuple(unsafe_farm_routes),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            "render overlap depth drifted",
        ):
            INFILL.audit_plan(unsafe_farm_plan)

        west_route_index = next(
            index
            for index, route in enumerate(self.plan.routes)
            if route.route_id == "west-farm-spine"
        )
        west_route = self.plan.routes[west_route_index]
        colliding_west_route = INFILL._build_route(
            route_id=west_route.route_id,
            source_anchor_id=west_route.source_anchor_id,
            destination_site_id=west_route.destination_site_id,
            served_site_ids=west_route.served_site_ids,
            xz_points=(
                (500.0, 365.14801),
                (500.0, 330.0),
                (668.0, 104.0),
                (1150.0, 300.0),
            ),
            width_m=8.0,
            comments=west_route.comments,
        )
        colliding_routes = list(self.plan.routes)
        colliding_routes[west_route_index] = colliding_west_route
        colliding_plan = replace(
            self.plan,
            routes=tuple(colliding_routes),
        )
        with self.assertRaisesRegex(
            INFILL.InfillFailure,
            (
                "overlaps building collision component "
                "west-farm-belt-farmstead-01/farmhouse"
            ),
        ):
            INFILL.audit_plan(colliding_plan)


if __name__ == "__main__":
    unittest.main()
