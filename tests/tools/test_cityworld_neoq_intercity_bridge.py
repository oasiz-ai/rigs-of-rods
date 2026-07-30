#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from dataclasses import dataclass, replace
import copy
import hashlib
import importlib.util
from pathlib import Path
import re
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT / "tools/cityworld_neoq_intercity_bridge.py"
)

SPEC = importlib.util.spec_from_file_location(
    "cityworld_neoq_intercity_bridge",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Neo intercity bridge tool")
BRIDGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BRIDGE
SPEC.loader.exec_module(BRIDGE)


@dataclass(frozen=True)
class Placement:
    line_number: int
    object_name: str
    position: tuple[float, float, float]
    rotation_degrees: tuple[float, float, float]


def exact_placements() -> tuple[Placement, ...]:
    source = BRIDGE.SOURCE_PLACEMENT
    destination = BRIDGE.DESTINATION_PLACEMENT
    ground_road = BRIDGE.GROUND_ROAD_PLACEMENT
    return (
        Placement(
            line_number=source["line_number"],
            object_name=source["object"],
            position=source["position_m"],
            rotation_degrees=source["rotation_degrees"],
        ),
        Placement(
            line_number=destination["line_number"],
            object_name=destination["object"],
            position=destination["position_m"],
            rotation_degrees=destination["rotation_degrees"],
        ),
        Placement(
            line_number=ground_road["line_number"],
            object_name=ground_road["object"],
            position=ground_road["position_m"],
            rotation_degrees=ground_road["rotation_degrees"],
        ),
        Placement(
            line_number=800,
            object_name="outside-swept-gap",
            position=(5000.0, 0.0, 4200.0),
            rotation_degrees=(0.0, 0.0, 0.0),
        ),
    )


class CityWorldNeoIntercityBridgeTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
    ) -> tuple[Path, tuple[dict[str, object], ...]]:
        archive_path = root / "CityWorld.zip"
        payloads = tuple(
            (
                record["name"],
                ("synthetic endpoint " + record["name"] + "\n").encode(),
                record["role"],
            )
            for record in BRIDGE.AUTHENTICATED_MEMBERS
        )
        with zipfile.ZipFile(
            archive_path,
            "w",
            compression=zipfile.ZIP_STORED,
        ) as archive:
            for name, payload, _ in payloads:
                archive.writestr(name, payload)
        contract = tuple(
            {
                "name": name,
                "role": role,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
            for name, payload, role in payloads
        )
        return archive_path, contract

    def test_route_merges_flush_at_both_city_road_seams(self) -> None:
        points, report = BRIDGE.build_route(surface_offset_m=0.08)
        self.assertEqual(report["format"], BRIDGE.FORMAT)
        self.assertEqual(
            (points[0].x, points[0].y, points[0].z),
            BRIDGE.SOURCE_SEAM,
        )
        self.assertEqual(
            (points[-1].x, points[-1].y, points[-1].z),
            BRIDGE.DESTINATION_SEAM,
        )
        self.assertEqual(len(points), 80)
        self.assertEqual(report["covered_centerline_length_m"], 3076.132100441)
        self.assertTrue(
            all(
                first.station_m < second.station_m
                and first.x < second.x
                for first, second in zip(points, points[1:])
            )
        )
        self.assertTrue(report["collision"]["continuous"])
        self.assertTrue(report["collision"]["single_surface"])
        self.assertFalse(
            report["collision"]["duplicate_authoritative_collision_surface"]
        )
        self.assertEqual(
            report["collision"]["authoritative_collision_surfaces_per_seam"],
            1,
        )
        self.assertEqual(
            report["collision"]["authority"],
            "native-procedural-road-v2-side-piers",
        )
        self.assertFalse(
            report["collision"]["endcap_collision_enabled"]
        )
        self.assertEqual(
            report["collision"]["endcap_collision_triangle_count"],
            0,
        )
        self.assertEqual(
            report["collision"]["endpoint_wheel_path_intrusion_m"],
            0.0,
        )
        self.assertTrue(report["profile"]["curb_free_approaches"])
        self.assertEqual(report["profile"]["width_m"], 24.0)
        self.assertEqual(
            report["profile"]["destination_merge_width_m"],
            15.1,
        )
        self.assertEqual(points[0].border_height_m, 0.0)
        self.assertEqual(points[-1].border_height_m, 0.0)
        self.assertEqual(points[-1].width_m, 15.1)
        self.assertEqual(points[-2].y, points[-1].y)
        self.assertEqual(points[-1].yaw_degrees, 0.0)
        self.assertLessEqual(
            report["profile"]["sampled_maximum_grade"],
            BRIDGE.MAXIMUM_GRADE,
        )
        self.assertEqual(
            report["connection"]["source_generated_overlap_m"],
            0.0,
        )
        self.assertEqual(
            report["connection"]["destination_generated_overlap_m"],
            0.0,
        )
        self.assertEqual(
            report["connection"]["destination_vertical_step_m"],
            0.0,
        )
        self.assertEqual(
            report["connection"][
                "destination_route_vs_decoded_surface_step_m"
            ],
            0.0,
        )
        self.assertEqual(
            report["connection"]["source_route_vs_decoded_surface_step_m"],
            0.0,
        )
        self.assertEqual(
            report["connection"]["destination_grade_discontinuity"],
            0.0,
        )
        self.assertEqual(
            report["connection"]["destination_heading_error_degrees"],
            0.0,
        )
        destination = report["destination"]
        self.assertTrue(destination["existing_lanes_preserved"])
        self.assertEqual(destination["generated_overlap_length_m"], 0.0)
        self.assertEqual(
            destination["open_carriageways_local_z_m"],
            [[-7.55, -0.7], [0.7, 7.55]],
        )
        self.assertEqual(destination["median_local_z_m"], [-0.7, 0.7])
        self.assertEqual(
            report["source"]["generated_overlap_length_m"],
            0.0,
        )
        self.assertEqual(
            destination["lane_handoff"]["world_position_m"],
            list(BRIDGE.DESTINATION_LANE_HANDOFF),
        )
        self.assertEqual(
            destination["elevation_authority"],
            {
                "authored_placement_origin_y_m": 50.0,
                "decoded_local_surface_y_m": 0.2,
                "route_surface_y_m": 0.2,
                "runtime_grounding_applied": True,
                "runtime_placement_origin_y_m": 0.0,
                "runtime_origin_plus_local_surface_y_m": 0.2,
                "route_vs_decoded_surface_step_m": 0.0,
            },
        )
        self.assertEqual(
            report["source"]["elevation_authority"],
            {
                "authored_placement_origin_y_m": 0.3,
                "decoded_local_surface_y_m": -0.2,
                "route_surface_y_m": 0.1,
                "runtime_grounding_applied": False,
                "runtime_placement_origin_y_m": 0.3,
                "runtime_origin_plus_local_surface_y_m": 0.1,
                "route_vs_decoded_surface_step_m": 0.0,
            },
        )
        self.assertEqual(report["supports"]["requested_count"], 56)
        self.assertLessEqual(
            report["supports"]["maximum_station_spacing_m"],
            BRIDGE.SAMPLE_SPACING_M,
        )
        self.assertEqual(report["supports"]["column_pair_count"], 56)
        self.assertEqual(report["supports"]["aabb_count"], 168)
        self.assertEqual(
            report["supports"]["aabb_vs_swept_roadway_prism"],
            "all-disjoint",
        )
        self.assertEqual(
            report["supports"][
                "minimum_lateral_clearance_from_deck_edge_m"
            ],
            2.5,
        )
        self.assertEqual(
            report["supports"][
                "minimum_vertical_clearance_below_collision_slab_m"
            ],
            0.05,
        )
        self.assertTrue(
            all(
                point.road_type
                in {"flat", "bridge_no_pillars", "bridge_side_pillars"}
                for point in points
            )
        )
        self.assertEqual(
            sum(
                point.road_type == "bridge_side_pillars"
                for point in points
            ),
            56,
        )
        self.assertEqual(
            report["supports"]["ground_road_no_pillar_stations_m"],
            list(BRIDGE.GROUND_ROAD_NO_PILLAR_STATIONS_M),
        )
        self.assertEqual(
            len(report["supports"]["ground_road_no_pillar_stations_m"]),
            18,
        )
        self.assertEqual(report["supports"]["stations_m"][0], 800.0)
        self.assertEqual(
            [
                point.station_m
                for point in points
                if point.station_m
                    in BRIDGE.GROUND_ROAD_NO_PILLAR_STATIONS_M
            ],
            list(BRIDGE.GROUND_ROAD_NO_PILLAR_STATIONS_M),
        )
        self.assertTrue(
            all(
                point.road_type == "bridge_no_pillars"
                for point in points
                if point.station_m
                    in BRIDGE.GROUND_ROAD_NO_PILLAR_STATIONS_M
            )
        )

    def test_route_and_fixture_schedule_are_deterministic(self) -> None:
        first_points, first_report = BRIDGE.build_route(surface_offset_m=0.08)
        second_points, second_report = BRIDGE.build_route(surface_offset_m=0.08)
        self.assertEqual(first_points, second_points)
        self.assertEqual(first_report, second_report)
        first_lights, first_lighting = BRIDGE.build_streetlights(first_points)
        second_lights, second_lighting = BRIDGE.build_streetlights(
            second_points
        )
        self.assertEqual(first_lights, second_lights)
        self.assertEqual(first_lighting, second_lighting)
        self.assertEqual(len(first_lights), 33)
        self.assertEqual(first_lighting["station_spacing_m"], 80.0)
        self.assertEqual(
            [light.side for light in first_lights],
            ["left", "right"] * 16 + ["left"],
        )
        self.assertEqual(
            len({light.instance_name for light in first_lights}),
            len(first_lights),
        )
        self.assertTrue(
            all(
                light.asset_id == BRIDGE.STREETLIGHT_ASSET_ID
                for light in first_lights
            )
        )

    def test_exact_endpoint_resources_and_placements_are_authenticated(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive, contract = self.make_archive(Path(directory))
            report = BRIDGE.authenticate_inputs(
                archive,
                exact_placements(),
                member_contract=contract,
            )
        self.assertEqual(report["format"], BRIDGE.AUTHENTICATION_FORMAT)
        self.assertEqual(report["source"]["line_number"], 366)
        self.assertEqual(report["destination"]["line_number"], 1230)
        self.assertEqual(report["ground_road"]["line_number"], 378)
        self.assertEqual(report["ground_road"]["object"], "autopistaQr")
        self.assertEqual(
            report["source"]["runtime_position_m"],
            [3676.970703, 0.3, 3993.104004],
        )
        self.assertEqual(
            report["destination"]["authored_position_m"],
            [7000.0, 50.0, 4018.0],
        )
        self.assertEqual(
            report["destination"]["runtime_position_m"],
            [7000.0, 0.0, 4018.0],
        )
        self.assertEqual(len(report["members"]), 8)
        self.assertTrue(report["open_gap"]["verified"])
        self.assertEqual(report["open_gap"]["placement_origin_count"], 0)

        _, route_report = BRIDGE.build_route(surface_offset_m=0.08)
        ground = BRIDGE.validate_ground_road_clearance(
            route_report,
            report,
        )
        self.assertEqual(ground["column_aabb_count"], 112)
        self.assertEqual(
            ground["clearance"],
            "all-column-aabbs-clear-of-authenticated-live-road-polygons",
        )
        self.assertTrue(ground["native_underside_visual_gate_required"])
        self.assertTrue(ground["legacy_mesh_world_bounds_available"])
        self.assertEqual(
            ground["ground_road"],
            {
                "collision_member": "autopistaQr.mesh",
                "collision_sha256":
                    BRIDGE.GROUND_ROAD_COLLISION_SHA256,
                "column_clearance_m": 2.5,
                "decoded_surface_materials": [
                    "calleunsolosentido",
                    "pavimento",
                ],
                "decoded_surface_triangle_count": 9599,
                "no_pillar_station_count": 18,
                "no_pillar_stations_m":
                    list(BRIDGE.GROUND_ROAD_NO_PILLAR_STATIONS_M),
                "placement_line": 378,
                "placement_object": "autopistaQr",
            },
        )

    def test_ground_road_clearance_rejects_authored_support_drift(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive, contract = self.make_archive(Path(directory))
            authentication = BRIDGE.authenticate_inputs(
                archive,
                exact_placements(),
                member_contract=contract,
            )
        _, report = BRIDGE.build_route(surface_offset_m=0.08)
        support_drift = copy.deepcopy(report)
        support_drift["supports"]["stations_m"].append(80.0)
        with self.assertRaisesRegex(
            BRIDGE.BridgeFailure,
            "enters the authenticated live ground road",
        ):
            BRIDGE.validate_ground_road_clearance(
                support_drift,
                authentication,
            )

        waypoint_drift = copy.deepcopy(report)
        waypoint = next(
            item
            for item in waypoint_drift["waypoints"]
            if item["station_m"] == 80.0
        )
        waypoint["road_type"] = "bridge_side_pillars"
        with self.assertRaisesRegex(
            BRIDGE.BridgeFailure,
            "span is not authored no-pillar",
        ):
            BRIDGE.validate_ground_road_clearance(
                waypoint_drift,
                authentication,
            )

    def test_ground_clearance_fails_closed_outside_authenticated_corridor(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive, contract = self.make_archive(Path(directory))
            authentication = BRIDGE.authenticate_inputs(
                archive,
                exact_placements(),
                member_contract=contract,
            )
        _, report = BRIDGE.build_route(surface_offset_m=0.08)
        first = report["supports"]["collision_aabbs"][0]
        first["aabb_world_m"][2] = (
            authentication["open_gap"]["bounds_xz_m"][2] - 1.0
        )
        with self.assertRaisesRegex(
            BRIDGE.BridgeFailure,
            "unauthenticated ground area",
        ):
            BRIDGE.validate_ground_road_clearance(
                report,
                authentication,
            )

    def test_placement_member_and_swept_gap_drift_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, contract = self.make_archive(root)
            placements = exact_placements()
            cases = (
                (
                    "source-position",
                    (
                        replace(
                            placements[0],
                            position=(3677.0, 0.3, 3993.104004),
                        ),
                        *placements[1:],
                    ),
                    contract,
                    "authenticated NeoQueretaro",
                ),
                (
                    "duplicate-source",
                    (*placements, placements[0]),
                    contract,
                    "authenticated NeoQueretaro",
                ),
                (
                    "swept-obstacle",
                    (
                        *placements,
                        Placement(
                            line_number=900,
                            object_name="new-building",
                            position=(5000.0, 0.0, 4000.0),
                            rotation_degrees=(0.0, 0.0, 0.0),
                        ),
                    ),
                    contract,
                    "gap is no longer empty",
                ),
                (
                    "member-hash",
                    placements,
                    (
                        {
                            **contract[0],
                            "sha256": "0" * 64,
                        },
                        *contract[1:],
                    ),
                    "authenticated bridge member changed",
                ),
            )
            for label, values, member_contract, message in cases:
                with (
                    self.subTest(label=label),
                    self.assertRaisesRegex(BRIDGE.BridgeFailure, message),
                ):
                    BRIDGE.authenticate_inputs(
                        archive,
                        values,
                        member_contract=member_contract,
                    )

    def test_invalid_surface_offsets_fail_without_partial_route(self) -> None:
        for value in (float("nan"), float("inf"), -2.1, 20.1, True):
            with (
                self.subTest(value=value),
                self.assertRaisesRegex(
                    BRIDGE.BridgeFailure,
                    "surface offset",
                ),
            ):
                BRIDGE.build_route(surface_offset_m=value)

    def test_native_visual_fixture_covers_both_joins_and_underside(self) -> None:
        fixture = (
            REPOSITORY_ROOT
            / "tests/fixtures/cityworld_neoq_bridge_runtime/"
            "cityworld_neoq_bridge_runtime.as"
        ).read_text(encoding="utf-8")
        for marker in (
            "const uint CAPTURE_COUNT = 6;",
            "const uint PASS_FRAME = 265;",
            '"sim_deterministic_fixed_steps_per_frame", "4"',
            '"ui_hide_gui", "true"',
            "vector3(3735.0f, 18.0f, 3955.0f)",
            "vector3(4300.0f, 10.2f, 3996.7f)",
            "vector3(4000.0f, 2.5f, 3960.0f)",
            "vector3(5250.0f, 2.5f, 3950.0f)",
            "vector3(5325.0f, 300.0f, 3650.0f)",
            "vector3(6902.0f, 1.15f, 4022.125f)",
            "vector3(6838.0f, 0.65f, 4022.125f)",
            "zero overlap and an open collision cap",
            "route_m=3076.132100441 supports=56 lights=33",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, fixture)

    def test_native_drive_fixture_enters_preserved_destination_lane(self) -> None:
        fixture = (
            REPOSITORY_ROOT
            / "tests/fixtures/cityworld_neoq_bridge_drive_runtime/"
            "cityworld_neoq_bridge_drive_runtime.as"
        ).read_text(encoding="utf-8")
        for marker in (
            "const float ROUTE_LENGTH_M = 3076.132100441f;",
            "const float PASS_DESTINATION_X_M = 6877.0f;",
            "const float DESTINATION_LANE_SAFE_MIN_LOCAL_Z_M = 1.95f;",
            "const float DESTINATION_LANE_SAFE_MAX_LOCAL_Z_M = 6.30f;",
            "gPath.length() != 82",
            "gStation[79] - DESTINATION_SEAM_STATION",
            "gStation[81] - ROUTE_LENGTH_M - 20.0f",
            "source_overlap_m=0",
            "destination_overlap_m=0",
            "destination-lane-footprint-",
            "reverse-destination-lane-footprint-",
            "destination_local_z_m=",
            "reverse_destination_local_z_m=",
            "REVERSE_DESTINATION_SEAM",
            "destination_traversals=2",
            "REVERSE_ACTOR_ID = 2026072903",
            "MSG_SIM_DELETE_ACTOR_REQUESTED",
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, fixture)
        path_block = fixture.split("array<vector3> gPath = {", 1)[1].split(
            "};",
            1,
        )[0]
        self.assertEqual(path_block.count("vector3("), 82)
        vectors = [
            tuple(float(value) for value in match)
            for match in re.findall(
                r"vector3\("
                r"(-?[0-9.]+)f, "
                r"(-?[0-9.]+)f, "
                r"(-?[0-9.]+)f\)",
                path_block,
            )
        ]
        points, _ = BRIDGE.build_route(surface_offset_m=0.08)
        self.assertEqual(len(vectors), len(points) + 2)
        for index, (vector, point) in enumerate(zip(vectors[:80], points)):
            with self.subTest(index=index):
                self.assertLessEqual(abs(vector[0] - point.x), 1.0e-9)
                self.assertLessEqual(abs(vector[1] - point.y), 1.0e-9)
                self.assertLessEqual(abs(vector[2] - point.z), 1.0e-9)
        self.assertEqual(
            vectors[80:],
            [
                (6877.0, 0.2, 4018.0),
                (6887.0, 0.2, 4018.0),
            ],
        )

    def test_native_side_pier_and_open_endcap_contracts_are_additive(
        self,
    ) -> None:
        parser = (
            REPOSITORY_ROOT
            / "source/main/resources/tobj_fileformat/TObjFileFormat.cpp"
        ).read_text(encoding="utf-8")
        manager_header = (
            REPOSITORY_ROOT / "source/main/terrain/ProceduralManager.h"
        ).read_text(encoding="utf-8")
        manager_source = (
            REPOSITORY_ROOT / "source/main/terrain/ProceduralManager.cpp"
        ).read_text(encoding="utf-8")
        road_header = (
            REPOSITORY_ROOT / "source/main/terrain/ProceduralRoad.h"
        ).read_text(encoding="utf-8")
        road_source = (
            REPOSITORY_ROOT / "source/main/terrain/ProceduralRoad.cpp"
        ).read_text(encoding="utf-8")
        bindings = (
            REPOSITORY_ROOT
            / "source/main/scripting/bindings/ProceduralRoadAngelscript.cpp"
        ).read_text(encoding="utf-8")
        editor = (
            REPOSITORY_ROOT / "source/main/terrain/TerrainEditor.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "bool collision_endcaps_enabled = true;",
            manager_header,
        )
        directive_block = parser.split(
            'if (strncmp("collision_endcaps_enabled"',
            1,
        )[1].split("// ** Process entries", 1)[0]
        self.assertIn('strcmp(valuebuf, "true") == 0', directive_block)
        self.assertIn('strcmp(valuebuf, "false") == 0', directive_block)
        self.assertIn("preserving prior value", directive_block)
        self.assertNotIn("parseBool", directive_block)
        self.assertIn(
            '"    collision_endcaps_enabled {}\\n"',
            parser,
        )
        self.assertIn(
            'obj_name == "bridge_side_pillars"',
            parser,
        )
        self.assertIn(
            '? "bridge_side_pillars"',
            parser,
        )
        self.assertIn(
            "setEndCapCollisionEnabled(po->collision_endcaps_enabled)",
            manager_source,
        )
        self.assertIn(
            "ROAD_PILLAR_TYPE_BRIDGE_SIDES = 3",
            road_header,
        )
        self.assertIn(
            "SIDE_PIER_HEAVY_TRUCK_CLEARANCE_M = 2.5f",
            road_source,
        )
        self.assertIn(
            "SIDE_PIER_UNDERSIDE_GAP_M = 0.05f",
            road_source,
        )
        self.assertIn(
            "[RoR|ProceduralRoad|SidePiers] requested={} built={} skipped={}",
            road_source,
        )
        self.assertGreaterEqual(
            road_source.count("if (!collision_endcaps)"),
            2,
        )
        for getter in (
            "getSidePierRequestedCount",
            "getSidePierBuiltCount",
            "getSidePierSkippedCount",
        ):
            self.assertIn(getter, bindings)
        self.assertIn(
            '"    collision_endcaps_enabled {}\\n"',
            editor,
        )
        self.assertIn("bridge_side_pillars", editor)

    def test_native_ui_free_capture_hides_top_menu_and_cursor(self) -> None:
        gui_manager = (
            REPOSITORY_ROOT / "source/main/gui/GUIManager.cpp"
        ).read_text(encoding="utf-8")
        top_menubar = (
            REPOSITORY_ROOT
            / "source/main/gui/panels/GUI_TopMenubar.cpp"
        ).read_text(encoding="utf-8")
        visible_case = gui_manager.split(
            "case MouseCursorVisibility::VISIBLE:",
            1,
        )[1].split(
            "case MouseCursorVisibility::HIDDEN:",
            1,
        )[0]
        self.assertIn("App::ui_hide_gui->getBool()", visible_case)
        self.assertIn("ImGui::GetIO().MouseDrawCursor = false;", visible_case)
        should_display = top_menubar.split(
            "bool TopMenubar::ShouldDisplay(ImVec2 window_pos)",
            1,
        )[1]
        hide_guard = should_display.split(
            "if (!App::GetGuiManager()->AreStaticMenusAllowed())",
            1,
        )[0]
        self.assertIn("App::ui_hide_gui->getBool()", hide_guard)
        self.assertIn("return false;", hide_guard)

    def test_real_private_archive_when_available(self) -> None:
        archive = (
            Path.home()
            / "Library/Application Support/Rigs of Rods/mods/CityWorld.zip"
        )
        if not archive.is_file():
            self.skipTest("private pinned CityWorld.zip is not installed")

        builder_path = (
            REPOSITORY_ROOT / "tools/build_cityworld_local_overlay.py"
        )
        spec = importlib.util.spec_from_file_location(
            "cityworld_overlay_for_neoq_bridge_test",
            builder_path,
        )
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load CityWorld overlay builder")
        builder = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = builder
        spec.loader.exec_module(builder)
        placements = builder.source_placements(archive)
        report = BRIDGE.authenticate_inputs(archive, placements)
        self.assertEqual(
            [record["sha256"] for record in report["members"]],
            [
                record["sha256"]
                for record in BRIDGE.AUTHENTICATED_MEMBERS
            ],
        )


if __name__ == "__main__":
    unittest.main()
