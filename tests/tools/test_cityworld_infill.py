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

    def test_render_and_collision_footprints_fit_without_overlap(self) -> None:
        sites = {site.site_id: site for site in self.plan.sites}
        for placement in self.plan.placements:
            with self.subTest(placement=placement.placement_id):
                site = sites[placement.site_id]
                for collision in (False, True):
                    footprint = INFILL.placement_footprint(
                        placement,
                        collision=collision,
                    )
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
                        INFILL.polygons_overlap(
                            INFILL.placement_footprint(
                                first,
                                collision=True,
                            ),
                            INFILL.placement_footprint(
                                second,
                                collision=True,
                            ),
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
            38.0,
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
                for placement in self.plan.placements:
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
            13.0,
        )
        self.assertEqual(
            report["collision"],
            {
                "collision_endcaps_enabled": False,
                "directive": "collision_endcaps_enabled false",
                "minimum_collision_proxy_clearance_m": 38.0,
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
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(
            INFILL.canonical_manifest_sha256(),
            "a77ad7fe20acd87a580d80b4e491cd43f0f77d69a32f12984d9760264f853fef",
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


if __name__ == "__main__":
    unittest.main()
